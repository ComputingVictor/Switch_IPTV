#include "player.hpp"

#include <SDL2/SDL.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <array>
#include <chrono>
#include <cctype>
#include <thread>

namespace {
std::string ffmpeg_error(int code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> message{};
    av_strerror(code, message.data(), message.size());
    return message.data();
}

AVCodecContext* open_decoder(AVFormatContext* input, int stream_index) {
    if (stream_index < 0) return nullptr;
    const AVCodecParameters* parameters = input->streams[stream_index]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
    if (!codec) return nullptr;
    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (!context) return nullptr;
    if (avcodec_parameters_to_context(context, parameters) < 0 || avcodec_open2(context, codec, nullptr) < 0) {
        avcodec_free_context(&context);
        return nullptr;
    }
    return context;
}

int interrupt_io(void* opaque) {
    return static_cast<std::atomic<bool>*>(opaque)->load() ? 1 : 0;
}

std::int64_t clock_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string stream_label(AVStream* stream, const char* fallback, int number) {
    AVDictionaryEntry* title = av_dict_get(stream->metadata, "title", nullptr, 0);
    AVDictionaryEntry* language = av_dict_get(stream->metadata, "language", nullptr, 0);
    std::string label = title && title->value ? title->value : language && language->value ? language->value : fallback;
    return label + " " + std::to_string(number);
}

std::string clean_ass(std::string text) {
    std::size_t p = 0;
    for (int commas = 0; commas < 9; ++commas) { p = text.find(',', p); if (p == std::string::npos) { p = 0; break; } ++p; }
    if (p) text.erase(0, p);
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '{') { const auto end = text.find('}', i); if (end != std::string::npos) { text.erase(i, end - i + 1); continue; } }
        if (i + 1 < text.size() && text[i] == '\\' && (text[i + 1] == 'N' || text[i + 1] == 'n')) { text.replace(i, 2, "\n"); ++i; continue; }
        ++i;
    }
    return text;
}
}

namespace switchiptv {
Player::Player(SDL_Renderer* renderer) : renderer_(renderer) { avformat_network_init(); }

Player::~Player() {
    stop();
    if (texture_) SDL_DestroyTexture(texture_);
    avformat_network_deinit();
}

void Player::set_error(const std::string& message) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_error_ = message;
}

std::string Player::last_error() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_error_;
}

std::string Player::audio_track() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    const int pos = requested_audio_.load();
    return pos >= 0 && pos < static_cast<int>(audio_tracks_.size()) ? audio_tracks_[pos].label : "No audio";
}

std::string Player::subtitle_track() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    const int pos = requested_subtitle_.load();
    return pos >= 0 && pos < static_cast<int>(subtitle_tracks_.size()) ? subtitle_tracks_[pos].label : "Off";
}

std::string Player::subtitle_text() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return clock_ms() < subtitle_until_ms_ ? subtitle_text_ : std::string{};
}

void Player::next_audio_track() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!audio_tracks_.empty()) requested_audio_ = (requested_audio_.load() + 1) % audio_tracks_.size();
}

void Player::next_subtitle_track() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (subtitle_tracks_.empty()) return;
    const int next = requested_subtitle_.load() + 1;
    requested_subtitle_ = next >= static_cast<int>(subtitle_tracks_.size()) ? -1 : next;
    subtitle_text_.clear();
}

void Player::load(const std::string& url, const std::string& agent, const std::string& referrer) {
    stop();
    stop_requested_ = false;
    active_ = true;
    paused_ = false;
    set_error({});
    { std::lock_guard<std::mutex> lock(state_mutex_); audio_tracks_.clear(); subtitle_tracks_.clear(); subtitle_text_.clear(); }
    requested_audio_ = 0; requested_subtitle_ = -1;
    worker_ = std::thread(&Player::decode, this, url, agent, referrer);
}

void Player::stop() {
    stop_requested_ = true;
    paused_ = false;
    if (worker_.joinable()) worker_.join();
    if (audio_device_) {
        SDL_ClearQueuedAudio(audio_device_);
        SDL_CloseAudioDevice(audio_device_);
        audio_device_ = 0;
    }
    active_ = false;
}

void Player::toggle_pause() {
    if (!active_) return;
    paused_ = !paused_.load();
    if (audio_device_) SDL_PauseAudioDevice(audio_device_, paused_ ? 1 : 0);
}

void Player::render(const SDL_Rect& destination) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (pixels_.empty()) return;
    int texture_width = 0, texture_height = 0;
    if (texture_) SDL_QueryTexture(texture_, nullptr, nullptr, &texture_width, &texture_height);
    if (!texture_ || texture_width != frame_width_ || texture_height != frame_height_) {
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                     frame_width_, frame_height_);
    }
    if (!texture_) return;
    if (frame_ready_) {
        SDL_UpdateTexture(texture_, nullptr, pixels_.data(), frame_width_ * 4);
        frame_ready_ = false;
    }
    // Cover the complete display without stretching: crop only the excess
    // from the source, like a television's zoom/fill mode.
    SDL_Rect source{0, 0, frame_width_, frame_height_};
    const float source_aspect = static_cast<float>(frame_width_) / frame_height_;
    const float target_aspect = static_cast<float>(destination.w) / destination.h;
    if (source_aspect > target_aspect) {
        source.w = static_cast<int>(frame_height_ * target_aspect);
        source.x = (frame_width_ - source.w) / 2;
    } else {
        source.h = static_cast<int>(frame_width_ / target_aspect);
        source.y = (frame_height_ - source.h) / 2;
    }
    SDL_RenderCopy(renderer_, texture_, &source, &destination);
}

void Player::decode(std::string url, std::string agent, std::string referrer) {
    AVFormatContext* input = avformat_alloc_context();
    if (!input) { set_error("Not enough memory for FFmpeg"); active_ = false; return; }
    input->interrupt_callback = {interrupt_io, &stop_requested_};
    AVDictionary* options = nullptr;
    av_dict_set(&options, "timeout", "30000000", 0);
    av_dict_set(&options, "rw_timeout", "30000000", 0);
    const std::string effective_agent = agent.empty() ? "VLC/3.0.21 LibVLC/3.0.21" : agent;
    av_dict_set(&options, "user_agent", effective_agent.c_str(), 0);
    if (!referrer.empty()) av_dict_set(&options, "referer", referrer.c_str(), 0);
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_at_eof", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);
    int rc = avformat_open_input(&input, url.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (rc >= 0) rc = avformat_find_stream_info(input, nullptr);
    if (rc < 0) {
        if (!stop_requested_) set_error(ffmpeg_error(rc));
        avformat_close_input(&input);
        active_ = false;
        return;
    }

    const int video_stream = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    int audio_stream = av_find_best_stream(input, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    int subtitle_stream = -1;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        int audio_number = 1, subtitle_number = 1, best_audio_pos = 0;
        for (unsigned int i = 0; i < input->nb_streams; ++i) {
            if (input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                if (static_cast<int>(i) == audio_stream) best_audio_pos = audio_tracks_.size();
                audio_tracks_.push_back({static_cast<int>(i), stream_label(input->streams[i], "Audio", audio_number++)});
            } else if (input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                subtitle_tracks_.push_back({static_cast<int>(i), stream_label(input->streams[i], "Subtitles", subtitle_number++)});
            }
        }
        requested_audio_ = best_audio_pos;
    }
    AVCodecContext* video = open_decoder(input, video_stream);
    AVCodecContext* audio = open_decoder(input, audio_stream);
    AVCodecContext* subtitle = nullptr;
    if (!video && !audio) {
        set_error("The stream contains no compatible audio or video");
        avformat_close_input(&input);
        active_ = false;
        return;
    }

    SwsContext* scaler = nullptr;
    SwrContext* resampler = nullptr;
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    std::vector<std::uint8_t> rgba;
    std::vector<std::uint8_t> pcm;
    auto video_clock = std::chrono::steady_clock::now();
    double first_video_pts = -1.0;

    while (!stop_requested_ && av_read_frame(input, packet) >= 0) {
        while (paused_ && !stop_requested_) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        int wanted_audio = audio_stream, wanted_subtitle = -1;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            const int ap = requested_audio_.load(), sp = requested_subtitle_.load();
            if (ap >= 0 && ap < static_cast<int>(audio_tracks_.size())) wanted_audio = audio_tracks_[ap].stream_index;
            if (sp >= 0 && sp < static_cast<int>(subtitle_tracks_.size())) wanted_subtitle = subtitle_tracks_[sp].stream_index;
        }
        if (wanted_audio != audio_stream) {
            avcodec_free_context(&audio); swr_free(&resampler);
            if (audio_device_) { SDL_ClearQueuedAudio(audio_device_); SDL_CloseAudioDevice(audio_device_); audio_device_ = 0; }
            audio_stream = wanted_audio; audio = open_decoder(input, audio_stream);
        }
        if (wanted_subtitle != subtitle_stream) {
            avcodec_free_context(&subtitle); subtitle_stream = wanted_subtitle;
            subtitle = open_decoder(input, subtitle_stream);
            std::lock_guard<std::mutex> lock(state_mutex_); subtitle_text_.clear();
        }
        if (subtitle && packet->stream_index == subtitle_stream) {
            AVSubtitle decoded{}; int got = 0;
            if (avcodec_decode_subtitle2(subtitle, &decoded, &got, packet) >= 0 && got) {
                std::string text;
                for (unsigned int i = 0; i < decoded.num_rects; ++i) {
                    const char* value = decoded.rects[i]->text ? decoded.rects[i]->text : decoded.rects[i]->ass;
                    if (value) { if (!text.empty()) text += '\n'; text += clean_ass(value); }
                }
                if (!text.empty()) { std::lock_guard<std::mutex> lock(state_mutex_); subtitle_text_ = text; subtitle_until_ms_ = clock_ms() + std::max(1500u, decoded.end_display_time); }
                avsubtitle_free(&decoded);
            }
            av_packet_unref(packet); continue;
        }
        AVCodecContext* decoder = packet->stream_index == video_stream ? video :
                                  packet->stream_index == audio_stream ? audio : nullptr;
        if (decoder && avcodec_send_packet(decoder, packet) >= 0) {
            while (!stop_requested_ && avcodec_receive_frame(decoder, frame) >= 0) {
                if (decoder == video) {
                    scaler = sws_getCachedContext(scaler, frame->width, frame->height,
                        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
                        AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (scaler) {
                        rgba.resize(static_cast<std::size_t>(frame->width) * frame->height * 4);
                        std::uint8_t* output[] = {rgba.data(), nullptr, nullptr, nullptr};
                        int strides[] = {frame->width * 4, 0, 0, 0};
                        sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, output, strides);
                        if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                            const double pts = frame->best_effort_timestamp * av_q2d(input->streams[video_stream]->time_base);
                            if (first_video_pts < 0.0) { first_video_pts = pts; video_clock = std::chrono::steady_clock::now(); }
                            const auto due = video_clock + std::chrono::microseconds(
                                static_cast<long long>((pts - first_video_pts) * 1000000.0));
                            while (!stop_requested_ && !paused_ && std::chrono::steady_clock::now() < due)
                                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        }
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        pixels_.swap(rgba);
                        frame_width_ = frame->width;
                        frame_height_ = frame->height;
                        frame_ready_ = true;
                    }
                } else if (decoder == audio) {
                    if (!resampler) {
                        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                        swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_S16, 48000,
                                            &frame->ch_layout, static_cast<AVSampleFormat>(frame->format),
                                            frame->sample_rate, 0, nullptr);
                        if (resampler && swr_init(resampler) < 0) swr_free(&resampler);
                        SDL_AudioSpec wanted{};
                        wanted.freq = 48000; wanted.format = AUDIO_S16SYS; wanted.channels = 2; wanted.samples = 2048;
                        audio_device_ = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
                        if (audio_device_) SDL_PauseAudioDevice(audio_device_, 0);
                    }
                    if (resampler && audio_device_) {
                        const int samples = av_rescale_rnd(swr_get_delay(resampler, frame->sample_rate) + frame->nb_samples,
                                                           48000, frame->sample_rate, AV_ROUND_UP);
                        pcm.resize(static_cast<std::size_t>(samples) * 2 * sizeof(std::int16_t));
                        std::uint8_t* output[] = {pcm.data()};
                        const int converted = swr_convert(resampler, output, samples,
                                                          const_cast<const std::uint8_t**>(frame->extended_data), frame->nb_samples);
                        if (converted > 0) SDL_QueueAudio(audio_device_, pcm.data(), converted * 2 * sizeof(std::int16_t));
                        while (!stop_requested_ && SDL_GetQueuedAudioSize(audio_device_) > 48000 * 2 * sizeof(std::int16_t))
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    sws_freeContext(scaler);
    swr_free(&resampler);
    avcodec_free_context(&video);
    avcodec_free_context(&audio);
    avcodec_free_context(&subtitle);
    avformat_close_input(&input);
    active_ = false;
}
}

#include <borealis.hpp>
#include <borealis/views/button.hpp>
#include <borealis/views/bottom_bar.hpp>
#include <borealis/views/image.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include "http_client.hpp"
#include "epg.hpp"
#include "player.hpp"
#include "playlist.hpp"
#include "storage.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace switchiptv;
const NVGcolor INK = nvgRGB(9, 16, 32), PANEL = nvgRGB(18, 29, 51);
const NVGcolor SOFT = nvgRGB(25, 39, 66), CYAN = nvgRGB(29, 211, 242);
const NVGcolor CORAL = nvgRGB(255, 82, 92), WHITE = nvgRGB(246, 249, 255);
const NVGcolor MUTED = nvgRGB(155, 169, 194);

std::string guideNow(EpgGuide* guide, const Channel& channel) {
    if (!guide) return {};
    std::string value = guide->now(channel.tvg_id);
    if (value.empty()) value = guide->now(channel.tvg_name);
    if (value.empty()) value = guide->now(channel.name);
    return value;
}
std::string guideNext(EpgGuide* guide, const Channel& channel) {
    if (!guide) return {};
    std::string value = guide->next(channel.tvg_id);
    if (value.empty()) value = guide->next(channel.tvg_name);
    if (value.empty()) value = guide->next(channel.name);
    return value;
}

std::string loadSource(const std::string& source) {
    if (source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0)
        return HttpClient{}.get(source);
    std::ifstream input(source, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open playlist source");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

brls::Label* label(const std::string& text, float size, NVGcolor color) {
    auto* value = new brls::Label();
    value->setText(text); value->setFontSize(size); value->setTextColor(color);
    return value;
}
void card(brls::View* view, NVGcolor color = PANEL, float radius = 18) {
    view->setBackgroundColor(color); view->setCornerRadius(radius);
}

class VideoView final : public brls::View {
public:
    explicit VideoView(Player* player) : player(player) {}
    ~VideoView() override {
        if (texture) nvgDeleteImage(brls::Application::getNVGContext(), texture);
    }
    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style, brls::FrameContext*) override {
        nvgBeginPath(vg); nvgRoundedRect(vg, x, y, w, h, 18);
        nvgFillColor(vg, nvgRGB(4, 9, 19)); nvgFill(vg);
        const auto frame = player->frame();
        if (frame.pixels.empty() || frame.width <= 0 || frame.height <= 0) {
            const float cx = x + w / 2, cy = y + h / 2 - 8;
            nvgBeginPath(vg); nvgCircle(vg, cx, cy, 42);
            nvgFillColor(vg, nvgRGBA(29, 211, 242, 38)); nvgFill(vg);
            nvgBeginPath(vg); nvgMoveTo(vg, cx - 11, cy - 18);
            nvgLineTo(vg, cx + 22, cy); nvgLineTo(vg, cx - 11, cy + 18);
            nvgClosePath(vg); nvgFillColor(vg, CYAN); nvgFill(vg); return;
        }
        if (!texture || textureWidth != frame.width || textureHeight != frame.height) {
            if (texture) nvgDeleteImage(vg, texture);
            texture = nvgCreateImageRGBA(vg, frame.width, frame.height, 0, frame.pixels.data());
            textureWidth = frame.width; textureHeight = frame.height;
        } else {
            nvgUpdateImage(vg, texture, frame.pixels.data());
        }
        if (!texture) return;
        const float scale = std::min(w / frame.width, h / frame.height);
        const float dw = frame.width * scale, dh = frame.height * scale;
        const float dx = x + (w - dw) / 2, dy = y + (h - dh) / 2;
        nvgBeginPath(vg); nvgRoundedRect(vg, dx, dy, dw, dh, 18);
        nvgFillPaint(vg, nvgImagePattern(vg, dx, dy, dw, dh, 0, texture, 1));
        nvgFill(vg);
    }
private:
    Player* player;
    int texture = 0, textureWidth = 0, textureHeight = 0;
};

class PlaybackActivity final : public brls::Activity {
public:
    PlaybackActivity(Player* player, const Playlist* playlist, EpgGuide* guide,
                     std::vector<size_t> channels, size_t position)
        : player(player), playlist(playlist), guide(guide), channels(std::move(channels)), position(position) {}
    ~PlaybackActivity() override { brls::cancelDelay(hideTimer); }
    brls::View* createContentView() override {
        auto* root = new brls::Box(brls::Axis::COLUMN); root->setWidth(1280); root->setHeight(720);
        root->setBackgroundColor(nvgRGB(0, 0, 0)); root->setFocusable(true); root->setHideHighlight(true);
        video = new VideoView(player); video->setWidth(1280); video->setHeight(720); root->addView(video);
        bar = new brls::Box(brls::Axis::ROW); bar->setWidth(1280); bar->setHeight(78);
        bar->setPositionType(brls::PositionType::ABSOLUTE); bar->setPositionBottom(58);
        bar->setPadding(10, 28, 10, 28); bar->setBackgroundColor(INK);
        auto* info = new brls::Box(brls::Axis::COLUMN); info->setGrow(1);
        name = label("", 21, WHITE); name->setHeight(38); info->addView(name);
        program = label("Guide unavailable", 15, CYAN); program->setHeight(30); info->addView(program); bar->addView(info);
        root->addView(bar);
        bottomBar = new brls::BottomBar(); bottomBar->setWidth(1280); bottomBar->setHeight(58);
        bottomBar->setPositionType(brls::PositionType::ABSOLUTE); bottomBar->setPositionBottom(0); root->addView(bottomBar);
        root->registerAction("Pause", brls::BUTTON_A, [this](brls::View*) { player->toggle_pause(); showControls(); return true; });
        root->registerAction("Previous channel", brls::BUTTON_UP, [this](brls::View*) { zap(-1); return true; }, true, true);
        root->registerAction("Next channel", brls::BUTTON_DOWN, [this](brls::View*) { zap(1); return true; }, true, true);
        root->registerAction("Previous channel", brls::BUTTON_LB, [this](brls::View*) { zap(-1); return true; }, true, true);
        root->registerAction("Next channel", brls::BUTTON_RB, [this](brls::View*) { zap(1); return true; }, true, true);
        root->registerAction("Audio", brls::BUTTON_X, [this](brls::View*) {
            player->next_audio_track(); updateTechnicalInfo(); showControls(); return true;
        }, true);
        root->registerAction("Subtitles", brls::BUTTON_Y, [this](brls::View*) {
            player->next_subtitle_track(); updateTechnicalInfo(); showControls(); return true;
        }, true);
        root->registerAction("Aspect ratio", brls::BUTTON_RIGHT, [this](brls::View*) {
            auto next = static_cast<int>(player->aspect()) + 1;
            if (next > static_cast<int>(Player::Aspect::Fill)) next = 0;
            player->set_aspect(static_cast<Player::Aspect>(next)); updateTechnicalInfo(); showControls(); return true;
        }, true);
        root->registerAction("Controls", brls::BUTTON_START, [this](brls::View*) { showControls(); return true; });
        root->registerAction("Back", brls::BUTTON_B, [this](brls::View*) {
            player->stop(); brls::Application::popActivity(brls::TransitionAnimation::FADE); return true;
        });
        playCurrent(); showControls();
        return root;
    }
private:
    void zap(int direction) {
        if (channels.empty()) return;
        if (direction < 0) position = position == 0 ? channels.size() - 1 : position - 1;
        else position = (position + 1) % channels.size();
        playCurrent(); showControls();
    }
    void playCurrent() {
        if (!playlist || channels.empty()) return;
        const auto& selected = playlist->channels[channels[position]];
        name->setText(selected.name);
        const std::string now = guideNow(guide, selected);
        const std::string next = guideNext(guide, selected);
        program->setText(now.empty() ? "Guide unavailable" : "Now: " + now + (next.empty() ? "" : "     |     Next: " + next));
        player->load(selected.url, selected.user_agent, selected.referrer);
    }
    void updateTechnicalInfo() {
        static const char* aspects[] = {"Auto", "16:9", "4:3", "Fill"};
        const auto index = static_cast<int>(player->aspect());
        program->setText("Audio: " + player->audio_track() + "     |     Subs: " +
            player->subtitle_track() + "     |     Aspect: " + aspects[index]);
    }
    void showControls() {
        controlsVisible = true; bar->setVisibility(brls::Visibility::VISIBLE);
        bottomBar->setVisibility(brls::Visibility::VISIBLE); brls::cancelDelay(hideTimer);
        hideTimer = brls::delay(3000, [this] {
            controlsVisible = false; bar->setVisibility(brls::Visibility::GONE);
            bottomBar->setVisibility(brls::Visibility::GONE);
        });
    }
    Player* player;
    const Playlist* playlist;
    EpgGuide* guide;
    std::vector<size_t> channels;
    size_t position;
    brls::Label* name = nullptr;
    brls::Label* program = nullptr;
    VideoView* video = nullptr;
    brls::Box* bar = nullptr;
    brls::BottomBar* bottomBar = nullptr;
    bool controlsVisible = true;
    size_t hideTimer = 0;
};

class LogoLoader {
public:
    using Completion = std::function<void(const std::string&, size_t)>;
    static LogoLoader& instance() { static LogoLoader loader; return loader; }
    void request(std::string url, Completion done) {
        { std::lock_guard<std::mutex> lock(mutex); jobs.push({std::move(url), std::move(done)}); }
        wake.notify_one();
    }
    ~LogoLoader() {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
        wake.notify_all(); for (auto& worker : workers) if (worker.joinable()) worker.join();
    }
private:
    struct Job { std::string url; Completion done; };
    LogoLoader() {
        for (int i = 0; i < 4; ++i) workers.emplace_back([this] { run(); });
    }
    void run() {
        for (;;) {
            Job job;
            { std::unique_lock<std::mutex> lock(mutex); wake.wait(lock, [this] { return stopping || !jobs.empty(); });
              if (stopping) return; job = std::move(jobs.front()); jobs.pop(); }
            try { const auto bytes = HttpClient{}.get(job.url, 10); job.done(bytes, bytes.size()); }
            catch (...) { job.done({}, 0); }
        }
    }
    std::mutex mutex; std::condition_variable wake; std::queue<Job> jobs;
    bool stopping = false; std::vector<std::thread> workers;
};

class SplashActivity final : public brls::Activity {
public:
    brls::View* createContentView() override {
        auto* root = new brls::Box(brls::Axis::COLUMN);
        root->setAlignItems(brls::AlignItems::CENTER);
        root->setJustifyContent(brls::JustifyContent::CENTER);
        root->setBackgroundColor(INK);
        auto* icon = new brls::Image(); icon->setImageFromRes("img/app_logo.jpg");
        icon->setScalingType(brls::ImageScalingType::FIT); icon->setWidth(184); icon->setHeight(184);
        icon->setCornerRadius(38); root->addView(icon);
        auto* title = label("SwitchIPTV", 40, WHITE); title->setHeight(58); title->setMarginTop(24);
        root->addView(title);
        auto* subtitle = label("Your television. Your way.", 19, MUTED); subtitle->setHeight(30);
        root->addView(subtitle); return root;
    }
};

class MainActivity final : public brls::Activity {
public:
    ~MainActivity() override { player.stop(); }
    brls::View* createContentView() override {
        auto* root = new brls::Box(brls::Axis::COLUMN);
        root->setBackgroundColor(INK); root->setPadding(26, 38, 24, 38);
        root->addView(buildHeader());
        auto* body = new brls::Box(brls::Axis::ROW); body->setGrow(1); body->setMarginTop(18);
        body->addView(buildSidebar()); body->addView(buildContent()); root->addView(body);
        root->registerAction("Playlist", brls::BUTTON_X, [this](brls::View*) { promptPlaylist(); return true; });
        root->registerAction("Refresh", brls::BUTTON_START, [this](brls::View*) { loadChannels(); return true; });
        root->registerAction("Search", brls::BUTTON_BACK, [this](brls::View*) { promptSearch(); return true; });
        root->registerAction("Previous category", brls::BUTTON_LB, [this](brls::View*) { switchCategory(-1); return true; }, true, true);
        root->registerAction("Next category", brls::BUTTON_RB, [this](brls::View*) { switchCategory(1); return true; }, true, true);
        auto* nativeBar = new brls::BottomBar(); nativeBar->setHeight(58); root->addView(nativeBar);
        loadChannels(); return root;
    }
private:
    brls::View* buildHeader() {
        auto* row = new brls::Box(brls::Axis::ROW); row->setAlignItems(brls::AlignItems::CENTER); row->setHeight(56);
        auto* icon = new brls::Image(); icon->setImageFromRes("img/app_logo.jpg");
        icon->setScalingType(brls::ImageScalingType::FIT); icon->setWidth(48); icon->setHeight(48); icon->setCornerRadius(12);
        row->addView(icon);
        auto* title = label("SwitchIPTV", 30, WHITE); title->setWidth(220); title->setHeight(48); title->setMarginLeft(14);
        row->addView(title);
        auto* spacer = new brls::Box(brls::Axis::ROW); spacer->setGrow(1); row->addView(spacer);
        auto* live = label("LIVE TV", 14, CORAL); live->setWidth(100); live->setHeight(32); row->addView(live);
        return row;
    }
    brls::View* buildSidebar() {
        auto* side = new brls::Box(brls::Axis::COLUMN); side->setWidth(224); side->setPadding(12); side->setMarginRight(18); card(side);
        auto* heading = label("LIBRARY", 14, MUTED); heading->setHeight(38); heading->setMarginLeft(10); side->addView(heading);
        addNav(side, "\u25b6  Live TV", "", CYAN); addNav(side, "\u2665  Favorites", "__favorites", CORAL);
        heading = label("CATEGORIES", 14, MUTED); heading->setHeight(38); heading->setMarginTop(12); heading->setMarginLeft(10); side->addView(heading);
        auto* scroll = new brls::ScrollingFrame(); scroll->setGrow(1); scroll->setScrollingIndicatorVisible(false);
        groups = new brls::Box(brls::Axis::COLUMN); scroll->setContentView(groups); side->addView(scroll);
        return side;
    }
    brls::View* buildContent() {
        auto* content = new brls::Box(brls::Axis::COLUMN); content->setGrow(1);
        auto* welcome = new brls::Box(brls::Axis::ROW); welcome->setHeight(72); welcome->setPadding(10, 12, 10, 20); card(welcome, SOFT);
        status = label("Preparing your library...", 17, MUTED); status->setGrow(1); status->setHeight(48); welcome->addView(status);
        addMenuButton(welcome, "Search", [this] { promptSearch(); }, 105);
        sortButton = addMenuButton(welcome, "Sort A-Z", [this] { toggleSort(); }, 115);
        addMenuButton(welcome, "Refresh", [this] { loadChannels(); }, 120);
        addMenuButton(welcome, "Add playlist", [this] { promptPlaylist(); }, 145);
        addMenuButton(welcome, "EPG", [this] { promptEpg(); }, 72);
        content->addView(welcome);
        auto* listHead = new brls::Box(brls::Axis::ROW); listHead->setHeight(60); listHead->setAlignItems(brls::AlignItems::CENTER); listHead->setMarginTop(8);
        listTitle = label("All channels", 22, WHITE); listTitle->setGrow(1); listHead->addView(listTitle);
        channelCount = label("0 CHANNELS", 14, MUTED); channelCount->setWidth(150); listHead->addView(channelCount); content->addView(listHead);
        channels = new brls::ScrollingFrame(); channels->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        channels->setScrollingIndicatorVisible(true); channels->setGrow(1);
        channelContent = new brls::Box(brls::Axis::COLUMN); channelContent->setPadding(0, 8, 4, 0);
        channels->setContentView(channelContent); content->addView(channels); return content;
    }
    template<typename Callback> brls::Button* addMenuButton(brls::Box* parent, const std::string& text, Callback callback, float width = 170) {
        auto* button = new brls::Button(); button->setText(text); button->setFontSize(15); button->setTextColor(WHITE);
        button->setWidth(width); button->setHeight(48); button->setMarginLeft(10); button->setCornerRadius(12); button->setBackgroundColor(PANEL);
        button->registerAction(text, brls::BUTTON_A, [callback](brls::View*) { callback(); return true; }); parent->addView(button); return button;
    }
    void addNav(brls::Box* parent, const std::string& text, const std::string& group, NVGcolor accent) {
        auto* b = new brls::Button(); b->setText(text); b->setFontSize(17); b->setTextColor(accent);
        b->setHeight(52); b->setMarginBottom(6); b->setCornerRadius(13); b->setBackgroundColor(SOFT);
        b->registerAction("Open", brls::BUTTON_A, [this, group](brls::View*) { selectedGroup = group; displayLimit = 120; rebuildChannels(); return true; }); parent->addView(b);
    }
    void loadChannels() {
        status->setText("Refreshing playlist...");
        if (channelContent) channelContent->clearViews();
        if (groups) groups->clearViews();
        Storage storage("sdmc:/switch/SwitchIPTV/config"); const auto settings = storage.load_settings();
        favorites = storage.load_favorites();
        if (settings.playlist_source.empty()) { status->setText("Add an IPTV playlist to get started."); listTitle->setText("Your library is empty"); addEmpty(); return; }
        try { playlist = M3uParser::parse(loadSource(settings.playlist_source), settings.playlist_name);
            hiddenGroups = settings.hidden_groups;
            epg.load_async(settings.epg_source.empty() ? playlist.epg_url : settings.epg_source);
            rebuildGroups(); rebuildChannels();
        } catch (const std::exception& error) { status->setText(std::string("Could not load playlist: ") + error.what()); listTitle->setText("Check your provider"); addEmpty(); }
    }
    void promptPlaylist() {
        Storage storage("sdmc:/switch/SwitchIPTV/config"); auto current = storage.load_settings();
        brls::Application::getImeManager()->openForText([this, current](std::string source) mutable {
            if (source.empty()) return;
            current.playlist_source = std::move(source);
            Storage("sdmc:/switch/SwitchIPTV/config").save_settings(current);
            loadChannels();
        }, "Add IPTV playlist", "Enter an M3U/M3U8 URL or an sdmc:/ path", 2047, current.playlist_source);
    }
    void promptSearch() {
        brls::Application::getImeManager()->openForText([this](std::string value) {
            searchQuery = std::move(value); displayLimit = 120; rebuildChannels();
        }, "Search channels", "Leave empty to show every channel", 80, searchQuery);
    }
    void promptEpg() {
        Storage storage("sdmc:/switch/SwitchIPTV/config"); auto current = storage.load_settings();
        brls::Application::getImeManager()->openForText([this, current](std::string source) mutable {
            if (source.empty()) return;
            current.epg_source = std::move(source);
            Storage("sdmc:/switch/SwitchIPTV/config").save_settings(current); loadChannels();
        }, "Programme guide", "Enter an XMLTV or XMLTV.GZ URL", 2047, current.epg_source);
    }
    void switchCategory(int direction) {
        if (categoryKeys.empty()) return;
        auto it = std::find(categoryKeys.begin(), categoryKeys.end(), selectedGroup);
        size_t position = it == categoryKeys.end() ? 0 : static_cast<size_t>(it - categoryKeys.begin());
        if (direction < 0) position = position == 0 ? categoryKeys.size() - 1 : position - 1;
        else position = (position + 1) % categoryKeys.size();
        selectedGroup = categoryKeys[position]; displayLimit = 120; rebuildChannels();
    }
    void addEmpty() {
        auto* box = new brls::Box(brls::Axis::COLUMN); box->setPadding(18, 24, 18, 24); box->setHeight(106); card(box);
        auto* title = label("Start by adding an M3U playlist", 20, WHITE); title->setHeight(38); box->addView(title);
        auto* help = label("Select Add playlist above or press X.", 16, MUTED); help->setHeight(30); box->addView(help); channelContent->addView(box);
    }
    void rebuildGroups() {
        groups->clearViews(); categoryKeys = {"", "__favorites"}; std::set<std::string> seen;
        for (const auto& channel : playlist.channels)
            if (!channel.group.empty() && !hiddenGroups.count(channel.group) && seen.insert(channel.group).second) {
                categoryKeys.push_back(channel.group); addGroup(channel.group);
            }
    }
    void addGroup(const std::string& name) {
        auto* b = new brls::Button(); b->setText(name); b->setFontSize(16); b->setTextColor(WHITE);
        b->setHeight(46); b->setMarginBottom(4); b->setCornerRadius(11); b->setBackgroundColor(PANEL);
        b->registerAction("Open category", brls::BUTTON_A, [this, name](brls::View*) { selectedGroup = name; displayLimit = 120; rebuildChannels(); return true; }); groups->addView(b);
    }
    void toggleSort() {
        sortAscending = !sortAscending;
        if (sortButton) sortButton->setText(sortAscending ? "Sort A-Z" : "Sort Z-A");
        rebuildChannels();
    }
    void rebuildChannels() {
        if (!channelContent) return; channelContent->clearViews();
        std::vector<size_t> visible;
        for (size_t i = 0; i < playlist.channels.size(); ++i) {
            const auto& channel = playlist.channels[i];
            if (hiddenGroups.count(channel.group)) continue;
            if (selectedGroup == "__favorites" && !favorites.count(channel.url)) continue;
            if (!selectedGroup.empty() && selectedGroup != "__favorites" && channel.group != selectedGroup) continue;
            if (!searchQuery.empty()) {
                std::string name = channel.name, query = searchQuery;
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
                std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return std::tolower(c); });
                if (name.find(query) == std::string::npos) continue;
            }
            visible.push_back(i);
        }
        std::stable_sort(visible.begin(), visible.end(), [this](size_t a, size_t b) {
            std::string left = playlist.channels[a].name, right = playlist.channels[b].name;
            std::transform(left.begin(), left.end(), left.begin(), [](unsigned char c) { return std::tolower(c); });
            std::transform(right.begin(), right.end(), right.begin(), [](unsigned char c) { return std::tolower(c); });
            return sortAscending ? left < right : left > right;
        });
        visibleChannels = visible;
        const size_t rendered = std::min(displayLimit, visible.size());
        for (size_t position = 0; position < rendered; ++position) {
            const size_t i = visible[position];
            const auto& channel = playlist.channels[i]; const bool fav = favorites.count(channel.url);
            auto* row = new brls::Box(brls::Axis::ROW); row->setHeight(58); row->setMarginBottom(5);
            row->setPadding(5, 16, 5, 12); row->setAlignItems(brls::AlignItems::CENTER); card(row);
            if (!channel.logo.empty()) {
                auto* logo = new brls::Image(); logo->setWidth(48); logo->setHeight(48); logo->setCornerRadius(8);
                logo->setScalingType(brls::ImageScalingType::FIT);
                const std::string logoUrl = channel.logo;
                logo->setImageAsync([logoUrl](auto done) { LogoLoader::instance().request(logoUrl, std::move(done)); });
                row->addView(logo);
            }
            const std::string currentProgram = guideNow(&epg, channel);
            auto* info = new brls::Box(brls::Axis::COLUMN); info->setGrow(1); info->setHeight(48); info->setMarginLeft(channel.logo.empty() ? 2 : 14);
            auto* channelName = label(std::string(fav ? "\u2665  " : "") + channel.name, 17, fav ? CORAL : WHITE);
            channelName->setHeight(currentProgram.empty() ? 48 : 27); info->addView(channelName);
            if (!currentProgram.empty()) { auto* show = label("Now: " + currentProgram, 13, MUTED); show->setHeight(21); info->addView(show); }
            row->addView(info);
            auto* group = label(channel.group, 14, MUTED); group->setWidth(230); group->setHeight(42); row->addView(group);
            row->setFocusable(true); row->setHighlightCornerRadius(12); row->setHighlightPadding(2);
            row->registerAction("Watch", brls::BUTTON_A, [this, position](brls::View*) {
                const size_t i = visibleChannels[position];
                const auto& c = playlist.channels[i]; status->setText("Opening " + c.name + "...");
                brls::Application::pushActivity(new PlaybackActivity(&player, &playlist, &epg, visibleChannels, position), brls::TransitionAnimation::NONE);
                return true;
            });
            row->registerAction("Favorite", brls::BUTTON_Y, [this, i](brls::View*) {
                const auto& c = playlist.channels[i]; if (!favorites.insert(c.url).second) favorites.erase(c.url);
                Storage("sdmc:/switch/SwitchIPTV/config").save_favorites(favorites); rebuildChannels(); return true;
            });
            channelContent->addView(row);
        }
        if (rendered < visible.size()) {
            auto* more = new brls::Button();
            more->setText("Show 120 more channels"); more->setFontSize(16); more->setTextColor(CYAN);
            more->setHeight(54); more->setMarginBottom(6); more->setCornerRadius(12); more->setBackgroundColor(PANEL);
            more->registerAction("Load more", brls::BUTTON_A, [this](brls::View*) {
                displayLimit += 120; rebuildChannels(); return true;
            });
            channelContent->addView(more);
        }
        const size_t count = visible.size();
        const std::string section = selectedGroup.empty() ? "All channels" : (selectedGroup == "__favorites" ? "Your favorites" : selectedGroup);
        listTitle->setText(section);
        channelCount->setText(std::to_string(count) + (count == 1 ? " CHANNEL" : " CHANNELS"));
        status->setText(playlist.name.empty() ? "Library ready" : playlist.name); if (!count) addEmpty();
    }
    Player player{nullptr}; brls::Label* status = nullptr;
    brls::Label *listTitle = nullptr, *channelCount = nullptr;
    brls::Button* sortButton = nullptr;
    brls::Box *groups = nullptr, *channelContent = nullptr; brls::ScrollingFrame* channels = nullptr;
    Playlist playlist; EpgGuide epg; std::vector<size_t> visibleChannels;
    std::vector<std::string> categoryKeys; std::set<std::string> favorites, hiddenGroups;
    std::string selectedGroup, searchQuery;
    size_t displayLimit = 120;
    bool sortAscending = true;
};
}

int main() {
    if (!brls::Application::init()) return 1;
    brls::Application::createWindow("SwitchIPTV"); brls::Application::setGlobalQuit(true);
    brls::Application::pushActivity(new SplashActivity(), brls::TransitionAnimation::NONE);
    const auto started = std::chrono::steady_clock::now(); bool launched = false;
    while (brls::Application::mainLoop()) {
        if (!launched && std::chrono::steady_clock::now() - started > std::chrono::milliseconds(1250)) {
            launched = true;
            // Never leave Borealis without an activity between frames. Popping the
            // splash first can terminate the application loop before its callback
            // gets a chance to install the main screen on Switch.
            brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::FADE);
        }
    }
    return 0;
}

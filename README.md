# SwitchIPTV

A modern IPTV player for Nintendo Switch. Use it only with playlists and streams you are authorized to access.

## Current status

SwitchIPTV supports local and remote M3U playlists, categories, favorites, channel logos, programme information, international text, controller and touch navigation, audio tracks, subtitles and full-screen playback.

## Installation

Copy the packaged directory to the SD card:

```text
/switch/SwitchIPTV/SwitchIPTV.nro
/switch/SwitchIPTV/config/
/switch/SwitchIPTV/assets/
```

The playlist can be entered inside the application by pressing X. An optional configuration file can also be created at `/switch/SwitchIPTV/config/settings.ini`:

```ini
playlist_source=https://authorized-provider.example/list.m3u
playlist_name=My IPTV
```

A local path such as `sdmc:/playlists/tv.m3u` is also supported. Never include credentials when sharing logs or reports.

## Controls

- A: play the selected channel; pause or resume during playback.
- B: stop playback; press again from the library to exit.
- D-pad: move through channels.
- L/R: switch category.
- Y: add or remove a favorite.
- X: open the Nintendo Switch keyboard to add or change the playlist.
- Touch screen: select a visible channel row.

During playback:

- A: pause or resume.
- B: stop playback and return to the channel browser.
- X: cycle through the available audio tracks.
- Y: cycle through subtitle tracks, including Off.
- +: show the playback controls again.

The application displays Nintendo-style button badges in its action bar.

## Implemented features

- Local and remote M3U/M3U8 playlists.
- All channels, category and favorites views.
- Current-programme information from compatible XMLTV guides.
- Channel logos and international channel names.
- Full-screen video and stereo audio playback.
- Audio-track and subtitle selection.
- On-screen playback controls.
- Persistent playlist and favorite settings.
- Nintendo Switch software keyboard support.
- Controller and touch-screen navigation.

## Planned work

- Extended programme guide and timeline.
- Channel and programme search.
- Additional playlist-provider integrations.
- More channel-logo formats.
- Dedicated channel-zapping controls.

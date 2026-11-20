# nyaa-wallpaperengine

A fork of [linux-wallpaperengine](https://github.com/Almamu/linux-wallpaperengine)
by Almamu, maintained as the engine bundled with
[nyaa-paper](https://github.com/diode701/nyaa-paper), a Wallpaper Engine
front end for Linux (Rust daemon + Qt/QML client, shipped as a Flatpak).

It runs Steam Workshop wallpapers (scene, video and web) on Linux desktops.
Wallpaper Engine itself must be owned and installed through Steam: its assets
are read from the Steam folder at runtime and are **never** included here.

## What is different from upstream

Based on Gaiser147's `codex/wallpaper-compat-all` branch (FreeType text
rendering), plus fixes found while packaging nyaa-paper, among them:

- rendering no longer stalls on Wayland frame callbacks or with the display off
- mouse input and hidden-group visibility work
- audio: heap overflow in resampling, broken FFmpeg decode loop (crackles),
  auto-mute inside a Flatpak / with one engine per monitor
- web wallpapers render (message pump, texture binding, shutdown) and pick
  Chromium's platform from the session
- runs from any install location and inside a Flatpak: `$ORIGIN` RUNPATH,
  libcef kept ahead of libc, Chromium's sandbox off under `/.flatpak-info`,
  CEF profile in the XDG cache

Every change, why it was needed and how it was verified: [FORK-NOTES.md](FORK-NOTES.md).

## Using it

The supported way is the nyaa-paper Flatpak, which builds this tree:
see the [releases](https://github.com/diode701/nyaa-paper/releases).

To build it on its own:

```sh
git submodule update --init --recursive --depth 1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target linux-wallpaperengine
build/output/linux-wallpaperengine --screen-root <output> --bg <workshop-id>
```

Build dependencies, all command-line options and general usage are as in
upstream: [docs/UPSTREAM-README.md](docs/UPSTREAM-README.md).

## Credits and license

- [Almamu](https://github.com/Almamu) and contributors: linux-wallpaperengine
- [Gaiser147](https://github.com/Gaiser147): text rendering and scene compatibility work this fork is based on
- [diode701](https://github.com/diode701): the fixes listed in FORK-NOTES.md

GPL-3.0, as upstream: see [LICENSE](LICENSE).

# nyaa-wallpaperengine

Fork of [linux-wallpaperengine](https://github.com/Almamu/linux-wallpaperengine)
carrying the fixes needed by **nyaa-paper**, a KDE wallpeper GUI/daemon.

Base: `Gaiser147/linux-wallpaperengine`, branch `codex/wallpaper-compat-all`,
commit `7299e43` (the FreeType text rendering work of PR #561), plus the fixes
below.

Everything in here was verified by running wallpapers, not by reading the code;
the measurement notes live in `~/lwe-audit/REPORT.md` on the development machine.

## What is fixed relative to the base commit

1. **Render loop no longer blocks on Wayland frame callbacks.**
   `dispatchEventQueue()` used `wl_display_dispatch()` and rendered from the
   frame callback, so the engine ran at a few fps, only updated on pointer
   events, and froze whenever the compositor stopped repainting the surface.
   It now pumps the queue with `poll()` on the frame budget and renders on its
   own clock.
2. **Ancestor visibility is honoured.** Children of a hidden group were drawn;
   wallpapers that switch languages or layouts by hiding groups drew every
   variant on top of each other.
3. **Script globals are built once per frame.** `evaluate()` rebuilt the whole
   scene layer table per evaluated value (~40k JS objects/frame on a 271-object
   scene). The LonelyCAT clock went from 5 fps to the 30 fps cap.
4. **Mouse works.** Pointer input was discarded by a stale `rendering` flag, and
   `cursorDown/cursorUp/cursorMove/cursorEnter/cursorLeave` were never exported
   or dispatched. They now are, with hit-testing that bubbles up the parent
   chain. `engine.canvasSize` and `thisLayer.size` exist.
5. **Text uses real fonts.** `systemfont_*` maps to the fonts shipped with the
   engine assets instead of an arbitrary host face.
6. **Web wallpapers render.** `external_message_pump` was never set (so
   `CefDoMessageLoopWork()` did nothing), `RenderHandler` bound an FBO name as a
   texture, and shutdown hung because the refcounted handler was `delete`d and
   the browser was never closed.
7. **Audio.** `resampleAudio()` allocated its output buffer for the *input*
   channel count; a mono mp3 into a stereo device overflowed the heap on every
   frame — this was the cause of the `double free or corruption` crashes.
   Also: per-stream packet accounting instead of a shared `static`, clamped copy
   size, per-object volume, and `random` playback mode picking one file instead
   of mixing the whole folder.
8. **Build hygiene.** Chromium's compiler flags (including
   `-fno-threadsafe-statics` and `-Werror` with clang-only switches) are no
   longer applied to the engine library.
9. **Assets** are also looked up next to the binary and in
   `../share/linux-wallpaperengine/assets`, so packaged builds work without
   `--assets-dir`.

## Building

```sh
git submodule update --init --recursive --depth 1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Submodules must be initialised **before** configuring, otherwise the
`add_subdirectory` calls fail.

Build output (binary, `libcef.so` ~1.4 GB, the CEF resources) lands in
`build/output/`.

## Running / testing

```sh
build/output/linux-wallpaperengine --screen-root eDP-1 --bg <workshop-id> \
  --assets-dir ~/.local/share/Steam/steamapps/common/wallpaper_engine/assets \
  --no-fullscreen-pause
```

Add `--no-fullscreen-pause` when a fullscreen window is open; the engine pauses
on purpose otherwise.

Debug switches (environment, no effect on normal runs):

| variable | effect |
|---|---|
| `LWE_FRAME_DEBUG` | presented frames per second, once a second |
| `LWE_PROFILE` | scene phase timings (scripts / textures / objects) |
| `LWE_CURSOR_DEBUG` | hovered object chain and cursor world position |
| `LWE_SCRIPT_DEBUG` | script identity and source head on update failures |
| `LWE_SHADER_DEBUG` | shader name + combos before compilation |
| `LWE_SHADER_DUMP=<dir>` | dump every compiled vertex/fragment pair |
| `LWE_FAKE_CURSOR=x,y` / `LWE_FAKE_CURSOR_ANIMATE` / `LWE_FAKE_CLICK` | drive the pointer without a display |
| `LWE_AUDIO_DEBUG` | sum of the 16-band spectrum the scripts see |
| `LWE_NO_AUDIO_DEVICE` | keep the audio driver without opening a device (sanitizer runs) |

## Known gaps

- screenshots of **web** and **video** wallpapers come out black even though they
  render on screen (affects thumbnail generation);
- `Light` objects are still unsupported;
- a few scenes use script APIs (`copy` / `subtract` / `toString` on layer values)
  that resolve to plain objects instead of `Vec3`.

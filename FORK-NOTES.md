# nyaa-wallpaperengine

Fork of [linux-wallpaperengine](https://github.com/Almamu/linux-wallpaperengine)
carrying the fixes needed by **nyaa-paper**, a KDE wallpeper GUI/daemon.

Base: `Gaiser147/linux-wallpaperengine`, branch `codex/wallpaper-compat-all`,
commit `7299e43` (the FreeType text rendering work of PR #561), plus the fixes
below.

Everything in here was verified by running wallpapers, not by reading the code;
the measurement notes live in `~/lwe-audit/REPORT.md` on the development machine.

## Start here (next session)

State on 2026-11-19, branch `nyaa-fixes`. Long-form evidence:
`~/lwe-audit/REPORT.md` (canonical) on the development machine.

**Rules**
- Wallpaper Engine's `assets/` are paid Steam content: the engine reads them
  from the host at runtime; **never copy, bundle or commit them** (that includes
  shader dumps made with `LWE_SHADER_DUMP`).
- Make the engine itself Flatpak-friendly instead of relying on wrapper scripts.

**Next, in order**
1. ~~*Web audio is intermittent* (`901759762`)~~ **not an engine bug.** The page
   never plays on load: `Scripts/script.js` calls `.play()` only from click
   handlers (`#PlayBGMButton`, `.Option.Line`, `#PlayRandomLineButton`). Sound
   appears only after a click reaches the page. On a real desktop the clicks go
   to plasmashell, not the wallpaper surface, so interactive web wallpapers stay
   silent until desktop clicks are forwarded (a KDE-side feature, out of scope).
2. ~~*Flatpak-compatible engine*~~ **done** (fix 10 below). Left for the
   packaging step: strip `libcef.so` (1.4 GB unstripped), install only the
   runtime files (not the test binaries / `chrome-sandbox` that land in
   `build/output`).
3. Flatpak module (sources pinned: glew, freeglut, mpv, CEF archive, this tree
   with submodules), wired into nyaa-paper's manifest, which already grants
   read-only access to the Steam assets folder.
4. Re-run the render audit on a clean build; then black screenshots for
   web/video, script API gaps, light objects.

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
10. **Runs from any install location, inside a Flatpak.** RUNPATH is `$ORIGIN`
    (it used to be the absolute build-tree paths, so installed copies needed an
    `LD_LIBRARY_PATH` wrapper); kissfft is linked statically. Chromium's own
    sandbox is turned off when `/.flatpak-info` exists (or `LWE_NO_CEF_SANDBOX`
    is set): it needs a setuid helper or nested user namespaces, which a Flatpak
    forbids. The CEF profile moved from a never-deleted `/tmp/<uuid>` to
    `$XDG_CACHE_HOME/linux-wallpaperengine/cef-<uuid>`, removed on shutdown
    (one per instance, since CEF locks it and nyaa-paper runs one per monitor).
    The executable links with `--no-as-needed`: with `--as-needed` (Flatpak SDK
    default) libcef dropped out of its NEEDED list, libc loaded first, and
    Chromium's `dlsym(RTLD_NEXT, "close")` came back empty: every web wallpaper
    aborted in `CefInitialize` with "close symbol missing".
    Chromium's ozone platform follows the session: `wayland` when
    `WAYLAND_DISPLAY` is set, else `x11`, else `headless` (X11, the default,
    exits without `$DISPLAY`; headless has no GL, so its GPU process fails and
    restarts in a loop: ~80 errors/s and a busy CPU).

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
| `LWE_NO_CEF_SANDBOX` | disable Chromium's sandbox outside a Flatpak too |
| `LWE_NO_AUDIO_DEVICE` | keep the audio driver without opening a device (sanitizer runs) |

## Known gaps

- ~~**All audio silent with auto-mute on**~~ fixed: the playing detector
  skipped only streams with its own PID; in a Flatpak (PID namespace) that
  never matched, and with one engine per monitor the instances muted each
  other. Now streams from `linux-wallpaperengine` and corked streams are
  ignored. Crackle below may be the same mute flapping: retest.
- ~~**Audio crackle/beeps on every wallpaper with sound**~~ fixed (verified by
  ear on `1823900922`): `decodeFrame()` misused FFmpeg's send/receive API
  (sent packets whatever the decoder said, so packets were dropped on
  `EAGAIN`; one frame per packet) and `dequeuePacket()` blocked on an empty
  queue inside the real-time audio callback. Now: drain frames, feed one
  packet when asked, never wait; an empty slice is played as silence.
- **Presets** (`project.json` with only `dependency` + `preset`, no `type`):
  "Project type missing". Resolve in nyaa-paper (pass the dependency and the
  preset values), see its START-HERE 4c.
- `3413921910` (Meteors at Dawn, 4K scene) is **blank in the Flatpak only**;
  it works on the host build (a black host *screenshot* misled the first
  diagnosis). Logs barely differ; lead: the Flatpak builds GLEW 2.2.0
  (host: 2.3.1) and `glewInit` reports "Unknown error" there instead of the
  expected `GLEW_ERROR_NO_GLX_DISPLAY`; try glew 2.3.1 in the manifest.
- Interactive web wallpapers: clicks DO reach the page on the desktop
  (user-verified). Web audio: retest with the auto-mute + decoder fixes.

- screenshots of **web** and **video** wallpapers come out black even though they
  render on screen (affects only *screenshot on apply* / pywal theming in nyaa-paper; its grid thumbnails come from each wallpaper's preview file);
- `Light` objects are still unsupported;
- a few scenes use script APIs (`copy` / `subtract` / `toString` on layer values)
  that resolve to plain objects instead of `Vec3`.

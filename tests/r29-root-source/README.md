# PSP Go Fullscreen Plugin — Tidy R29

[Download Tidy R29 build and source](PSPGoTVScale-tidy-r29.zip) · [PRX](PSPGoTVScale.prx)

Cleanup of the uploaded fixed-v2 Soft-default source. Starts scaled with Soft;
L+R+Triangle cycles the same12 filters and L+R+Square switches scaling off/on.
Native HOME, NODS controller integration, system save/load dialogs, GE finish
confirmation and presentation-driven scheduling remain. This is not a claimed
fix for Burnout's end-of-race crash; that investigation remains paused.

## Corrections

- Flush every active GPU-read packet range: commands, image vertices, optional
  CRT/RGB overlays, posterise quads and pillarbox clear vertices. Unused ranges
  are not flushed. CPU-only metadata is not a GPU input.
- Keep WaitList bounded: all list-status calls poll; no blocking driver wait
  can bypass the timeout. Each polling sleep is clamped to the remaining budget.
  Timed-out drawing/paused work retains its storage until safely fenced.
- Queue scaled output for vblank instead of flipping immediately. The display
  hook uses vblank mode for substituted scaled layers too, so a caller cannot
  accidentally force an immediate scaled flip. Native passthrough keeps the
  caller's sync mode. The advertised output pointer changes only after the
  driver accepts publication. Vblank queueing may add a refresh of latency.
- Initialise only packet metadata and arrays actually used by the selected
  effect, rather than memset the entire~21KiB packet each frame. Cache writeback
  uses the active lengths. Image/filter operations still set their own state.
- One canonical Soft-default control test, no unused ge_api_guard.inc, updated
  comments and a distinct tidy_r29 banner/module1.29 identify this build.

## Filters

Sharp → Soft → Softer → Blur → Blurrier → CRT → Soft CRT → RGB Mask → Sharpen →
Posterise → Dither → Deposterise → Sharp. Softer/Blurrier and all other presets
are retained. Deposterise remains an optional CPU effect with extra user RAM.

## Install/test

Replace ef0:/SEPLUGINS/PSPGoTVScale.prx and fully restart. Keep one GAME entry:

    game, ef0:/SEPLUGINS/PSPGoTVScale.prx, on

Confirm startup/filter Soft, gameplay, filter cycling (especially CRT/RGB Mask/
Posterise), scaling off/on, HOME open/cancel/exit, and the normal save/load dialog.
Log is ef0:/PSPGoTVScale.txt, with ms0:/ fallback; copy before relaunching another
game. Hooks queue log messages; only the worker writes files. No new controls,
clock changes or memory unlocks are introduced. Hardware performance/behaviour
of this exact build still needs confirmation.

## Build and verification

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/tidy-test
    /tmp/tidy-test

Strict PSP build and ASan/UBSan checks pass: actual-source startup/controls,
HOME/utility/native/reboot/event handling, exact cache-writeback coverage at GE
submission, callback-enabled stuck-list timeout and late-exit handling, rejected
publication preserving previous output, queued scaled swaps and native sync
passthrough. Also144 GPU packet cases,192 filter cases with active-range metadata,
untouched unused-data checks, CPU effects and queued-logger checks pass. These
host checks validate requested cache ranges, not physical GPU cache operation.

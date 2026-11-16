# PSP Go Fullscreen Plugin — revision 16

[Download R16 build and source](pspgo-tv-scale-v1-auto-home-r16.zip) · [Plugin PRX](PSPGoTVScale.prx)

Immediate fullscreen TV scaling for PSP Go, with live filters and NODS support.
R16 restores R13's working automatic load-screen handling and corrects the
manual native-output toggle.

## Load-screen rollback and manual toggle fix

R15's device log identifies native_handoff_wait,detach_layer, followed by a
250 ms timeout with native_ready0. Firmware inspection explains the rejected
call: sync0 permits only an address update with the layer's existing shared
stride/pixel format. R15 passed stride0 while the scaled layer used stride768.
It therefore never completed the native load-screen handoff.

R16 restores R13's automatic save/load and HOME implementation. PauseForHome,
RenderHome, SavedataInitHook, ModeHook and the complete write_guard.inc are
unchanged from that working baseline. Automatic save/load transitions retain
R13's GPU allocation policy. R15's automatic teardown is removed, and R14's
hardware-register repair is not included.

Only the manual L+R+Square path uses the new fenced teardown:

1. Drain the owned GE list, retaining paused jobs safely.
2. Query the layer's actual shared stride/format and detach with those values.
   The query and current/queued clears are atomic with respect to frame updates.
3. Wait for scanout, restore native TV mode, and rebind the game's native buffers.
   An overlay with a new stride/format is queued (sync1), then latched by vblank;
   it is never forced through the incompatible immediate-update path.
4. After two native scanouts, verify all current/queued display references and
   global GE idle before restoring 2 MiB EDRAM and freeing cache-cleaned dynamic
   GPU/deposterise buffers. If fences or references remain, retain the memory.
5. On return, reacquire fresh buffers using normal activation. Keep the selected
   filter; unavailable optional deposterise memory falls back to Sharp.

The helper checks manual_native and has one manual-only call site. Module code,
input hooks and normal HOME/reboot handling remain resident. This corrects a
specific driver-contract error; real PSP Go image/stability testing is still
required. The original automatic Burnout Legends save-time crash is unresolved.

## Install and controls

Fully power off the Go, replace ef0:/SEPLUGINS/PSPGoTVScale.prx, then restart.
Keep the existing GAME entry and NODS configuration:

    game, ef0:/SEPLUGINS/PSPGoTVScale.prx, on

Fullscreen starts automatically with Soft filtering. No startup delay or INI.
Old INI files are ignored. opicker is not required.

- Hold L+R and tap Triangle: next filter; brief filter-name label.
- Hold L+R and tap Square: switch fullscreen/native output. Release Square
  between toggles. Default NODS mappings: L1+R1+Triangle/Square.

The chords remain visible to the game. Changes are suppressed while system
menus are active. Manually selected native mode uses no scaler GPU jobs and releases its dynamic
RAM/VRAM buffers after the fences above. Returning allocates fresh buffers.
Ordinary HOME outside a save/native handoff retains the working bilinear menu
renderer and existing early reboot cleanup. No NODS or volume code is modified.

First confirm the ordinary load screen works without using either combo. Then
check an off/on toggle during ordinary gameplay for the duplicated native image.
Report the outcome and log. The manual-native-before-finish workaround remains
available while the original save-time crash is investigated.

## Filters

Cycle: Sharp -> Soft -> Blur -> CRT -> Soft CRT -> RGB Mask -> Sharpen ->
Posterise -> Dither -> Deposterise -> Sharp. Default Soft; session-only selection.

| Filter | Appearance / cost |
| --- | --- |
| Sharp | Nearest-neighbour; one image pass |
| Soft | Bilinear; one image pass |
| Blur | Five same-frame averaged samples |
| CRT | Bilinear plus scanline overlay |
| Soft CRT | Lighter scanlines and ordered dithering |
| RGB Mask | Subtle vertical RGB stripes |
| Sharpen | Six-pass fixed-function unsharp approximation |
| Posterise | Roughly eight colour levels per channel |
| Dither | Bilinear with ordered RGB565 dithering |
| Deposterise | CPU edge-aware smoothing, then bilinear; optional 544 KiB buffer |

Effects are native PSP approximations, not identical PPSSPP shaders. They use
only the current image, not previous-frame feedback. Deposterise and multi-pass
filters can reduce performance. HOME filtering is independent of game filtering.

## Save guards and diagnostics

The savedata-init wrapper also covers silent/AUTO operations using that API.
Direct nonzero user file writes on fd>=3 use R13's original native pause path,
separate from the manual fenced teardown. Async
writes stay gated until completion is observed through poll/wait/status or close.
Original arguments, caller K1, pointers and results are preserved; no save data
is inspected or independently modified. Reads, standard streams and zero-length
writes bypass the direct-write guard. Firmware-internal direct kernel calls
remain outside the user wrappers. Game log/config writes also use the guard.

Log: ef0:/PSPGoTVScale.txt (ms0:/ fallback), banner auto_home_r16. Copy it before
starting another game. Useful records:

- native_output_bound: current/queued native addresses, strides and pixel formats.
- native_handoff_complete: expected edram2097152, reserved0, gpu_memory-1,
  dep_memory-1 after successful release.
- native_handoff_wait / native_handoff_error: manual release deferral and exact
  failing stage/result.
- native_closed,fresh_scaler_resources,1: fullscreen reactivated with fresh buffers.
- savedata_init/file_write markers: handoff timing and original results.

The native_handoff_* teardown records apply only to the manual toggle. Automatic
load/save transitions retain R13's utility_native_output/home_paused records.

The supported target remains PSP Go, ARK, 6.60/6.61, progressive component 480p,
with supported native 480x272 game framebuffers. It stretches the existing image;
it does not extend game geometry. Universal game compatibility is not established.

## Build and checks

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

Host regressions use actual plugin source and fake PSP APIs, with ASan/UBSan:

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/go-tv-auto-test
    /tmp/go-tv-auto-test

The display model now follows the firmware contract: live/pending addresses
share layout metadata, and sync0 rejects stride/format changes with 0x80000107.
Tests reproduce R15's rejected call, then verify a legal manual detach and queued
overlay-format change without further rejection. They also cover automatic
load ownership retained as R13, scanout timeout, upper-VRAM references, busy GE,
IRQ-safe waits, fenced manual release, fresh allocation and existing regressions. Additional renderer
and CPU-effect checks are in render_test.c, filter_test.c and effects_test.c.
Host tests do not execute physical GE drawing or firmware scheduling.

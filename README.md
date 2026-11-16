# PSP Go Fullscreen Plugin — revision 15

[Download R15 build and source](pspgo-tv-scale-v1-auto-home-r15.zip) · [Plugin PRX](PSPGoTVScale.prx)

Immediate fullscreen TV scaling for PSP Go, with live filters and NODS support.
R15 is based on R13. It replaces the native-output handoff; R14's hardware-register
repair changes are not included.

## Native-output and save handoff

The reported duplication happens when turning scaling OFF. R13 cleared only the
next-vblank priority-0 layer before changing mode, allowing the current scaled
RGB565/stride768 layer to remain active during the native transition.

R15's worker performs this sequence:

1. Stop scaler submissions and wait for its owned list to finish or be safely
   retired. Paused lists retain their storage.
2. Detach both current and queued scaled layers. Wait for scanout to latch.
3. Set the original native TV mode, then re-read and rebind the game's current
   and queued framebuffer tuples separately, preserving native stride/format.
   Restore the latest native priority-0 layer, including NULL for no overlay.
4. Wait two native scanouts. Verify the native mode, inspect all current/queued
   display layers for upper-VRAM references and check global GE idle.
5. Restore the normal 2 MiB EDRAM configuration and free the dynamic user-RAM GPU
   workspace and optional deposterise buffer after cleaning their cache ranges.
   Module code/hooks remain resident.

A missing fence, busy GE or other live upper-VRAM layer postpones release. No
in-flight memory is forcibly freed, and no global queue reset is performed.
The same helper is used for manual native mode and detected save/write handoffs.
The save API's handoff wait is bounded at 250 ms; the original operation still
runs after that deadline, with unresolved resources retained until safe.

Fullscreen return allocates a fresh aligned user-partition GPU workspace and
reacquires the upper TV buffer bank using the original activation path. The
selected filter is retained; an unavailable optional deposterise allocation
falls back to Sharp. No old command/context allocation is reused after release.

This addresses a concrete handoff ordering gap and removes scaler buffer
ownership during completed native handoffs. The exact cause of the original
Burnout Legends save-time crash is still unconfirmed. Real PSP Go verification
is required for the native image, return to fullscreen and the save transition.

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
menus are active. Native mode uses no scaler GPU jobs and releases its dynamic
RAM/VRAM buffers after the fences above. Returning allocates fresh buffers.
Ordinary HOME outside a save/native handoff retains the working bilinear menu
renderer and existing early reboot cleanup. No NODS or volume code is modified.

First check an off/on toggle during ordinary gameplay for the duplicated native
image. Then repeat the same event/save transition and report both the outcome
and log. The old manual-native-before-finish workaround remains available.

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
Direct nonzero user file writes on fd>=3 request the same native handoff. Async
writes stay gated until completion is observed through poll/wait/status or close.
Original arguments, caller K1, pointers and results are preserved; no save data
is inspected or independently modified. Reads, standard streams and zero-length
writes bypass the direct-write guard. Firmware-internal direct kernel calls
remain outside the user wrappers. Game log/config writes also use the guard.

Log: ef0:/PSPGoTVScale.txt (ms0:/ fallback), banner auto_home_r15. Copy it before
starting another game. Useful records:

- native_output_bound: current/queued native addresses, strides and pixel formats.
- native_handoff_complete: expected edram2097152, reserved0, gpu_memory-1,
  dep_memory-1 after successful release.
- native_handoff_wait: why a fence/release is deferred.
- native_closed,fresh_scaler_resources,1: fullscreen reactivated with fresh buffers.
- savedata_init/file_write markers: handoff timing and original results.

The supported target remains PSP Go, ARK, 6.60/6.61, progressive component 480p,
with supported native 480x272 game framebuffers. It stretches the existing image;
it does not extend game geometry. Universal game compatibility is not established.

## Build and checks

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

Host regressions use actual plugin source and fake PSP APIs, with ASan/UBSan:

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/go-tv-auto-test
    /tmp/go-tv-auto-test

The current/queued display model detects mode changes made while an old scaled
layer is live. Checks cover separate native tuples, scanout timeout, another
upper-VRAM layer, busy GPU, no wait with interrupts disabled, fenced RAM/VRAM
release, fresh reallocation, filter retention/fallback, original savedata/write
semantics and the existing HOME/reboot/input regressions. Additional renderer
and CPU-effect checks are in render_test.c, filter_test.c and effects_test.c.
Host tests do not execute physical GE drawing or firmware scheduling.

# PSP Go Fullscreen Plugin — R17 diagnostic

[Download R17 diagnostic build and source](pspgo-tv-scale-v1-auto-home-r17.zip) · [Plugin PRX](PSPGoTVScale.prx)

Immediate fullscreen TV scaling for PSP Go, with a short read-only hardware
comparison for the Burnout Legends native-output problem.

**Diagnostic behavior: after L+R+Square switches scaling off, this build does not
switch it back on. A second press captures the state and stays native, avoiding
the currently crashing reactivation. Restart the game to restore fullscreen.**

## What the R16 log established

The native handoff completed: software EDRAM returned to 2 MiB, GPU/deposterise
buffers were freed, scaler draws stopped, and the reported framebuffer was
stride512/format3 in native TV mode. The user still saw four quarters and a crash
on reactivation. That rules out the earlier rejected-call explanation for this
run; it does not establish the remaining hardware/display-state cause.

The existing working load-screen path remains the baseline. The utility also
initializes its own graphics/display setup; the manual toggle lacks that setup.
R17 records both states for comparison, without guessing more register writes.
This is an evidence-gathering build, not a claimed fix.

## Short test

1. Fully power off, replace ef0:/SEPLUGINS/PSPGoTVScale.prx and restart.
2. Open the normal load screen, leave it visible for a second, then load normally.
3. In gameplay, hold L+R and tap Square once. Note the picture and wait two seconds.
4. Stop the test and copy ef0:/PSPGoTVScale.txt before starting another game.
   You do not need to replay the level ending or try re-enabling scaling.

Default NODS mapping is L1+R1+Square. The existing GAME entry is unchanged:

    game, ef0:/SEPLUGINS/PSPGoTVScale.prx, on

Soft fullscreen remains the startup default. L+R+Triangle still cycles all ten
filters: Sharp, Soft, Blur, CRT, Soft CRT, RGB Mask, Sharpen, Posterise, Dither,
Deposterise. INI files remain ignored. opicker is not required.

## Captures

Snapshots compare:

- cold_native, before the first scaler activation;
- scaled_settled, at least two vblanks after scaled output is published;
- utility_pause and utility_visible, before initialization and after the working
  savedata/load UI has been visible for 100 ms;
- manual_before, manual_bound_before_release and manual_after_release;
- resume_request_blocked if the Square chord is pressed again.

Records include actual display-DMA base/format/width/stride/control registers,
the driver's cached DMA format, GE enabled-size/address translation, GE framebuffer
command words, LCDC timing and all four current/pending display-layer tuples.
DMA hardware pixel-format codes differ from PSP framebuffer format numbers;
compare them to the working native baseline instead of equating the numbers.

The five raw DMA words are exactly 0xBC800100..0xBC800110, identified from this
Go firmware's own setters. They are read only while TV output is active. GE and
cached-DMA getters are export-resolved and optional. No new hardware settings
are written, no queues are reset, and snapshots submit no GPU jobs. Reads are
collected under a short interrupt guard; file logging happens after it is released.
The R13 automatic save/load flow and R16 manual handoff otherwise remain.

Look for scanout_probe, scanout_dma, scanout_ge_framebuffer, scanout_lcdc and
scanout_layer. File fallback is ms0:/PSPGoTVScale.txt. Ordinary HOME/reboot handling,
NODS integration and original save/write arguments/results remain in place.
The original automatic save-time crash remains unresolved.

## Build and validation

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/go-tv-auto-test
    /tmp/go-tv-auto-test

Strict PSP build and ASan/UBSan host checks pass. Tests enforce the five-register
read whitelist and interrupt guard, no snapshot allocations/mode changes/GE
submissions, two-vblank settled capture and blocked manual reactivation, plus
prior load, native-handoff, memory and input regressions. Host checks do not
execute physical GPU/display hardware; R17 needs the short real-device test above.

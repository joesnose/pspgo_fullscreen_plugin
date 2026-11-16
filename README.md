# PSP Go Fullscreen Plugin — revision 8

[Download R8 build and source](pspgo-tv-scale-v1-auto-home-r8.zip) · [Plugin PRX](PSPGoTVScale.prx)

Immediate fullscreen scaling with ten live filters. Retains revision 6's
user-confirmed HOME rendering and return-to-VSH handling, and the user-memory
GPU fix that removes the opicker dependency.

## Save/load dialog fix (R8)

Detects standard PSP utility dialogs independently of HOME, addressing the
reported Burnout save/load transition that left fullscreen disabled. The display
hook allows the dialog's temporary mode change without treating it as a fatal
unsupported game layout. The worker pauses game substitution, scales a supported
native dialog buffer with bilinear filtering, and resumes the selected game
filter after shutdown/cancellation. Dialogs using the game framebuffer are read
from priority 2 when no native priority-0 layer exists. Nested HOME and repeated
dialogs remain paused until both close.

Utility detection uses export-resolved private type/status getters only after
validating their complete read-only leaf bodies. It does not call public
SavedataGetStatus, which can invoke an application callback on this firmware.
Unavailable or unrecognized getters disable this detection; no firmware data
addresses are guessed. Logs include utility_status support, utility_dialog
transitions and system_dialog_closed,scaling_resumed.

Host tests cover these transitions; Burnout and other games still need a real
PSP Go test. If fullscreen remains lost, copy PSPGoTVScale.txt after reproducing
it and before launching another game.

## Install and control

1. Fully power off the PSP Go and replace ef0:/SEPLUGINS/PSPGoTVScale.prx.
2. Keep the existing GAME entry and NODS configuration:

       game, ef0:/SEPLUGINS/PSPGoTVScale.prx, on

3. Restart. Fullscreen starts automatically with Soft filtering and no delay.
4. Hold L + R and tap Triangle to advance one filter. Release Triangle before
   advancing again. Default NODS controller mapping is L1 + R1 + Triangle.
   A label shows the selected filter for 1.5 seconds. Changes are suppressed
   while native HOME is open. The chord is still visible to the game.

The INI is no longer used or included. An old INI can be deleted or left in place;
it is ignored. Selection lasts for this game session and resets to Soft on the
next launch. NODS positive-peek input follows Impose's validated merged import;
local PSP input is the fallback. No NODS changes are required.

## Filter cycle

Sharp -> Soft -> Blur -> CRT -> Soft CRT -> RGB Mask -> Sharpen -> Posterise ->
Dither -> Deposterise -> Sharp. Initial selection is Soft.

| Filter | Effect | Work |
| --- | --- | --- |
| Sharp | Nearest-neighbour scaling | One image pass |
| Soft | Bilinear scaling; default | One image pass |
| Blur | Stronger spatial smoothing | Five averaged image passes |
| CRT | Bilinear image and alternating scanlines | Image plus overlay |
| Soft CRT | Lighter scanlines with ordered dithering | Image plus overlay |
| RGB Mask | Subtle repeating red/green/blue stripes | Image plus overlay |
| Sharpen | Mild fixed-function unsharp approximation | Six image passes |
| Posterise | Fewer colour levels, roughly eight per channel | Image plus two colour operations |
| Dither | Bilinear scaling with ordered RGB565 dithering | One image pass |
| Deposterise | Colour-aware smoothing of small colour steps | CPU prepass plus bilinear scaling |

Posterise reduces colour levels. Deposterise instead smooths small colour
changes: horizontal and vertical 1:2:1 samples are averaged only when neighbours
are within 12 levels in every RGB channel; stronger edges and centre alpha are
preserved. It handles all four supported source formats and uses an extra
544 KiB user-RAM buffer, allocated only when selected and freed when leaving
that mode after the owned GPU list finishes. If allocation fails, cycling skips
Deposterise and logs the reason. This optional CPU effect may reduce frame rate.

These are PSP implementations, not identical PPSSPP shaders. Sharpening is an
approximation with intermediate clamping; CRT does not simulate curvature.
Filtering uses only the current image, never previous frames. Soft and Sharp
have the least processing work. Actual quality and cost require PSP Go testing.

## HOME, exit and memory

Native HOME always uses bilinear filtering independently of the game preset.
Revision 6's live menu-source capture, full-TV menu rendering, cancel/resume,
early reboot worker quiesce, owned-list drain, priority-0 clear, native TV-mode
restore and scanout/idle-gated EDRAM release remain in place. Reboot does not
free GPU storage or rewrite entry trampolines; late callbacks only set flags.

All GPU-read commands, vertices, saved GE context and optional processed pixels
live in aligned user partition 2. No clocks or DDR protection registers are
changed. opicker is unnecessary. Supported target: PSP Go, ARK, 6.60/6.61,
progressive component 480p, games with supported lower-2-MiB framebuffer layouts.
The existing game image is stretched; game world geometry is not extended.

Log: ef0:/PSPGoTVScale.txt (ms0:/ fallback). Copy it before opening another game.
Filter changes appear as filter records; banner identifies auto_home_r8.

## Build and verification

PSP build uses -Wall -Wextra -Werror:

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

Host checks:

    cc -O2 -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined render_test.c render.c -o /tmp/go-tv-packet-test
    /tmp/go-tv-packet-test
    cc -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined filter_test.c render.c effects.c -lm -o /tmp/go-tv-filter-test
    /tmp/go-tv-filter-test
    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined effects_test.c render.c effects.c -o /tmp/go-tv-effects-test
    /tmp/go-tv-effects-test
    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/go-tv-auto-test
    /tmp/go-tv-auto-test

Checks cover all ten packet modes, format/aspect/buffer combinations, UV bounds,
blur weights, CRT geometry, CPU deposterising against an independent full-frame
reference for all four formats, edge rows/alpha/padding, notice bounds, merged
input validation, one change per press, HOME suppression, optional allocation
failure, processed user-RAM texture submission, and the existing HOME/reboot
regressions. Host tests use fake PSP APIs and do not execute actual GE drawing
or firmware scheduling. Revision 8 has not been tested on real hardware here.

# PSP Go Fullscreen Plugin — Simple R26

[Download Simple R26 build and source](PSPGoTVScale-simple-r26.zip) · [PRX](PSPGoTVScale.prx)

Automatic fullscreen TV scaling for PSP Go on ARK, 6.60/6.61, progressive
component output. Starts enabled with Soft filtering. No INI is needed.

## R26 GPU-work reduction

The SMW R25 Soft log shows approximately 59 scaler draws per second while game
presentations drop to about 30 per second; after scaling is disabled the game
returns to about 58–60 presentations per second. There are no recorded timeouts.
This indicates that scaling is contributing overhead and duplicating unchanged
presentations, but does not identify every scheduling interaction.

The worker now submits game scaling only when the captured presentation serial
or framebuffer layout changes. Initial activation, HOME close and filter changes
still redraw as needed. Scaled output remains selected between updates. This
uses the display-present calls to identify fresh frames; continuously writing a
front buffer without presenting is outside that scheduling assumption.

The default stretched image covers every output pixel, so its redundant
720x480 color clear is removed. The packet explicitly disables clear mode before
the image pass. Pillarboxed output still clears borders. The first pass remains
opaque and spans the complete scaled rectangle; no previous-frame blend is added.
All filters, finish-confirmed frames and HOME stability handling are retained.
Real PSP testing is required to quantify the speed improvement.

## R25 completion tracking and HOME stability

R24's Assassin's Creed log confirms 3,029 retired-list events. R24 stopped
showing the uncertain frames, but still lacked a way to recognize completed
frames whose list IDs were already retired. It also repeatedly restored native
HOME output on these events, causing menu flicker.

R25 registers one owned GE finish callback. Each scaler list has a nonzero
16-bit token in FINISH; the interrupt callback only copies a matching token to
a volatile memory flag. No file I/O, allocation, waiting or GE calls run there.
The worker can accept a positively identified completed frame even when the
list ID is invalid and the game has started more GPU work. PSP's synchronous
single-CPU finish interrupt also restores the list's saved context before the
worker resumes; the worker observes the flag under an interrupt guard.

Unconfirmed/reset work still requires the existing idle fence before reuse;
paused-list storage remains retained. A failed callback registration logs a
negative finish_callback ID and falls back to the existing polling behaviour.
The callback is unregistered only after normal worker quiescence on unload;
reboot leaves it resident with the protected workspace until firmware teardown.
logger_stats now includes finish_confirmed, counting callback-confirmed waits.

A retired ID during HOME no longer clears an already scaled menu or repeatedly
restores the native menu. Native layer clears while a scaled HOME image is ready
retain that image until a replacement or actual menu close. The original clear
is still captured for close/resume. System utility dialogs keep their native
ownership path. These changes need real-game performance and HOME verification.

## Controls

- Hold L+R and tap Triangle: next filter. Release Triangle between presses.
- Hold L+R and tap Square: switch fullscreen scaling off/on.
- HOME works normally, including NODS external-controller HOME.

Default NODS equivalents use L1+R1 with Triangle or Square. Native HOME and
system save/load dialogs yield to their own output; fullscreen resumes after
closing them unless scaling was switched off manually. Selecting off restores
native display/2 MiB EDRAM and frees scaler user-RAM buffers after safe fences.

## Filters

Sharp → Soft → Softer → Blur → Blurrier → CRT → Soft CRT → RGB Mask → Sharpen →
Posterise → Dither → Deposterise → Sharp.

Soft remains the default bilinear pass. Softer averages four bilinear samples
at ±0.5 source-pixel diagonal offsets, broadening the smoothing footprint.
Blur retains its five-sample average at ±0.75 offsets; Blurrier uses the same
five passes at ±1.5, exactly twice the sampling radius. Perceived softness is
image-dependent, so the two presets target stronger smoothing rather than a
precise perceptual multiplier. Only the current frame is sampled; no temporal
feedback is added. Softer/Blurrier cost more GPU work than Soft. Native HOME
always uses Soft independently of the game filter.

All previous filters are retained. Deposterise uses an optional 544 KiB CPU
buffer and can be slower; it is allocated only when that mode is selected.

## Simplified scope

Retains the working HOME rendering/reboot cleanup, native switch geometry
verification, firmware utility save/load detection, automatic resume, and
paused/retired GE-list handling. Experimental Circle profiles, Select memory
controls, memory measurements, GE-context syscall hooks, direct file-write
hooks and savedata-entry syscall hooks have been removed. Only the two requested
button chords remain. This is not a claimed fix for Burnout Legends' separate
silent end-of-race save crash; the system save/load dialog fix is retained.

Basic queued diagnostic logging remains at ef0:/PSPGoTVScale.txt (ms0:/ fallback).
Hooks queue messages in fixed memory; the worker writes them outside interrupt
context. The banner identifies simple_r26. No clocks, DDR unlocks, NODS binary
changes or VSH volume work are included.

## Install

Fully power off, replace ef0:/SEPLUGINS/PSPGoTVScale.prx, and restart. Keep one
GAME plugin entry:

    game, ef0:/SEPLUGINS/PSPGoTVScale.prx, on

Check fullscreen startup, Triangle filter cycling, Square off/on, HOME cancel
and game exit, and the game's normal system save/load dialog. Hardware behaviour
of this exact combined build still needs confirmation.

## Build and verification

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/simple-test
    /tmp/simple-test

Strict PSP build and ASan/UBSan host checks pass: actual-source startup/controls,
HOME/utility/native/reboot handling, 144 GPU format/stride/aspect/buffer cases,
192 filter cases including new sample footprints/weights/UV bounds, CPU effects,
and queued logger safety. Host tests do not execute real PSP hardware.

New actual-source checks repeatedly retire 50 list IDs without a menu and verify no output/mode handoff or unconfirmed-frame publication, then verify successful rendering resumes. Layer-clear substitution and genuine HOME clearing are also checked.

R25 actual-source checks verify finish-confirmed invalid IDs under a busy global GE queue, mismatched-token rejection, unchanged idle fencing for unconfirmed work, stable scaled HOME across retired IDs and layer clears, and normal HOME close/resume.

R26 host checks verify 60 duplicate source samples submit no extra GE/scanout work, a new serial submits one job, and a filter change forces a redraw. Packet/filter checks verify clears are omitted only for fullscreen and clear mode is explicitly disabled in every packet.

# PSP Go Fullscreen Plugin — Simple R24

[Download Simple R24 build and source](PSPGoTVScale-simple-r24.zip) · [PRX](PSPGoTVScale.prx)

Automatic fullscreen TV scaling for PSP Go on ARK, 6.60/6.61, progressive
component output. Starts enabled with Soft filtering. No INI is needed.

## R24 display stability

The Assassin's Creed R23 log contains about 600 retired-list events and 355
scaling resumes despite one real utility cycle. An unconfirmed/paused/retired
scaler frame now leaves the last confirmed scaled buffer selected during normal
gameplay. It no longer invokes the native/menu restore path without a real HOME
or utility transition. Retired IDs still require global idle before packet reuse;
a paused list is still retained until completion. Unconfirmed frames are discarded.

Clearing the top game display layer while scaling now keeps the scaled layer
selected instead of exposing a native framebuffer under 720-wide scanout. The
original clear is recorded and genuine menu/utility clears still pass through.
These changes target the observed quartered/fullscreen flicker, but require
real-game verification. Controls and all twelve filters remain unchanged.

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
context. The banner identifies simple_r24. No clocks, DDR unlocks, NODS binary
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

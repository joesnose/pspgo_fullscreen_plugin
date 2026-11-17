# PSP Go Fullscreen Plugin — R18 test build

[Download R18 test build and source](pspgo-tv-scale-v1-auto-home-r18.zip) · [Plugin PRX](PSPGoTVScale.prx)

Immediate fullscreen TV scaling for PSP Go. Soft is the default; opicker is not required.

## R18 change

The R17 device log reports native 480×272 mode, but actual display DMA remains
720 pixels wide with a 512-pixel stride. The cold native baseline uses width
480 and stride 512. This mismatch can explain the duplicated/quartered picture.

Native and fullscreen TV use the same physical display mode. R18 requests native
geometry before detaching the scaled framebuffer, so the subsequent framebuffer
format/stride transition programs the requested native width. Previously that
transition happened while the requested width was still 720.

Manual reactivation is restored, with a hardware check: actual DMA width, stride,
pixel format and framebuffer address must match native output before memory is
released or scaling can restart. Cached DMA settings and the cold native LCDC
baseline are checked when available. If these checks fail, the plugin stays in
native handoff and records the mismatch. Hardware observations are read only;
this revision adds no guessed register writes.

The R13 automatic save/load control flow remains the baseline. HOME/NODS support,
reboot cleanup and all ten filters are retained. The original end-of-level save
crash is not yet confirmed fixed on hardware.

## Install and short test

1. Fully restart the PSP after replacing ef0:/SEPLUGINS/PSPGoTVScale.prx.
2. Confirm the normal load screen still works.
3. During gameplay hold L+R and tap Square to switch to native output. Wait two
   seconds, then repeat to restore fullscreen. Check whether duplication is gone.
4. Copy ef0:/PSPGoTVScale.txt before starting another game and report the result.

Default NODS mapping is L1+R1+Square. Existing GAME entry:

    game, ef0:/SEPLUGINS/PSPGoTVScale.prx, on

L+R+Triangle cycles Sharp, Soft, Blur, CRT, Soft CRT, RGB Mask, Sharpen,
Posterise, Dither and Deposterise. INI files remain ignored.

## Diagnostics and validation

Read-only snapshots include cold_native, scaled_settled, utility_pause,
utility_visible, manual_before, manual_bound_before_release and
manual_after_release. Failed native checks record native_hardware_mismatch or
resume_not_ready. Logs fall back to ms0:/PSPGoTVScale.txt.

The five DMA register reads are 0xBC800100..0xBC800110, identified from the Go
firmware setters. Reads are guarded by disabled interrupts; logging happens
outside that guard. Firmware reference binaries are not distributed.

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/go-tv-auto-test
    /tmp/go-tv-auto-test

PSP compilation and ASan/UBSan host regressions pass. The host model reproduces
the old ordering's software-480/hardware-720 mismatch, verifies the new ordering,
and refuses release/reactivation with stale DMA width or LCDC geometry. Previous
load, HOME, paused GE, write-guard, memory and input regressions also pass.
Host checks do not execute PSP hardware; this remains a device test build.

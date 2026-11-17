# PSP Go Fullscreen Plugin — R19 test build

[Download R19 test build and source](pspgo-tv-scale-v1-auto-home-r19.zip) · [Plugin PRX](PSPGoTVScale.prx)

Immediate fullscreen TV scaling for PSP Go. Soft is the default; opicker is not required.
The R18 native/fullscreen display handoff is retained unchanged. Its toggle worked
on the user's PSP Go; the end-of-level Burnout Legends save still crashed.

## Several tests in one PRX

Hold L+R and tap Circle during scaled gameplay to cycle these profiles once per
press. The name appears briefly onscreen and is recorded in PSPGoTVScale.txt.
Default NODS mapping is L1+R1+Circle. No INI is needed.

| Profile | Circle taps after starting the game | Behaviour |
| --- | --- | --- |
| GUARD | 0 (default) | Stop new scaler lists while the game saves/restores GE context; wait up to 100 ms for the scaler's own list to finish before calling the original context API. |
| IDLE | 1 | Submit scaler work only when the GE queue is idle; context calls are traced without the new drain guard. |
| BOTH | 2 | Apply both checks. |
| BASELINE | 3 | Disable the new context/idle guards; retain tracing and the existing save/load protections. |

Use one profile for each attempt at the failing level ending. If it crashes,
restart, choose the next profile before reproducing it, and copy the log before
starting another game (startup replaces the log). You can name copies GUARD.txt,
IDLE.txt, BOTH.txt, etc. There is no need to reinstall the plugin between profiles.
Start with GUARD; if that still fails, try IDLE, then BOTH. BASELINE is optional
because the failure with the previous build is already known. Profiles reset to
GUARD each game startup. Do not change profiles during a save/load operation.

The idle check may reduce update frequency under heavy GPU load; this is an
experimental comparison. None of these modes is a confirmed device crash fix.

## Why these tests

The R18 log records successful toggles: native DMA width returns to 480 with a
512-pixel stride, and scaler resources are released. The later crash occurs
before a new savedata-init or file-write entry appears. There are zero recorded
list timeouts. This does not establish a RAM shortage or pinpoint the fault.

Firmware GE context save/restore requires GE idle. R19 tests whether concurrent
scaler submissions interfere with the game's own context operations. The guard
suppresses new submissions atomically with enqueue, while the existing worker
retires its own list. It never cancels the game's work, modifies context buffers,
fakes success or retries the original call. Timeout still calls the original
exactly once with its original arguments/K1 and preserves its result.

User GE Break is traced and runs immediately, since it may be needed to resume
the queue. Internal driver context operations and the plugin's kernel imports
bypass these user-syscall wrappers. Successful tracing is rate-limited to one
entry/return pair per operation per second, with cumulative counters in stats;
a missing return is only a clue, and rate limiting means some calls are unlogged.
All profiles retain existing utility/save/HOME controls, with a final atomic
utility gate before scaler enqueue. BASELINE is therefore a comparison with the
new experimental guards disabled, not a byte-identical copy of R18.

## Install and controls

Fully restart after replacing ef0:/SEPLUGINS/PSPGoTVScale.prx. Existing GAME entry:

    game, ef0:/SEPLUGINS/PSPGoTVScale.prx, on

- L+R+Circle: select test profile.
- L+R+Square: switch fullscreen/native output (R18 hardware verification retained).
- L+R+Triangle: cycle Sharp, Soft, Blur, CRT, Soft CRT, RGB Mask, Sharpen,
  Posterise, Dither and Deposterise.

Confirm the load screen still works before the level-ending test. Logs are at
 ef0:/PSPGoTVScale.txt, falling back to ms0:/PSPGoTVScale.txt. Look for test_profile,
 test_stats, ge_api_enter, ge_api_handoff and ge_api_return. ge_api_guard,1 confirms
all three user GE hooks resolved; ge_api_guard,0 means the context guard is unavailable.

## Build and validation

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/go-tv-auto-test
    /tmp/go-tv-auto-test

Strict PSP compilation and ASan/UBSan actual-source host regressions pass. New
checks cover profile input/edges, context pointer/K1/result preservation, worker
retirement and bounded paused-list waits, immediate Break, atomic enqueue gating,
and busy idle-only skipping without changing the visible frame or faulting.
Prior native handoff, utility/load, HOME, memory, paused-GE and input tests pass.
The user GE NIDs were verified against the Go 6.61 driver export table; Sony
firmware binaries are not distributed. No guessed register writes are added.
R19 has not been tested on physical PSP hardware.

# PSP Go Fullscreen Plugin — R21 user-memory comparison

[Download R21 test build and source](pspgo-tv-scale-v1-auto-home-r21.zip) · [Plugin PRX](PSPGoTVScale.prx)

Immediate fullscreen TV scaling with a controlled user-RAM retention test.
This diagnostic uses R20 queued logging rather than the separate cleaned/no-log
build. No new logging I/O runs in hooks. No crash fix is claimed.

## Test A: native with user-RAM buffers retained

1. Install PSPGoTVScale.prx at ef0:/SEPLUGINS/ and fully restart.
2. Keep the default Soft filter and GUARD profile for both attempts.
3. Play normally. Just before selecting Complete at the failing race ending,
   hold L+R and tap SELECT ONCE. Wait two seconds for native output to settle.
4. Select Complete. Record whether it passes or freezes.
5. Copy ef0:/PSPGoTVScale.txt before launching the game again; call it retained.txt.

SELECT from scaled gameplay stops scaler GPU jobs and restores native display
and 2 MiB EDRAM, but keeps existing user-RAM blocks allocated. It does not allocate
a deposterise buffer unless one was already present. Soft normally retains only
the approximately 23 KiB GE workspace. Do not change filters between attempts.

## Test B: native with user-RAM buffers freed (if A crashes)

Restart and reproduce the same setup. Before Complete, hold L+R and tap SELECT,
wait two seconds, release the chord, then hold L+R and tap SELECT AGAIN. Wait two
seconds. The second press frees both buffers while staying native; it does not
restart scaling. Select Complete and collect the log as freed.txt.

Both tests use identical native geometry, no scaler jobs and 2 MiB EDRAM.
The user-RAM allocation lifetime is the deliberate difference. If A passes,
resident user-RAM blocks alone are not sufficient to reproduce the fault in that
attempt. If A fails but B passes, user-RAM occupancy or corruption becomes a
stronger suspect; this alone does not prove an out-of-memory failure.

L+R+Square retains its previous behaviour: from scaled mode it enters native
and frees buffers; from ready native mode it returns to scaling. Avoid Square
during test A because it would leave the retained-native test. Filter cycling
is disabled in native mode to prevent accidental deposterise-buffer release.
Existing L+R+Circle profiles and L+R+Triangle filters remain available while scaled.
Default external NODS mapping uses L1+R1 with the same third button.

## Measurements

user_memory records point, time, partition-2 total_free/max_free, allocation UIDs,
requested buffer sizes, EDRAM size and keep flag. Measurements occur at state
changes and once per second. Look for native_retained (GPU UID >=0, EDRAM2097152)
and native_freed (GPU/dep UIDs -1, EDRAM2097152). Actual allocator rounding may
make the free-byte difference larger than the requested size. Each free getter
is its own snapshot; the game may allocate between calls. The largest free block
and total free RAM do not prove how the game's allocator behaves.

Log fallback is ms0:/PSPGoTVScale.txt. Queued tail messages can be lost in a hard
freeze; copy logs before the next game startup overwrites them.

## Build and checks

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/memory-test
    /tmp/memory-test

PSP build and ASan/UBSan actual-source regressions pass. The new test verifies
that retained-native has no new GE jobs, unchanged user allocation IDs and free
bytes, native DMA geometry and 2 MiB EDRAM; a second SELECT releases both buffers
without changing output or submitting work, increases measured mock free RAM,
and Square then resumes safely. Previous native/load/HOME/input tests remain.
Host free-memory getters are controlled fakes, not device measurements.

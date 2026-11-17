# PSP Go Fullscreen Plugin — R22 EDRAM comparison

R21 Complete succeeded with the user-RAM workspace retained, native scanout,
2 MiB EDRAM and no scaler jobs. R22 adds native scanout with the same user-RAM
allocation and no scaler jobs, but retains 4 MiB EDRAM. This is a diagnostic,
not a claimed crash fix. It uses R20 queued logging.

## Test

Install the PRX and fully restart. Keep Soft and the default GUARD profile.
Before selecting Complete at the usual race ending, hold L+R and tap SELECT
ONCE. Release, wait two seconds, then select Complete. Copy
 ef0:/PSPGoTVScale.txt before launching another game and report success/freeze.
Default NODS controller chord is L1+R1+SELECT.

A single press now leaves physical EDRAM at 4 MiB. Look for
 user_memory,point,native_retained_4m and edram,4194304,held_edram,1 in the log.
The visible image is native-sized, the user buffers remain allocated, and
there are no new scaler GPU submissions. The game's public size query remains
2 MiB, as during normal scaling, while actual driver EDRAM remains 4 MiB.
Native display-mode reporting and save-hook bypass match the previous native
comparison; extended bank ownership is tracked separately from active scaling.

Optional comparison after a failed first attempt: restart, reproduce the same
setup, press SELECT once and wait two seconds, then release and press SELECT
AGAIN. Wait two seconds and select Complete. This shrinks EDRAM to 2 MiB while
retaining user RAM, matching the R21 successful condition. A THIRD SELECT frees
user RAM too. Each tap requires L+R. Do not press multiple times in the first test.

L+R+Square returns to scaling from ready native output. Held EDRAM is first
released under native scanout/GE-idle checks before reactivation. Reboot cleanup
also releases the held bank. The existing display handoff order and geometry
checks are retained. L+R+Triangle filters and L+R+Circle profiles remain while
scaled. Do not change filters or profiles between comparison attempts.

## Interpretation

If Complete fails with 4 MiB but succeeds with 2 MiB under native/no-jobs output,
expanded EDRAM/driver state becomes a stronger suspect. If 4 MiB native passes,
4 MiB state alone is insufficient in that attempt; active scaler rendering or
its interaction with the game remains a suspect. Neither outcome alone identifies
the exact failing instruction or proves an out-of-memory condition.

Logs include partition-2 total/largest free blocks, buffer IDs, actual EDRAM
size and held flag once per second and at transitions. Queued tail messages can
be lost on hard freeze. Fallback path is ms0:/PSPGoTVScale.txt.

## Build and checks

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/edram-test
    /tmp/edram-test

Strict PSP compilation and ASan/UBSan actual-source regressions pass, covering
4 MiB native retention with unchanged RAM IDs/no jobs, public size masking,
separate 2 MiB shrink, later RAM release, direct resume and reboot bank cleanup,
plus previous native/load/HOME/input checks. R22 has not run on device yet.

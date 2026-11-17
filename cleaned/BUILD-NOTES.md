# Cleaned-source build

Built from the nine files in the uploaded PSPGoTVScale_cleaned.zip. Every uploaded
file is retained byte-for-byte, including main.c and the supplied tests. Module
version remains 1.19 as supplied. No device crash fix is claimed.

The upload lacked Makefile, render.c/render.h, native_output.inc,
scanout_probe.inc, write_guard.inc and test_psp.h. These were supplied from the
previous project. Makefile now force-includes build_compat.h, whose no-op logline
allows the missing support files to compile without file logging. The logger
implementation and its file writes are not included. Render_test.c was also
supplied for verification. No uploaded runtime source was edited.

The uploaded auto_home_test.c has an invalid chained pointer/integer initializer
and expects the old once-per-list pause counter. A separate auto_home_build_test.c
fixes only that initializer and changes the two counter assertions to >=1, matching
the uploaded main.c which counts pause polls. The original test remains untouched.

Build passed with -Wall -Wextra -Werror using pspdev/pspdev:latest. ASan/UBSan checks
passed for the adapted actual-source control suite, GPU packets, filters and
HOST_TEST deposterise. The unadapted uploaded control test does not compile cleanly.

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

    cc -DHOST_TEST -D_GNU_SOURCE -include build_compat.h -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_build_test.c render.c effects.c -o /tmp/cleaned-test
    /tmp/cleaned-test

Install PSPGoTVScale.prx in ef0:/SEPLUGINS/ and fully restart. Existing controls
remain L+R+Circle for test profiles, L+R+Square for native/fullscreen, and
L+R+Triangle for filters. GUARD is the startup profile; two Circle taps select BOTH.
This build does not produce a new PSPGoTVScale.txt log, so report its visible
behaviour. A previously existing log may remain and should not be read as new.

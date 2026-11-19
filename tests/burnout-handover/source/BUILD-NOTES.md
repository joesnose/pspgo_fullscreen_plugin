# gotvscale_files.zip compiled build

All ten uploaded files are preserved byte-for-byte. The upload omitted Makefile,
render.c/render.h, native_output.inc, scanout_probe.inc and test_psp.h. These were
supplied from Tidy R29. Render_test.c was also supplied for completeness.
Module version remains1.29 as provided; no runtime source edits were made.

Strict PSP compilation passed with -Wall -Wextra -Werror using
pspdev/pspdev:latest. Supplied actual-source control-flow and all192 filter cases
passed ASan/UBSan. Physical PSP behaviour/performance remains untested.

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/gotvscale-test
    /tmp/gotvscale-test

Replace ef0:/SEPLUGINS/PSPGoTVScale.prx and fully restart. Soft remains the
supplied startup default. Existing GAME entry and controls are retained.

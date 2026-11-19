# Uploaded fixed-source build

All 11 files from PSPGoTVScale_fixed.zip are preserved byte-for-byte. The upload
omitted the Makefile, renderer, native output/scanout support and test PSP header;
these were supplied from R28. The module version remains 1.28 as uploaded.
This is an alternate build, not a new numbered plugin revision.

PSP compilation passed with -Wall -Wextra -Werror using pspdev/pspdev:latest.
The supplied actual-source auto_home_test.c passed ASan/UBSan with the supplied
runtime sources and added support files. No runtime source fixes were applied.
Host checks do not execute real PSP graphics/cache operations. Hardware behaviour
and speed improvements are unverified.

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/upload-fixed-test
    /tmp/upload-fixed-test

Replace ef0:/SEPLUGINS/PSPGoTVScale.prx and fully restart. Retain the existing
GAME entry. Source and binary are packaged together with SHA256SUMS.

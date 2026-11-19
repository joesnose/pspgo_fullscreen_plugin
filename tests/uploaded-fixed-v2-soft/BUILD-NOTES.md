# Uploaded v2 — Soft-default build

As requested, both main.c startup defaults change from Sharp to Soft. These
are the only changes to supplied runtime source. The other ten uploaded files
remain byte-for-byte unchanged. Missing Makefile, renderer, native output,
scanout support and test header were supplied from R28. Module version stays1.28.

The Sharp-default upload was not published. This package is the Soft variant.
Strict PSP compilation passed. The original host test resets its fixture to
Sharp but expects Soft later, so it does not pass unchanged. A separate
 auto_home_soft_test.c uses Soft in Reset and updates the expected first cycle
to Softer (and subsequent cycle start to Blur); it passes ASan/UBSan. The
original uploaded test is retained. No hardware speed/stability claim is made.

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_soft_test.c render.c effects.c -o /tmp/v2-soft-test
    /tmp/v2-soft-test

Replace ef0:/SEPLUGINS/PSPGoTVScale.prx and fully restart. Startup log should
show filter,Soft. Controls and the supplied v2 rendering changes remain.

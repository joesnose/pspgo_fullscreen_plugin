# Build identity and reproducibility

The PRX and ELF are copied from the exact latest gotvscale upload build, not
rebuilt during handover. The PRX equals Tidy R29 byte-for-byte. SHA256SUMS includes
both. No claim is made that the user reproduced Burnout with this exact hash.

Build from source/ (Docker PSPDEV image; :latest is not a pinned toolchain):

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

Host control/ownership tests:

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/gotv-test
    /tmp/gotv-test

Filter cases:

    cc -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined filter_test.c render.c effects.c -lm -o /tmp/gotv-filters
    /tmp/gotv-filters

These passed when this upload was compiled, including192 filter combinations.
Latest R29 baseline also passed packet, CPU-effect and logger checks. Fakes do
not execute physical hardware. A fresh build with a different toolchain may
produce different addresses; keep its PRX and ELF paired separately.

Symbols/disassembly of the preserved ELF are in build/. To resolve an actual
crash PC, identify the owning module and its loaded text base first. Use its
module-relative offset with the matching ELF; take branch-delay EPC/Cause into
account. Do not feed arbitrary absolute runtime addresses into addr2line and
assume the result is valid. This ELF has a symbol table, but plugin sources were
compiled without -g. Existing DWARF lines are principally linked libgcc, so a
plugin source-line lookup can return ??:0 even for a useful symbol/offset.

    psp-nm -n build/PSPGoTVScale.elf
    psp-objdump -d build/PSPGoTVScale.elf
    psp-addr2line -f -e build/PSPGoTVScale.elf MODULE_RELATIVE_OFFSET

If a new -g diagnostic build is needed, preserve it as a NEW artifact and
reproduce against it; it is not a substitute ELF for the old PRX.

# joesnose Go fullscreen — R33

Build filename: joesnose_Go_fullscreen.prx

Changes from R32:
- Runtime logging is disabled at compile time. The plugin does not create, truncate or append PSPGoTVScale.txt. Periodic diagnostic snapshots and reports are disabled. The cold native hardware reference used for safe output restoration is retained.
- Blurrier uses 1.5 times Blur's sampling radius, reduced from 2 times. Its five-pass blend and brightness are unchanged. Blur and every other filter are unchanged.
- The build target is renamed joesnose_Go_fullscreen, including its matching ELF.

The R32 fix for unused GPU CALL return registers is retained. Fullscreen still starts automatically with Soft. L + R + Triangle cycles filters; L + R + Square switches output mode. HOME/NODS and system save/load handling are retained. This build otherwise keeps R32's behaviour, including its native-mode hidden-render comparison; it is not a broader cleanup of the diagnostic architecture.

## Install

Copy joesnose_Go_fullscreen.prx into your existing plugin directory. Update the filename in the existing ARK GAME plugin entry from PSPGoTVScale.prx to joesnose_Go_fullscreen.prx, preserving the directory and enabled setting. Disable/remove the old entry so only one copy loads. Restart the PSP.

Existing PSPGoTVScale.txt files are left untouched and may contain old records. This build produces no new log. Keep the R32 diagnostic PRX separately if logging is needed later.

## Validation

PSP build passed with -Wall -Wextra -Werror. Host filter and lifecycle suites passed under ASan/UBSan, including 192 filter/format/aspect/buffer combinations, the 1.5x Blurrier radius, all four saved CALL-depth cases, HOME, utility, resume and cleanup handling. The built ELF contains neither the log-file path nor diagnostic log format strings. The R32 base passed Burnout Legends Complete/Continue in both native hidden-render and fullscreen tests reported by the user. This exact R33 build has not yet been tested on hardware.

The archive contains complete source, host checks, the renamed PRX, matching ELF and SHA256SUMS. Previous repository release/test packages are retained.

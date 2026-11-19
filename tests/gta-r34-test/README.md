# joesnose Go fullscreen — R34 GTA comparison

This separate test compares against R33 by disabling only unused CALL return-register clearing. The FINISH callback still signals completion, but leaves every saved context word untouched. No game-ID detection or automatic per-game exception is included: clearing is disabled for every game while THIS test PRX is installed. Keep R33 separately for Burnout, whose crash may return with this test.

Logging remains disabled. Blurrier remains at 1.5x Blur radius, Soft remains the startup filter and the PRX filename is joesnose_Go_fullscreen.prx. All other R33 display, HOME/NODS, utility, rendering and native hidden-test behaviour is retained.

## Test Liberty City Stories

1. Replace the installed joesnose_Go_fullscreen.prx with this archive's PRX. Make sure only one scaler entry is enabled, then restart the PSP.
2. Launch Liberty City Stories with the same plugins/settings and leave fullscreen enabled. Check whether it gets past the point that freezes/goes black in R32/R33.
3. Report whether gameplay runs continuously without needing HOME. If it still stalls, report whether HOME briefly revives it as before and where the stall happens.

No log is generated. Existing PSPGoTVScale.txt files are old and are not modified by this build. This is a comparison, not a confirmed GTA fix. If GTA works, a game-specific solution can be considered; if it still stalls, return clearing alone is not enough to explain the problem.

## Validation

Host ASan/UBSan lifecycle checks pass, including all four nesting combinations: matching FINISH tokens still signal completion and every saved context word remains unchanged; unrelated tokens are ignored. Other lifecycle checks cover HOME/utility handling, native/fullscreen and cleanup. PSP build uses warnings as errors. Exact R34 hardware test is pending. Source, matching ELF and SHA256SUMS are included. Previous downloads remain available.

# PSP Go TV Scale — hidden-native R31 diagnostic

This is a comparison test, not a confirmed crash fix. R30 passed Burnout Legends Complete with native display, 4 MiB EDRAM and the GE workspace retained, without scaler rendering. R31 adds Soft-filter scaler jobs into hidden upper EDRAM buffers while keeping the same native display output. Hidden rendering never publishes its buffers or changes the display mode.

## Test

1. Replace the installed PSPGoTVScale.prx with this archive's PRX and restart the PSP. Disable PSPLINK and BSOD for this comparison; keep other settings as in the passing R30 test.
2. Start Burnout Legends. Fullscreen starts automatically, using Soft.
3. Before selecting Complete, press L + R + Square once. The picture should become native-sized. Wait two seconds, then select Complete. Do not toggle back before testing.
4. Report whether Complete passes and upload PSPGoTVScale.txt. Keep the log from this run before launching another game.

The banner is hidden_native_r31. In native mode the hidden_test records should show increasing hidden_draws, edram 4194304, held 1, mode 0x2D2, width 480, height 272 and the retained gpu_uid. A pass only tests hidden rendering if those jobs actually ran during the transition. HOME and system utility screens pause hidden rendering. A hidden_test_failed record means that rendering stopped; include the log even if Complete passes.

A crash here implicates rendering/context handling or worker load. A pass shifts attention toward visible fullscreen output, layer or publication interactions. Neither outcome identifies an exact cause by itself. Confirming the normal R29 crash remains useful as a control.

L + R + Square again resumes visible fullscreen. L + R + Triangle retains filter cycling in visible fullscreen; the hidden comparison always uses Soft.

## Build and validation

Built using pspdev/pspdev:latest with make -j4, warnings treated as errors. Host lifecycle tests pass with AddressSanitizer and UndefinedBehaviorSanitizer, including hidden-buffer bounds, unchanged display calls, menu suppression, native/fullscreen resume and reboot cleanup. Hardware results are pending. The matching ELF and complete source are included; SHA256SUMS covers the package files.

Normal release files in the repository are unchanged.

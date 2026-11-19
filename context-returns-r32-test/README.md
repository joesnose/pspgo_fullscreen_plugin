# PSP Go TV Scale — context-return R32 diagnostic

R31 crashed inside sceGE_Manager + 0x1DB8 while reading the command before a CALL return address. R30 (native output, retained 4 MiB EDRAM and workspace, no rendering) passed. R31 added hidden rendering and reproduced the crash.

R32 keeps the R31 hidden-render comparison and changes only the saved context return-register handling: in our own FINISH callback, before the firmware restores our saved context, it clears RADR2 if saved EXEC has no DEPTH2 bit, and also RADR1 if neither nesting bit is present. This mirrors the firmware queue's own nesting rules. Active CALL return addresses, EXEC, all other context fields and GPU commands are preserved. No firmware code is patched. The callback performs only memory reads/writes and counters; file logging stays in the worker.

The suspected mechanism is stale return addresses being dereferenced during restoration. This remains a hypothesis. The address in the crash photo alone does not prove why access failed. This test does not clear active return addresses or bypass context restoration.

## Test

1. Replace the installed PSPGoTVScale.prx with this archive's PRX and restart the PSP. Keep the same configuration as your R31 run.
2. Play Burnout Legends until just before Complete. Fullscreen starts automatically using Soft.
3. Press L + R + Square once to switch to native size. Wait two seconds, then select Complete. Do not toggle back before the test.
4. Report pass or crash and upload PSPGoTVScale.txt. If an exception screen appears, photograph it too. Keep the log before launching another game.

The log banner is context_returns_r32. hidden_test should show increasing hidden_draws while native output remains 480x272 with 4 MiB EDRAM retained. context_returns records checks, cleared_radr1, cleared_radr2 and nested. Counters cover visible startup rendering and hidden rendering, so compare successive records. If neither clear counter increases, this run did not exercise the suspected stale-return condition. A pass supports the hypothesis but needs a visible-scaling follow-up; a crash needs its new exception location compared with R31. Pending/zero-timeout records cannot establish what happened after the last flushed log line.

L + R + Square again resumes visible fullscreen. Filter cycling remains L + R + Triangle; hidden rendering uses Soft for the controlled comparison. HOME and utility dialogs pause hidden rendering as in R31.

## Validation

PSP build passed with warnings treated as errors. Host ASan/UBSan tests passed: all four CALL-depth combinations, unrelated-token rejection, preservation of every other context word, hidden-buffer bounds, no hidden display publication, HOME/utility suppression, resume and reboot cleanup. Firmware layout and callback-before-restore ordering were checked against the reference GE implementation and the inspected 6.61 Go binary. Hardware testing is pending.

Complete source, matching ELF and SHA256SUMS are included. This is a separate diagnostic package; repository root release files are unchanged.

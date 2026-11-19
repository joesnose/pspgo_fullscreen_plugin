# joesnose Go fullscreen — R35 continuous-refresh test

User comparison: Liberty City Stories works with R25 but freezes/goes black with R26. R26 added suppression of unchanged source presentations. The R26 log shows the game presentation count stuck at 136 without a reported GPU timeout; HOME temporarily revives output. This narrows the suspect change but does not prove a causal mechanism.

R35 starts from R33 (not R34), keeps the R32 unused CALL return-register fix, and removes only the unchanged-source early return in UpdateGameOutput. While visible scaling is active, the worker can render and publish the current valid source again even if its address/serial/stride/format have not changed. It retains the same-refresh fence, vblank publication, completion tracking, UI ownership handling, worker priority and event timeout. This is not a full rollback to R25's architecture. The native hidden-render diagnostic remains unchanged.

Logging stays disabled, filename stays joesnose_Go_fullscreen.prx, startup filter is Soft, Blurrier radius stays 1.5x Blur. Shortcuts remain L+R+Triangle for filters and L+R+Square for scaled/native.

## Test

Replace the installed joesnose_Go_fullscreen.prx with this archive's PRX, ensure only one scaler is enabled, and restart the PSP. Launch Liberty City Stories with fullscreen left enabled. Check whether it progresses beyond the previous freeze without pressing HOME, and report whether gameplay is smooth or slower. If still stuck, report whether HOME temporarily revives it again. No new log is generated; existing PSPGoTVScale.txt files belong to old runs.

After GTA, repeat Burnout's Complete/Continue check, because this build retains its context fix but adds continuous work. Rendering unchanged sources costs additional GPU work and may affect performance; exact device behaviour is pending.

## Validation

PSP build passes with warnings as errors. Host ASan/UBSan lifecycle suite passes, including 60 consecutive refreshes of an identical source with rendering/publication each time, prevention of a second render within one TV refresh, saved CALL-depth preservation/clearing, menu/utility transitions, native resume and cleanup. Matching source, PRX, ELF and SHA256SUMS are included. Prior packages remain available.

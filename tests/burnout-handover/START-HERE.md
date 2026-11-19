# PSP Go fullscreen plugin: independent review handover

Read HANDOVER.md first, then inspect source/. The request is to independently
review the unresolved Burnout Legends Complete/end-of-race crash. Separate
facts from hypotheses, and check the source before relying on prior diagnoses.

The latest PRX and its exact matching ELF are in build/. They are preserved,
not rebuilt for this handover. SHA256SUMS identifies every included file.
The last explicitly reported Burnout crash was on an earlier uploaded-v2 Soft
build: latest-binary Burnout failure has NOT been confirmed. See evidence notes.

Useful reviewer prompt:

> Review this PSP Go kernel TV-scaling plugin for a reproducible Burnout Legends
> crash on selecting Complete after a race. Native output with the scaler's
> user-RAM workspace retained and no scaler jobs passed in R21. PSPLINK could
> not establish USB; ARK BSOD produced solid red followed by power-off, with no
> readable exception. Audit GE/list/context lifetime, EDRAM ownership, display
> transitions, hook/reboot lifecycle and exception-path interactions. Treat
> performance tests from SMW and Assassin's Creed separately. Report concrete
> findings with source lines and distinguish proven bugs from hypotheses.
> Propose one-variable experiments with explicit predicted outcomes. Do not
> assume a missing log entry proves an API was never reached. Do not introduce
> file I/O or allocation in interrupt/exception handlers. Avoid repeating the
> failed debugger setup without investigating its incompatibility first.

No game ROM/ISO, save data, Sony firmware or credentials are included. The
original development repository is private. Keep the bundle private unless
its owner authorizes wider sharing. No messages were sent to other reviewers.

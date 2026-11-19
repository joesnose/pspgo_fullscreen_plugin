# Burnout Legends / PSP Go TV scaler handover

Prepared 2026-11-19. This document is a conversation/evidence summary, not a
confirmed diagnosis. The prior assistant has not reproduced this on hardware.

## Objective and current scope

Generic PSP Go TV fullscreen software scaler, with minimal user controls:
automatically on with Soft; L+R+Triangle cycles filters; L+R+Square switches
scaled/native. Preserve NODS external HOME, correctly rendered quit menu,
return to VSH and system save/load dialog handoff. The game image is stretched;
this is not a game-world widescreen extension. The unresolved issue is a crash
in Burnout Legends at a race ending, not the already-fixed ordinary save UI.

## Hardware/software and remaining unknowns

- Real PSP Go, component TV output, progressive480p. External controller uses
  NODS. Default chords map to L1+R1 plus the same third button.
- User describes ARK5 and firmware6.61. Captured logs report model4 and devkit
  0x06060010 (6.60). This discrepancy has not been resolved; do not silently
  replace the observed value with the reported version.
- Game confirmed as Burnout Legends, not Dominator. Region/game ID/revision,
  ISO versus other launch method, save slot and exact failing event are unknown.
- Linux PC: CachyOS (Arch family), fish shell. Used for USB/debugger attempts.
- Other enabled plugins/current ARK full revision are not fully inventoried.
  opicker previously caused a separate VSH volume crash. Scaler was changed
  to stop depending on its global RAM-unlock side effect; no clocks or DDR
  unlocks are intentionally applied by this scaler.

## Reproduction and strongest reported results

1. Launch Burnout with TV scaling active, play race, choose Complete on the
   results/end screen. The user repeatedly confirms freezing/crashing at the
   same point BEFORE the system save dialog appears. Background saving is the
   user's description; exact game operation at the fault has not been captured.
2. Unloading/disabling the scaler allowed the game past the failing screen.
3. Ordinary system save/load dialogs now enter, close and return to fullscreen.
   That success is distinct from the silent/end-of-race Complete crash.
4. R21 native-retained test: before Complete, stop scaler jobs and restore native
   480x272/2MiB EDRAM while KEEPING the user-RAM GE workspace. User explicitly
   reported "COMPLETE WORKED". This shows that allocation alone did not suffice
   to trigger the fault in that attempt. It does not exclude corruption or
   establish which other changed condition mattered.
5. R22 added native output + retained user RAM + retained4MiB EDRAM, no scaler
   jobs. A result for this comparison was never confirmed. Do not invent one.
6. R19 IDLE/BOTH modes and R20 queued logging did not resolve the same crash.
   These are earlier build results, not proof about every later implementation.

## Last explicitly reported failure versus latest binary

The user supplied several edited source archives and asked only for compilation.
The last explicitly reported Burnout failure was the uploaded fixed-v2 Soft
build, not a confirmed run of the latest bundle PRX. Associated log is
 logs/burnout-uploaded-v2-soft-reported-crash.txt. User said Burnout felt snappy
but still crashed on Complete before the system save screen.

Latest source was compiled from gotvscale_files.zip, preserving all ten supplied
files and adding missing build/runtime files from Tidy R29. Its PRX is byte-
identical to Tidy R29, SHA256:
1275f1be1ef37d1314ae72ba987aa5a4d0bc19b45c00c7d53baf738b628b9cc6

No log confirming Burnout's Complete result on this exact binary was supplied.
The associated earlier v2 Soft PRX/ELF/source ZIP and R21/R22 comparison ZIPs
are preserved separately under reference-builds/. The installed-device hash of
the earlier failing build was not verified.
The matching build/PSPGoTVScale.elf is included; do not substitute the ELF of a
new rebuild when interpreting an address from the preserved PRX. Main sources
were built -O2 without -g: symbols are present, but DWARF source lines found in
the ELF principally come from linked libgcc, not full plugin source debugging.
Module-relative addresses/disassembly remain useful. A -g rebuild would be a
new artifact requiring hash tracking and a fresh reproduction.

## Plugin architecture to inspect

- main.c hooks kernel display frame/mode entry points with guarded prologue
  trampolines, and user mode/EDRAM-size query syscalls. Game display layer2 is
  captured; scaled output uses higher-priority layer0.
- Physical TV output720x480, RGB565, stride768. Output buffers at0x04200000 and
  0x042B4000, each737280bytes, in the upper2MiB of4MiB EDRAM. Game-visible size
  query stays2MiB while scaling owns the expansion. Native manual-off restores
  real2MiB only after reference/geometry/idle checks.
- GPU commands, vertices and PspGeContext are in aligned high USER partition2
  memory, approximately23KiB. Context contains GPU-executed restoration code;
  moving only vertices out of protected kernel memory was insufficient.
- Default Soft does NOT allocate the optional544KiB deposterise buffer.
- Native software TV mode0x2D2 and scaled0xD2 map to the same physical LCDC mode.
  R17 showed logical480x272 but actual DMA width720/stride512. R18 fixed order:
  request native geometry BEFORE latching the source format/stride transition.
  User confirmed manual toggle duplication fixed. NativeHardwareReady verifies
  actual DMA plus optional cached/LCDC geometry before freeing/restarting.
- Immediate display sync0 accepts address changes only with matching live
  layout metadata. Sync1 queues layout changes. Clearing NULL with stride0 via
  sync0 was rejected in earlier attempts; the native handoff queries metadata.
- Utility/HOME paths preserve native firmware buffers (including kernel aliases
  for output; normalized addresses for texture input), pause game substitution,
  then resume. Native HOME menu is scaled separately with Soft.
- GE SDK state4 is treated as PAUSED/resumable, not completed/cancelled. Retain
  packet/context until completion. Invalid ID0x80000100 alone is not a fence;
  unconfirmed retired work requires nonblocking global idle before reuse.
- Owned FINISH callback copies a matching16-bit token into a volatile flag only;
  no I/O/waits/allocation in that callback. Worker uses token completion even
  when a list ID is already invalid. Firmware finish interrupt restores saved
  context after calling the callback; current reasoning relies on synchronous
  single-CPU handler completion before worker observes it. Audit that reasoning.
- Source presentations signal a sticky event; worker priority0x18, bounded event
  wait, unchanged-source suppression and at most one publication per refresh.
- Queued fixed-size logging: producers format/copy under short IRQ guard, worker
  writes files. Queued tail records may be lost on a hard freeze. Hence lack of
  savedata/write/exception text is not proof that an operation never occurred.
- Reboot: early phase flags/join worker; drain own list, clear scaled output,
  restore native mode/2MiB when safe. Late callback only sets stop flags.

## Recent cleanup (latest R29 source)

Restored writeback of all GPU-read arrays, removed unbounded ListSync(wait)
inside timeout loop, queued scaled publication for vblank, stopped clearing the
whole~21KiB packet every draw, canonical Soft tests, distinct tidy_r29 banner.
This compilation passed the host checks listed in BUILD-AND-SYMBOLS.md. Host
mocks do not establish real GPU/interrupt/cache/timing or commercial-game safety.

Experimental syscall guards for savedata-init, direct writes and GE context
operations, Circle test profiles and Select memory experiments were removed in
the simplified branch. They are not silently active in the latest runtime.

## Debugger attempts: failures, not diagnoses

- PSPLINK3.2.1 as an ARK GAME plugin: Burnout stayed black, no PSPLINK USB device.
- Standalone EBOOT displayed its full bootstrap/version banner, but no PSPLINK
  USB enumeration. Linux host waited for device. Normal XMB USB DOES work,
  lsusb054c:0381. Expected PSPLINK054c:01c9 never appeared. Cable/ordinary USB
  was verified; repeated cable checks are not a useful next experiment.
- Cause of PSPLINK incompatibility/startup failure is unknown. Initial advice
  to load it as a GAME plugin was not established for this exact ARK/Go setup.
- ARK BSOD.PRX enabled before scaler: after Complete the screen became entirely
  red, then PSP powered off. No readable exception type/EPC/RA/module offset.
  This is consistent with handler display startup, but does not prove where
  the initial failure happened or whether a secondary handler fault occurred.
- ARK source inspected: exception handler initialises a480x272 framebuffer,
  clears it and prints registers/module info, including reads of instructions
  at EPC/RA. Interaction with scaled layer0/720x480 and compromised register
  addresses deserves review; neither is established as the fault.
- A hard GE/kernel hang, exception, watchdog/timeout shutdown or secondary fault
  remains possible. No reliable crash dump has been captured. Do not claim a
  solid red screen identifies RAM failure or a particular exception.

## Performance is a separate issue

SMW and Assassin's Creed logs must not be labelled Burnout logs. R23 Assassin's
Creed repeatedly retired IDs and repeatedly restored native/menu layers, causing
quartered/fullscreen flicker. R24 held last output but discarded completed frames;
R25 FINISH tracking addressed that recognition problem. Device validation of
all combined menu/game paths remains limited.
SMW scaled/native comparisons showed genuine presentation-rate changes: R25
~30 scaled versus~58–60 native; R26 often~56–60 but sustained~39–45; R27 often
~30–32; restoring priority in R28 reduced recorded queue/wait from~8.4ms to~1.6ms
but demanding sections still fell~30–40. Queue/wait includes scheduling time,
not isolated GPU time. Burnout's~30fps presentation rate felt responsive to user;
that alone is not the SMW slowdown symptom. Filter cycling mixes work costs.

## Hypotheses still open

Active GE insertion/context restoration; queue reset/pause/lifetime races;
expanded EDRAM state versus rendering; display transition races; game-specific
memory or API assumptions; exception-handler/display interaction. None has been
proven as the Complete crash cause. Several earlier builds changed multiple
variables, so avoid overly strong causal conclusions from their differences.

## Recommended next review steps

1. Static audit, concrete findings with lines; reproduce host checks. Inspect
   privileged hooks, unload/reboot races, list ownership/finish tokens, cache
   ranges, hardware geometry and API failure paths.
2. Confirm exact ARK revision/devkit discrepancy, game region/ID, launch method,
   other plugins and SHA256 of the currently failing PRX. Establish latest R29
   reproduction before treating it as confirmed failing.
3. Obtain a reliable exception/hang capture with minimal disturbance. Audit why
   ARK BSOD went red then powered off before adding another handler. Avoid
   blocking file I/O/allocation in compromised exception/interrupt context.
4. If more behavioural testing is required, the native4MiB/no-job comparison
   from R22 remains unconfirmed. Preserve identical state except the intended
   variable and define predicted outcomes before asking for a race replay.

Private repository: https://github.com/joesnose/pspgo_fullscreen_plugin
Latest upload commit:44c44a1c419647b4c2b46d777bf12ff94018630c
Tidy R29 commit:92ac9f4608d99b9f84debdb0bbad9d21fd840c1b
Evidence logs and source snapshots are bundled for reviewers without repository
access. No credentials, game content, save files or Sony firmware are included.

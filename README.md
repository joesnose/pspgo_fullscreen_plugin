# PSP Go Fullscreen Plugin — revision 12

[Download R12 build and source](pspgo-tv-scale-v1-auto-home-r12.zip) · [Plugin PRX](PSPGoTVScale.prx)

Immediate fullscreen scaling with ten live filters. Retains revision 6's
user-confirmed HOME rendering and return-to-VSH handling, and the user-memory
GPU fix that removes the opicker dependency.

## Direct file-write guard (R12 test build)

The R11 log shows a savedata-init handoff succeeded in about 20 ms near the start
of the run and the system save screen returned to fullscreen. The later same-place
freeze has no further savedata entry, so that guard did not identify the failing
operation. R12 adds direct-write handoff and markers to test this other path;
the crash cause remains unconfirmed.

User sceIoWrite/sceIoWriteAsync calls on fd >= 3 with nonzero length request the
existing worker's native-output handoff before calling the original operation.
Buffers, descriptors, counts, K1, result pointers and return values are preserved;
no save data is inspected, altered or independently written. Reads, zero-length
writes and standard input/output/error bypass this guard. Plugin kernel logging
bypasses the user syscall wrappers, avoiding recursive logging. Game log/config
writes also trigger this handoff; it is not limited to save files.

Synchronous writes use the existing 300 ms quiet window after return. Async
writes additionally hold native output until successful PollAsync, WaitAsync,
WaitAsyncCB, GetAsyncStat or synchronous Close confirms completion. Busy/error
polls and failed closes retain tracking; a second failed async submission does
not discard a prior tracked write. Callback/result pointers go to the original
function unchanged. There are 128 tracking slots; overflow conservatively holds
native output for the session. The user write operation is still allowed after
the bounded 250 ms handoff deadline, even if native output is not acknowledged.

All seven exports must resolve before any direct-write wrappers are installed.
Diagnostics: file_write_guard, file_write_enter (fd/byte count/async),
file_write_handoff,native_ready, file_write_return and file_write_async_complete.
These establish whether the failing save uses direct file writes. Explicit
unload is blocked while a tracked write is pending. Firmware-internal direct
kernel I/O calls are outside these user syscall wrappers.

Checks cover synchronous partial/error results, no-utility-status handoff,
async hold beyond timer expiry, busy/completed polling, all completion wrappers,
failed submission/close, retained GPU storage, quiet-window resume and stdout/
zero-length bypass. Hardware confirmation is still required. R9 visible-dialog,
R10 paused-list and R11 savedata-entry handling remain.

## Savedata-entry guard (R11 test build)

R10's device log shows six paused lists completing correctly, followed by another
abrupt freeze before a results screen, with no new utility event or GPU timeout.
User testing confirms the plugin is involved and the game saves without opening
the system save screen. The exact failing operation is still unconfirmed.

R11 wraps the game's sceUtilitySavedataInitStart syscall, covering AUTO/silent
operations using that API. Before the original function starts, it suppresses
new scaler submissions and requests native output from the existing worker.
The calling thread yields for at most 250 ms for the worker to drain the owned
list and acknowledge the handoff. It then calls the original function exactly
once, passing the untouched parameter pointer, with the original caller K1 and
return value. Even if the handoff times out, the normal save API is allowed to
run and new scaler jobs remain suppressed. No save files are opened or modified
by the guard. Display callbacks still perform no waits, GPU work or file I/O.

A 300 ms quiet period after the original call returns covers short operations;
normal utility status continues to suppress scaling for longer operations. The
worker resumes the selected filter using the existing native-dialog recovery.
An outstanding entry blocks explicit plugin unload. Reboot keeps hooks/storage
resident as before. The existing visible-save-screen fix remains in place.

Logs identify savedata_entry_guard support, savedata_init_enter,
savedata_init_handoff,native_ready (1 confirms the handoff), and
savedata_init_return. These markers distinguish an entry race from a game that
bypasses this API and writes its files directly. No direct file-I/O hooks were
added. This is a targeted test build, not a confirmed crash diagnosis.

Host checks cover the handoff before any utility status is visible, caller K1,
parameter identity, original success/failure results, bounded timeout with
in-flight storage retained, quiet-period expiry, and normal fullscreen resume.

## Paused GPU list handling (R10 test build)

The R9 device log confirms the save/load screen fix worked and fullscreen resumed.
The later freeze occurred before the level-results screen. Its last gameplay
record has no timeout, invalid source or utility-entry event, so the log does not
identify the crash cause. R10 preserves R9's working native save/load path.

Code review found an unsafe completion assumption: PSPSDK names list-sync value
4 CANCEL_DONE, while firmware sceGeListSync also returns 4 for a resumable PAUSED
list after sceGeBreak(0), or a previously running list queued again. R9 treated
that as finished and could overwrite its still-owned command packet/context.

R10 accepts only status 0 as completed. Paused lists retain their ID and all
GPU-read storage without publishing their image, dequeueing them or latching a
timeout fault. The worker waits for the game to resume the list; an actual queue
reset instead uses R9's invalid-ID/global-idle retirement fence. The plugin does
not force a global GPU continuation or reset.

New logs: list_paused, list_pause_finished and per-second paused_lists/pending.
Host regressions verify paused packets and contexts remain byte-for-byte intact,
unload/filter memory cannot be freed while paused, repeated polls do not submit,
normal completion resumes fullscreen, and pause-to-reset uses the idle fence.
This is a targeted correctness fix, not a confirmed diagnosis of the reported
freeze. Please repeat the same level-end transition and retain the log.

## Save/load dialog recovery (R9)

The uploaded R8 Burnout log confirms utility detection works, followed by
list_sync_error 0x80000100 (invalid GE display-list ID) during utility startup.
R8 kept that ID pending, repeatedly polled it, and latched a scaling fault.

R9 leaves save/load and other standard utility screens at their native game-sized
TV layout while they own the shared GPU queue. It submits no scaler jobs during
utility init, visible, finished or shutdown phases. Fullscreen resumes with the
selected game filter after the utility closes or is cancelled. HOME opened while
a utility is running also uses native output until the utility closes; ordinary
HOME rendering outside utilities retains the previously working scaler path.

An invalid owned list ID is treated as a retired queue entry only after a
nonblocking global GPU-idle poll succeeds. Until then the packet, processed
pixels and context remain reserved. The retired image is discarded, not
published as complete; the paused restore path retries without a permanent
fault. There are no global queue resets or cancellations of the game's work.
Repeated invalid-ID errors are logged once per submission rather than every loop.
Other errors retain the existing fault handling.

Utility detection still uses export-resolved private type/status getters with
validated read-only leaf bodies, without application callback APIs or guessed
data addresses. If these getters are unavailable or unrecognized, detection is
unavailable. Log banner: auto_home_r12. Look for utility_native_output,
list_id_retired,idle_fence_passed and system_dialog_closed,scaling_resumed.

Host regressions reproduce the reported invalid-ID failure with both busy and
idle GPU states and verify native dialog handling, no dialog submissions,
repeated/cancelled dialogs, startup during a dialog, nested HOME and resumed
fullscreen. Real Burnout save/load behavior still needs a PSP Go test.

## Install and control

1. Fully power off the PSP Go and replace ef0:/SEPLUGINS/PSPGoTVScale.prx.
2. Keep the existing GAME entry and NODS configuration:

       game, ef0:/SEPLUGINS/PSPGoTVScale.prx, on

3. Restart. Fullscreen starts automatically with Soft filtering and no delay.
4. Hold L + R and tap Triangle to advance one filter. Release Triangle before
   advancing again. Default NODS controller mapping is L1 + R1 + Triangle.
   A label shows the selected filter for 1.5 seconds. Changes are suppressed
   while native HOME is open. The chord is still visible to the game.

The INI is no longer used or included. An old INI can be deleted or left in place;
it is ignored. Selection lasts for this game session and resets to Soft on the
next launch. NODS positive-peek input follows Impose's validated merged import;
local PSP input is the fallback. No NODS changes are required.

## Filter cycle

Sharp -> Soft -> Blur -> CRT -> Soft CRT -> RGB Mask -> Sharpen -> Posterise ->
Dither -> Deposterise -> Sharp. Initial selection is Soft.

| Filter | Effect | Work |
| --- | --- | --- |
| Sharp | Nearest-neighbour scaling | One image pass |
| Soft | Bilinear scaling; default | One image pass |
| Blur | Stronger spatial smoothing | Five averaged image passes |
| CRT | Bilinear image and alternating scanlines | Image plus overlay |
| Soft CRT | Lighter scanlines with ordered dithering | Image plus overlay |
| RGB Mask | Subtle repeating red/green/blue stripes | Image plus overlay |
| Sharpen | Mild fixed-function unsharp approximation | Six image passes |
| Posterise | Fewer colour levels, roughly eight per channel | Image plus two colour operations |
| Dither | Bilinear scaling with ordered RGB565 dithering | One image pass |
| Deposterise | Colour-aware smoothing of small colour steps | CPU prepass plus bilinear scaling |

Posterise reduces colour levels. Deposterise instead smooths small colour
changes: horizontal and vertical 1:2:1 samples are averaged only when neighbours
are within 12 levels in every RGB channel; stronger edges and centre alpha are
preserved. It handles all four supported source formats and uses an extra
544 KiB user-RAM buffer, allocated only when selected and freed when leaving
that mode after the owned GPU list finishes. If allocation fails, cycling skips
Deposterise and logs the reason. This optional CPU effect may reduce frame rate.

These are PSP implementations, not identical PPSSPP shaders. Sharpening is an
approximation with intermediate clamping; CRT does not simulate curvature.
Filtering uses only the current image, never previous frames. Soft and Sharp
have the least processing work. Actual quality and cost require PSP Go testing.

## HOME, exit and memory

Native HOME always uses bilinear filtering independently of the game preset.
Revision 6's live menu-source capture, full-TV menu rendering, cancel/resume,
early reboot worker quiesce, owned-list drain, priority-0 clear, native TV-mode
restore and scanout/idle-gated EDRAM release remain in place. Reboot does not
free GPU storage or rewrite entry trampolines; late callbacks only set flags.

All GPU-read commands, vertices, saved GE context and optional processed pixels
live in aligned user partition 2. No clocks or DDR protection registers are
changed. opicker is unnecessary. Supported target: PSP Go, ARK, 6.60/6.61,
progressive component 480p, games with supported lower-2-MiB framebuffer layouts.
The existing game image is stretched; game world geometry is not extended.

Log: ef0:/PSPGoTVScale.txt (ms0:/ fallback). Copy it before opening another game.
Filter changes appear as filter records; banner identifies auto_home_r9.

## Build and verification

PSP build uses -Wall -Wextra -Werror:

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

Host checks:

    cc -O2 -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined render_test.c render.c -o /tmp/go-tv-packet-test
    /tmp/go-tv-packet-test
    cc -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined filter_test.c render.c effects.c -lm -o /tmp/go-tv-filter-test
    /tmp/go-tv-filter-test
    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined effects_test.c render.c effects.c -o /tmp/go-tv-effects-test
    /tmp/go-tv-effects-test
    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/go-tv-auto-test
    /tmp/go-tv-auto-test

Checks cover all ten packet modes, format/aspect/buffer combinations, UV bounds,
blur weights, CRT geometry, CPU deposterising against an independent full-frame
reference for all four formats, edge rows/alpha/padding, notice bounds, merged
input validation, one change per press, HOME suppression, optional allocation
failure, processed user-RAM texture submission, and the existing HOME/reboot
regressions. Host tests use fake PSP APIs and do not execute actual GE drawing
or firmware scheduling. Revision 12 has not been tested on real hardware here.

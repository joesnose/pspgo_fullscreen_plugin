# Native4M R30 — Burnout comparison based on R29

This diagnostic separates retained physical4MiB EDRAM from active scaler jobs.
It is not a crash fix. It does not include a new exception handler or hidden
rendering. Keep the normal R29 release separately; this is a test variant.

## Before the comparison

First confirm the exact R29 still fails on Complete. Its preserved PRX SHA256 is
1275f1be1ef37d1314ae72ba987aa5a4d0bc19b45c00c7d53baf738b628b9cc6.
The last user-confirmed crash was an earlier v2 Soft build; latest-binary R29
reproduction was not confirmed in the handover. Keep game/save/other plugins
unchanged. Keep PSPLINK and BSOD disabled. No PC debugger is needed here.

## Install and run R30

1. Replace ef0:/SEPLUGINS/PSPGoTVScale.prx with this test PRX and fully restart.
2. Launch Burnout Legends. Keep Soft; fullscreen starts as in R29.
3. Just before selecting Complete at the known failing race ending, hold L+R
   and tap SQUARE ONCE. Release and wait at least two seconds for native output.
4. Select Complete. Report pass/freeze/power-off, and copy ef0:/PSPGoTVScale.txt
   before launching another game (fallback ms0:/PSPGoTVScale.txt).

The first line must identify native4m_r30. A native4m_test,ready,1 record must
show edram4194304,held1,gpu_uid>=0,pending-1. Later draw counts must equal
 draws_at_handoff. A missing ready record means the intended test state was not
confirmed; do not infer a result from that run alone. Queued log tail records
may be lost during a hard freeze.

Square now intentionally behaves differently in THIS diagnostic: from fullscreen
it enters native output but keeps4MiB EDRAM and existing user-RAM workspace.
There are no new scaler GE submissions after handoff. Layer0 is restored to the
original native overlay; actual DMA native geometry is verified. Game-facing
EDRAM query still reports2MiB, as under scaling. Ordinary native mode reporting
and display passthrough are used. Soft does not allocate a deposterise buffer.
If an optional deposterise block already existed, it is retained too, so keep
Soft for the comparison. Filter cycling is disabled in native mode.

A second Square tap returns to scaling: the held bank is first safely restored
to2MiB before reactivation. Do NOT do this before Complete in the comparison.
Reboot/unload cleanup accounts for the retained bank. Buffers remain resident
until safe cleanup; this test does not modify game save buffers or clock speed.

## Interpretation

R21 Complete passed with native output,2MiB EDRAM and the workspace retained.
A failure here makes retained4MiB EDRAM/driver state a stronger suspect; it is
not proof that size alone is the trigger. A pass here means4MiB native/no-jobs
state was insufficient to reproduce it in this attempt; the next separate test
could keep native display while rendering into hidden buffers. No such hidden
rendering experiment is active in this build.

Keep failed/successful logs labelled with exact version, game and action. Host
checks do not prove device safety or commercial-game behaviour.

## Build/checks

    docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make -j4

    cc -DHOST_TEST -D_GNU_SOURCE -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fsanitize=address,undefined auto_home_test.c render.c effects.c -o /tmp/native4m-test
    /tmp/native4m-test

PSP build passes -Wall -Wextra -Werror. ASan/UBSan actual-source checks cover
native4MiB handoff, retained UID and native geometry, public size query masking,
60 no-job native control ticks, safe resume and reboot bank cleanup, plus R29
baseline regressions. Host tests exercise the ordinary2MiB path with an internal
fixture setting and explicitly exercise the actual4MiB diagnostic default.
The ELF paired with this PRX is included. No source changes outside the test's
EDRAM/workspace lifetime, state reporting and associated cleanup are intended.

# joesnose Go fullscreen LME — experimental R37

Separate 6.60 LME HOME comparison for PSP Go with component progressive TV output. This is based on R35, not the LCD experiment. It does not replace the ARK release and is not a confirmed fix.

## Changes

- Filename and build target: joesnose_Go_fullscreen_LME.prx; module version 1.37.
- Logging is enabled to ef0:/PSPGoTVScale_LME.txt, falling back to ms0:/PSPGoTVScale_LME.txt. Only the worker performs file I/O. HOME edge records include status, pending-list ID and draw count.
- HOME uses native 480x272 output. The worker requests native geometry before binding the native menu layer; no HOME filter/scaler jobs are submitted. HOME-native mode requests pass through the display hook. Existing owned GPU work must drain before the worker hands off output. The saved workspace and 4 MiB EDRAM are retained through the menu, as with utility dialogs; resources are not freed while potentially in use.
- A HOME check both before packet building and immediately before enqueue prevents new scaler work once the flag is visible. It cannot prevent a crash occurring before HOME status changes or in work already queued.
- Fullscreen resumes after HOME closes. R35's continuous rendering, R32's Burnout context change, Soft default, lighter 1.5x Blurrier, and existing shortcuts are retained. No LCD filters were added here. This does not detect or modify LME firmware automatically.

## Install and test

1. Copy joesnose_Go_fullscreen_LME.prx to your plugin directory and enable it as a GAME plugin. Disable the ordinary joesnose_Go_fullscreen.prx and every other scaler entry so only this copy loads. Restart the PSP.
2. On 6.60 LME with TV output selected, start the same game and open HOME once. Report whether it still crashes, whether the menu is readable and whether cancelling HOME restores fullscreen. If that succeeds, test opening HOME again and selecting Quit Game.
3. Upload PSPGoTVScale_LME.txt before another game launch overwrites it. If an exception screen appears, photograph it. The banner should read lme_native_home_r37. Old PSPGoTVScale.txt files belong to previous builds.

L+R+Triangle cycles filters and L+R+Square switches fullscreen/native. Keep the R35 PRX separately to restore the known working ARK version. This binary retains the PSP Go model and 6.60/6.61 guards; its filename denotes the intended test, not verified support for all LME revisions.

## Validation

PSP build passed with warnings treated as errors. Host ASan/UBSan lifecycle checks pass, including native HOME geometry, no menu GPU jobs across 30 worker iterations, native mode-hook passthrough, and fullscreen resume after dismissal, plus the existing R35 context/rendering and utility/cleanup checks. This is a host comparison; exact LME hardware behaviour remains pending. Source, matching ELF and SHA256SUMS are included.

# joesnose Go fullscreen — R36 LCD filter test

Separate experimental build based on R35. It adds internal-screen filters to the same PRX while retaining the TV renderer. The working R35 release is unchanged. Hardware testing is pending; this is not a confirmed LME HOME fix.

## Install and try

Replace the installed joesnose_Go_fullscreen.prx with this package's PRX, enable only one scaler copy and restart the PSP. Start a game with TV output disconnected/disabled to select the LCD path. L+R+Triangle cycles filters; release Triangle between taps. Try CRT, RGB Mask or Posterise for an obvious visual check, since Soft at native resolution may look very similar to the original. L+R+Square switches LCD filtering off/on.

On the internal screen this uses a 480x272 RGB565 output with 512-pixel stride and two buffers in upper EDRAM, separate from the game's lower-bank buffers. The TV path remains 720x480. Internal HOME and system save/load dialogs pass through unfiltered; filtering resumes afterward. LCD off mode releases the extra EDRAM/workspace after the existing scanout/GE fences and does not run hidden filter jobs. TV native mode retains the R35 diagnostic behaviour. Select the target display before launching the game; hot-switching TV output is not validated in this test.

All 12 filters are available: Sharp, Soft, Softer, Blur, Blurrier, CRT, Soft CRT, RGB Mask, Sharpen, Posterise, Dither and Deposterise. Soft starts by default; Blurrier is 1.5x Blur radius. RGB565 output can reduce colour precision; Dither/Soft CRT use ordered dithering. Filters add rendering cost, particularly the multi-pass and CPU Deposterise effects. R32's Burnout context fix and R35's repeated-source rendering are retained. Logging is disabled.

This build still accepts only PSP Go (model 4), firmware 6.60/6.61. It does not add other PSP models or establish support for a particular LME revision.

Please report whether filters visibly change the internal screen, gameplay speed, whether HOME and save/load still work, and whether disabling/re-enabling filters works. Keep the R35 PRX to restore the known working TV version.

## Validation

PSP build passed with warnings treated as errors. Host ASan/UBSan checks passed for the existing TV lifecycle and filter paths and new LCD activation, mode/stride, HOME pass-through/resume, native off/on and workspace release. LCD packet checks cover 4 source formats x 2 buffers x 12 filters, verifying clip bounds, framebuffer address/stride and image/overlay/post vertices. TV filter checks retain their 192 combinations. CPU Deposterise and filter notice bounds pass for LCD and TV. Exact hardware rendering/performance is pending. Complete source, matching ELF and SHA256SUMS are included.

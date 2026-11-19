# joesnose Go fullscreen

Fullscreen TV scaling for PSP Go with progressive component output on ARK 6.60/6.61.

**Latest release: [R35](https://github.com/joesnose/pspgo_fullscreen_plugin/releases/latest)** · [Download ZIP](https://github.com/joesnose/pspgo_fullscreen_plugin/raw/refs/heads/main/joesnose_Go_fullscreen-r35.zip)

The current source, `joesnose_Go_fullscreen.prx` and matching ELF are at the repository root. Earlier builds, diagnostics and their sources are archived in [tests/](tests/README.md).

## Install

Copy `joesnose_Go_fullscreen.prx` into your existing plugin directory. Update the filename in your enabled ARK GAME plugin entry, preserving its directory and enabled setting. Disable the old scaler entry so only one copy loads. Restart the PSP. Fullscreen starts enabled with Soft filtering; no INI is required.

## Controls and filters

- **L + R + Triangle:** cycle filters; release Triangle between presses.
- **L + R + Square:** switch between fullscreen and native-size output.
- HOME and NODS external-controller HOME handling are retained, along with system save/load dialog handling.

Filters: Sharp, Soft, Softer, Blur, Blurrier, CRT, Soft CRT, RGB Mask, Sharpen, Posterise, Dither and Deposterise. Blurrier uses 1.5 times Blur's sampling radius. Soft is the startup default.

## R35 changes and validation

R35 restores rendering of the current framebuffer even when the game stops submitting new framebuffer presentations, while retaining the one-render-per-TV-refresh fence and queued display publication. The user confirmed that this resolves the Liberty City Stories stall on their PSP Go, after isolating its introduction to R26. The R32 unused GPU CALL return-register fix is retained; the user confirmed Burnout Legends Complete/Continue passes in R32 with both native and scaled output. The user also confirmed Burnout works with this exact R35 build.

Logging is disabled. No new `PSPGoTVScale.txt` is created or written; any existing file contains an earlier run. Rendering unchanged sources adds GPU work and can affect performance. This build retains the diagnostic native-mode behaviour from R32: native-size output can still render scaled frames into hidden buffers. The shortcut therefore changes output size without guaranteeing that all scaler GPU work stops.

The PSP build passed with warnings treated as errors. Host lifecycle checks passed under ASan/UBSan, including repeated rendering of an unchanged source over 60 refreshes, the same-refresh fence, CALL-depth handling, HOME/utility transitions and cleanup. Host checks do not establish compatibility with every game or filter on hardware.

## Build

With PSPSDK installed, run `make -j4`. Alternatively:

```sh
docker run --rm -v "$PWD:/src" -w /src pspdev/pspdev:latest make -j4
```

The ZIP includes source, PRX, matching ELF and `SHA256SUMS`. Historical test packages are preserved without overwriting their contents.

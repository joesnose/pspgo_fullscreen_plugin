# joesnose Go fullscreen

Fullscreen TV scaling for PSP Go with progressive component output on ARK 6.60/6.61.

**Latest release: [R35](https://github.com/joesnose/pspgo_fullscreen_plugin/releases/latest)** · [Download ZIP](https://github.com/joesnose/pspgo_fullscreen_plugin/raw/refs/heads/main/joesnose_Go_fullscreen-r35.zip)

The current source, `joesnose_Go_fullscreen.prx` and matching ELF are at the repository root. Earlier builds, diagnostics and their sources are archived in [tests/](tests/README.md).

## Install

Copy `joesnose_Go_fullscreen.prx` into your existing plugin directory. Update the filename in your enabled ARK GAME plugin entry, preserving its directory and enabled setting. Restart the PSP. Fullscreen starts enabled with Soft filtering.
## Controls and filters

- **L + R + Triangle:** cycle filters; release Triangle between presses.
- **L + R + Square:** switch between fullscreen and native-size output.
- HOME and NODS external-controller HOME handling are retained, along with system save/load dialog handling.

Filters: Sharp, Soft, Softer, Blur, Blurrier, CRT, Soft CRT, RGB Mask, Sharpen, Posterise, Dither and Deposterise. Blurrier uses 1.5 times Blur's sampling radius. Soft is the startup default.


## Build

With PSPSDK installed, run `make -j4`. Alternatively:

```sh
docker run --rm -v "$PWD:/src" -w /src pspdev/pspdev:latest make -j4
```

The ZIP includes source, PRX, matching ELF and `SHA256SUMS`. Historical test packages are preserved without overwriting their contents.

# Third-party components

## Included in this repository

- **Lua 5.0.2** (`third_party/lua-5.0.2/`) - Copyright (C) 2003-2004 Tecgraf, PUC-Rio,
  MIT license (see `third_party/lua-5.0.2/COPYRIGHT`). Small local modifications are
  listed in `third_party/lua-5.0.2/UNDERTOW_PATCHES.md`.

## Linked at build time (not included)

- **SDL2** - zlib license.
- **FFmpeg** (libavformat, libavcodec, libavutil, libswresample) - LGPL-2.1-or-later in
  its default configuration. Undertow links it dynamically.
- **zlib** - zlib license.

## Bundled in the Windows release zip

- **FFmpeg 8.1.3** (avcodec, avformat, avutil, swresample DLLs) - LGPL-2.1-or-later.
  A minimal build with no GPL or external components, made by `tools/build_ffmpeg_min.sh`
  from the unmodified source at https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz (the
  script holds the exact configure line). Undertow links it dynamically, so the DLLs can
  be replaced with any compatible FFmpeg build.
- **SDL2** - zlib license. **zlib** - zlib license. **mingw-w64 winpthreads** - MIT.

Their license texts are in the zip's `licenses/` folder.

### Note on local development builds

The MSYS2 `mingw-w64-x86_64-ffmpeg` package is configured with `--enable-gpl
--enable-version3`, so a package that bundles those DLLs (as `tools/bundle_dlls.sh`
does) is covered by the **GPLv3** as a whole. Undertow's own source code stays MIT.
Release zips use the minimal LGPL build above instead.

## Research references

- GameWaveFans project (github.com/gamewavefans/GameWaveFans) - community research on
  Game Wave formats and the engine's Lua API, which this project builds on and extends.

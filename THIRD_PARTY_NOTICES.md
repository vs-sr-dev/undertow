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

### Note on binary packages

The MSYS2 `mingw-w64-x86_64-ffmpeg` package is configured with `--enable-gpl
--enable-version3`, so a binary package that bundles those DLLs (as
`tools/bundle_dlls.sh` does) is covered by the **GPLv3** as a whole. Undertow's own source
code stays MIT. For LGPL-only binary releases, build against an FFmpeg configured
without GPL components (Undertow only needs the MPEG-PS demuxer, the MPEG-2 video and
MP2 audio decoders, and libswresample).

## Research references

- GameWaveFans project (github.com/gamewavefans/GameWaveFans) - community research on
  Game Wave formats and the engine's Lua API, which this project builds on and extends.

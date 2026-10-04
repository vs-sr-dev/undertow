#!/bin/sh
# Build the minimal LGPL FFmpeg that release packages ship (MSYS2 mingw64 shell).
# Only what Undertow uses: MPEG-PS/elementary demuxers, MPEG-1/2 video and the DVD audio
# decoders, libswresample. No GPL parts, no external libraries: four small DLLs.
# usage: tools/build_ffmpeg_min.sh [prefix]          (default deps/ffmpeg-min)
# then:  PKG_CONFIG_PATH=$(cygpath -m deps/ffmpeg-min)/lib/pkgconfig cmake ...
set -e
VER=8.1.3
SHA256=7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3
DEPS=$(pwd)/deps
PREFIX=$(cygpath -m "${1:-$DEPS/ffmpeg-min}")   # D:/... so mingw gcc reads the .pc paths

mkdir -p "$DEPS"
cd "$DEPS"
[ -f ffmpeg-$VER.tar.xz ] || curl -fL -o ffmpeg-$VER.tar.xz https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz
echo "$SHA256 *ffmpeg-$VER.tar.xz" | sha256sum -c -
rm -rf ffmpeg-$VER
tar xf ffmpeg-$VER.tar.xz
cd ffmpeg-$VER

./configure --prefix="$PREFIX" \
    --enable-shared --disable-static \
    --disable-autodetect --disable-everything --disable-programs --disable-doc \
    --disable-network --disable-avdevice --disable-avfilter --disable-swscale \
    --enable-demuxer=mpegps,mpegvideo,mp3,ac3 \
    --enable-parser=mpegvideo,mpegaudio,ac3 \
    --enable-decoder=mpeg1video,mpeg2video,mp1,mp1float,mp2,mp2float,mp3,mp3float,ac3,pcm_dvd,pcm_s16be,pcm_s16le \
    --extra-ldflags=-static-libgcc
make -j"$(nproc)"
make install
cp COPYING.LGPLv2.1 "$PREFIX/"
echo "FFmpeg $VER (LGPL) installed to $PREFIX"

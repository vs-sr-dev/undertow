#!/bin/sh
# Build the Windows release zip (MSYS2 mingw64 shell): undertow.exe, zbcc.exe, the few DLLs
# they need and the licenses. Links the minimal LGPL FFmpeg from tools/build_ffmpeg_min.sh,
# never the GPL one from MSYS2, and refuses to package if any av*/sw* DLL comes from elsewhere.
# usage: tools/package_release.sh [version]          (default: git describe)
set -e
VER=${1:-$(git describe --tags --always --dirty)}
FF=$(cygpath -m "$(pwd)/deps/ffmpeg-min")
LIC=/mingw64/share/licenses
[ -f "$FF/lib/pkgconfig/libavcodec.pc" ] || { echo "run tools/build_ffmpeg_min.sh first"; exit 1; }

PKG_CONFIG_PATH="$FF/lib/pkgconfig" cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

NAME=undertow-$VER-win64
OUT=dist/$NAME
rm -rf "$OUT" "dist/$NAME.zip"
mkdir -p "$OUT/licenses"
cp build-release/undertow.exe build-release/zbcc.exe "$OUT/"
strip "$OUT"/*.exe

# every non-system DLL the two executables load, FFmpeg resolved from deps/ffmpeg-min
PATH="$(cygpath -u "$FF")/bin:$PATH" ldd "$OUT/undertow.exe" "$OUT/zbcc.exe" |
    awk '$3 ~ /^\// && tolower($3) !~ /^\/c\/windows\// {print $3}' | sort -u > dist/dlls.txt
if grep -Ei '/(av|sw)[a-z]*-[0-9]+\.dll$' dist/dlls.txt | grep -v "^$(cygpath -u "$FF")/bin/"; then
    echo "error: FFmpeg DLLs above are not from $FF"; exit 1
fi
xargs -I{} cp {} "$OUT/" < dist/dlls.txt

cp README.md LICENSE THIRD_PARTY_NOTICES.md "$OUT/"
cp third_party/lua-5.0.2/COPYRIGHT "$OUT/licenses/Lua.txt"
cp "$FF/COPYING.LGPLv2.1" "$OUT/licenses/FFmpeg-LGPL-2.1.txt"
cp $LIC/SDL2/LICENSE.txt "$OUT/licenses/SDL2.txt"
cp $LIC/zlib/LICENSE "$OUT/licenses/zlib.txt"
cp $LIC/winpthreads/COPYING "$OUT/licenses/winpthreads.txt"

(cd dist && zip -qr9 "$NAME.zip" "$NAME")
echo "dist/$NAME.zip: $(ls "$OUT" | grep -c '\.dll$') DLLs, $(du -h "dist/$NAME.zip" | cut -f1)"

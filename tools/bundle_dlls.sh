#!/bin/sh
# Copy the MSYS2 mingw64 DLLs undertow.exe needs next to it, so it runs without PATH tweaks.
# usage: tools/bundle_dlls.sh [build/undertow.exe]
EXE=${1:-build/undertow.exe}
DIR=$(dirname "$EXE")
PATH="/c/msys64/mingw64/bin:$PATH" /c/msys64/usr/bin/ldd "$EXE" |
    awk '$3 ~ /mingw64/ {print $3}' | sort -u |
    while read -r dll; do cp -u "/c/msys64$dll" "$DIR/"; done
echo "DLLs copied to $DIR: $(ls "$DIR"/*.dll | wc -l)"

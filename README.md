# Undertow

A ZAPiT **Game Wave** emulator (work in progress).

Game Wave games are Lua 5.0.2 bytecode (`.zbc`) run by the "ZIT" engine shipped on each
disc. Undertow takes the high-level route: it runs the bytecode on a patched Lua 5.0.2 and
reimplements the engine libraries (`gl`, `movie`, `audio`, `input`, ...) on the PC.

## Build (MSYS2 mingw64)

    export PATH=/c/msys64/mingw64/bin:$PATH
    cmake -S . -B build -G Ninja
    cmake --build build

## Run

    build/undertow "Sudoku (USA).iso"            # needs C:\msys64\mingw64in on PATH (DLLs)

Keys: arrows, Enter/Space = SELECT, Z/X/C/V = A/B/C/D, 0-9, Backspace = DVD MENU,
Tab = GAME MENU, Esc = quit. Testing options: `--trace`, `--keys 14,14 --key-start 4000
--key-interval 1500`, `--exit-after MS --screenshot shot.bmp`.

Working: MPEG-2 movies with sound, iframes, OSD textures/overlays, sound effects, remote
input. Missing: text/fonts, OSD animations, saves (eeprom), pointer library.

## Layout

- `src/` - emulator sources
- `third_party/lua-5.0.2/` - Lua with local patches (`UNDERTOW_PATCHES.md`)
- `tools/` - Python research tools (extractors, disassemblers, format decoders)
- `docs/` - research notes (`NOTES.md`, `HARDWARE.md`, `engine_api_tables.txt`)

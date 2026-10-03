# Undertow

An emulator for the **ZAPiT Game Wave Family Entertainment System** (2005-2009), the
Canadian DVD-based game console.

Game Wave games are Lua 5.0.2 bytecode (`.zbc`) run by the "ZIT" engine that ships on each
disc, with MPEG-2 movies on the video plane and a 16-bit OSD on top. Undertow takes the
high-level route: it runs the original bytecode on a lightly patched Lua 5.0.2 and
reimplements the engine's libraries (`gl`, `text`, `movie`, `audio`, `input`, ...) on the
PC with SDL2 and FFmpeg. No firmware or BIOS is needed.

## Status

| Game | Status |
|-|-|
| Sudoku | Playable (menus, a full board with timer and number entry, save and Load & Resume) |
| Gemz | Playable (menus, Classic mode board, animations) |

Working: MPEG-2 movies with sound, MPEG stills, OSD textures/overlays with z-order,
alpha and animations, text with the disc fonts, sound effects, remote control input,
disc file access, reading discs directly from `.iso` images, save games.

Not yet: parabola/blinking animations, deinterlacing, engine
built-in screens (`iframe.ShowPredefined`), multiple remotes. Other titles of the
16-disc library are untested.

## Legal

This repository contains **no** Game Wave firmware, game code or assets. You need your
own disc images. Undertow is an independent project, not affiliated with ZAPiT Games.

## Build (Windows, MSYS2 mingw64)

Packages: `mingw-w64-x86_64-gcc`, `-cmake`, `-ninja`, `-SDL2`, `-ffmpeg`, `-zlib`.

    export PATH=/c/msys64/mingw64/bin:$PATH
    cmake -S . -B build -G Ninja
    cmake --build build
    sh tools/bundle_dlls.sh          # copy the needed DLLs next to build/undertow.exe

The code is portable C99 + SDL2 + FFmpeg; other platforms should only need build tweaks.

## Run

    build/undertow "Sudoku (USA).iso"     # or drag an .iso onto undertow.exe

An extracted disc directory works too.

Save games go to `undertow.eep` next to the executable: a 32 KB image of the console's
save EEPROM in the engine's own format, shared by every disc as on the real machine.
`--eeprom FILE` uses another image.

| Remote | Keyboard |
|-|-|
| Arrows | Arrow keys |
| SELECT | Enter / Space |
| A B C D | Z X C V |
| 0-9 | 0-9 (top row or keypad) |
| DVD MENU / GAME MENU | Backspace / Tab |
| (quit) | Esc |

Debugging/testing options: `--trace` (log every engine API call; prints the Lua stack
when stopped), `--keys 14,14 --key-start 4000 --key-interval 1500` (scripted input),
`--exit-after MS --screenshot shot.bmp`.

## Repository layout

- `src/` - emulator sources
- `third_party/lua-5.0.2/` - Lua with local patches (`UNDERTOW_PATCHES.md`)
- `tools/` - Python research tools: `.lzh`/cheese container extractors, ZBM image
  decoder, annotated MIPS disassembler, xref/MMIO mappers, ZBC bytecode disassembler and
  API usage scanner, engine Lua library table finder
- `docs/` - research notes: `NOTES.md` (formats, engine API semantics), `HARDWARE.md`
  (CPU, memory map, boot flow), `engine_api_tables.txt` (engine Lua library entry points)

## Credits

Built on the format research of the [GameWaveFans](https://github.com/gamewavefans/GameWaveFans)
project. See `THIRD_PARTY_NOTICES.md` for licenses.

## License

MIT - see `LICENSE`.

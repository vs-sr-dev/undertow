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
| Lock 5 | Playable (menus, player join, name entry, spinning and locking reels) |
| Letter Zap! | Cube playable (word dictionary, scoring); Tag! untested |
| 4 Degrees: Bible Edition | Playable (menus, question movies, answering) |
| VeggieTales: Veg-Out! Family Tournament | Playable (menus, minigames such as Bingo, multiplayer) |
| Quiz Konnect | Playable (late Indian release of 4 Degrees, not in Redump; see Credits) |
| Zap 21 | Playable (blackjack: menus, table, dealing, a full hand) |
| Rewind | Playable (trivia: menus, questions, hints) |

Working: MPEG-2 movies with sound, MPEG stills, OSD textures/overlays with z-order,
alpha and animations, text with the disc fonts, sound effects, remote control input,
disc file access, reading discs directly from `.iso` images, save games.

Not yet: parabola/blinking animations, deinterlacing. Other titles of the
16-disc Redump set are untested.

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

The keyboard is remote 1 (red); game controllers are remotes 2-6 in connection order, so
multiplayer games can be played with several pads.

| Remote | Keyboard | Game controller |
|-|-|-|
| Arrows | Arrow keys | D-pad / left stick |
| SELECT | Enter / Space | LB / RB |
| A B C D | Z X C V | A B X Y |
| 0-9 | 0-9 (top row or keypad) | - |
| DVD MENU / GAME MENU | Backspace / Tab | Back / Start |
| (quit) | Esc | - |

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

Thanks to Reddit user u/Amasteriscool for sharing the dumps of the system upgrade disc
1.021.060505, the source of the firmware research in `docs/HARDWARE.md`, and of Quiz
Konnect, a late title that is not in the Redump set (provenance and integrity checks in
`docs/NOTES.md`).

## License

MIT - see `LICENSE`.

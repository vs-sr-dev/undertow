# Game Wave research notes

Running log of what we have verified ourselves. Community background lives in the
GameWaveFans repo/wiki (github.com/gamewavefans/GameWaveFans).

## Sources on disk

- `discs/Upgrade Disc/` - system upgrade disc **1.021.060505** (from `Upgrade Disc.zip`,
  dump shared on Reddit by u/Amasteriscool).
  Only `NYTRIC_FIRM/DVDAPP/SANYO_7xx/{app.cat.bin,upgrade.bin}` matter; the
  `.TMP`, `DrvMgt.dll`, `SECDRV.SYS` files are PC-side SafeDisc junk.
- `discs/Sudoku (USA).iso`, `discs/Gemz (USA).iso` - Redump game images.
- `extracted/upgrade_060505/` - output of `tools/gwextract.py` on both upgrade binaries.
- `extracted/{sudoku,gemz}/disc/` - game files without movies; `extracted/engine_3/` -
  the ZIT engine unpacked; `extracted/*/game.dis.txt` - script disassembly.

## Containers

### "cheese" file table (verified)
Appended after code in `*.cat.bin`, `upgrade.bin` and inside decompressed images.
Big-endian.

| off | type | |
|-|-|-|
| 0 | u64 | `12 34 56 78 87 65 43 21` |
| 8 | u32 | file count |
| 12 | entry[count] | char name[40] (NUL-terminated, may contain `\` dirs), u32 offset, u32 size |

Offsets are relative to the magic.

### `.lzh` blobs (verified on all 84 + 1 blobs of the 060505 disc)
- header: u32 LE unpacked size, u32 LE packed size (blob may carry 2 bytes of padding)
- payload: plain LHA **-lh5-** bitstream (static Huffman + LZSS, 8 KiB window,
  NC=510, NT=19, NP=14, no LHA archive header). Decoder: `tools/lzh.py`.

### `app.cat.bin` (firmware upgrade image)
- 0x00000-0x17ac7: bootloader, MIPS32 BE, reset vector at +0 (`b 0x40`),
  touches `0xBFC00000` (flash) and `0xA80E8000` (SoC regs?). Nucleus PLUS string present.
- cheese: `app_sdram.bin.lzh` (1.44 MB -> 2.47 MB main firmware), `startup.osd`.
- Main firmware entry stub: `lui k0,0x80a0; addiu k0,0x0a50; or k0,0x20000000; jr k0`;
  load address **0x80A00000** (verified by string xrefs).
- Main firmware contains its own cheese at 0x186d18: 358 files (system menu `.zbm`,
  `game_wave.m2v`, `insert_disc.m2v`, `launching.m2v`, `invalid_media.m2v`,
  `nytric_logo.m2v`, `zapit_logo.m2v`, `ding.zwf`, `maui\dvd*.*\*.lzh` DSP microcode).
- Boot logic strings: `gamewave.diz`, `board=`, `engine=`, `app_sdram.cat.bin`, `game.zbc`,
  `Engine file name is [%s]`.

### `upgrade.bin`
- Loads at 0x80600000 (same stub pattern -> `0xA0600A50`), the flasher program.
- cheese: 181 files - `.osd` 1-bit font glyphs, `upgrade.m2v`, `med_ntsc.m2v`,
  `maui\dvd2.1\*` and `maui\dvd5.2\*` audio DSP microcode (ac3, dts, mp3, mpeg, wma, lpcm...).

## Image formats

### Firmware `.zbm` variant (verified, 263/264 decode)
Little-endian header, 0x2C bytes, then zlib:

| off | |
|-|-|
| 0x00 | 0x10 (variant marker?) |
| 0x04 | width |
| 0x08 | height |
| 0x0C | 1 (0 on a few) |
| 0x10 | 0xF6CD on most, 0x6B3B on fonts -> likely colour key |
| 0x14 | 1 |
| 0x18 | packed size |
| 0x1C | unpacked size (= w*h*2) |
| 0x20 | 1 / 0 |
| 0x24,0x28 | 0 |

Pixels: u16 **LE**, `A[15:12] Y[11:8] Cb[7:4] Cr[3:0]` (OSD mode "4444"), chroma neutral at 8.
`ii_nothing.zbm` fails its zlib checksum (probably a dummy).

### Game-disc `.zbm` (verified on all 3514 images of the 10 discs)
0x30-byte header, u32 LE: +0 version 1, +4 type 1 (TEXTURE_OSD), +8 format, +0xC bytes per
pixel 2, +0x10 w, +0x14 h, +0x18 colour-key enable, +0x1C key (packed pixel), +0x20 1 =
zlib / 0 = raw pixels, +0x24 packed, +0x28 unpacked size, +0x2C full-screen fast-copy flag;
then the data. Pixels u16 BE with pairs swapped (= 32-bit LE word read). The engine loader
(0x8060e784) asserts only version, type and the unpacked size, and stores pixels
bit-inverted in memory. Every disc image is format 4 with no key. Fonts `*.zbm` are 16x6
ASCII atlases (32..127) with a `.dat` of metrics (see Font `.dat`).

Pixel formats, from the colour packer 0x80612860(Y, Cb, Cr, A, fmt) (name -> code parser
0x80612780; format 0 means the current OSD mode):

| code | name | u16 |
|-|-|-|
| 1 | `844` | Y[15:8] Cb[7:4] Cr[3:0] |
| 2 | `655` | Y[15:10] Cb[9:5] Cr[4:0] |
| 3 | `4444` | A[15:12] Y[11:8] Cb[7:4] Cr[3:0] |
| 4 | `4633` | A[15:12] Y[11:6] Cb[5:3] Cr[2:0] |

## Game discs (verified: Sudoku, Gemz)

- Images are UDF 1.02 / ISO9660 bridge; ISO9660 names are 8.3-truncated, **Joliet** has
  full names (`;1` suffix). Undertow reads Joliet.
- `gamewave.diz`: `[global] appname, appfile, version` / `[platform] board=3,
  engine=/data/app_sdram_3.cat.bin, version`. Both games ship the same engine
  (md5 71ef9287..., "ZIT", platform 0.11.3) plus `app_sdram_a.cat.bin` for board "a".
- Engine `app_sdram_3.cat.bin`: uncompressed MIPS BE image for 0x80600000 (code 0x1606e0
  bytes) + cheese table (built-in font `f_1955_s_22_m_4633_32_128`, `game_wave.m2v`,
  `insert_disc.m2v`, `launching.m2v`, maui DSP microcode). Sources `apps/zit/*.c`,
  `apps/zit/zap_lua/zlua_*.c`, Lua 5.0.2, zlib, Nucleus.
- `.zbc` = zlib wrapper (magic `1B 5A 43 53 0A 1A` "ZCS", u32 LE unpacked, u32 LE packed,
  data @0x10) around **stock Lua 5.0.2 bytecode** except: signature `1B 5A 42 43 0A 1A`
  ("ZBC"), 3 extra bytes `01 00 01` after the version, little-endian, size_t=4,
  **lua_Number = int32** (test number 31415926). Debug info kept (source names
  `src/*.zsc`, line numbers, local names). Parsed byte-exact by `tools/zbcdis.py`;
  API usage by `tools/zbcapi.py`.
- Lua library tables (name -> C address) for the engine: `docs/engine_api_tables.txt`.

### Discs seen
- Sudoku, Gemz: engine platform 0.11.3.68fbbs (md5 71ef9287...).
- Lock 5: `appname=wilds`, engine 0.11.3.65nhfs (older build, md5 b2323224...). A
  Yahtzee-style game on five reels; the script's main table is `WildSevens` (working title,
  log lines `WildSevens.Unload...`), sources `ws_*.zsc`, `wild_sevens.zsc`. No saves.
- Letter Zap: engine 0.11.3.59 (oldest so far, md5 d9952fe9...). Cube (4x4 Boggle; boards
  from `tag.mtx` records such as `sndeelidfetebars`) and Tag!; `data_xx/` per language
  (dic38.zdt, lfreq.dat letter pool, tag.mtx); saves under "LETTER ZAP".
- 4 Degrees: The Arc of Trivia - Bible Edition: engine 1.01.3.5bpa1s (md5 666674c7...),
  same .zbc format and API. Despite the number it is *older* than the 0.11.3 engines
  (built 2005-11-25, see "Build stamps"). One MPEG movie per question in six
  category folders (`01_O_T_People/01_01_D_Japheth.mpg`...). Saves under "4 DEGREES BIBLE".
  The letter in the movie name is the correct answer; the script's question table holds
  `{0, answer_index (A=0), 415, movie, answer}` (main/22).
- VeggieTales: Veg-Out! Family Tournament: engine 0.11.3.68 (same as Sudoku), by Big Idea
  (bumper movie). Minigames (Bingo...) call `iframe.ShowPredefined(2)` before each one.
  Saves under "VeggieTales2007".
- Studio: Nytric (logo movie) made Gemz, Lock 5, Letter Zap and 4 Degrees.
- Quiz Konnect (late Indian release, not in Redump; file dump shared on Reddit by
  u/Amasteriscool): `appname=Four_Degrees`, a 4 Degrees reworked with Indian and
  international questions (`01_Arts/01_AmitabhBachchan_A.mpg`...). Script build stamp
  2008-11-02 12:24:43 on host `NATIONAL`, while the .diz still carries the 2006 build
  `1.00.669c8s` of the base game; engine 0.11.3.669das (2006-06-09, md5 d0673661...);
  movie file dates 2008-12..2009-04. Nytric logo but no ZAPiT logo at boot. No demo
  limits or "demo" strings in the script (the case reportedly says "Demo Game Not For
  Resale"). Dump check: all 127 zlib assets inflate cleanly and all 329 MPEG files decode
  without errors (FFmpeg). Archive checked: `Quiz Konnect.zip`, 509 files, SHA-1
  8bb854630f336dd8c608b4c6cf7510ff3470abe2, MD5 501581b32467a0c5763385f0edf7c7a7.

- Zap 21: blackjack (Nytric), game build `5blalj` (2005-11-21), engine 1.01.3.5bpa1s as
  4 Degrees Bible. No saves. Hides the OSD with `gl.Show(0)` while the full-screen deck
  shuffle movie plays, `gl.Show(1)` after.

- Rewind: trivia (Nytric), game build `5a5eir` (2005-10-05), the oldest engine seen:
  0.09.3.58.id4s (its suffix does not follow the later build-stamp format; the board "a"
  engine 0.09.a.58gans = 2005-08-16). The .diz lists two `[platform]` sections (board 3 and
  board a); Undertow keeps the board 3 one. 280 regular, 56 true-or-false and 28 "news or
  not" questions. Same quiz mechanics as 4 Degrees and Quiz Konnect (four options, timer,
  hints, wrong answers removed): the ancestor of that family.

- Click!: Wheel of Fortune style word puzzle (Nytric, internal gameID 15). Its .diz says
  `appname=4 Degrees Test` and has no versions (built from a 4 Degrees test project);
  board 3 and board a engines, md5 6f2f00c3... Clues in `clues1..6`, letter images in
  `letters/`; player names are composed with `BlitOverlayWithCR` (4 args, plain copy). The
  phrase is solved out loud: the board reveals it and the player confirms whether they
  were right. Saves under "CLICK".

### Build stamps (version suffixes)
The last part of a version string (`68fbbs`, `5asf0s`...) is the build time in base 36
(`0-9a-z`): year-2000, month, day, hour, minutes/2, then the first letter of the build
host, lowercased (4 Degrees script `Game_BuildNumber`, globals `version_date_*`,
`version_host_name`). Checked: 4 Degrees Bible's script holds 2005-10-28 15:00:37 host
`SSIMEN` = `5asf0s`, its .diz version; the Sudoku engine `68fbbs` = 2006-08-15 11:22
matches its file date. Engines: 58gans 2005-08-16 (Rewind, board a), 59uc7s 2005-09-30 (Letter Zap), 5bpa1s 2005-11-25
(4 Degrees Bible, "1.01.3"), 65nhfs 2006-05-23 (Lock 5), 669das 2006-06-09 (Quiz
Konnect), 68fbbs 2006-08-15 (Sudoku, Gemz, VeggieTales).

### Engine API semantics (verified in engine code or by script usage)
- Key codes (script constants): 0-9 digits, 10 UP, 11 DOWN, 12 RIGHT, 13 LEFT,
  14 SELECT, 15 DVD_MENU, 16-19 A-D, 20 GAME_MENU, 255 NO_KEY. Remotes 1-6 = red,
  yellow, blue, green, purple, orange.
- `input.GetKey()` -> key_id, remote_id, timestamp (core 0x8060fee0, 5 input modes via a
  jump table). With no key it returns the constant event at 0x8073550c: **key 255, remote
  255, time 0**; Lock 5 tests `remote < NO_KEY` to detect a key, so remote must be 255.
  - Modes (`input.SetMode`, 0x8060f920, clears the slots; default 0): 0 = one slot per remote,
    only the round-robin remote (index advanced after every GetKey) is checked; 1 = mode 0 or
    a random key from `SetRandomKeysTable` (auto test); 2 = random only; 3 = scan all six
    slots from the round-robin index; 4 = per-remote FIFO of `SetQueueSize` (default 4)
    entries. A slot keeps the first unread press. All shipped games use mode 0.
  - The IR code is decoded by an external chip: the engine reads a 5-bit key and 3-bit remote
    (1-6) on GPIO with a strobe interrupt (0x80623afc). There is **no key-up event, no held
    state and no software repeat**; whether a held button re-strobes is up to the decoder
    (unknown). The timestamp is the Nucleus tick (same clock as `time.GetRealTime`).
  - `EnableRemotes`/`DisableRemotes` return 0 / -1 (failure); Undertow returns 1.
- `movie.GetState()` -> **0 when finished** (wiki says the opposite); scripts poll it.
- `gl.SetParameters(ovl, x, y, z, visible)` (core 0x80615228: overlay+0x34 pos,
  +0x44 z, +0x48 visible; `SetVisibility` writes the same +0x48), marks dirty rects.
- Menus = MPEG movies (intro + looping) on the video plane, OSD overlays on top.
- `movie.Load(path)` -> **0 on success** (Gemz checks it); Load also **resets looping**
  (Gemz plays transitions without `SetLoop(0)`). Movies: MPEG-PS, MPEG-2 720x480 29.97
  interlaced 4:3, MP2 44.1 kHz stereo.
- `.zwf` sound effects are **mono** s16 BE at 44.1 kHz (header +4 = sample count, often odd;
  zlib data at +0x14). The wiki's "stereo 22050" plays identically by accident.
  Header, five u32 LE (all 175 .zwf of 9 games): magic `0x7c90ee02`, sample count,
  channels (always 1), size of the zlib data, a value that differs per file (a pointer
  of the authoring tool?).
- `audio` has only `Load(res, name)`, `Unload(id)`, `Play(id, delay)`: no volume, pan or
  stop. `Play` (core 0x8061c9c8) queues `{sound, id, now_ms + delay}`: the **2nd argument
  delays the start in ms** (every game passes 0). With more than 8 entries pending and
  delay 0 it logs "Sound id %d will be queued (too many sounds)" (at most every 5 s), so
  about 8 sounds play at once. Undertow: delay emulated, 16 voices, no queue.
- `zmath.Rand(min, max)` -> [min, max) (engine: min + rand() % (max - min)).
- `pointer`: byte offsets; `ToStringRange(buf, a, b)` -> bytes [a, b).
- `zfile.ReadBytes(file, offset, count)` -> buffer; ReadLine -> (0, line) | (1, nil).
- Animations (times on the time.GetRealTime clock): `AddPositionAnimation(ovl, fx, fy,
  tx, ty, start, dur)`, `AddAlphaAnimation(ovl, start, 1=in|2=out, dur)`,
  `AddVisibilityAnimation(ovl, at, visible)`, `CreateTextureAnimation(ovl, start,
  {{cmd, arg, ms}...})` with cmd 1 TA_DISPLAY_TEXTURE frame, 2 TA_END_ANIMATION,
  3 TA_JUMP step (names from script locals); `HasAnimations` is busy-waited on.
- `BlitOverlay(src_ovl, dst_ovl, x, y [, ?, blend])` draws src into dst's texture and
  **copies pixels by default** (Gemz clears board cells by blitting an empty overlay;
  blending left stale gems under new ones, confirmed by playtest). Arg 6 = 1 assumed blend.
- `BlitOverlayWithCR(src, dst, x, y [, ?, blend, flag, Y1,Cb1,Cr1,A1, Y2,Cb2,Cr2,A2])`
  (binding 0x80625f60): same core 0x80613c44 as BlitOverlay, plus two packed colours
  (0x80612860) for a colour replacement. Letter Zap passes only 6 args.
- `gl.Show(on)` (binding 0x80625dec -> 0x80612650 -> 0x806cb9b0): turns the whole OSD layer
  on/off; the video plane stays. Lock 5 and Zap 21 pair Show(0)/Show(1) around movies.
- `iframe.ShowPredefined(n)`: the engine builds a 4-entry predefined still table at boot
  (0x8060c3c0, count byte set to 4 at 0x8060c654): 0 `launching.m2v`, 1 `insert_disc.m2v`
  (from its file table; solid black if missing), 2 solid black (Y 16, Cb/Cr 128),
  3 another kind (not understood). Core 0x8060e960 ignores n >= count.
- `dict.Load(res, name) -> handle` (light userdata, NULL on failure), `dict.Lookup(handle,
  word) -> boolean`, `dict.Unload(handle)`; see "Dictionary .zdt".
- Text: `text.Render(str, font, w, h, halign, valign, line_spacing, word_spacing,
  char_spacing, tint, Y, Cb, Cr, debug_border)` (binding 0x80628d4c -> 0x80606ba8 -> layout
  and draw 0x80608d34). Args 7/8/9 are signed bytes at +0x16/+0x15/+0x14 of the style struct
  (stores at 0x80628e14/34/54): line spacing is added to the line pitch (cell + arg 7,
  0x80608ad0), word spacing to the gap between words (advance of glyph ' ' + arg 8,
  0x80608abc), char spacing between glyphs of a word (0x80607dd4). Static scan of all
  discs: arg 8 is always 0, arg 9 is 0/1/2 (Gemz names, Letter Zap, Lock 5, Zap 21,
  VeggieTales). Arg 14 draws a 1-pixel debug outline round the texture. Colours YCbCr.
  - Layout: paragraphs split with `strtok(s, "
")` (0x80608220), words with
    `strtok(p, " 	
")` (0x8060808c): runs collapse and empty lines vanish; spaces are
    never drawn. Greedy wrap at word boundaries only (new line when line + gap + word > w,
    0x80608538); a word is never broken (it is clipped). No fixed limits: string, words and
    lines live in heap vectors; anything outside the texture is clipped.
  - Glyphs: no range check (a char outside [first, end) reads past the font's arrays);
    glyph 32 is read unconditionally for the word gap. Measuring uses `advance`; drawing
    blits atlas columns [x0+lb, x0+lb+ink) x rows [y0, y1) at pen + lb, then moves the pen
    by lb+ink+rb (0x80607d60). Kerning is a linear search per character pair (0x80607a68).
  - The text texture takes the **font atlas's format** (0x80606be0) and is filled with the
    atlas key; without tint, opaque glyph pixels are copied as they are. The blitter refuses
    copies between different formats (0x80613654), so a non-4633 font needs the matching
    `gl.SelectOSDMode`.
  - `RenderSimple(font, str)` (0x80607acc): every byte is a glyph (spaces and `
`
    included), one line, no kerning. `GetOverlayId(tid)` -> hidden overlay the script
    positions.
- `gl.SelectOSDMode(name)` (0x80626f1c -> 0x806126a0): sets the OSD pixel format (all discs
  pass "4633"). `gl.CreateEmptyTexture(w, h [, format name])` (0x80626224): format as above
  (default: the OSD mode); the texture is filled with the colour YCbCr (0x6a, 0xca, 0xdd)
  (magenta) as key. There is no way to upload pixels from a script through `gl`.
- `pointer.Get*/Set*` do **no bounds check** in the engine (SetU32MSB 0x8062839c writes at
  buf + offset), and `tostring` of a light userdata shows its address: on the console a
  script can read and write any memory. Undertow checks bounds.

### Save EEPROM (engine `apps/zit/eeprom_mgr.c` 0x80604000.., Lua `zlua_eeprom.c` 0x806295f4..)
Implemented byte-compatibly in `src/eeprom.c`. AT25256A, 512 pages x 64 bytes, page i at
address i*64, whole image read once then written through page by page.
- Page: u16 LE CRC-16/XMODEM (poly 0x1021, init 0) of bytes 2..63; u16 LE flags (bit15
  free, bits13-12 type 0 single / 1 first / 2 middle / 3 last, bits8-0 next page); u32 0;
  56 bytes payload. **Little-endian** although the CPU is big-endian. Formatted page =
  `EE 3D 00 80` + 60 zeros; a blank 0xFF chip is not handled by the engine and no game
  calls `Format`, so new images start formatted.
- A save = header `{u16 app_id, u16 size, char app_name[17], char save_name[33]}` (54
  bytes) + data, `ceil((size+54)/56)` pages allocated first-fit in ascending order; first
  page holds the header and data[0..1], then 56 data bytes per page.
- **IDs are indices into the list built by the last Enumerate call** (pages scanned
  ascending for first pages). `EnumerateGameSavesByName` matches app_name (skips app_id 0);
  `ByID(0)` matches everything.
- Lua returns end with an **error flag** (false on success), which is what scripts test:
  `SaveGameToNewSlot(app_id, size, app_name, save_name, data) -> err` (err only when out of
  pages), `SaveGameToExistingSlot(id, data) -> false` (stored size, cannot grow),
  `EnumerateGameSaves*(x) -> count, false`, `GetSaveNameByID(id) -> app_name, save_name,
  false`, `LoadSaveByID(id) -> buffer, err` (buffer of whole pages = n*56 bytes; err on CRC
  failure, data still returned), `UnloadData(buf)`, `Format()`, `CorruptFlash()`,
  `CheckFlashIntegrity() -> bad page count`. A delete exists (0x806060f8) but is not exposed.
- Sudoku: app_id 6902 "SUDOKU01", 4 saves SOLUTION/GUESSES/SCRATCHES/CLOCK (84/44/164/4
  bytes). Gemz: "GEMZ", BACKGROUNDS (4 bytes, saved on level completion) and top scores.

### Dictionary `.zdt` (engine dict core 0x80624a64.., implemented in `src/lib_dict.c`)
8-byte header (`01 01 00..`, unused) then a plain trie: node = u8 edge count + edges;
edge = byte (bits0-4 letter code, char = code|0x60; bit5 end of word; bits6-7 K) + K-byte
LE offset of the child node from the start of the parent node. Lookup scans edges
linearly (the Latin list is unsorted), exact and case-sensitive; "" is found. Letter Zap
ships accent-stripped lists for en/fr/it/lat/pt/ro (16k-80k words, 3-8 letters; ro 2-16).

### Font `.dat` (verified on all fonts of both discs)
5 x char[128] (face, family, charset, style, atlas .zbm name), then at 0x280:
int count(96), first(32), end(128), cell size, kerning count; 0x294: count x 8 ints
{x0, y0, x1, y1, advance, left bearing, ink width, right bearing}; then kerning count x
{first, second, adjust}. The engine loader (0x80607150) asserts only end - first == count
(no limit of 256, first may be 0) and builds a rect per char code in [first, end) (see Text).

## Hardware / boot
See `HARDWARE.md` (CPU core, memory map, SoC register blocks, boot flow).
Tools: `tools/mdis.py` (annotated disassembler), `tools/xref.py` (string/address xrefs),
`tools/mmiomap.py` (MMIO blocks vs debug strings).

## Open questions
- Register-level behaviour of the SoC blocks (OSD, video decoder, ATAPI, ASP, UART/IR).
- `.osd` glyph format (header `00000002 00000010`), `startup.osd`.
- OSD resolution/coordinate space (720x480?), animation timing, text rendering.

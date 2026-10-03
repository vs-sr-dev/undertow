# Game Wave hardware map (work in progress)

Derived from static analysis of firmware 060505 (`app_sdram.bin`, built "May 5 2006")
and `upgrade.bin`, from the system upgrade disc 1.021.060505 (dump shared by Reddit user
u/Amasteriscool). Confidence: **[V]** verified in code, **[I]** inferred from strings/usage.

## CPU

- MIPS32 big-endian, **MIPS I class with R3000-style COP0** [V]:
  - exception return via `rfe` (0x42000010); no `eret` anywhere.
  - exception vectors copied by crt0 to `0x80000000` (UTLB) and `0x80000080` (general);
    boot exception vector at `0xBFC00180` -> `0x9FC00418`.
  - COP0 regs used by crt0: 3 (Config, set to 2), 9/11 (Count/Compare timer),
    10 (PortSize?), 12 SR (`0x10000015` at boot), 13 Cause, 14 EPC.
    This matches the IDT **R3041** register layout; Nucleus port string is "IDT305xIG".
  - BUT code uses MIPS II **branch-likely** (`beql`/`bnel`, ~4000 occurrences) [V].
  - main firmware contains no `lwl/lwr/swl/swr` (only a handful in bootloader/upgrade).
  - Emulation target: MIPS I + branch-likely + R3000 COP0 (+ Count/Compare). Load-delay
    slots: assume interlocked (compiled code does not rely on them - TBC).
- Address decoding: code freely uses kseg0 `0x8xxxxxxx`, kseg1 `0xAxxxxxxx` and even
  `0x2xxxxxxx` (firmware jumps to the game engine at `0x20600000`) -> treat physical
  address as `vaddr & 0x1FFFFFFF` [I].

## Memory map (physical)

| range | what |
|-|-|
| `0x00000000`-`0x00FFFFFF` | 16 MB SDRAM [I] (crt0 probes `0xA0000030` with a pattern) |
| `0x00000000`-`0x0000017F` | exception vector stubs (copied at boot) |
| `0x00010000`-`0x0005B197` | firmware .data (0x3108 B, copied from image) + .bss |
| `0x00600000`- | **game engine / upgrade program load address** (entry +0, stub jumps to +0xA50) |
| `0x00A00000`-`0x00C5B467` | main firmware image (text from 0x80A00000, .data source at 0x80B83C10) |
| `0x00C00000` | `gamewave.diz` is read here while launching a game |
| `0x05000000` | unknown device (12 refs) |
| `0x05400000` | **ATAPI** interface to the Sanyo DVD loader, via EPLD ("EPLD Ver") [I] |
| `0x08000000`-`0x080FFFFF` | **SoC registers** ("Pantera" = Mediamatics 8611) |
| `0x1FC00000` | 2 MB boot flash (`0xBFC00000`), firmware `app.cat.bin` lives here |

### SoC register blocks (`0xA80xxxxx`)

| block | role |
|-|-|
| `0xA8008000` | UART(s); consumer-IR remote ("cir:") goes through a UART (`+0xB0`) [I] |
| `0xA8010000` | disc front-end / sector transfer (CD mode2, MSF) [I] |
| `0xA801C000` | ? |
| `0xA8020000`, `0xA8030000` | ? (likely MPEG video decoder) |
| `0xA8060000` | SPU (DVD subpicture) + OSD vsync (`+0x40`) [I] |
| `0xA8068000`-`0xA806E000` | display / SPU palette, scaler ? |
| `0xA8070000` | system control: `+0x34` (written 0x20FF/0x20FE at boot), `+0x4C` [V] |
| `0xA80AD000`, `0xA80B0000`, `0xA80C0000` | **ASP** audio DSP ("maui"): DMEM0/1, CRAM, microcode load [I] |
| `0xA80D0000` | chip revision / system (`+0xA0` reset-ish control at boot, `+0xC0`) [V] |
| `0xA80E8000` | written 0x20 when flash is blank (reset/remap?) [V] |
| `0xA80F0000` | chip revision ("Pantera-II"/"Pantera-III") [I] |

## Boot flow [V]

1. Reset at `0xBFC00000` -> `b 0x40`; warm-reset magic `k0 == 0xDEADBEEF` handling;
   if flash word 0 is zero -> write 0x20 to `0xA80E8000` and hang.
2. Bootloader crt0 at `0xBFC00E50`: COP0 init, SoC init (`0xA80D00A0`, `0xA807004C`,
   `0xA8070034`), RAM probe, data copy to `0x80010000`, bss clear, vectors to `0x80000000/80`.
3. Bootloader unpacks `app_sdram.bin.lzh` (-> `0x80A00000`) and jumps (entry stub -> `0x80A00A50`).
4. Firmware crt0 (identical SDK code), then Nucleus PLUS + apps (DVD navigator, menus).
5. Game launch (`0x80A02CD4`): if `gamewave.diz` exists, load it to `0xC00000`, parse
   `[platform] engine=`, load that file to `0x600000`, else fall back to `app_sdram.cat.bin`
   + `game.zbc`; then `jalr 0x20600000`. **No arguments, no firmware services:** the engine
   is a complete self-contained program (own crt0 + Nucleus + drivers). `upgrade.bin` was
   checked: zero calls into firmware addresses.

Loader `0x80A029DC(name, dest, &size)`: looks up the file entry, reads 2048-byte sectors and
copies **byte by byte to `(dest ^ 3) | 0x20000000`** - no checksum/signature check [V].
Open question: the `^3` + `0x2xxxxxxx` combination suggests `0x2xxxxxxx` is a
byte-lane-swapped alias of RAM (net effect: straight copy). Verify with a real engine file.

=> For low-level emulation of games the firmware is **not required**: load the engine at
`0x80600000` and start it. Only a stub at `0xBFC00010` (`jr ra`) is called by crt0.

## Firmware modules (from debug strings)

Nucleus PLUS RTOS; apps/dvdnav (DvdMenu*, eeprom_mgr.c, spi_eeprom.c, Infofile.c,
PanteraSetupProperty.c), app_main/app_file/app_iframe/app_alloc/app_message, asp.c +
AudioDriver/LoadDecoder/Wma/Mlp (maui DSP), spu, osd ("zapgl vsync", "OSD Physical Vsync"),
atapi_imp/_atapi_cmd, udf_pars, dvd_nav/dvd_pres/vcd_*, cir (IR), uart0/uart1, macrovision.

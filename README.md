# Out of This World (DOS demo) -> Sega Master System

Work in progress. No game data is included: every asset is regenerated from the original
DOS demo files (MEMLIST.BIN + BANKxx) by the tools below.

## Layout
- `sms/src/`      Z80 code (SDCC + devkitSMS): `main.c` flow, `fmv.c` FMV player, `engine.c` bytecode VM,
                   tile-page polygon renderer, page-state cache, PSG sound.
- `sms/build.sh`, `sms/mkrom.py`  compile and assemble the 4 MB ROM (Sega mapper, 32 KB cartridge RAM).
- `sms/tests/`    smstest scripts (headless emulator checks: FMV pacing, gameplay, profiling).
- `rawgl/`        cyxx/rawgl (GPL) plus a headless "oracle" build: `oracle.cpp`, `trace.h`,
                   `bgcap.inc` (page-state capture), `search.inc` (Go-Explore level search). `make -f Makefile.oracle`.
- `fmv/`          cutscene capture -> shared tile dictionary encoder (`encode.py`, `verify.py`, `pack.py`,
                   driver `build_fmv.py`, cut list `cuts.txt`).
- `audio/`        Amiga-sample music/SFX -> SN76489 PSG streams (`psgconv.py`, `trackaudio.py`).
- `game/`         `mkgame.py` (bytecode, polygons, palettes, strings, SFX, LUTs -> ROM banks),
                   `mkbg.py` (pre-rendered page states -> ROM tile pool + key table).
- `tools/`        `tools_rebuild.py` (rebuilds memlist/bank from the another_js demo dump), smstest emulator.
- `NOTES.md`      detailed engineering notes and current status.

## Pipeline (paths in the scripts assume /home/claude/w; adjust as needed)
1. Game data -> `data/memlist.bin`, `data/bank01` (`tools/tools_rebuild.py`, or the original demo files).
2. `cd rawgl && make -f Makefile.oracle`
3. Intro capture: `./oracle ../data 16001 3000 ../cap/intro.bin ../cap/intro.txt`
4. Page-state captures for 16002 (see NOTES.md): `OOTW_VARS=../cap/vars_16002_start.bin BGCAP=../cap/pc_<name>.bin ./oracle ...`
5. `python3 game/mkgame.py`, `python3 game/mkbg.py 160`, `python3 fmv/build_fmv.py`
6. `SRCS="fmv engine main" BLOBS="2:gen/game.bin 64:gen/fmv.bin 160:gen/bg.bin" bash sms/build.sh`
7. Tests: `tools/smstest/smstest sms/ootw.sms sms/tests/fmv.txt` (and game2/pool tests)

Requires the devkitSMS toolchain (SDCC, ihx2sms, SMSlib, PSGlib), Python 3 with numpy + Pillow.

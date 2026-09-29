
## Session note (written 22:4x) — READ BEFORE EDITING
Two sessions appear to be editing this tree concurrently (engine.c changed under me; search launched in parallel).
Findings from this session:
1. The blue "tunnel" at the start of part 16002 (displays 0..~107 in the no-input run) is GAMEPLAY, not a cutscene:
   it is Lester sinking in the pool; the player must hold UP from about display 30 (oracle: "30 8" -> pool bg at display 64;
   no input -> death screen at ~108). So the intro FMV must end at the part boundary: fmv/cuts.txt handover = 2982
   (reverted from 3046). gen/fmv.bin has NOT been rebuilt yet after that revert -> run `python3 fmv/build_fmv.py`.
   engine.c FF_DISPLAYS must therefore be 0 (fast-forward was built for the wrong assumption).
2. engine.c SCL(): zoom > 255 overflowed 16-bit (original uses int); fixed with a 32-bit path.
3. draw_polygon: scanlines above y=0 are now skipped in one multiply step.
4. Renderer speed is the blocker: the underwater scene is many thin polygons; ~3 scanlines per SMS frame with the
   per-scanline C span(). Planned fix (not applied, edit conflicted): flush each 8-line band per TILE COLUMN
   (one COW/lookup per tile per band, rows applied by an asm routine: b ^= (b ^ fill) & mask), full tiles -> ref swap.
5. fmv.c: tile uploads moved to an asm batch loop (up_batch); unpaced only when VCOUNT in [0xC1,0xEC).
   Encoder cost model: CYC_TILE=1150, CYC_RUN=420 per nametable run, gap-merged runs.
6. cap/search.log room hashing is too fine (74k "rooms"): the bg hash includes per-frame noise.

## Session note (23:3x) — single session, tree owned
- Page-state cache (all 4 pages): rawgl/bgcap.inc <-> engine.c "page-state cache" section, identical hash protocol
  (fill/copy/scroll-copy/shape/string). Capture MUST use OOTW_VARS=cap/vars_16002_start.bin (intro hand-over vars).
  Rebuild data: game/mkbg.py 160 -> gen/bg.bin (banks 160..223 now) + gen/bgdata.h. Page cells may reference ROM tiles (id>=1024).
- Coordinate LUTs moved to ROM bank LUT_BANK (game.bin now banks 2..11). Code bank use 80%.
- display() diff loop in asm (find_diff); uploads straight from source (upload_direct), unpaced only in VBlank window.
- Build: SRCS="fmv engine main" BLOBS="2:gen/game.bin 64:gen/fmv.bin 160:gen/bg.bin" bash sms/build.sh
- Speed now: underwater ~6 fps, live pool gameplay ~1.3-2 fps (C polygon pipeline is the limit; next: asm scanline/band code).
- Level search: rawgl search.inc, screen = var 0x67 (0 underwater,2 pool,3,4 slugs,5 beast,1 vine cliff left of pool).
  Blocker: slug on screen 4 must be killed (kick) on the way right, otherwise it bites while fleeing the beast.
  Paths to each new screen saved in cap/room_<n>.txt; running in background -> cap/search3.log, cap/solve.txt on success.

## Optimization round (00:xx)
Deterministic benchmark: sms/tests/bench2.txt (hold Up to game frame 90, idle, profile frames 110..150).
  before this round ~1.5-2 fps live pool gameplay; now 40 game frames in 537 SMS frames = 4.5 fps (~820k cycles/frame).
Profiler in tools/smstest now per-address (PROF_SHIFT 0) -> /tmp/prof.py gives exact per-function costs.
New asm: scan_lines (edge stepping), band_lines (band -> coverage matrix), vconv (vertex LUT conversion),
  cpr_loop/cpr_ref/cpr_unref (page copy + bg_load), fill_loop, find_diff, upload_direct, fetch, bgb, snd_voices, rt_sync, mul16u.
map_rom/map_sram are macros. Per-page change tracking (pbase/pgen/prow + TOUCH/page_all) lets copy_page_refs restore
  only touched tile rows when the source is unchanged.
Remaining top costs per frame: cpr_loop ~98k (copy + cache loads, full passes), exec_task+fetch ~140k (VM in C),
  find_diff ~83k, psg_write ~35k, bgb ~33k. Next ideas: lazy page copy dropped on full cache hit; asm VM core
  for common opcodes; row-restricted screen diff.
Build: CF in build.sh accepts $XCF; --max-allocs-per-node 5000 for dev (100000 gives smaller code, same speed).

## Optimization round 2
Benchmark bench2/bench3 now holds Up from the moment the intro is skipped (input timing no longer depends on engine speed).
  40 game frames: 537 -> 431 SMS frames (4.5 -> 5.6 fps).
- STACK BUG FIXED: data ended at 0xDEA6 while the stack reached 0xDEA9 (3 bytes margin) -> intermittent corruption.
  scr[] moved to cartridge RAM (last 32 tile slots of half 1, SCR_TILE=992; pool now 16..991); NDEF 24->16;
  debug arrays removed. Data now ends 0xDAFF, measured stack low 0xDE7F (~900 bytes margin). smstest has "spmin".
- main.c waits for buttons to be released after skipping the intro (skip button no longer leaks into the game).
- VM fast path in asm (vm_fast: ops 00-03,07,09,0A with bank-crossing fetch); other ops stay in C.
- Lazy page copies (pend[]): copy recorded, done on read/draw or when the source changes, dropped on a full cache hit.
- Screen diff restricted to rows that can differ (drow/rowstamp/lastdisp); cache loads and copies mark only changed rows.
- bg_feed (9-byte hash in one asm call), byte-wise psg_write, asm fill_page.
Verification tools: -DVMTRACE (port 0x04 stream, SMSTEST_TRACE=file; compare with oracle VARS via /tmp/vmcmp.py),
  -DDIFFCHECK (counts screen cells a row-restricted display missed; must be 0).

## Level solved + capture ending (round 3)
- The demo's 16002 ends via story var 0x2A: 1-3 entering screens 3/4, 4 meeting the beast (screen 5), 5 the vine jump on
  screen 1 (x<=110, Up), 6 at x>=317 on screen 5 -> task 0x0FDC = the CAPTURE scene + credits, then back to part 16001.
  (updateResources(16003) in the demo script is dead code.) Route found with the oracle: cap/solution_16002.txt
  (frame mask pairs). Tools: tools/try.py, tools/trace.py (screen/progress/position/slug-state timeline).
- Capture FMV: cut "capture" = cap/ending_run.bin displays 764..1099 (fmv/cuts.txt), shared dictionary (24110 tiles).
  Engine: display() sees vars[0x2A]==6 -> GAME_EXIT_CAPTURE -> main plays FMV_CAPTURE -> intro again.
- -DDEMOPLAY: input from gen/demo_input.h (recorded route) = self-playing build (like DEMO3.JOY).
- Page-state cache now row-deduplicated (mkbg.py 150: tiles/rows/maps/keys, banks 150-217); MIN_POLYS 8, LIVE_POLYS 6;
  captures: cap/pc_route.bin (full route) + underwater variants.
- Screen shake (task 60, scroll copy page3->work) = reference copy + VDP vertical scroll (pscroll[] per page).
- NDEF 32 (queue overflow forced live rendering of 16 draws per frame on screens 3-5).
- Full route on the SMS: 764 game frames in 8788 SMS frames (~5.2 fps average; original ~12.5); 37 cache misses.

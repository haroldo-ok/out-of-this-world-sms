
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

## Optimization round 4
- disp_upload (asm): per changed cell derives the VRAM address from the cell pointer, picks the source
  (solid table / ROM tile bank / cartridge-RAM half), sends 32 bytes (VBlank-fast or paced), stores the key.
- bgl_do (asm): cached-state loader over 17 rows; prowid[p][r] (ROM row id a page row is known to equal,
  invalidated by TOUCH/page_all, inherited by copies) lets loads skip identical rows without comparing.
- BUGS FIXED (found by the new -DVRAMCHECK build, which re-reads all 442 VRAM tiles after each display):
  * mapper written before slot2 in disp_upload: an interrupt in between restored the old bank -> key written to ROM.
  * VDP address latch race: the VBlank handler reads VDP status between the two control-port bytes (and may
    write VRAM itself). Tile uploads, palette writes and the shake scroll register now run with interrupts off.
- Results: full route 8692 -> 7499 SMS frames; bench2 375 -> 316 SMS frames (40 game frames, ~7.6 fps).
  VRAMCHECK + DIFFCHECK clean over the whole route; VM identical to oracle; stack margin ~436 bytes.

## Optimization round 5
- band_flush_: column mask gather (col_gather) and colour-0x11 row merge (apply_rows_src) in asm (save/restore IX:
  SDCC's frame pointer). -DPAGETRACE (checksum of the shown page per display on port 0x04) proved rendering identical.
- find_diff rewritten: key low byte == tile id low byte, so ROM/solid cells compare 2 bytes with an 8-bit djnz counter;
  RAM-tile keys computed only when the high byte differs. (fd_n is now used as an 8-bit count <= 26.)
- rt_loop: task dispatch loop in asm. Page copies skip rows whose known ROM row ids match (prowid).
- Results: route 7499 -> 6893 SMS frames; bench2 316 -> 298 (40 game frames, ~8 fps). Code bank at 94.5% (release),
  97.5% with DEMOPLAY: further asm needs space (candidate: move the FMV player into a switchable bank).

## Hero sprites (in progress)
Done:
- FMV player moved to ROM bank 12 (linked --codeseg BANK12 at 0xC4000, mapped into slot 1 only while it plays;
  main.c is linked first so it stays in slot 0; mkrom.py copies banked code from game.ihx).
- VRAM: border tile -> 506 (zero tile in the SAT gap); sprite patterns at 0x2000 (reg 6 = 0xFF); 14 free 8x8
  sprite tiles: VRAM tiles 442-447, 498-501 (nametable rows 25-26; hardware shake limited to +-7 so they never
  show), 507, 509-511. Sprite palette = BG palette with index 14 = BG colour 0 (Lester's colour 0 remapped).
- Hero detection: draw op 0x40 with x from VAR 1 and y from VAR 2 (both engines).
- Canvas: "page 4" (hcan, 4 tile rows x 26) rendered by the normal rasterizer (pgp[], clip_h, g_oy);
  spr_upload() turns non-empty canvas tiles into sprites (follows the shake); association hero_pages follows copies.
Finding: Lester is almost never the last draw (5-10 small shapes follow him; they overlap him in 54/621 frames),
  so the "trailing hero" rule rarely fires and the cache keeps Lester baked into its states.
Next: exclude hero draws from the page hash in both engines (oracle capture renders pages without Lester),
  compute bounding boxes of the draws after Lester from polygon headers; no overlap -> sprites, overlap ->
  load the longest cached prefix before Lester and draw him in order. Stack margin now ~200 bytes: watch it.

## Hero sprites: working (round 2)
- Protocol (both engines): Lester's draws (op 0x40 with x from VAR 1, y from VAR 2) are queued but NOT hashed; the
  oracle capture (BGCAP) does not render him, so cached pages never contain Lester and are pose-independent.
  The oracle also stores the state just before Lester when shapes follow him ("prehero").
- SMS bg_flush: bounding boxes of the draws after Lester are computed by walking shape headers (bbm mode).
  No overlap -> Lester rendered into the 5-row canvas (hcan) and shown as 8x8 sprites; overlap -> longest cached
  state before him, then everything drawn in order into the page.
- Bitmap masks and single points also render into the canvas (offset g_oy, clip_h 40).
- Fixed: sprite palette was written one entry late (border grey, Lester colours shifted).
- Constant tables (palettes, font, sfx table, recorded input) moved to ROM bank 13 by game/mkconst.py
  (run after mkgame.py); build.sh has default BLOBS and checks the real end of fixed code (map file).
- Verified: screenshots with sprites == with -DNOSPRITES (0 differing pixels at 12 route frames); VRAM/diff checks clean;
  VM identical to the oracle. Stack margin ~100 bytes (data ends 0xDDDD, stack low 0xDE48): tight.
- Speed: the recorded route got slower (6893 -> ~18300 SMS frames) because the route cache used to contain Lester
  in his exact poses; now he is rasterized every frame, and his bitmap masks alone cost ~210k cycles/frame
  (per-run span() calls). Next: reuse the canvas when his pose and sub-tile alignment are unchanged, and render
  masks through the band/coverage machinery (or pre-render mask bitmaps) instead of span() per run.

## Hero sprites round 3
- Masks: every mask row of the 1991 renderer has at most one run -> mkgame.py stores masks as (x0,x1) per row and the
  engine rasterises them through the normal band path (was a bit loop + span() per run): ~230k -> ~45k cycles/frame.
- pfetch in asm; canvas reused when Lester's draws are identical to the canvas in place.
- Dead code removed (band_full, band_put, copy_rows, upload_direct, psg_write): fixed code now ends ~0x772B.
- Data pipeline: mkgame.py -> mkdemo.py -> mkconst.py (mkconst strips tables from the generated headers).
- Fair comparison (free play in the pool area, game frames 100-250, not the recorded route):
  pre-sprite engine + its Lester-baked cache 4807 SMS frames; sprite engine 5101. The recorded-route slowdown
  (6893 -> 16800) comes from the old cache holding Lester in the exact route poses.
- Free-play profile: ~2.1M cycles/frame, dominated by the general polygon pipeline (fill_polygon, band, pfetch,
  shape hierarchy, scanlines, vertex conversion, copy-on-write, edge steps). Next big lever: that pipeline in asm.

## Tiny shapes (particles) experiment
- Free play (pool area, game frames 100-250): ~40 non-Lester polygons/frame, about half are 1-2 px particles.
- Upper bound (not drawing tiny shapes at all, test only): 5076 -> 4198 SMS frames (17%).
- plot() now writes the pixel directly (pix(): one COW + 4 bit ops) instead of span(): exact, 5101 -> 5076.
- Fixed: span() wrote in place into private tiles without TOUCH (row not marked modified).
- Tried: tiny polygons written pixel by pixel instead of the band path -> slower (5306), reverted.
- Conclusion: a particle's cost is the per-polygon setup in C plus the tile copy-on-write / upload / restore it causes;
  real gains need the polygon setup in asm, or particles as sprites (needs free sprite tiles + overlap handling).

## Polygon setup in asm (round 1)
- calc_step_a + dpl_loop: edge setup (recip table, |dx|*rc via mul16u, <<2, sign) and the edge-pair loop in asm;
  rare cases in C (cs_divf for edges > 255 lines, dp_skip for lines above the window).
- fp_fast: fill_polygon common case (zoom 64, colours 0-15, not hero canvas / bbox walk): header, off-screen cull,
  vertex copy with LDIR (C path if near a bank end), vconv, draw; points go straight to plot().
- mul16u: 8-iteration path when the multiplicand < 256.
- Tried and reverted: pread() bulk header reads in C (slower than asm pfetch for 3-4 bytes).
- Free play (pool, game frames 100-250): 5076 -> 4542 SMS frames; route 16854 -> 15619. Pixel-identical
  (route screenshots + free-play end frame), VRAM/diff checks clean, VM == oracle.
- Remaining: overlap bbox walks (~5000 fill_polygon calls in bbm mode over 250 frames) -> offline bbox table per
  (segment, offset); shape hierarchy traversal (draw_shape_parts + pfetch) in asm.

## Overlap boxes from an offline table
- game/mkbbox.py: relative bounding box (zoom 64) of all 742 shapes the 16002 script draws (offsets from the
  disassembly), computed with the same rules as the engine's bbm walk; appended to gamedata.h and moved to bank 13
  by mkconst.py (which now also emits *_ADDR numeric addresses for asm). Pipeline: mkgame -> mkbbox -> mkdemo -> mkconst.
- ent_bbox(): table lookup (asm binary search bbox_find) for zoom-64 shapes, walk otherwise.
  -DBBCHECK compares table vs walk: 0 mismatches (free play 1489 lookups, route 4131).
- Free play 4542 -> 4312 SMS frames; route 15619 -> 14860. Pixel-identical, all checks clean.
- Next: band_flush_ column loop and writable() (copy-on-write) in asm.

## Renderer core (round)
- Copy-on-write from a solid-colour tile copies straight from solid_tiles into cartridge RAM (no 32-byte build + bounce);
  load_tile() also uses solid_tiles.
- writable() skips TOUCH when called from band_flush_ (the band's row is already marked; flag wr_touched).
- Tried: band column loop in asm (bf_cols). After fixing a register bug (A held f1 instead of tx when entering the
  partial-column path; caught by -DPAGETRACE, 422 of 603 frames differed) it was exact but gave no gain -> reverted.
  The band cost is in partial tiles (copy-on-write + row writes), not in the loop.
- Free play 4312 -> 4221 SMS frames; route ~14860 -> ~14500. Page checksums identical over 603 frames.

## Adaptive frame skipping (busy scenes)
- display(): a schedule advances by each game frame's original duration (VAR 0xFF x 20 ms = 1.2 ticks per unit).
  If the real clock is already one frame behind, the picture is dropped (VM, input, sound and game_frames continue;
  never with a pending palette; at most MAXSKIP in a row, default 1). Pacing waits on the same schedule; lag
  beyond 4 frames resyncs. Dropped pages' queued draws are discarded by the next fill/copy (lazy rendering).
- -DNOFRAMESKIP: exact original pacing (use for pixel-identity tests). -DMAXSKIP=2: drop up to 2 of 3.
- Free play (pool, game frames 100-250): 4221 -> 3232 SMS frames (MAXSKIP 1), 2902 (MAXSKIP 2).
  Route 764 game frames: ~14500 -> 11422 (367 pictures dropped). VM == oracle; VRAM/diff checks clean on shown frames.

## Wider page-state cache from simulated play
- tools/mkvariants.py: random play variants (recorded route up to a random frame + random input segments, or random
  play from the start). The oracle runs ~1200 of them in a few minutes.
- Oracle additions (bgcap.inc): BGSEEN (keys already captured, read + appended: cross-run dedup), BGKEYS (log every
  candidate state key, repeats included), BGPREFIX (log every prefix key + polygons), BGWANT (store pages for wanted keys).
- Selection: states seen in the most independent runs (tools/selvar.py for thresholds; top 3500 of 1200 runs used).
  Ranking prefixes by runs x polygons (tools/selprefix.py) did NOT help on held-out play.
- Key table now spans several banks: filter bank (bitmap + per-bank first keys) + key banks of 2730 entries (mkbg.py,
  bg_lookup). ROM: 5403 states, banks 150-245.
- Held-out free play (never in the capture set), game frames 100-250: no skip 4217 -> 3851 SMS frames (hits 105 -> 164);
  default (frame skip) 3232 -> 3010. Ceiling analysis: base ROM already covers 52% of cacheable polygons in that play;
  all 300-run prefixes would give 81% but need ~30k states (does not fit).

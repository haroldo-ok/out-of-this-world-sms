/*
 * Another World / Out of this World engine for the Sega Master System.
 * VM: port of the original bytecode interpreter (rawgl Script).
 * Graphics: the 1991 DOS polygon renderer, retargeted to a 208x136 window made of
 * 442 tiles; the four 320x200 pages are arrays of copy-on-write tile references
 * whose pixels live in 32 KB of cartridge RAM.
 */
#include <stdint.h>
#include <string.h>
#include "SMSlib.h"
#include "engine.h"
#include "../gen/gamedata.h"
#include "../gen/bgdata.h"

__sfr __at 0xBF VDPC;
__sfr __at 0xBE VDPD;
__sfr __at 0x7F PSGP;
#define T0(i)
#define T1(i)
#define MAPCTL (*(volatile uint8_t*)0xFFFC)
#define MAP2   (*(volatile uint8_t*)0xFFFF)

/* ------------------------------------------------------------------ slot 2 */
/* 0..0xFD: ROM bank, 0xFE/0xFF: cartridge RAM half 0/1 */
volatile uint8_t slot2 = 0xFD;
#define map_rom(b) do { uint8_t _mb = (b); if (slot2 != _mb) { slot2 = _mb; MAPCTL = 0; MAP2 = _mb; } } while (0)
#define map_sram(h) do { uint8_t _mh = (h), _mv = 0xFE | _mh; if (slot2 != _mv) { slot2 = _mv; MAPCTL = 0x08 | (_mh << 2); } } while (0)
void slot2_apply(uint8_t s) { if (s >= 0xFE) MAPCTL = 0x08 | ((s & 1) << 2); else { MAPCTL = 0; MAP2 = s; } }

/* ------------------------------------------------------------------ state */
#define W 208
#define H 136
#define CW 26
#define NCELL 442
#define NT_BLANK 442

int16_t vars[256];
uint16_t tpc[2][64];
uint8_t tst[2][64];
uint16_t pc, stk[16];
static uint8_t sp_, paused;
static uint16_t next_part;
static uint8_t ending_hit;   /* demo story variable 0x2A reached 6: the capture ending plays as FMV */
uint16_t game_frames, gfx_alloc_fail, vm_bad_op;
volatile uint16_t ticks;
static uint16_t last_disp;
/* 16002 opens with the tunnel fall, shown by the intro FMV: run those displays without drawing */
#define FF_DISPLAYS 0
uint8_t ff;

uint16_t page[4][NCELL];          /* also lent to the FMV player as scratch */
static uint8_t rcg[1024];      /* bits 0-2 refcount, bits 3-7 generation */
/* tile ids: 0-15 solid colour, 16-1023 cartridge-RAM tiles (refcounted), >= 1024 read-only ROM tiles */
#define IS_RAMT(t) ((t) >= 16 && (t) < 1024)
#define REF(t) do { if (IS_RAMT(t)) rcg[t]++; } while (0)
/* what each VRAM slot holds (id | gen << 10): kept in cartridge RAM, in the last 32 tile slots of half 1 */
#define SCR_TILE 992
#define scr ((uint16_t *)(0x8000 + ((SCR_TILE & 511) << 5)))
static uint16_t alloc_ptr;
uint8_t buf[3];         /* rawgl Video::_buffers: [0] work, [1] front, [2] back */
static uint8_t next_pal, cur_pal;
static uint8_t tbuf[32], tbuf2[32];

#define TADDR(t) ((uint8_t *)(0x8000 + (((t) & 511) << 5)))

uint8_t pending(uint8_t p);
void bg_force(uint8_t i);
void bg_use(uint8_t p);
/* ------------------------------------------------------------------ tiles */
void solid(uint8_t c, uint8_t *d) {
  uint8_t i, p0 = (c & 1) ? 0xFF : 0, p1 = (c & 2) ? 0xFF : 0, p2 = (c & 4) ? 0xFF : 0, p3 = (c & 8) ? 0xFF : 0;
  for (i = 0; i < 8; i++) { *d++ = p0; *d++ = p1; *d++ = p2; *d++ = p3; }
}
void load_tile(uint16_t t, uint8_t *d) {
  if (t < 16) solid((uint8_t)t, d);
  else if (t >= 1024) { uint16_t r = t - 1024; map_rom(BGT_BANK0 + (r >> 9)); memcpy(d, (const uint8_t *)(0x8000 + ((r & 511) << 5)), 32); }
  else { map_sram(t >> 9); memcpy(d, TADDR(t), 32); }
}
void unref(uint16_t t) { if (IS_RAMT(t)) rcg[t]--; }
uint16_t alloc_tile(void) {
  uint16_t n = SCR_TILE - 16, p = alloc_ptr;
  while (n--) {
    if (!(rcg[p] & 7)) {
      rcg[p] = ((rcg[p] & 0xF8) + 8) | 1;
      alloc_ptr = (p + 1 >= SCR_TILE) ? 16 : p + 1;
      return p;
    }
    if (++p >= SCR_TILE) p = 16;
  }
  gfx_alloc_fail++;
  return 0xFFFF;
}
/* ---- per-page change tracking: rows of cells modified since the page was a plain copy of pbase[p] */
const uint8_t row_of[442] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16};
uint8_t pbase[4] = {0xFF, 0xFF, 0xFF, 0xFF}, prow[4][17];
void vreg(uint8_t r, uint8_t v);
int8_t pscroll[4];            /* vertical shake offset (pixels, down) shown with the VDP scroll register */
uint8_t vscroll_cur;
uint16_t pgen[4], pbgen[4];
/* drow[p][r]: row r of page p changed since p was last displayed; rowstamp[r]: display number of the
   last upload into screen row r; lastdisp[p]: display number when p was last shown */
uint16_t diff_missed, diff_cell;
uint8_t drow[4][17]; uint16_t rowstamp[17], lastdisp[4], dispcount;
#define TOUCH(p, r) do { pgen[p]++; prow[p][r] = 1; drow[p][r] = 1; } while (0)
void page_all(uint8_t p) { pgen[p]++; pbase[p] = 0xFF; memset(drow[p], 1, 17); }
extern uint8_t pend[4];
void materialize(uint8_t q);
void src_changing(uint8_t p);
/* make page[pg][cell] private and return its tile id (0xFFFF when the pool is full) */
uint16_t writable(uint8_t pg, uint16_t cell) {
  uint16_t *pp = page[pg] + cell, t = *pp, n;
  TOUCH(pg, row_of[cell]);
  if (IS_RAMT(t) && (rcg[t] & 7) == 1) { rcg[t] += 8; return t; }
  n = alloc_tile();
  if (n == 0xFFFF) return n;
  load_tile(t, tbuf);
  map_sram(n >> 9); memcpy(TADDR(n), tbuf, 32);
  unref(t); *pp = n;
  return n;
}
/* store a freshly built 32-byte tile into a cell (detecting solid colours) */
void store_tile(uint8_t pg, uint16_t cell, const uint8_t *d) {
  uint8_t i, c = 0, ok = 1; uint16_t n;
  TOUCH(pg, row_of[cell]);
  for (i = 0; i < 4; i++) {
    if (d[i] == 0xFF) c |= 1 << i; else if (d[i] != 0) { ok = 0; break; }
  }
  if (ok) for (i = 4; i < 32; i++) if (d[i] != d[i & 3]) { ok = 0; break; }
  if (ok) { unref(page[pg][cell]); page[pg][cell] = c; return; }
  n = alloc_tile();
  if (n == 0xFFFF) return;
  map_sram(n >> 9); memcpy(TADDR(n), d, 32);
  unref(page[pg][cell]); page[pg][cell] = n;
}

/* ------------------------------------------------------------------ pages */
uint8_t get_page(uint8_t p) {
  if (p <= 3) return p;
  if (p == 0xFF) return buf[2];
  if (p == 0xFE) return buf[1];
  return 0;
}
uint8_t fl_color; uint16_t *fl_ptr;
void fill_loop(void) __naked {
  __asm
    ld hl,(_fl_ptr)
    ld bc,#442
00001$:
    ld a,(_fl_color)
    cp (hl)
    jr nz,00002$
    inc hl
    ld a,(hl)
    dec hl
    or a
    jr z,00003$
00002$:
    push bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    dec hl
    call _cpr_unref
    ld a,(_fl_color)
    ld (hl),a
    inc hl
    ld (hl),#0
    dec hl
    pop bc
00003$:
    inc hl
    inc hl
    dec bc
    ld a,b
    or c
    jr nz,00001$
    ret
  __endasm;
}
void fill_page(uint8_t pg, uint8_t color) {
  pscroll[pg] = 0;
  src_changing(pg); pend[pg] = 0xFF;
  page_all(pg);
  fl_color = color & 15; fl_ptr = page[pg]; fill_loop();
}
uint16_t *cp_sp, *cp_dp, cp_n = 442;
uint8_t cpr_chg;
void cpr_loop(void);
/* copy src->dst row by row, marking only rows that changed as needing a screen check */
void copy_rows(uint16_t *src, uint8_t dst) {
  uint8_t r; uint16_t *d = page[dst];
  for (r = 0; r < 17; r++, src += CW, d += CW) { cp_sp = src; cp_dp = d; cp_n = CW; cpr_chg = 0; cpr_loop(); if (cpr_chg) drow[dst][r] = 1; }
}
void cpr_loop(void) __naked {
  __asm
    ld hl,(_cp_sp)
    ld de,(_cp_dp)
    ld a,(_cp_n)
    ld b,a
00001$:
    ld a,(de)
    cp (hl)
    jr nz,00010$
    inc hl
    inc de
    ld a,(de)
    cp (hl)
    jr nz,00011$
    inc hl
    inc de
    djnz 00001$
    ret
00011$:
    dec hl
    dec de
00010$:
    ld a,#1
    ld (_cpr_chg),a
    push bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    push hl
    call _cpr_ref
    ld a,(de)
    ld c,a
    inc de
    ld a,(de)
    ld b,a
    push de
    call _cpr_unref
    pop de
    pop hl
    dec hl
    ld a,(hl)
    dec de
    ld (de),a
    inc hl
    inc de
    ld a,(hl)
    ld (de),a
    inc hl
    inc de
    pop bc
    djnz 00001$
    ret
  __endasm;
}
void cpr_ref(void) __naked {
  __asm
    ld a,b
    cp #4
    ret nc
    or a
    jr nz,00001$
    ld a,c
    cp #16
    ret c
00001$:
    push hl
    ld hl,#_rcg
    add hl,bc
    inc (hl)
    pop hl
    ret
  __endasm;
}
void cpr_unref(void) __naked {
  __asm
    ld a,b
    cp #4
    ret nc
    or a
    jr nz,00001$
    ld a,c
    cp #16
    ret c
00001$:
    push hl
    ld hl,#_rcg
    add hl,bc
    dec (hl)
    pop hl
    ret
  __endasm;
}
/* ---- lazy page copies: copy_page_refs() only records pend[dst] = src; the reference copy is done when
   the destination is read or drawn into, or its source is about to change. A full cache hit on the
   destination replaces the whole page, so the copy is then dropped without ever being made. */
uint8_t pend[4] = {0xFF, 0xFF, 0xFF, 0xFF};
void copy_now(uint8_t src, uint8_t dst);
void materialize(uint8_t q) {
  uint8_t s = pend[q];
  if (s == 0xFF) return;
  pend[q] = 0xFF;
  materialize(s);
  copy_now(s, q);
}
void src_changing(uint8_t p) { uint8_t q; for (q = 0; q < 4; q++) if (pend[q] == p) materialize(q); }
void copy_page_refs(uint8_t src, uint8_t dst) {
  if (src == dst) return;
  pscroll[dst] = pscroll[src];
  materialize(src);
  src_changing(dst);
  pend[dst] = src;
}
void copy_now(uint8_t src, uint8_t dst) {
  uint8_t r;
  if (src == dst) return;
  if (pbase[dst] == src && pbgen[dst] == pgen[src]) {     /* dst = src except the rows touched since */
    for (r = 0; r < 17; r++) if (prow[dst][r]) {
      cp_sp = page[src] + (uint16_t)r * CW; cp_dp = page[dst] + (uint16_t)r * CW; cp_n = CW; cpr_loop();
      prow[dst][r] = 0; drow[dst][r] = 1;
    }
  } else {
    copy_rows(page[src], dst);
    memset(prow[dst], 0, 17);
  }
  pbase[dst] = src; pbgen[dst] = pgen[src]; pgen[dst]++;
}
void get_row(uint16_t t, uint8_t r, uint8_t *o) {
  const uint8_t *p;
  if (t < 16) {
    o[0] = (t & 1) ? 0xFF : 0; o[1] = (t & 2) ? 0xFF : 0; o[2] = (t & 4) ? 0xFF : 0; o[3] = (t & 8) ? 0xFF : 0; return;
  }
  if (t >= 1024) { uint16_t q = t - 1024; map_rom(BGT_BANK0 + (q >> 9)); p = (const uint8_t *)(0x8000 + ((q & 511) << 5)) + (r << 2); }
  else { map_sram(t >> 9); p = TADDR(t) + (r << 2); }
  o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = p[3];
}
/* copyBuffer with a vertical offset: dst rows dy.. come from src rows 0.. (rows outside keep dst) */
void copy_page_scroll(uint8_t src, uint8_t dst, int16_t dy) {
  uint8_t cy, cx, r; int16_t y, sy; uint16_t cell;
  materialize(src); src_changing(dst); materialize(dst);
  page_all(dst);
  if ((dy & 7) == 0) {
    /* whole-cell shift: pure reference moves */
    int8_t dc = dy >> 3;
    for (cy = 0; cy < 17; cy++) {
      int8_t scy = (int8_t)cy - dc;
      if (scy < 0 || scy >= 17) continue;
      for (cx = 0; cx < CW; cx++) {
        uint16_t t = page[src][scy * CW + cx]; cell = cy * CW + cx;
        REF(t);
        unref(page[dst][cell]); page[dst][cell] = t;
      }
    }
    return;
  }
  /* rows are rebuilt bottom-up or top-down so in-place (src==dst never happens: rawgl skips it) */
  for (cy = 0; cy < 17; cy++) {
    for (cx = 0; cx < CW; cx++) {
      cell = cy * CW + cx;
      for (r = 0; r < 8; r++) {
        y = cy * 8 + r; sy = y - dy;
        if (sy >= 0 && sy < H) get_row(page[src][(sy >> 3) * CW + cx], sy & 7, tbuf2 + (r << 2));
        else get_row(page[dst][cell], r, tbuf2 + (r << 2));
      }
      store_tile(dst, cell, tbuf2);
    }
  }
}

/* ------------------------------------------------------------------ spans */
static uint8_t src4[4];
static uint8_t sk0 = 1, sk1 = 0;   /* tile columns handled by band_full(): skip in span() */


/* ---- fast span: planar masked write done in asm; colour plane bytes preloaded by set_span_color() */
uint8_t cp0, cp1, cp2, cp3, g_m;
static uint8_t span_col = 0xFF;
void set_span_color(uint8_t c) {
  span_col = c;
  if (c < 16) { cp0 = (c & 1) ? 0xFF : 0; cp1 = (c & 2) ? 0xFF : 0; cp2 = (c & 4) ? 0xFF : 0; cp3 = (c & 8) ? 0xFF : 0; }
}
/* hl = pointer to the 4 plane bytes of one tile row: row = ((row ^ cp) & m) ^ row */
void plane_write(uint8_t *p) __naked __sdcccall(1) {
  (void)p;
  __asm
    ld a,(_g_m)
    ld c,a
    ld a,(_cp0)
    ld b,(hl)
    xor b
    and c
    xor b
    ld (hl),a
    inc hl
    ld a,(_cp1)
    ld b,(hl)
    xor b
    and c
    xor b
    ld (hl),a
    inc hl
    ld a,(_cp2)
    ld b,(hl)
    xor b
    and c
    xor b
    ld (hl),a
    inc hl
    ld a,(_cp3)
    ld b,(hl)
    xor b
    and c
    xor b
    ld (hl),a
    ret
  __endasm;
}
static const uint8_t lmask[8] = {0xFF,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01};
static const uint8_t rmask[8] = {0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xFF};
void span(int16_t xa, int16_t xb, uint8_t y, uint8_t color) {
  uint8_t tx, tl, tx0, r, work = buf[0];
  uint16_t *pp, t, cell; uint8_t *p;
  if (xa > xb) { int16_t k = xa; xa = xb; xb = k; }
  if (xa < 0) xa = 0;
  if (xb >= W) xb = W - 1;
  if (xa > xb || y >= H) return;
  if (color == 0x11 && work == 0) return;
  if (color != span_col) set_span_color(color);
  tx0 = tx = (uint8_t)xa >> 3; tl = (uint8_t)xb >> 3; r = (y & 7) << 2;
  cell = (uint16_t)(y >> 3) * CW + tx;
  pp = page[work] + cell;
  for (; tx <= tl; tx++, cell++, pp++) {
    if (tx >= sk0 && tx <= sk1) continue;
    g_m = 0xFF;
    if (tx == tx0) g_m = lmask[(uint8_t)xa & 7];
    if (tx == tl) g_m &= rmask[(uint8_t)xb & 7];
    t = *pp;
    if (color < 16) {
      if (t == color) continue;
    } else if (color == 0x11) {
      get_row(page[0][cell], r >> 2, src4);
    }
    if (IS_RAMT(t) && (rcg[t] & 7) == 1) rcg[t] += 8;
    else { t = writable(work, cell); if (t == 0xFFFF) continue; }
    map_sram(t >> 9);
    p = TADDR(t) + r;
    if (color < 16) plane_write(p);
    else if (color == 0x10) p[3] |= g_m;
    else { uint8_t m = g_m; p[0] = (p[0] & ~m) | (src4[0] & m); p[1] = (p[1] & ~m) | (src4[1] & m); p[2] = (p[2] & ~m) | (src4[2] & m); p[3] = (p[3] & ~m) | (src4[3] & m); }
  }
}
/* ------------------------------------------------------------------ bands */
/* Polygon scanlines are collected per 8-line band; tile columns that every line of the band
   covers completely become a reference change (solid tile / page-0 tile) with no pixel work. */
uint8_t bxa[8], bxb[8];
uint8_t bmask, bcolor;
int16_t band_y0 = -1;
const uint8_t bittab[8] = {1, 2, 4, 8, 16, 32, 64, 128};
void band_full(uint8_t ty, uint8_t f0, uint8_t f1) {
  uint8_t work = buf[0], tx, r; uint16_t cell = (uint16_t)ty * CW + f0, t, *pp = page[work];
  for (tx = f0; tx <= f1; tx++, cell++) {
    t = pp[cell];
    if (bcolor < 16) { if (t != bcolor) { unref(t); pp[cell] = bcolor; } }
    else if (bcolor == 0x10) {
      if (t < 16) pp[cell] = t | 8;
      else { t = writable(work, cell); if (t == 0xFFFF) continue; map_sram(t >> 9); { uint8_t *p = TADDR(t) + 3; for (r = 0; r < 8; r++, p += 4) *p = 0xFF; } }
    } else if (work != 0) {
      uint16_t s2 = page[0][cell];
      if (s2 != t) { REF(s2); unref(t); pp[cell] = s2; }
    }
  }
}
void band_flush_(void);
void band_flush(void) { T0(2); band_flush_(); T1(2); }
static uint8_t rowm[8], smp[4];
uint8_t *ar_p;
/* rows of the tile at ar_p: b = b ^ ((b ^ fill[plane]) & rowm[row]); mask 0xFF rows are plain stores */
void apply_rows(void) __naked {
  __asm
    ld hl,(_ar_p)
    ld de,#_rowm
    ld b,#8
00001$:
    ld a,(de)
    inc de
    or a
    jr nz,00002$
    inc hl
    inc hl
    inc hl
    inc hl
    djnz 00001$
    ret
00002$:
    ld c,a
    inc a
    jr nz,00003$
    ld a,(_smp+0)
    ld (hl),a
    inc hl
    ld a,(_smp+1)
    ld (hl),a
    inc hl
    ld a,(_smp+2)
    ld (hl),a
    inc hl
    ld a,(_smp+3)
    ld (hl),a
    inc hl
    djnz 00001$
    ret
00003$:
    ld a,(_smp+0)
    xor (hl)
    and c
    xor (hl)
    ld (hl),a
    inc hl
    ld a,(_smp+1)
    xor (hl)
    and c
    xor (hl)
    ld (hl),a
    inc hl
    ld a,(_smp+2)
    xor (hl)
    and c
    xor (hl)
    ld (hl),a
    inc hl
    ld a,(_smp+3)
    xor (hl)
    and c
    xor (hl)
    ld (hl),a
    inc hl
    djnz 00001$
    ret
  __endasm;
}
/* Resolve one 8-line band per tile column: fully covered tiles become reference changes,
   partial tiles get one copy-on-write and all their rows written at once. */
uint8_t mm[8][CW];                 /* per-band coverage masks, kept all-zero between flushes */
const uint8_t lmt[8] = {0xFF,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01};
const uint8_t rmt[8] = {0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xFF};
/* phase 1 of a band flush: write each present line's coverage into mm[] and find
   the touched column range [bf_txa, bf_txb] and the common interval [bf_A, bf_B] */
uint8_t bf_txa, bf_txb, bf_A, bf_B;
void band_lines(void) __naked {
  __asm
    ld a,#26
    ld (_bf_txa),a
    xor a
    ld (_bf_txb),a
    ld (_bf_A),a
    dec a
    ld (_bf_B),a
    ld c,#0
    ld hl,#_mm
00010$:
    push hl
    ld hl,#_bittab
    ld b,#0
    add hl,bc
    ld a,(_bmask)
    and (hl)
    pop hl
    jp z,00019$
    push hl
    ld hl,#_bxa
    add hl,bc
    ld d,(hl)
    ld hl,#_bxb
    add hl,bc
    ld e,(hl)
    pop hl
    ld a,(_bf_A)
    cp d
    jr nc,00011$
    ld a,d
    ld (_bf_A),a
00011$:
    ld a,(_bf_B)
    cp e
    jr c,00012$
    ld a,e
    ld (_bf_B),a
00012$:
    push bc
    push hl
    ld a,d
    rrca
    rrca
    rrca
    and #0x1F
    ld b,a
    ld a,e
    rrca
    rrca
    rrca
    and #0x1F
    ld c,a
    ld a,(_bf_txa)
    cp b
    jr c,00013$
    ld a,b
    ld (_bf_txa),a
00013$:
    ld a,(_bf_txb)
    cp c
    jr nc,00014$
    ld a,c
    ld (_bf_txb),a
00014$:
    ld a,l
    add a,b
    ld l,a
    jr nc,00015$
    inc h
00015$:
    push hl
    ld hl,#_lmt
    ld a,d
    and #7
    add a,l
    ld l,a
    jr nc,00021$
    inc h
00021$:
    ld d,(hl)
    ld hl,#_rmt
    ld a,e
    and #7
    add a,l
    ld l,a
    jr nc,00022$
    inc h
00022$:
    ld e,(hl)
    pop hl
    ld a,c
    sub b
    jr nz,00016$
    ld a,d
    and e
    ld (hl),a
    jr 00018$
00016$:
    ld (hl),d
    inc hl
    dec a
    jr z,00017$
    ld b,a
00020$:
    ld (hl),#0xFF
    inc hl
    djnz 00020$
00017$:
    ld (hl),e
00018$:
    pop hl
    pop bc
00019$:
    ld de,#26
    add hl,de
    inc c
    ld a,c
    cp #8
    jp nz,00010$
    ret
  __endasm;
}
void band_flush_(void) {
  uint8_t l, tx, txa = CW, txb = 0, work = buf[0], m, r, an, orr, a, b, ta, tb;
  uint16_t cell, t, *pp; uint8_t *row, f0 = 1, f1 = 0, A = 0, B = 255;
  if (!bmask) { band_y0 = -1; return; }
  band_lines(); txa = bf_txa; txb = bf_txb; A = bf_A; B = bf_B;
  TOUCH(work, (uint8_t)(band_y0 >> 3));
  if (bcolor < 16) { smp[0] = (bcolor & 1) ? 0xFF : 0; smp[1] = (bcolor & 2) ? 0xFF : 0; smp[2] = (bcolor & 4) ? 0xFF : 0; smp[3] = (bcolor & 8) ? 0xFF : 0; }
  if (bmask == 0xFF && B >= A) { f0 = (A + 7) >> 3; f1 = ((uint16_t)B + 1) >> 3; if (f1) f1--; else f0 = 1; }
  cell = (uint16_t)(band_y0 >> 3) * CW + txa; pp = &page[work][cell];
  for (tx = txa; tx <= txb; tx++, cell++, pp++) {
    if (tx >= f0 && tx <= f1) {                /* whole column covered: reference-only fast path */
      t = *pp;
      if (bcolor < 16) { if (t != bcolor) { unref(t); *pp = bcolor; } continue; }
      if (bcolor == 0x11) { uint16_t s2 = page[0][cell]; if (s2 != t) { REF(s2); unref(t); *pp = s2; } continue; }
      if (t < 16) { *pp = t | 8; continue; }
      for (l = 0; l < 8; l++) rowm[l] = 0xFF;
      goto partial;
    }
    an = 0xFF; orr = 0;
    for (l = 0; l < 8; l++) { m = mm[l][tx]; rowm[l] = m; an &= m; orr |= m; }
    if (!orr) continue;
    t = *pp;
    if (an == 0xFF) {
      if (bcolor < 16) { if (t != bcolor) { unref(t); *pp = bcolor; } continue; }
      if (bcolor == 0x11) { uint16_t s2 = page[0][cell]; if (s2 != t) { REF(s2); unref(t); *pp = s2; } continue; }
      if (t < 16) { *pp = t | 8; continue; }
    }
    if (bcolor < 16 && t == bcolor) continue;
  partial:
    if (bcolor == 0x11) load_tile(page[0][cell], tbuf2);
    t = writable(work, cell);
    if (t == 0xFFFF) continue;
    map_sram(t >> 9);
    ar_p = TADDR(t);
    if (bcolor < 16) apply_rows();
    else if (bcolor == 0x10) { uint8_t *p = ar_p + 3; for (r = 0; r < 8; r++, p += 4) *p |= rowm[r]; }
    else { uint8_t *p = ar_p, *q = tbuf2; for (r = 0; r < 8; r++) { m = rowm[r]; for (l = 0; l < 4; l++, p++, q++) *p = (*p & ~m) | (*q & m); } }
  }
  { uint8_t *rp = &mm[0][txa], nclr = txb - txa + 1; for (l = 0; l < 8; l++, rp += CW) { uint8_t *q = rp, k2 = nclr; do *q++ = 0; while (--k2); } }
  bmask = 0; band_y0 = -1;
}
void band_put(int16_t a, int16_t b, uint8_t y) {
  if ((int16_t)(y & 0xF8) != band_y0) { band_flush(); band_y0 = y & 0xF8; }
  if (a > b) { int16_t k = a; a = b; b = k; }
  bxa[y & 7] = a; bxb[y & 7] = b; bmask |= 1 << (y & 7);
}

/* ------------------------------------------------------------------ polygons */
int16_t tx_(int16_t x) {
  if (x >= LUTX_MIN && x <= LUTX_MAX) { map_rom(LUT_BANK); return ((const int16_t *)0x8000)[x - LUTX_MIN]; }
  return (int16_t)(((int32_t)x * 13 - ((x < 0) ? 19 : 0)) / 20);
}
int16_t ty_(int16_t y) {
  if (y >= LUTY_MIN && y <= LUTY_MAX) { map_rom(LUT_BANK); return ((const int16_t *)(0x8000 + LUTY_OFF))[y - LUTY_MIN]; }
  return (int16_t)(((int32_t)y * 17 - ((y < 0) ? 24 : 0)) / 25);
}
int16_t vx[72], vy[72];
uint16_t vc_err;
/* zoom-64 vertex conversion with the coordinate LUT mapped at 0x8000:
   vx[i] = LUTX[x1 + raw[2i]], vy[i] = LUTY[y1 + raw[2i+1]]; raw lives in vy[] (read before written).
   vc_oob is set if any coordinate falls outside the tables (caller then uses the C path). */
uint8_t *vc_raw; uint8_t vc_n, vc_oob; int16_t vc_x1, vc_y1; int16_t *vc_py;
void vconv(void) __naked {
  __asm
    xor a
    ld (_vc_oob),a
    ld hl,#_vy
    ld (_vc_py),hl
    ld de,#_vx
    ld hl,(_vc_raw)
    ld a,(_vc_n)
    ld b,a
00001$:
    push bc
    ld c,(hl)
    inc hl
    ld a,(hl)
    inc hl
    push hl
    push af
    ld b,#0
    ld hl,(_vc_x1)
    add hl,bc
    ld bc,#512
    add hl,bc
    ld a,h
    cp #6
    jr c,00002$
    ld a,#1
    ld (_vc_oob),a
    ld hl,#0
00002$:
    add hl,hl
    ld bc,#0x8000
    add hl,bc
    ld a,(hl)
    ld (de),a
    inc hl
    inc de
    ld a,(hl)
    ld (de),a
    inc de
    pop af
    ld c,a
    ld b,#0
    ld hl,(_vc_y1)
    add hl,bc
    ld bc,#384
    add hl,bc
    ld a,h
    cp #4
    jr c,00003$
    ld a,#1
    ld (_vc_oob),a
    ld hl,#0
00003$:
    add hl,hl
    ld bc,#0x8C00
    add hl,bc
    push de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld hl,(_vc_py)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld (_vc_py),hl
    pop de
    pop hl
    pop bc
    djnz 00001$
    ret
  __endasm;
}
static uint16_t poly_off; static uint8_t poly_bank0;
uint8_t pfetch(void) {
  uint8_t v;
  map_rom(poly_bank0 + (poly_off >> 14));
  v = *(const uint8_t *)(0x8000 | (poly_off & 0x3FFF));
  poly_off++;
  return v;
}
/* DEHL = DE * BC (unsigned 16x16 -> 32), classic shift-add */
uint16_t mul_a, mul_b; uint32_t mul_r;
void mul16u(void) __naked {
  __asm
    ld de,(_mul_a)
    ld bc,(_mul_b)
    ld hl,#0
    ld a,#16
00001$:
    add hl,hl
    rl e
    rl d
    jr nc,00002$
    add hl,bc
    jr nc,00002$
    inc de
00002$:
    dec a
    jr nz,00001$
    ld (_mul_r),hl
    ld (_mul_r+2),de
    ret
  __endasm;
}
int32_t calc_step(uint8_t a, uint8_t b, uint16_t *dy) {
  uint16_t d = (uint16_t)(vy[b] - vy[a]), delta = (d <= 1) ? 1 : d, rc;
  *dy = d;
  rc = (delta < 256) ? recip[delta] : (uint16_t)(0x4000 / delta);
  { int16_t dx = vx[b] - vx[a]; uint8_t neg = dx < 0;
    mul_a = neg ? (uint16_t)(-dx) : (uint16_t)dx; mul_b = rc; mul16u();
    return neg ? -(int32_t)(mul_r << 2) : (int32_t)(mul_r << 2); }
}
/* ---- scanline stepper: for dp_h lines, record [hi(c1),hi(c2)] clipped to the window into the band arrays */
uint32_t dp_c1, dp_c2; int32_t dp_s1, dp_s2; uint16_t dp_h; int16_t dp_y; uint8_t dp_stop;
void band_flush(void);
void band_newband(void) { band_flush(); band_y0 = dp_y & 0xF8; }
void scan_lines(void) __naked {
  __asm
00001$:
    ld hl,(_dp_h)
    ld a,h
    or l
    ret z
    dec hl
    ld (_dp_h),hl
    ld de,(_dp_c1+2)
    ld bc,(_dp_c2+2)
    ld a,d
    bit 7,a
    jr nz,00010$
    or a
    jp nz,00050$
    ld a,e
    cp #208
    jp nc,00050$
00010$:
    bit 7,b
    jp nz,00050$
    bit 7,d
    jr z,00011$
    ld de,#0
00011$:
    ld a,b
    or a
    jr nz,00012$
    ld a,c
    cp #208
    jr c,00013$
00012$:
    ld bc,#207
00013$:
    ld a,c
    cp e
    jr nc,00014$
    ld c,e
    ld e,a
00014$:
    ld a,(_dp_y)
    and #0xF8
    ld hl,#_band_y0
    cp (hl)
    jr nz,00020$
    inc hl
    ld a,(hl)
    or a
    jr z,00030$
00020$:
    push bc
    push de
    call _band_newband
    pop de
    pop bc
00030$:
    ld a,(_dp_y)
    and #7
    ld b,a
    ld hl,#_bxa
    add a,l
    ld l,a
    jr nc,00031$
    inc h
00031$:
    ld (hl),e
    ld a,b
    ld hl,#_bxb
    add a,l
    ld l,a
    jr nc,00032$
    inc h
00032$:
    ld (hl),c
    ld a,b
    ld hl,#_bittab
    add a,l
    ld l,a
    jr nc,00033$
    inc h
00033$:
    ld a,(_bmask)
    or (hl)
    ld (_bmask),a
00050$:
    ld hl,(_dp_c1)
    ld de,(_dp_s1)
    add hl,de
    ld (_dp_c1),hl
    ld hl,(_dp_c1+2)
    ld de,(_dp_s1+2)
    adc hl,de
    ld (_dp_c1+2),hl
    ld hl,(_dp_c2)
    ld de,(_dp_s2)
    add hl,de
    ld (_dp_c2),hl
    ld hl,(_dp_c2+2)
    ld de,(_dp_s2+2)
    adc hl,de
    ld (_dp_c2+2),hl
    ld hl,(_dp_y)
    inc hl
    ld (_dp_y),hl
    ld a,l
    cp #136
    jp c,00001$
    ld a,#1
    ld (_dp_stop),a
    ret
  __endasm;
}
void draw_polygon_(uint8_t color, uint8_t n);
void draw_polygon(uint8_t color, uint8_t n) { T0(3); draw_polygon_(color, n); T1(3); }
void draw_polygon_(uint8_t color, uint8_t n) {
  uint8_t i = 0, j = n - 1;
  int16_t x1 = vx[j], x2 = vx[0], y = (vy[0] < vy[j]) ? vy[0] : vy[j];
  uint32_t c1, c2; int32_t s1, s2; uint16_t h;
  int8_t nv = n;
  ++i; --j;
  bcolor = color; bmask = 0; band_y0 = -1;
  if (color == 0x11 && buf[0] == 0) return;
  c1 = (uint32_t)(int32_t)x1 << 16; c2 = (uint32_t)(int32_t)x2 << 16;
  for (;;) {
    nv -= 2;
    if (nv <= 0) { band_flush(); return; }
    s1 = calc_step(j + 1, j, &h);
    s2 = calc_step(i - 1, i, &h);
    ++i; --j;
    c1 = (c1 & 0xFFFF0000UL) | 0x7FFF;
    c2 = (c2 & 0xFFFF0000UL) | 0x8000;
    if (h == 0) { c1 += s1; c2 += s2; }
    else {
      if (y < 0) {                      /* skip scanlines above the window in one step */
        uint16_t k = (uint16_t)(-y); if (k > h) k = h;
        c1 += s1 * (int32_t)k; c2 += s2 * (int32_t)k; y += k; h -= k;
      }
      if (h) {
        dp_c1 = c1; dp_c2 = c2; dp_s1 = s1; dp_s2 = s2; dp_h = h; dp_y = y; dp_stop = 0;
        scan_lines();
        c1 = dp_c1; c2 = dp_c2; y = dp_y;
        if (dp_stop) { band_flush(); return; }
      }
    }
  }
}
void plot(int16_t ax, int16_t ay, uint8_t color) {
  int16_t x = tx_(ax), y = ty_(ay);
  if (x >= 0 && x < W && y >= 0 && y < H) span(x, x, (uint8_t)y, color);
}
#define SCL(v, z) ((z) == 64 ? (int16_t)(v) : ((z) < 256 ? (int16_t)(((uint16_t)(v) * (z)) >> 6) : (int16_t)(((uint32_t)(v) * (z)) >> 6)))
void bg_force(uint8_t i);
void fill_polygon(uint8_t color, uint16_t zoom, int16_t px, int16_t py) {
  if (color == 0x11 && buf[0] != 0) { if (pending(0)) bg_force(0); materialize(0); }
  uint16_t bbw = SCL(pfetch(), zoom), bbh = SCL(pfetch(), zoom);
  int16_t x1 = px - bbw / 2, x2 = px + bbw / 2, y1 = py - bbh / 2, y2 = py + bbh / 2;
  uint8_t n, i;
  if (x1 > 319 || x2 < 0 || y1 > 199 || y2 < 0) return;
  n = pfetch();
  if ((n & 1) || n > 70) return;
  {
    uint8_t *raw = (uint8_t *)vy, k = n * 2, part;          /* raw vertex bytes staged in vy[] */
    uint16_t o = poly_off;
    while (k) {                                              /* copy, splitting at a bank boundary */
      uint16_t room = 0x4000 - (o & 0x3FFF);
      part = (room < k) ? (uint8_t)room : k;
      map_rom(poly_bank0 + (o >> 14));
      memcpy(raw, (const uint8_t *)(0x8000 | (o & 0x3FFF)), part);
      raw += part; o += part; k -= part;
    }
    poly_off = o; raw = (uint8_t *)vy;
    map_rom(LUT_BANK);
    if (zoom == 64 && n) {
      vc_raw = raw; vc_n = n; vc_x1 = x1; vc_y1 = y1;
#ifdef VCHECK
      { static uint8_t cpy[144]; static int16_t sx[72], sy[72]; uint8_t q; memcpy(cpy, raw, n * 2);
        vconv();
        memcpy(sx, vx, n * 2); memcpy(sy, vy, n * 2); memcpy(raw, cpy, n * 2);
        if (!vc_oob) { for (q = 0; q < n; q++) { int16_t ax = x1 + cpy[q * 2], ay = y1 + cpy[q * 2 + 1];
            if (sx[q] != tx_(ax) || sy[q] != ty_(ay)) vc_err++; } map_rom(LUT_BANK); }
        memcpy(raw, cpy, n * 2); vconv(); }
#else
      vconv();
#endif
      if (!vc_oob) goto converted;
      raw = (uint8_t *)vy;   /* raw bytes were overwritten: re-copy and use the C path */
      { uint8_t k2 = n * 2; uint16_t o2 = poly_off - k2; uint8_t *r2 = raw;
        while (k2) { uint16_t room = 0x4000 - (o2 & 0x3FFF); uint8_t pt = (room < k2) ? (uint8_t)room : k2;
          map_rom(poly_bank0 + (o2 >> 14)); memcpy(r2, (const uint8_t *)(0x8000 | (o2 & 0x3FFF)), pt); r2 += pt; o2 += pt; k2 -= pt; } }
      map_rom(LUT_BANK);
    }
    for (i = 0; i < n; i++) {
      int16_t ax = x1 + SCL(raw[0], zoom), ay = y1 + SCL(raw[1], zoom);
      raw += 2;
      vx[i] = (ax >= LUTX_MIN && ax <= LUTX_MAX) ? ((const int16_t *)0x8000)[ax - LUTX_MIN] : tx_(ax);
      vy[i] = (ay >= LUTY_MIN && ay <= LUTY_MAX) ? ((const int16_t *)(0x8000 + LUTY_OFF))[ay - LUTY_MIN] : ty_(ay);
      if (slot2 != LUT_BANK) map_rom(LUT_BANK);
    }
  }
converted:
  if (n == 4 && bbw == 0 && bbh <= 1) plot(px, py, color);
  else draw_polygon(color, n);
}
void draw_sprite_mask(uint8_t num, int16_t x, int16_t y, uint8_t color) {
  uint16_t off; uint8_t w, h, j, i, b, words, run0 = 0, inrun; uint16_t msk; int16_t ay, sy, last_sy = -1000;
  if (num >= NMASKS) return;
  off = mask_off[num];
  map_rom(MASK_BANK); w = *(const uint8_t *)(0x8000 + off); h = *(const uint8_t *)(0x8001 + off); off += 2;
  x -= w / 2; y -= h / 2; words = w / 16 + 1;
  for (j = 0; j < h; j++) {
    ay = y + j; sy = ty_(ay);
    if (sy == last_sy || ay < 0 || ay >= 200 || sy < 0 || sy >= H) { off += words * 2; continue; }  /* rows merging into one SMS line */
    last_sy = sy; inrun = 0;
    for (i = 0; i < words; i++) {
      map_rom(MASK_BANK);
      msk = (*(const uint8_t *)(0x8000 + off) << 8) | *(const uint8_t *)(0x8001 + off); off += 2;
      for (b = 0; b < 16; b++, msk <<= 1) {
        uint8_t bit = (msk & 0x8000) != 0, px = i * 16 + b;
        if (bit && !inrun) { inrun = 1; run0 = px; }
        else if (!bit && inrun) { inrun = 0; span(tx_(x + run0), tx_(x + px - 1), (uint8_t)sy, color); }
      }
    }
    if (inrun) span(tx_(x + run0), tx_(x + words * 16 - 1), (uint8_t)sy, color);
  }
}
void draw_shape(uint8_t color, uint16_t zoom, int16_t px, int16_t py);
void draw_shape_parts(uint16_t zoom, int16_t x, int16_t y) {
  int16_t ptx, pty, n;
  ptx = x - SCL(pfetch(), zoom);
  pty = y - SCL(pfetch(), zoom);
  n = pfetch();
  for (; n >= 0; --n) {
    uint16_t off = pfetch() << 8; uint16_t bak; int16_t pox, poy; uint8_t color = 0xFF;
    off |= pfetch();
    pox = ptx + SCL(pfetch(), zoom);
    poy = pty + SCL(pfetch(), zoom);
    if (off & 0x8000) {
      uint8_t num;
      color = pfetch(); num = pfetch();
      if (color & 0x80) { bak = poly_off; draw_sprite_mask(num, pox, poy, color & 0x7F); poly_off = bak; continue; }
      color &= 0x7F;
    }
    off <<= 1;
    bak = poly_off; poly_off = off;
    draw_shape(color, zoom, pox, poy);
    poly_off = bak;
  }
}
void draw_shape(uint8_t color, uint16_t zoom, int16_t px, int16_t py) {
  uint8_t i = pfetch();
  if (i >= 0xC0) { if (color & 0x80) color = i & 0x3F; fill_polygon(color, zoom, px, py); }
  else if ((i & 0x3F) == 2) draw_shape_parts(zoom, px, py);
}

/* ------------------------------------------------------------------ strings */
void draw_string(uint8_t color, uint16_t x, uint16_t y, uint16_t id) {
  uint8_t k; const char *s = 0; uint16_t xx = x;
  for (k = 0; k < NSTRINGS; k++) if (str_id[k] == id) s = str_txt[k];
  if (!s) return;
  for (; *s; s++) {
    if (*s == '\n' || *s == '\r') { y += 8; x = xx; continue; }
    if (x * 8 <= 320 - 8 && y <= 200 - 8) {
      int16_t X = tx_(x * 8), Y = ty_(y); uint8_t r, b; const uint8_t *g = font8 + (uint8_t)(*s - 0x20) * 8;
      for (r = 0; r < 8; r++) for (b = 0; b < 8; b++)
        if (g[r] & (0x80 >> b)) { int16_t px = X + b, py = Y + r; if (px < W && py < H) span(px, px, (uint8_t)py, color); }
    }
    x++;
  }
}

/* ------------------------------------------------------------------ display */
static const uint8_t solid_tiles[16][32] = {
#define S4(c) (((c)&1)?0xFF:0),(((c)&2)?0xFF:0),(((c)&4)?0xFF:0),(((c)&8)?0xFF:0)
#define ST(c) {S4(c),S4(c),S4(c),S4(c),S4(c),S4(c),S4(c),S4(c)}
  ST(0),ST(1),ST(2),ST(3),ST(4),ST(5),ST(6),ST(7),ST(8),ST(9),ST(10),ST(11),ST(12),ST(13),ST(14),ST(15)
};
/* 32 bytes from src to VRAM: unpaced if the V-counter says VBlank has >= 20 lines left, else 28-cycle pacing */
uint16_t ut_addr; const uint8_t *ut_src;
/* scan page cells against what VRAM holds; stop at the first difference (fd_n = 0 when none) */
uint16_t *fd_pp, *fd_sc, fd_n, fd_key;
void find_diff(void) __naked {
  __asm
    ld hl,(_fd_pp)
    ld de,(_fd_sc)
    ld bc,(_fd_n)
00001$:
    ld a,b
    or c
    jr z,00090$
    push bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    dec hl
    ld a,b
    cp #4
    jr nc,00010$
    or a
    jr nz,00020$
    ld a,c
    cp #16
    jr c,00010$
00020$:
    push hl
    ld hl,#_rcg
    add hl,bc
    ld a,(hl)
    and #0xF8
    rrca
    or b
    or #0x80
    ld b,a
    pop hl
00010$:
    ld a,(de)
    cp c
    jr nz,00030$
    inc de
    ld a,(de)
    dec de
    cp b
    jr nz,00030$
    pop bc
    dec bc
    inc hl
    inc hl
    inc de
    inc de
    jr 00001$
00030$:
    ld (_fd_key),bc
    pop bc
    ld (_fd_n),bc
    ld (_fd_pp),hl
    ld (_fd_sc),de
    ret
00090$:
    ld (_fd_n),bc
    ret
  __endasm;
}
void upload_direct(void) __naked {
  __asm
    ld hl,(_ut_addr)
    ld c,#0xBF
    out (c),l
    out (c),h
    ld hl,(_ut_src)
    ld c,#0xBE
    in a,(#0x7E)
    cp #0xC1
    jr c,00020$
    cp #0xEC
    jr nc,00020$
    .rept 32
    outi
    .endm
    ret
00020$:
    .rept 32
    outi
    nop
    nop
    nop
    .endm
    ret
  __endasm;
}
void set_vram(uint16_t a) { VDPC = a & 0xFF; VDPC = (a >> 8) | 0x40; }
void display(uint8_t p) {
  uint16_t c, key, t; uint16_t *pp;
  uint8_t i;
  if (p != 0xFE) {
    if (p == 0xFF) { uint8_t k = buf[1]; buf[1] = buf[2]; buf[2] = k; }
    else buf[1] = get_page(p);
  }
  /* original pacing: VAR(0xFF) slices of 20 ms (1.2 ticks each) since the last display */
  if (vars[0x2A] == 6) { ending_hit = 1; paused = 1; return; }   /* hand over before showing the first ending frame */
  bg_use(buf[1]);
  if (ff) { ff--; if (next_pal != 0xFF) { cur_pal = next_pal; next_pal = 0xFF; } if (!ff) last_disp = ticks; return; }
  if (next_pal == 0xFF && cur_pal != 0xFF && game_frames == 0) next_pal = cur_pal;
  while ((uint16_t)(ticks - last_disp) * 5 < (uint16_t)vars[0xFF] * 6) ;
  if (next_pal != 0xFF) SMS_waitForVBlank();   /* colour RAM is only written in VBlank */
  last_disp = ticks;
  if (next_pal != 0xFF) {
    if (next_pal < 32) { VDPC = 0; VDPC = 0xC0; for (i = 0; i < 16; i++) VDPD = pal_sms[next_pal][i]; cur_pal = next_pal; }
    next_pal = 0xFF;
  }
  materialize(buf[1]);
  { uint8_t v = (uint8_t)(-pscroll[buf[1]]); if (pscroll[buf[1]] > 0) v = (uint8_t)(224 - pscroll[buf[1]]);
    if (v != vscroll_cur) { vscroll_cur = v; vreg(9, v); } }
  {
    uint8_t pg = buf[1], r;
    if (++dispcount == 0) { memset(rowstamp, 0, sizeof(rowstamp)); memset(lastdisp, 0, sizeof(lastdisp)); memset(drow, 1, sizeof(drow)); dispcount = 1; }
    for (r = 0; r < 17; r++) {
      if (!drow[pg][r] && rowstamp[r] <= lastdisp[pg]) continue;     /* screen row already equals this page */
      drow[pg][r] = 0;
      fd_pp = page[pg] + (uint16_t)r * CW; fd_sc = scr + (uint16_t)r * CW; fd_n = CW;
      for (;;) {
        map_sram(SCR_TILE >> 9);
        find_diff();
        if (!fd_n) break;
        t = *fd_pp;
        ut_addr = 0x4000 | ((uint16_t)(r * CW + CW - fd_n) << 5);
        if (t < 16) ut_src = solid_tiles[t];
        else if (t >= 1024) { uint16_t q = t - 1024; map_rom(BGT_BANK0 + (q >> 9)); ut_src = (const uint8_t *)(0x8000 + ((q & 511) << 5)); }
        else { map_sram(t >> 9); ut_src = TADDR(t); }
        upload_direct();
        map_sram(SCR_TILE >> 9);
        *fd_sc = fd_key;
        rowstamp[r] = dispcount;
        fd_pp++; fd_sc++; fd_n--;
      }
    }
    lastdisp[pg] = dispcount;
#ifdef DIFFCHECK
    fd_pp = page[pg]; fd_sc = scr; fd_n = NCELL;
    map_sram(SCR_TILE >> 9); find_diff();
    if (fd_n) { diff_missed++; diff_cell = NCELL - fd_n; }
#endif
  }
  game_frames++;
}

/* ------------------------------------------------------------------ sound */
static uint8_t sv_bank[4], sv_att[4], sv_on[4];
static const uint16_t *sv_ptr[4];
static uint16_t psg_last[4];      /* per PSG channel: period/mode | att << 12 */
void psg_write(uint8_t ch, uint16_t per, uint8_t att) {
  uint8_t *l = (uint8_t *)&psg_last[ch];
  uint8_t lo = (uint8_t)per, hi = ((uint8_t)(per >> 8) & 3) | (uint8_t)(att << 4);
  uint8_t ol = l[0], oh = l[1];
  if (ol == lo && oh == hi) return;
  if (ch < 3) {
    if (ol != lo || ((oh ^ hi) & 3)) { PSGP = 0x80 | (ch << 5) | (lo & 15); PSGP = ((lo >> 4) | (uint8_t)(hi << 4)) & 0x3F; }
  } else if ((ol ^ lo) & 3) PSGP = 0xE4 | (lo & 3);
  if ((oh ^ hi) & 0xF0) PSGP = 0x90 | (ch << 5) | att;
  l[0] = lo; l[1] = hi;
}
static uint8_t snd_quiet;
void snd_tick_(void);
void snd_tick(void) { T0(4); snd_tick_(); T1(4); }
uint8_t sn_na[3], sn_nn, sn_np; uint16_t sn_tp[3];
void snd_voices(void) __naked {
  __asm
    ld a,#16
    ld (_sn_na),a
    ld (_sn_na+1),a
    ld (_sn_na+2),a
    ld (_sn_nn),a
    ld c,#0
00001$:
    ld hl,#_sv_on
    ld b,#0
    add hl,bc
    ld a,(hl)
    or a
    jp z,00090$
    ld hl,#_sv_bank
    add hl,bc
    ld a,(hl)
    ld hl,#_slot2
    cp (hl)
    jr z,00002$
    ld (hl),a
    push af
    xor a
    ld (#0xFFFC),a
    pop af
    ld (#0xFFFF),a
00002$:
    ld hl,#_sv_ptr
    add hl,bc
    add hl,bc
    push hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,d
    inc a
    jr nz,00010$
    ld a,e
    cp #0xFE
    jr nz,00005$
    push bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    or a
    sbc hl,bc
    pop bc
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,d
    inc a
    jr nz,00010$
    ld a,e
00005$:
    cp #0xFF
    jr nz,00010$
    pop hl
    ld hl,#_sv_on
    add hl,bc
    ld (hl),#0
    jp 00090$
00010$:
    ex de,hl
    ex (sp),hl
    ld (hl),e
    inc hl
    ld (hl),d
    pop de
    ld a,d
    rrca
    rrca
    rrca
    and #15
    ld hl,#_sv_att
    add hl,bc
    add a,(hl)
    cp #16
    jr c,00011$
    ld a,#15
00011$:
    bit 7,d
    jr z,00020$
    ld hl,#_sn_nn
    cp (hl)
    jr nc,00090$
    ld (hl),a
    ld a,e
    and #3
    ld (_sn_np),a
    jr 00090$
00020$:
    push bc
    ld b,a
    ld a,c
    cp #3
    jr c,00021$
    ld a,#2
00021$:
    ld c,a
    ld a,b
    ld b,#0
    ld hl,#_sn_na
    add hl,bc
    cp (hl)
    jr nc,00022$
    ld (hl),a
    ld hl,#_sn_tp
    add hl,bc
    add hl,bc
    ld (hl),e
    inc hl
    ld a,d
    and #3
    ld (hl),a
00022$:
    pop bc
00090$:
    inc c
    ld a,c
    cp #4
    jp nz,00001$
    ret
  __endasm;
}
/* write the 4 PSG channels from sn_na/sn_tp/sn_nn/sn_np, only the bytes that changed.
   psg_last[ch]: byte0 = period low, byte1 = (period bits 8-9) | attenuation << 4 */
#ifdef PSGCHECK
uint16_t psg_last2[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, psg_mis, psg_ticks;
void psgw2(uint8_t ch, uint16_t per, uint8_t att) {      /* the previous C logic, state only */
  uint8_t *l = (uint8_t *)&psg_last2[ch];
  uint8_t lo = (uint8_t)per, hi = ((uint8_t)(per >> 8) & 3) | (uint8_t)(att << 4);
  if (l[0] == lo && l[1] == hi) return;
  l[0] = lo; l[1] = hi;
}
#endif
void psg_apply(void) __naked {
  __asm
    ld c,#0
00001$:
    ld b,#0
    ld a,c
    cp #3
    jr z,00030$
    ld hl,#_sn_na
    ld b,#0
    add hl,bc
    ld a,(hl)
    cp #16
    jr nc,00010$
    rlca
    rlca
    rlca
    rlca
    ld d,a
    ld hl,#_sn_tp
    add hl,bc
    add hl,bc
    ld e,(hl)
    inc hl
    ld a,(hl)
    and #3
    or d
    ld d,a
    jr 00040$
00010$:
    ld hl,#_psg_last
    add hl,bc
    add hl,bc
    ld e,(hl)
    inc hl
    ld a,(hl)
    and #3
    or #0xF0
    ld d,a
    jr 00040$
00030$:
    ld a,(_sn_nn)
    cp #16
    jr nc,00010$
    rlca
    rlca
    rlca
    rlca
    ld d,a
    ld a,(_sn_np)
    ld e,a
00040$:
    ld hl,#_psg_last
    ld b,#0
    add hl,bc
    add hl,bc
    ld a,(hl)
    cp e
    jr nz,00041$
    inc hl
    ld a,(hl)
    dec hl
    cp d
    jr z,00090$
00041$:
    ld a,c
    cp #3
    jr z,00050$
    ld a,(hl)
    cp e
    jr nz,00042$
    inc hl
    ld a,(hl)
    dec hl
    xor d
    and #3
    jr z,00060$
00042$:
    ld a,c
    rrca
    rrca
    rrca
    ld b,a
    ld a,e
    and #15
    or b
    or #0x80
    out (#0x7F),a
    ld a,e
    rrca
    rrca
    rrca
    rrca
    and #15
    ld b,a
    ld a,d
    and #3
    rlca
    rlca
    rlca
    rlca
    or b
    out (#0x7F),a
    jr 00060$
00050$:
    ld a,(hl)
    xor e
    and #3
    jr z,00060$
    ld a,e
    and #3
    or #0xE4
    out (#0x7F),a
00060$:
    inc hl
    ld a,(hl)
    dec hl
    xor d
    and #0xF0
    jr z,00070$
    ld a,c
    rrca
    rrca
    rrca
    ld b,a
    ld a,d
    rrca
    rrca
    rrca
    rrca
    and #15
    or b
    or #0x90
    out (#0x7F),a
00070$:
    ld (hl),e
    inc hl
    ld (hl),d
00090$:
    inc c
    ld a,c
    cp #4
    jp nz,00001$
    ret
  __endasm;
}
void snd_tick_(void) {
  uint8_t s = slot2;
  if (!(sv_on[0] | sv_on[1] | sv_on[2] | sv_on[3])) { if (snd_quiet) return; snd_quiet = 1; } else snd_quiet = 0;
  snd_voices();
#ifdef PSGCHECK
  psgw2(0, sn_na[0] < 16 ? sn_tp[0] : (psg_last2[0] & 0x3FF), sn_na[0] < 16 ? sn_na[0] : 15);
  psgw2(1, sn_na[1] < 16 ? sn_tp[1] : (psg_last2[1] & 0x3FF), sn_na[1] < 16 ? sn_na[1] : 15);
  psgw2(2, sn_na[2] < 16 ? sn_tp[2] : (psg_last2[2] & 0x3FF), sn_na[2] < 16 ? sn_na[2] : 15);
  psgw2(3, sn_nn < 16 ? sn_np : (psg_last2[3] & 0x3FF), sn_nn < 16 ? sn_nn : 15);
#endif
  psg_apply();
#ifdef PSGCHECK
  { uint8_t q; for (q = 0; q < 4; q++) if (psg_last[q] != psg_last2[q]) psg_mis++; psg_ticks++; }
#endif
  slot2 = s; slot2_apply(s);
}
void play_sound(uint16_t res, uint8_t freq, uint8_t vol, uint8_t ch) {
  uint8_t k;
  ch &= 3;
  if (vol == 0) { sv_on[ch] = 0; return; }
  if (vol > 63) vol = 63;
  if (freq > 39) freq = 39;
  for (k = 0; k < NSFX; k++) if (sfx_tab[k][0] == res && sfx_tab[k][1] == freq) {
    __asm di __endasm;
    sv_bank[ch] = sfx_tab[k][2]; sv_ptr[ch] = (const uint16_t *)(sfx_tab[k][3] | (sfx_tab[k][4] << 8));
    sv_att[ch] = volatt[vol]; sv_on[ch] = 1;
    __asm ei __endasm;
    return;
  }
}
void isr(void) { ticks++; snd_tick(); }

/* ------------------------------------------------------------------ page-state cache */
/* Every page carries a rolling hash of what produced it (fills, copies, draw operands), identical to the
   offline capture (rawgl/bgcap.inc). Draws are deferred; when a page is used (copied from or displayed)
   the longest prefix of its pending draws whose resulting state was pre-rendered is loaded as ROM tile
   references, and only the remaining draws are rasterised. */
#define NDEF 32
typedef struct { uint8_t pg, ks; uint16_t off; int16_t x, y; uint16_t zoom; uint32_t key; } defent; /* ks: 0x81/0x82 shape seg, 0x40 string */
static defent dl[NDEF];
static uint8_t ndl;
static uint16_t bh[4], blen[4];
uint16_t bg_hits, bg_miss;
void bgb(uint8_t p, uint8_t b) __naked {
  (void)p; (void)b;
  __asm
    ld c,l
    add a,a
    ld e,a
    ld d,#0
    push de
    ld hl,#_bh
    add hl,de
    push hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,h
    ld h,a
    ld b,#3
00001$:
    ld a,l
    rrca
    rr h
    rr l
    djnz 00001$
    ld a,l
    xor c
    ld l,a
    ex de,hl
    pop hl
    ld (hl),e
    inc hl
    ld (hl),d
    pop de
    ld hl,#_blen
    add hl,de
    inc (hl)
    ret nz
    inc hl
    inc (hl)
    ret
  __endasm;
}
void bgw(uint8_t p, uint16_t w) { bgb(p, w >> 8); bgb(p, w & 255); }
uint8_t pending(uint8_t p) { uint8_t k; for (k = 0; k < ndl; k++) if (dl[k].pg == p) return 1; return 0; }
/* binary search of the key table: returns 1 and fills *bank/*addr on a hit */
uint8_t bg_lookup(uint32_t key, uint8_t *bank, uint16_t *addr) {
  int16_t lo = 0, hi = NBG - 1;
  uint16_t fi = ((uint16_t)(key >> 16) ^ (uint16_t)key) & 0x3FFF;
  map_rom(BGK_BANK);
  if (!(*(const uint8_t *)(0xB800 + (fi >> 3)) & bittab[fi & 7])) return 0;   /* key filter */
  while (lo <= hi) {
    int16_t mid = (lo + hi) >> 1; uint16_t m6 = ((uint16_t)mid << 2) + ((uint16_t)mid << 1);
    const uint8_t *e = (const uint8_t *)(0x8000 + m6);
    uint32_t k = *(const uint32_t *)e;
    if (k == key) { *bank = 0; *addr = *(const uint16_t *)(e + 4); return 1; }
    if (k < key) lo = mid + 1; else hi = mid - 1;
  }
  return 0;
}
/* load cached state mi into page p row by row (maps -> row pool), flagging rows that changed */
void bg_load(uint8_t bank, uint16_t mi, uint8_t p) {
  uint8_t r; uint16_t rows[17], rid, lo8;
  (void)bank;
  pgen[p]++; pbase[p] = 0xFF; pscroll[p] = 0;
  lo8 = mi & 255;
  map_rom(BGM_BANK0 + (mi >> 8));
  memcpy(rows, (const uint8_t *)(0x8000 + (lo8 << 5) + (lo8 << 1)), 34);
  for (r = 0; r < 17; r++) {
    rid = rows[r]; lo8 = rid & 255;
    map_rom(BGR_BANK0 + (rid >> 8));
    cp_sp = (uint16_t *)(0x8000 + (lo8 << 5) + (lo8 << 4) + (lo8 << 2));
    cp_dp = page[p] + (uint16_t)r * CW; cp_n = CW; cpr_chg = 0; cpr_loop();
    if (cpr_chg) drow[p][r] = 1;
  }
}
void render_ent(defent *e) {
  uint8_t save = buf[0];
  buf[0] = e->pg;
  if (e->ks & 0x80) { poly_bank0 = (e->ks == 0x82) ? V2_BANK : V1_BANK; poly_off = e->off; draw_shape(0xFF, e->zoom, e->x, e->y); }
  else draw_string((uint8_t)e->zoom, e->x, e->y, e->off);
  buf[0] = save;
}
/* materialise page p: load the longest cached prefix, rasterise the rest */
void bg_flush(uint8_t p) {
  uint8_t k, n = 0, idx[NDEF], j, bank, sb = poly_bank0; int8_t hit = -1; uint16_t addr, so = poly_off;
  for (k = 0; k < ndl; k++) if (dl[k].pg == p) idx[n++] = k;
  if (!n) return;
  src_changing(p);
  for (j = n; j > 0; j--) if (bg_lookup(dl[idx[j - 1]].key, &bank, &addr)) { hit = j - 1; break; }
  if (hit >= 0) { pend[p] = 0xFF; bg_load(bank, addr, p); bg_hits++; } else { materialize(p); bg_miss++; }
  if (hit + 1 < n) pscroll[p] = 0;       /* drawing into a shaken page: the shake is dropped for it */
  for (j = hit + 1; j < n; j++) render_ent(&dl[idx[j]]);
  for (j = 0, k = 0; k < ndl; k++) if (dl[k].pg != p) dl[j++] = dl[k];
  ndl = j; poly_bank0 = sb; poly_off = so;
}
void bg_discard(uint8_t p) { uint8_t j = 0, k; for (k = 0; k < ndl; k++) if (dl[k].pg != p) dl[j++] = dl[k]; ndl = j; }
void bg_force(uint8_t i) { bg_flush(i); }
void bg_use(uint8_t p) { bg_flush(p); }
void bg_add(uint8_t ks, uint16_t off, int16_t x, int16_t y, uint16_t zoom) {
  defent *e;
  if (ndl == NDEF) bg_flush(dl[0].pg);
  e = &dl[ndl++]; e->pg = buf[0]; e->ks = ks; e->off = off; e->x = x; e->y = y; e->zoom = zoom;
  e->key = ((uint32_t)bh[buf[0]] << 16) | blen[buf[0]];
}
void bg_fill(uint8_t p, uint8_t c) {
  bg_discard(p);
  bh[p] = 0xACE1; blen[p] = 0; bgb(p, 0xF0); bgb(p, c);
}
/* hash bfe_n bytes at bfe_src into page bfe_p's state (same as bfe_n calls of bgb) */
uint8_t bfe_p, bfe_n, bfe_buf[9]; const uint8_t *bfe_src;
void bg_feed(void) __naked {
  __asm
    ld a,(_bfe_p)
    add a,a
    ld e,a
    ld d,#0
    ld hl,#_bh
    add hl,de
    push hl
    push de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld de,(_bfe_src)
    ld a,(_bfe_n)
    ld b,a
00001$:
    ld a,l
    ld l,h
    ld h,a
    ld c,#3
00002$:
    ld a,l
    rrca
    rr h
    rr l
    dec c
    jr nz,00002$
    ld a,(de)
    xor l
    ld l,a
    inc de
    djnz 00001$
    ex de,hl
    pop bc
    pop hl
    ld (hl),e
    inc hl
    ld (hl),d
    ld hl,#_blen
    add hl,bc
    ld a,(_bfe_n)
    add a,(hl)
    ld (hl),a
    ret nc
    inc hl
    inc (hl)
    ret
  __endasm;
}
uint8_t bg_shape(uint8_t seg, uint16_t off, int16_t x, int16_t y, uint16_t zoom) {
  uint8_t p = buf[0];
  bfe_buf[0] = 0xA0 | seg; bfe_buf[1] = (uint8_t)(off >> 8); bfe_buf[2] = (uint8_t)off;
  bfe_buf[3] = (uint8_t)((uint16_t)x >> 8); bfe_buf[4] = (uint8_t)x; bfe_buf[5] = (uint8_t)((uint16_t)y >> 8); bfe_buf[6] = (uint8_t)y;
  bfe_buf[7] = (uint8_t)(zoom >> 8); bfe_buf[8] = (uint8_t)zoom;
  bfe_p = p; bfe_src = bfe_buf; bfe_n = 9; bg_feed();
  bg_add(0x80 | seg, off, x, y, zoom);
  return 1;
}
void bg_string(uint16_t id, uint8_t x, uint8_t y, uint8_t c) {
  uint8_t p = buf[0];
  bgb(p, 0xB0); bgw(p, id); bgb(p, x); bgb(p, y); bgb(p, c);
  bg_add(0x40, id, x, y, c);
}
void bg_copy(uint8_t src, uint8_t dst, uint8_t scroll, int16_t vs) {
  if (src == dst) return;
  if (!scroll) { bg_discard(dst); bh[dst] = bh[src] ^ 0xC0C0; blen[dst] = blen[src] + 1; }
  else { uint16_t h = bh[dst]; bg_flush(dst); bh[dst] = ((h << 5) | (h >> 11)) ^ bh[src] ^ (uint16_t)vs ^ 0x5C5C; blen[dst] += blen[src] + 1; }
}

/* ------------------------------------------------------------------ VM */
uint8_t fetch(void) __naked {
  __asm
    ld hl,(_pc)
    ld a,h
    rlca
    rlca
    and #3
    add a,#BC_BANK
    ld c,a
    ld a,(_slot2)
    cp c
    jr z,00001$
    ld a,c
    ld (_slot2),a
    xor a
    ld (#0xFFFC),a
    ld a,c
    ld (#0xFFFF),a
00001$:
    ld a,h
    and #0x3F
    or #0x80
    ld d,a
    ld e,l
    inc hl
    ld (_pc),hl
    ld a,(de)
    ret
  __endasm;
}

uint16_t fetch16(void) { uint16_t v = fetch() << 8; return v | fetch(); }

void restart_part(void) {
  uint8_t i;
  memset(tpc, 0xFF, sizeof(tpc));
  memset(tst, 0, sizeof(tst));
  tpc[0][0] = 0;
  (void)i;
}
#ifdef DEMOPLAY
#include "../gen/demo_input.h"
static uint16_t demo_pos;
#endif
void update_input(void) {
#ifdef DEMOPLAY
  { uint8_t dm = 0; int16_t lr = 0, ud = 0;
    while (demo_pos + 1 < NDEMO && demo_in[demo_pos + 1][0] <= game_frames) demo_pos++;
    if (demo_in[demo_pos][0] <= game_frames) dm = (uint8_t)demo_in[demo_pos][1];
    if (dm & 1) lr = 1;
    if (dm & 2) lr = -1;
    if (dm & 4) ud = 1;
    if (dm & 8) ud = -1;
    vars[0xE5] = ud; vars[0xFB] = ud; vars[0xFC] = lr; vars[0xFD] = dm & 0x0F; vars[0xFA] = (dm & 0x80) ? 1 : 0; vars[0xFE] = dm;
    return; }
#endif
  unsigned int k = SMS_getKeysStatus();
  int16_t lr = 0, ud = 0, jd = 0; uint8_t m = 0;
  if (k & PORT_A_KEY_RIGHT) { lr = 1; m |= 1; }
  if (k & PORT_A_KEY_LEFT) { lr = -1; m |= 2; }
  if (k & PORT_A_KEY_DOWN) { ud = jd = 1; m |= 4; }
  if (k & (PORT_A_KEY_UP | PORT_A_KEY_2)) { ud = jd = -1; m |= 8; }
  vars[0xE5] = ud; vars[0xFB] = jd; vars[0xFC] = lr; vars[0xFD] = m;
  vars[0xFA] = (k & PORT_A_KEY_1) ? 1 : 0;
  if (k & PORT_A_KEY_1) m |= 0x80;
  vars[0xFE] = m;
}
#ifdef CAPTURE_PC
static uint8_t capture_hit;
#endif
uint8_t vm_cmode;
#ifdef VMTRACE
__sfr __at 0x04 DBGTR;
#endif

/* ---- VM fast path: executes ops 00,01,02,03,07,09,0A in assembly; returns with pc at the first other op */
uint8_t vm_bank;
void vm_nextbank(void) __naked {
  __asm
    push af
    ld a,(_vm_bank)
    inc a
    ld (_vm_bank),a
    ld (_slot2),a
    push af
    xor a
    ld (#0xFFFC),a
    pop af
    ld (#0xFFFF),a
    ld de,#0x8000
    pop af
    ret
  __endasm;
}
/* HL = virtual pc of the physical pointer DE in bank vm_bank */
void vm_vpc(void) __naked {
  __asm
    ld a,(_vm_bank)
    sub #BC_BANK
    rrca
    rrca
    and #0xC0
    ld h,a
    ld a,d
    and #0x3F
    or h
    ld h,a
    ld l,e
    ret
  __endasm;
}
/* jump to virtual address in HL: map bank, DE = physical pointer */
void vm_goto(void) __naked {
  __asm
    ld a,h
    rlca
    rlca
    and #3
    add a,#BC_BANK
    ld (_vm_bank),a
    ld (_slot2),a
    ld c,a
    xor a
    ld (#0xFFFC),a
    ld a,c
    ld (#0xFFFF),a
    ld a,h
    and #0x3F
    or #0x80
    ld d,a
    ld e,l
    ret
  __endasm;
}
void vm_fast(void) __naked {
  __asm
    ld hl,(_pc)
    call _vm_goto
00001$:
    ld a,(de)
    cp #0x0B
    jp nc,00090$
    ld l,a
    ld h,#0
    add hl,hl
    ld bc,#00002$
    add hl,bc
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    jp (hl)
00002$:
    .dw 00100$,00110$,00120$,00130$,00140$,00150$,00160$,00170$,00180$,00190$,00200$
; ---- 00: vars[a] = imm16
00100$:
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld l,a
    ld h,#0
    add hl,hl
    ld bc,#_vars
    add hl,bc
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld b,a
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld c,a
    ld (hl),c
    inc hl
    ld (hl),b
    jp 00001$
; ---- 01: vars[a] = vars[b]
00110$:
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld c,a
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld l,a
    ld h,#0
    add hl,hl
    push de
    ld de,#_vars
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    push hl
    ld l,c
    ld h,#0
    add hl,hl
    add hl,de
    pop bc
    ld (hl),c
    inc hl
    ld (hl),b
    pop de
    jp 00001$
; ---- 02: vars[a] += vars[b]
00120$:
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld c,a
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld l,a
    ld h,#0
    add hl,hl
    push de
    ld de,#_vars
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    push hl
    ld l,c
    ld h,#0
    add hl,hl
    add hl,de
    pop bc
    ld a,(hl)
    add a,c
    ld (hl),a
    inc hl
    ld a,(hl)
    adc a,b
    ld (hl),a
    pop de
    jp 00001$
; ---- 03: vars[a] += imm16
00130$:
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld l,a
    ld h,#0
    add hl,hl
    ld bc,#_vars
    add hl,bc
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld b,a
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld c,a
    ld a,(hl)
    add a,c
    ld (hl),a
    inc hl
    ld a,(hl)
    adc a,b
    ld (hl),a
    jp 00001$
; ---- 07: jmp imm16
00170$:
    inc de
    bit 6,d
    call nz,_vm_nextbank
00171$:
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld h,a
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld l,a
    call _vm_goto
    jp 00001$
; ---- 09: if (--vars[a]) jmp imm16 else skip 2
00190$:
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld l,a
    ld h,#0
    add hl,hl
    ld bc,#_vars
    add hl,bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    dec bc
    ld (hl),b
    dec hl
    ld (hl),c
    ld a,b
    or c
    jr nz,00171$
00191$:
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    jp 00001$
; ---- 0A: conditional jump
00200$:
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld (_vm_cmode),a
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld l,a
    ld h,#0
    add hl,hl
    ld bc,#_vars
    add hl,bc
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    push hl
    ld a,(_vm_cmode)
    bit 7,a
    jr z,00210$
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld l,a
    ld h,#0
    add hl,hl
    ld bc,#_vars
    add hl,bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    jr 00220$
00210$:
    bit 6,a
    jr z,00215$
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld b,a
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld c,a
    jr 00220$
00215$:
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld c,a
    ld b,#0
00220$:
    pop hl
    or a
    sbc hl,bc
    push af
    ld c,#0
    jp po,00230$
    jp m,00240$
    inc c
    jr 00240$
00230$:
    jp p,00240$
    inc c
00240$:
    pop af
    ld b,#0
    jr nz,00241$
    inc b
00241$:
    ld a,(_vm_cmode)
    and #7
    jr nz,00250$
    ld a,b
    jr 00290$
00250$:
    dec a
    jr nz,00251$
    ld a,b
    xor #1
    jr 00290$
00251$:
    dec a
    jr nz,00252$
    ld a,b
    or c
    xor #1
    jr 00290$
00252$:
    dec a
    jr nz,00253$
    ld a,c
    xor #1
    jr 00290$
00253$:
    dec a
    jr nz,00254$
    ld a,c
    jr 00290$
00254$:
    dec a
    jr nz,00255$
    ld a,b
    or c
    jr 00290$
00255$:
    xor a
00290$:
    or a
    jp nz,00171$
    jp 00191$
;---- 04: call imm16
00140$:
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld h,a
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld l,a
    push hl
    call _vm_vpc
    ld a,(_sp_)
    cp #16
    jr nc,00141$
    ex de,hl
    ld hl,#_stk
    add a,a
    add a,l
    ld l,a
    jr nc,00142$
    inc h
00142$:
    ld (hl),e
    inc hl
    ld (hl),d
    ld hl,#_sp_
    inc (hl)
00141$:
    pop hl
    call _vm_goto
    jp 00001$
; ---- 05: ret
00150$:
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld a,(_sp_)
    or a
    jp z,00001$
    dec a
    ld (_sp_),a
    ld hl,#_stk
    add a,a
    add a,l
    ld l,a
    jr nc,00151$
    inc h
00151$:
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    call _vm_goto
    jp 00001$
; ---- 06: yield
00160$:
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld a,#1
    ld (_paused),a
    jp 00090$
; ---- 08: installTask a, imm16
00180$:
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld c,a
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld b,a
    ld a,(de)
    inc de
    bit 6,d
    call nz,_vm_nextbank
    ld l,a
    ld a,c
    cp #64
    jp nc,00001$
    ld h,b
    push hl
    ld l,c
    ld h,#0
    add hl,hl
    ld bc,#_tpc+128
    add hl,bc
    pop bc
    ld (hl),c
    inc hl
    ld (hl),b
    jp 00001$
; ---- exit: store virtual pc
00090$:
    ld a,(_vm_bank)
    sub #BC_BANK
    rrca
    rrca
    and #0xC0
    ld h,a
    ld a,d
    and #0x3F
    or h
    ld h,a
    ld l,e
    ld (_pc),hl
    ret
  __endasm;
}
void exec_task(void) {
  while (!paused) {
    vm_fast();
    if (paused) break;
    uint8_t op = fetch();
    if (op & 0x80) {
      uint16_t off = (((uint16_t)op << 8) | fetch()) << 1;
      int16_t x = fetch(), y = fetch(), hh = y - 199;
      if (hh > 0) { y = 199; x += hh; }
      poly_bank0 = V1_BANK; poly_off = off;
      if (!bg_shape(1, off, x, y, 64)) draw_shape(0xFF, 64, x, y);
    } else if (op & 0x40) {
      uint16_t off; int16_t x, y; uint16_t zoom = 64;
      off = fetch() << 8; off = (off | fetch()) << 1;
      x = fetch();
      poly_bank0 = V1_BANK;
      if (!(op & 0x20)) {
        if (!(op & 0x10)) x = (x << 8) | fetch(); else x = vars[x];
      } else if (op & 0x10) x += 0x100;
      y = fetch();
      if (!(op & 8)) {
        if (!(op & 4)) y = (y << 8) | fetch(); else y = vars[y];
      }
      if (!(op & 2)) { if (op & 1) zoom = vars[fetch()]; }
      else { if (op & 1) poly_bank0 = V2_BANK; else zoom = fetch(); }
      poly_off = off;
      if (!bg_shape(poly_bank0 == V2_BANK ? 2 : 1, off, x, y, zoom)) draw_shape(0xFF, zoom, x, y);
    } else {
      uint8_t a, b; uint16_t w;
      switch (op) {
      case 0x00: a = fetch(); vars[a] = fetch16(); break;
      case 0x01: a = fetch(); b = fetch(); vars[a] = vars[b]; break;
      case 0x02: a = fetch(); b = fetch(); vars[a] += vars[b]; break;
      case 0x03: a = fetch(); vars[a] += (int16_t)fetch16(); break;
      case 0x04: w = fetch16(); if (sp_ < 16) stk[sp_++] = pc; else vm_bad_op++; pc = w; break;
      case 0x05: if (sp_) pc = stk[--sp_]; break;
      case 0x06: paused = 1; break;
      case 0x07: pc = fetch16(); break;
      case 0x08: a = fetch(); w = fetch16(); if (a < 64) tpc[1][a] = w; break;
      case 0x09: a = fetch(); if (--vars[a] != 0) pc = fetch16(); else pc += 2; break;
      case 0x0A: {
        uint8_t c = fetch(); int16_t bv = vars[fetch()], av; uint8_t e = 0;
        if (c & 0x80) av = vars[fetch()]; else if (c & 0x40) av = (int16_t)fetch16(); else av = fetch();
        switch (c & 7) {
        case 0: e = (bv == av); break; case 1: e = (bv != av); break; case 2: e = (bv > av); break;
        case 3: e = (bv >= av); break; case 4: e = (bv < av); break; case 5: e = (bv <= av); break;
        }
        if (e) pc = fetch16(); else pc += 2;
        break; }
      case 0x0B: w = fetch16(); next_pal = w >> 8; break;
      case 0x0C: {
        uint8_t s = fetch(), e = fetch(), st;
        if (e < s) break;
        st = fetch();
        if (e > 63) e = 63;
        if (st == 2) for (; s <= e; ++s) tpc[1][s] = 0xFFFE;
        else if (st < 2) for (; s <= e; ++s) tst[1][s] = st;
        break; }
      case 0x0D: buf[0] = get_page(fetch()); break;
      case 0x0E: a = fetch(); b = fetch(); bg_fill(get_page(a), b); fill_page(get_page(a), b); break;
      case 0x0F: {
        uint8_t s = fetch(), d = fetch();
        if (s >= 0xFE || ((s &= ~0x40) & 0x80) == 0) {
          uint8_t sp = get_page(s), dp = get_page(d);
          bg_use(sp); bg_copy(sp, dp, 0, 0);
          copy_page_refs(sp, dp);
        } else {
          uint8_t sl = get_page(s & 3), dl = get_page(d); int16_t vs = vars[0xF9];
          if (sl != dl && vs >= -199 && vs <= 199) {
            bg_use(sl); bg_copy(sl, dl, 1, vs);
            int16_t dy = (int16_t)(((int32_t)vs * 17 - ((vs < 0) ? 24 : 0)) / 25);
            if (dy >= -24 && dy <= 24) {                 /* screen shake: reference copy + hardware scroll */
              int8_t base = pscroll[sl];
              copy_page_refs(sl, dl); pscroll[dl] = base + (int8_t)dy;
            } else { pscroll[dl] = 0; copy_page_scroll(sl, dl, dy); }
          }
        }
        break; }
      case 0x10: a = fetch(); vars[0xF7] = 0;
#ifdef VMTRACE
        { uint16_t q; const uint8_t *vp = (const uint8_t *)vars; for (q = 0; q < 512; q++) DBGTR = vp[q]; }
#endif
        display(a); break;
      case 0x11: pc = 0xFFFF; paused = 1; break;
      case 0x12: { uint16_t id = fetch16(); uint8_t x = fetch(), y = fetch(), c = fetch(); bg_string(id, x, y, c);
        break; }
      case 0x13: a = fetch(); b = fetch(); vars[a] -= vars[b]; break;
      case 0x14: a = fetch(); vars[a] = (uint16_t)vars[a] & fetch16(); break;
      case 0x15: a = fetch(); vars[a] = (uint16_t)vars[a] | fetch16(); break;
      case 0x16: a = fetch(); vars[a] = (uint16_t)vars[a] << fetch16(); break;
      case 0x17: a = fetch(); vars[a] = (uint16_t)vars[a] >> fetch16(); break;
      case 0x18: { uint16_t r = fetch16(); uint8_t f = fetch(), v = fetch(), c = fetch(); if (!ff) play_sound(r, f, v, c); break; }
      case 0x19: w = fetch16();
        if (w == 0) { uint8_t k; for (k = 0; k < 4; k++) sv_on[k] = 0; }
        else if (w >= 16000) next_part = w;
        /* other resources (samples, modules, bitmaps of this part) are resident in ROM */
        break;
      case 0x1A: fetch16(); fetch16(); fetch(); break;   /* no music module is played by part 16002 */
      default: vm_bad_op++; paused = 1; pc = 0xFFFF; break;
      }
    }
  }
}
/* apply pending task states/pcs (tst[1], tpc[1]) to the current ones, as the original does per frame */
void rt_sync(void) __naked {
  __asm
    ld hl,#_tst+64
    ld de,#_tst
    ld bc,#64
    ldir
    ld hl,#_tpc+128
    ld de,#_tpc
    ld b,#64
00001$:
    ld a,(hl)
    inc hl
    and (hl)
    inc a
    jr z,00003$
    dec hl
    ld a,(hl)
    cp #0xFE
    jr nz,00002$
    inc hl
    ld a,(hl)
    dec hl
    inc a
    jr nz,00002$
    ld a,#0xFF
    ld (de),a
    inc de
    ld (de),a
    dec de
    jr 00004$
00002$:
    ld a,(hl)
    ld (de),a
    inc hl
    inc de
    ld a,(hl)
    ld (de),a
    dec de
    dec hl
00004$:
    ld (hl),#0xFF
    inc hl
    ld (hl),#0xFF
00003$:
    inc hl
    inc de
    inc de
    djnz 00001$
    ret
  __endasm;
}
void run_tasks(void) {
  uint8_t i; uint16_t *p0; uint8_t *s0;
  rt_sync();
  if (ff) { vars[0xE5] = vars[0xFB] = vars[0xFC] = vars[0xFD] = vars[0xFA] = vars[0xFE] = 0; } else update_input();
  for (i = 64, p0 = tpc[0], s0 = tst[0]; i; i--, p0++, s0++) {
    if (*s0 == 0 && *p0 != 0xFFFF) {
      pc = *p0; sp_ = 0; paused = 0;
#ifdef CAPTURE_PC
      if (pc == CAPTURE_PC) { capture_hit = 1; return; }
#endif
      exec_task();
      *p0 = pc;
      if (ending_hit) return;
    }
  }
}

/* ------------------------------------------------------------------ setup */
void vreg(uint8_t r, uint8_t v) { VDPC = v; VDPC = 0x80 | r; }
void video_setup(void) {
  uint16_t i; uint8_t cx, cy;
  vreg(1, 0x80);
  vreg(0, 0x06); vreg(2, 0xFF); vreg(3, 0xFF); vreg(4, 0xFF); vreg(5, 0xFF); vreg(6, 0xFB);
  vreg(7, 0x00); vreg(8, 0); vreg(9, 0); vreg(10, 0xFF);
  set_vram(0); for (i = 0; i < 0x3800; i++) VDPD = 0;
  set_vram(0x3800);
  for (cy = 0; cy < 24; cy++) for (cx = 0; cx < 32; cx++) {
    uint16_t e = 0x800 | NT_BLANK;
    if (cx >= 3 && cx < 3 + CW && cy >= 3 && cy < 20) e = (cy - 3) * CW + (cx - 3);
    VDPD = e & 0xFF; VDPD = e >> 8;
  }
  set_vram(0x3F00); VDPD = 0xD0;
  VDPC = 0; VDPC = 0xC0; for (i = 0; i < 32; i++) VDPD = 0;
  map_sram(SCR_TILE >> 9);
  for (i = 0; i < NCELL; i++) scr[i] = 0;          /* VRAM cleared = solid colour 0 */
  memset(rowstamp, 0, sizeof(rowstamp)); memset(lastdisp, 0, sizeof(lastdisp)); memset(drow, 1, sizeof(drow)); dispcount = 0;
}
uint8_t game_run(void) {
  uint8_t i;
  __asm di __endasm;
  video_setup();
  memset(vars, 0, sizeof(vars));
  memset(rcg, 0, sizeof(rcg));
  for (i = 0; i < 4; i++) { uint16_t c; for (c = 0; c < NCELL; c++) page[i][c] = 0; sv_on[i] = 0; psg_last[i] = 0xFFFF; page_all(i); pend[i] = 0xFF; }
  alloc_ptr = 16; gfx_alloc_fail = 0; vm_bad_op = 0; game_frames = 0; next_part = 0;
  buf[2] = 1; buf[1] = 2; buf[0] = 2;                 /* Video::init: setWorkPagePtr(0xFE) */
  next_pal = cur_pal = 0xFF;
  restart_part(); ff = FF_DISPLAYS;
  ndl = 0; memset(bh, 0, sizeof(bh)); memset(blen, 0, sizeof(blen)); bg_hits = bg_miss = 0;
  for (i = 0; i < NINITVARS; i++) vars[initvar_idx[i]] = initvar_val[i];
  SMS_setFrameInterruptHandler(isr);
  memset(pscroll, 0, sizeof(pscroll)); vscroll_cur = 0; vreg(9, 0);
  vreg(1, 0xE0);                                       /* display on + frame IRQ */
  __asm ei __endasm;
  last_disp = ticks;
  for (;;) {
    run_tasks();
    if (ending_hit) break;
#ifdef CAPTURE_PC
    if (capture_hit) { capture_hit = 0; break; }
#endif
    if (next_part) {
      if (next_part == 16002) { restart_part(); next_part = 0; continue; }
      break;
    }
  }
  __asm di __endasm;
  for (i = 0; i < 4; i++) sv_on[i] = 0;
  PSGP = 0x9F; PSGP = 0xBF; PSGP = 0xDF; PSGP = 0xFF;
  MAPCTL = 0; slot2 = 0xFD;
  if (ending_hit) { ending_hit = 0; return GAME_EXIT_CAPTURE; }
#ifdef CAPTURE_PC
  if (!next_part) return GAME_EXIT_CAPTURE;
#endif
  return GAME_EXIT_TO_INTRO;
}

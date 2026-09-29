/* Full-screen FMV player: streams tile uploads + double-buffered nametable deltas,
   plus a per-tick PSG register stream. Interrupts off; paced on the V-counter. */
#include <stdint.h>
#include <string.h>
#include "fmv.h"
#include "../gen/fmv_data.h"

__sfr __at 0xBF VDPC;
__sfr __at 0xBE VDPD;
__sfr __at 0x7E VCOUNT;
__sfr __at 0x7F PSG;
__sfr __at 0xDC JOY;
#define MAPPER_CTL (*(volatile uint8_t*)0xFFFC)
#define MAPPER2    (*(volatile uint8_t*)0xFFFF)

static uint8_t cur_map;
static uint8_t v_bank; static const uint8_t *v_ptr;
static uint8_t a_bank; static const uint8_t *a_ptr; static uint8_t a_done;
static uint16_t ticks; static uint8_t last_vc_in_vb;
static uint8_t skip;
extern uint16_t page[4][442];
#define runbuf ((uint8_t *)page[0])
#define upbuf  ((uint8_t *)page[1])
uint16_t fmv_frames_shown;       /* test visibility */
uint16_t fmv_late_max;           /* worst lateness in ticks */

static void map(uint8_t b) { if (cur_map != b) { cur_map = b; MAPPER2 = b; } }

static uint8_t vrd8(void) {
  uint8_t v;
  map(v_bank); v = *v_ptr++;
  if (v_ptr == (const uint8_t*)0xC000) { v_ptr = (const uint8_t*)0x8000; v_bank++; }
  return v;
}
static void vread(uint8_t *dst, uint16_t n) {
  while (n) {
    uint16_t room = 0xC000 - (uint16_t)v_ptr, c = n < room ? n : room;
    map(v_bank); memcpy(dst, v_ptr, c); dst += c; v_ptr += c; n -= c;
    if (v_ptr == (const uint8_t*)0xC000) { v_ptr = (const uint8_t*)0x8000; v_bank++; }
  }
}
static uint16_t vrd16(void) { uint8_t lo = vrd8(); return lo | ((uint16_t)vrd8() << 8); }
static uint8_t ard8(void) {
  uint8_t v;
  map(a_bank); v = *a_ptr++;
  if (a_ptr == (const uint8_t*)0xC000) { a_ptr = (const uint8_t*)0x8000; a_bank++; }
  return v;
}

static void psg_silence(void) { PSG = 0x9F; PSG = 0xBF; PSG = 0xDF; PSG = 0xFF; }

/* one tick of audio */
static void audio_tick(void) {
  uint8_t n;
  if (a_done) return;
  n = ard8();
  if (n == 0xFF) { a_done = 1; return; }
  while (n--) PSG = ard8();
}

/* poll the V-counter: counts 60Hz ticks at VBlank entry; returns 1 on the entry edge */
static uint8_t poll(void) {
  uint8_t vc = VCOUNT;
  uint8_t in_vb = (vc >= 0xC0 && vc < 0xE0);
  if (in_vb && !last_vc_in_vb) {
    last_vc_in_vb = 1; ticks++; audio_tick();
    if ((JOY & 0x30) != 0x30) skip = 1;
    return 1;
  }
  if (!in_vb) last_vc_in_vb = 0;
  return 0;
}

/* 32 paced bytes (28 cycles apart) from src to VRAM address addr (write flag set) */
void upload_tile(uint16_t addr, const uint8_t *src) __naked __sdcccall(1) {
  (void)addr; (void)src;
  __asm
    ld c,#0xBF
    out (c),l
    out (c),h
    ex de,hl
    ld c,#0xBE
    .rept 32
    outi
    nop
    nop
    nop
    .endm
    ret
  __endasm;
}
/* Upload ub_cnt tiles described by 5-byte entries at ub_ptr (vram addr lo/hi, bank, src lo/hi).
   Unpaced only when the V-counter says VBlank has >= ~20 lines left, else 28-cycle pacing.
   Counts VBlank entry edges into ub_edges so the caller can service audio ticks. */
uint8_t *ub_ptr; uint8_t ub_cnt, ub_edges, ub_invb;
static void up_batch(void) __naked {
  __asm
    ld hl,(_ub_ptr)
00010$:
    ld c,#0xBF
    ld a,(hl)
    out (c),a
    inc hl
    ld a,(hl)
    out (c),a
    inc hl
    ld a,(hl)
    inc hl
    ld (#0xFFFF),a
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    push hl
    ex de,hl
    ld c,#0xBE
    in a,(#0x7E)
    cp #0xC1
    jr c,00020$
    cp #0xEC
    jr nc,00020$
    .rept 32
    outi
    .endm
    jp 00030$
00020$:
    .rept 32
    outi
    nop
    nop
    nop
    .endm
00030$:
    ld e,#0
    in a,(#0x7E)
    cp #0xC0
    jr c,00040$
    cp #0xE0
    jr nc,00040$
    inc e
00040$:
    ld a,(_ub_invb)
    or a
    jr nz,00050$
    ld a,e
    or a
    jr z,00050$
    ld a,(_ub_edges)
    inc a
    ld (_ub_edges),a
00050$:
    ld a,e
    ld (_ub_invb),a
    pop hl
    ld a,(_ub_cnt)
    dec a
    ld (_ub_cnt),a
    jp nz,00010$
    ret
  __endasm;
}
/* in VBlank with at least `lines` lines to go before active display */

/* n bytes paced (30 cycles apart) */
static void out_run(uint16_t addr, const uint8_t *src, uint8_t n) __naked __sdcccall(1) {
  (void)addr; (void)src; (void)n;
  __asm
    ld c,#0xBF
    out (c),l
    out (c),h
    ex de,hl
    ld iy,#2
    add iy,sp
    ld b,0(iy)
    ld c,#0xBE
00001$:
    outi
    nop
    jp nz,00001$
    pop hl
    inc sp
    jp (hl)
  __endasm;
}

static void vdp_reg(uint8_t r, uint8_t v) { VDPC = v; VDPC = 0x80 | r; }

static void vdp_setup(void) {
  uint16_t i;
  vdp_reg(1, 0x80);            /* display off, no IRQs */
  vdp_reg(0, 0x06); vdp_reg(2, 0xFF); vdp_reg(3, 0xFF); vdp_reg(4, 0xFF);
  vdp_reg(5, 0xFF); vdp_reg(6, 0xFB); vdp_reg(7, 0x00); vdp_reg(8, 0); vdp_reg(9, 0); vdp_reg(10, 0xFF);
  VDPC = 0x00; VDPC = 0x40;    /* clear VRAM (display off: fast is fine) */
  for (i = 0; i < 0x4000; i++) VDPD = 0;
  VDPC = 0x00; VDPC = 0x7F; VDPD = 0xD0;   /* SAT at 0x3F00: no sprites */
  VDPC = 0x00; VDPC = 0xC0;
  for (i = 0; i < 32; i++) VDPD = 0;
}

uint8_t fmv_play(uint8_t cut) {
  uint8_t flags, first = 1, i, pal[16];
  uint16_t tick, n; uint8_t cnt;
  uint8_t front = 0;           /* 0 -> 0x3800 shown */
  __asm di __endasm;
  MAPPER_CTL = 0; cur_map = 0xFF;
  vdp_setup(); psg_silence();
  v_bank = fmv_tab[cut][0]; v_ptr = (const uint8_t*)(fmv_tab[cut][1] | (fmv_tab[cut][2] << 8));
  a_bank = fmv_tab[cut][3]; a_ptr = (const uint8_t*)(fmv_tab[cut][4] | (fmv_tab[cut][5] << 8));
  a_done = 0; ticks = 0; last_vc_in_vb = 1; skip = 0; fmv_frames_shown = 0; fmv_late_max = 0;
  while ((JOY & 0x30) != 0x30) ;       /* wait for buttons released */
  for (;;) {
    flags = vrd8(); tick = vrd16();
    if (flags & 2) break;
    if (flags & 1) vread(pal, 16);
    n = vrd16();
    while (n) {
      uint8_t k = n > 16 ? 16 : (uint8_t)n;
      n -= k; vread(upbuf, k * 5);
      ub_ptr = upbuf; ub_cnt = k; ub_edges = 0; ub_invb = last_vc_in_vb;
      up_batch(); cur_map = 0xFF;
      last_vc_in_vb = ub_invb;
      while (ub_edges) { ub_edges--; ticks++; audio_tick(); if ((JOY & 0x30) != 0x30) skip = 1; }
      poll();
    }
    n = vrd16();
    while (n--) {
      uint8_t h[3];
      vread(h, 3); cnt = h[2];
      vread(runbuf, cnt * 2);
      out_run(h[0] | (h[1] << 8), runbuf, cnt * 2);
      poll();
    }
    /* wait for its tick, then flip at VBlank entry */
    for (;;) {
      if (poll() && ticks >= tick) break;
      if (skip) goto done;
    }
    if (ticks - tick > fmv_late_max) fmv_late_max = ticks - tick;
    front ^= 1;
    vdp_reg(2, front ? 0xFD : 0xFF);
    if (flags & 1) { VDPC = 0x00; VDPC = 0xC0; for (i = 0; i < 16; i++) VDPD = pal[i]; }
    if (first) { vdp_reg(1, 0xC0); first = 0; }
    fmv_frames_shown++;
    if (skip) break;
  }
  /* hold the last frame until the end tick */
  while (!skip && ticks < tick) poll();
done:
  psg_silence();
  vdp_reg(1, 0x80);
  return skip;
}

/* Title / options screen shown after the intro (linked into ROM bank 12 with the FMV player, runs with
   interrupts off). Uses the game's 8x8 font from ROM bank 13 (CONST_BANK) mapped in slot 2. */
#include <stdint.h>
#include "../gen/constdata.h"
__sfr __at 0xBF VDPC_T;
__sfr __at 0xBE VDPD_T;
__sfr __at 0xDC JOY_T;
#define MAPCTL_T (*(volatile uint8_t *)0xFFFC)
#define MAP2_T (*(volatile uint8_t *)0xFFFF)
extern uint8_t slot2;
uint8_t opt_skip;              /* frame-skip setting chosen here: 0 off, 1 normal, 2 high (set by main) */
static void treg(uint8_t r, uint8_t v) { VDPC_T = v; VDPC_T = 0x80 | r; }
static void taddr(uint16_t a) { VDPC_T = (uint8_t)a; VDPC_T = (uint8_t)(a >> 8) | 0x40; }
static void vwait(void) { while (!(VDPC_T & 0x80)) ; }
static void tprint(uint8_t x, uint8_t y, const char *s, uint8_t hi) {
  taddr(0x3800 + ((uint16_t)y * 32 + x) * 2);
  while (*s) { uint8_t c = (uint8_t)(*s++ - 32); VDPD_T = hi ? c + 96 : c; VDPD_T = 0; }
}

static void draw_menu(uint8_t sel) {
  tprint(8, 12, sel == 0 ? "> START" : "  START", sel == 0);
  tprint(8, 14, sel == 1 ? "> FRAME SKIP: " : "  FRAME SKIP: ", sel == 1);
  tprint(22, 14, opt_skip == 0 ? "OFF   " : opt_skip == 1 ? "NORMAL" : "HIGH  ", sel == 1);
}
/* returns 0 = start the game, 1 = no input for 30 s (replay the intro) */
uint8_t run_title(void) {
  uint16_t i, idle = 0;
  if (opt_skip > 2) opt_skip = 2; uint8_t b, r, sel = 0, prev = 0xFF, now;
  treg(1, 0x80); treg(0, 0x06); treg(2, 0xFF); treg(3, 0xFF); treg(4, 0xFF); treg(5, 0xFF); treg(6, 0xFB);
  treg(7, 0x00); treg(8, 0); treg(9, 0); treg(10, 0xFF);
  taddr(0x3800); for (i = 0; i < 32 * 28; i++) { VDPD_T = 0; VDPD_T = 0; }
  taddr(0x3F00); VDPD_T = 0xD0;
  /* font: tiles 0-95 in colour 1, tiles 96-191 in colour 3 (highlight) */
  slot2 = CONST_BANK; MAPCTL_T = 0; MAP2_T = CONST_BANK;
  taddr(0);
  for (b = 0; b < 2; b++) for (i = 0; i < 96 * 8; i++) {
    r = FONT8[i]; VDPD_T = r; VDPD_T = b ? r : 0; VDPD_T = 0; VDPD_T = 0;
  }
  VDPC_T = 0; VDPC_T = 0xC0;
  VDPD_T = 0x00; VDPD_T = 0x3F; VDPD_T = 0x15; VDPD_T = 0x0F;
  for (i = 4; i < 32; i++) VDPD_T = 0;
  tprint(8, 4, "OUT OF THIS WORLD", 1);
  tprint(6, 6, "SEGA MASTER SYSTEM PORT", 0);
  tprint(4, 18, "BUTTON 1   ACTION / RUN", 0);
  tprint(4, 19, "BUTTON 2   JUMP", 0);
  tprint(4, 20, "PAUSE      PAUSE", 0);
  draw_menu(sel);
  vwait(); treg(1, 0xC0);
  for (;;) {
    vwait();
    now = ~JOY_T & 0x3F;
    if (now) idle = 0; else if (++idle > 1800) { treg(1, 0x80); return 1; }
    b = now & ~prev; prev = now;            /* newly pressed */
    if (!b) continue;
    if (b & 0x01) sel = 0;
    if (b & 0x02) sel = 1;
    /* (no % or other library helpers here: this code runs from bank 12 while slot 1 is remapped) */
    if (sel == 1 && (b & 0x04)) opt_skip = opt_skip ? opt_skip - 1 : 2;
    if (sel == 1 && (b & 0x08)) opt_skip = opt_skip < 2 ? opt_skip + 1 : 0;
    if (b & 0x30) {
      if (sel == 0) { while (~JOY_T & 0x30) vwait(); treg(1, 0x80); return 0; }
      opt_skip = opt_skip < 2 ? opt_skip + 1 : 0;
    }
    draw_menu(sel);
  }
}

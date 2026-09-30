#include <stdint.h>
#include "SMSlib.h"
#include "fmv.h"
#include "engine.h"
#include "../gen/fmv_data.h"
SMS_EMBED_SEGA_ROM_HEADER(9999, 0);
SMS_EMBED_SDSC_HEADER_AUTO_DATE(1, 0, "SMS port", "Out of this World", "Another World / Out of this World demo for Sega Master System");
__sfr __at 0xDC JOYPORT;
#define MAP1 (*(volatile uint8_t *)0xFFFE)
/* the FMV player lives in ROM bank 12, mapped into slot 1 only while it runs */
static void play_fmv(uint8_t n) { __asm di __endasm; MAP1 = 12; fmv_play(n); MAP1 = 1; }
uint8_t stage, last_exit;
uint16_t intro_plays;
void main(void) {
  for (;;) {
    stage = 1; intro_plays++;
    play_fmv(FMV_INTRO);
    while ((JOYPORT & 0x30) != 0x30) ;      /* the button that skipped the intro must not act in-game */
    stage = 2;
    last_exit = game_run();
    if (last_exit == GAME_EXIT_CAPTURE) { stage = 3; play_fmv(FMV_CAPTURE); }   /* then the demo restarts with the intro */
  }
}

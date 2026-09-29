#ifndef FMV_H
#define FMV_H
#include <stdint.h>
uint8_t fmv_play(uint8_t cut);   /* returns 1 if skipped */
extern uint16_t fmv_frames_shown, fmv_late_max;
#endif

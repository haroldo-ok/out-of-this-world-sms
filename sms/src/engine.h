#ifndef ENGINE_H
#define ENGINE_H
#include <stdint.h>
#define GAME_EXIT_TO_INTRO   1   /* script asked for part 16000/16001 */
#define GAME_EXIT_CAPTURE    2   /* the capture cutscene starts: hand over to FMV */
uint8_t game_run(void);
extern int16_t vars[256];
extern uint16_t game_frames, gfx_alloc_fail, vm_bad_op;
extern volatile uint16_t ticks;
void upload_tile(uint16_t addr, const uint8_t *src) __sdcccall(1);
#endif

#ifndef TRACE_H__
void bg_fill(int p, uint8_t c); void bg_shape(int p, int seg, uint16_t off, int16_t x, int16_t y, uint16_t zoom);
void bg_string(int p, uint16_t id, uint8_t x, uint8_t y, uint8_t c); void bg_use(int p); void bg_copy(int src, int dst, int scroll, int16_t vs);
extern int g_dead; extern int g_lastCopySrc;
#define TRACE_H__
#include <stdio.h>
#include <stdint.h>
struct Color;
extern FILE *g_trace;          // text event trace (may be NULL)
extern uint32_t g_vtime;        // virtual ms clock
extern uint32_t g_opcount[256]; // executed opcodes
extern uint32_t g_opsThisFrame;
extern void (*g_varsHook)();
void trace_frame(const uint8_t *page, const Color *pal); // called on each displayed frame
#define TR(...) do { if (g_trace) { fprintf(g_trace, "%u ", g_vtime); fprintf(g_trace, __VA_ARGS__); fputc('\n', g_trace);} } while(0)
#endif

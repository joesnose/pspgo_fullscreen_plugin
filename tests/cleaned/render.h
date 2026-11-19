#ifndef GOTVSCALE_RENDER_H
#define GOTVSCALE_RENDER_H
#include <stdint.h>
enum { FILTER_SHARP, FILTER_SOFT, FILTER_BLUR, FILTER_CRT, FILTER_SOFT_CRT,
       FILTER_RGB_MASK, FILTER_SHARPEN, FILTER_POSTERISE, FILTER_DITHER, FILTER_DEPOSTERISE, FILTER_COUNT };
#define TV_STRIDE 768
#define TV_HEIGHT 480
#define TV_FRAME_BYTES (TV_STRIDE*TV_HEIGHT*2u)
#define TV_VRAM_OFFSET 0x200000u
typedef struct {uint32_t address;int stride,format;unsigned serial;} Frame;
typedef struct {float u,v;int16_t x,y,z,pad;} ScaleVertex;
typedef struct {uint32_t color;int16_t x,y,z,pad;} ClearVertex;
typedef struct __attribute__((aligned(64))) {
  uint32_t commands[256];
  ScaleVertex vertices[180];
  ClearVertex scanlines[1440];
  ClearVertex post[4];
  ClearVertex clear[2];
  unsigned count;
} ScalePacket;
int ValidFrame(Frame f);
int BuildFilteredScalePacket(ScalePacket *p,Frame source,unsigned output,int aspect,int filter);
int BuildScalePacket(ScalePacket *p,Frame source,unsigned output,int aspect);
#endif

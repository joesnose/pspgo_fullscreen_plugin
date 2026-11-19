#ifndef GOTV_EFFECTS_H
#define GOTV_EFFECTS_H

#include "render.h"

const char *FilterName(int filter);
unsigned SmoothBandPixel(unsigned left, unsigned centre, unsigned right);
int DeposteriseFrame(Frame source, unsigned *destination, unsigned stride);
void DrawFilterNotice(unsigned short *surface, unsigned pitch, const char *label);

#endif /* GOTV_EFFECTS_H */

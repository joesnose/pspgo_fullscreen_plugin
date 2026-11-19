#include "render.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
static ScalePacket p;
int main(void) {
  assert(TV_VRAM_OFFSET+2*TV_FRAME_BYTES<=0x400000);
  assert(!(TV_VRAM_OFFSET&8191)&&!(TV_FRAME_BYTES&8191));
  unsigned cases=0;
  for(int format=0;format<4;++format)for(int stride=512;stride<=1024;stride+=64)
    for(int aspect=0;aspect<2;++aspect)for(unsigned target=0;target<2;++target) {
      Frame f={0x04000000,stride,format,1};
      assert(ValidFrame(f));assert(BuildScalePacket(&p,f,target,aspect)==0);
      assert(p.count<256&&p.commands[p.count-2]==0x0f000000&&p.commands[p.count-1]==0x0c000000);
      int width=aspect?540:720,left=(720-width)/2,previous=left;
      for(int i=0;i<15;++i) {
        ScaleVertex a=p.vertices[i*2],b=p.vertices[i*2+1];
        assert(a.x==previous&&b.x>a.x&&a.y==0&&b.y==480);
        assert(a.u>=0.5f&&b.u<=479.5001f&&a.v==0.5f&&b.v==271.5f);
        if(i)assert(fabsf(a.u-p.vertices[i*2-1].u)<0.0001f);
        previous=b.x;
      }
      assert(previous==left+width);
      if(aspect)assert(p.clear[0].x==0&&p.clear[1].x==720&&p.clear[1].y==480);
      int fb=0,fmt=0,ctx=0;
      for(unsigned i=0;i<p.count;++i) {
        unsigned cmd=p.commands[i]>>24,arg=p.commands[i]&0xffffff;
        if(cmd==0x9c){assert(arg==TV_VRAM_OFFSET+target*TV_FRAME_BYTES);++fb;}
        if(cmd==0xc3){assert(arg==(unsigned)format);++fmt;}
        if(cmd==0x12){assert(arg&(1u<<23));++ctx;}
      }
      assert(fb==1&&fmt==1&&ctx==1+aspect);++cases;
    }
  Frame invalid[]={{0x04200000,512,3,0},{0x041f0000,512,3,0},
    {0x04000004,512,0,0},{0x04000000,480,0,0},{0x04000000,512,4,0},
    {0x04000000,1088,0,0},{0x0bfff000,512,3,0},{0x88000000,512,3,0},
    {0,512,0,0}};
  for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i)assert(!ValidFrame(invalid[i]));
  assert(ValidFrame((Frame){0x08800000,512,3,0}));
  assert(ValidFrame((Frame){0x44000000,512,3,0}));
  puts("GPU packet checks: 144 format/stride/aspect/buffer combinations, contiguous strips, protected VRAM and invalid sources passed.");
  assert(cases==144);
}

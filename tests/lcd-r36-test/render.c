#include "render.h"
#include <stddef.h>
_Static_assert(sizeof(ScaleVertex)==16,"GE UV float/XYZ short vertex stride");
_Static_assert(sizeof(ClearVertex)==12,"GE color/XYZ short vertex stride");
int ValidFrame(Frame f) {
  if(f.address&0x80000000u)return 0; // Alpha 1 accepts user/uncached aliases only.
  uint32_t a=f.address&0x1fffffffu;
  if((a&15)||f.stride<480||f.stride>1024||(f.stride&63)||f.format<0||f.format>3)return 0;
  uint32_t bytes=(uint32_t)f.stride*272*(f.format==3?4u:2u);
  if(a>=0x04000000u&&a<0x04200000u)return bytes<=0x04200000u-a;
  if(a>=0x08000000u&&a<0x0c000000u)return bytes<=0x0c000000u-a;
  return 0;
}
static void Emit(ScalePacket *p,unsigned cmd,unsigned arg) {
  p->commands[p->count++]=(cmd<<24)|(arg&0xffffff);
}
static void Address(ScalePacket *p,unsigned cmd,const void *ptr) {
  unsigned a=(uintptr_t)ptr&0x1fffffffu;
  Emit(p,0x10,(a>>8)&0xf0000);Emit(p,cmd,a);
}
int BuildFilteredOutputPacket(ScalePacket *p,Frame s,unsigned output,int aspect,int filter,int lcd) {
  int canvas_width=lcd?480:720,canvas_height=lcd?272:480,stride=lcd?512:TV_STRIDE;
  unsigned frame_bytes=(unsigned)stride*canvas_height*2u;
  if(lcd!=0&&lcd!=1)return -1;
  if(!ValidFrame(s)||output>1||(aspect!=0&&aspect!=1)||filter<FILTER_SHARP||filter>=FILTER_COUNT)return -1;
  // Initialise metadata; each referenced array is written explicitly below.
  p->count=p->image_vertices=p->overlay_vertices=p->post_vertices=p->clear_vertices=0;
  unsigned dest=TV_VRAM_OFFSET+output*frame_bytes;
  const unsigned end=(unsigned)(canvas_width-1)|((unsigned)(canvas_height-1)<<10);
  // An isolated 2D state; GE's per-list context restores the game's state.
  for(unsigned cmd=0x17;cmd<=0x28;++cmd)Emit(p,cmd,0);
  Emit(p,0x13,0);Emit(p,0x4c,0);Emit(p,0x4d,0);
  Emit(p,0x15,0);Emit(p,0x16,end);Emit(p,0xd4,0);Emit(p,0xd5,end);
  Emit(p,0x9c,dest);Emit(p,0x9d,stride);Emit(p,0xd2,0); // RGB565 output.
  Emit(p,0xe8,0);Emit(p,0xe9,0);Emit(p,0xe7,1); // No channel masks; no Z writes.
  // In stretched fullscreen the first opaque image pass overwrites every
  // destination pixel. Clear only when pillarbox borders need black pixels.
  if(aspect&&!lcd){
  p->clear_vertices=2;
  p->clear[0]=(ClearVertex){0,0,0,0,0};
  p->clear[1]=(ClearVertex){0,canvas_width,canvas_height,0,0};
  Emit(p,0xd3,0x101); // Clear color only.
  Emit(p,0x12,(1u<<23)|(2<<7)|(7<<2));Address(p,1,p->clear);
  Emit(p,4,(6<<16)|2);
  }
  Emit(p,0xd3,0); // Always leave clear mode, even without a border clear.
  if(filter==FILTER_DITHER||filter==FILTER_SOFT_CRT) {
    // Ordered values -4..3, balanced around zero for RGB565 conversion.
    Emit(p,0xe2,0x2e0c);Emit(p,0xe3,0xf3d1);Emit(p,0xe4,0x0c2e);Emit(p,0xe5,0xd1f3);Emit(p,0x20,1);
  }
  Emit(p,0x1e,1);Emit(p,0xc0,0);Emit(p,0xc2,0);Emit(p,0xc3,s.format);
  Emit(p,0x48,0x3f8000);Emit(p,0x49,0x3f8000);Emit(p,0x4a,0);Emit(p,0x4b,0);
  Emit(p,0xc6,filter==FILTER_SHARP?0:0x101);Emit(p,0xc7,0x101);Emit(p,0xc8,0);Emit(p,0xc9,3);
  unsigned tex=s.address&0x1fffffffu;
  Emit(p,0xa0,tex);Emit(p,0xa8,((tex>>8)&0xf0000)|s.stride);
  Emit(p,0xb8,9|(9<<8));Emit(p,0xcc,0);Emit(p,0xcb,0);
  int width=lcd?480:aspect?540:720,left=(canvas_width-width)/2;
  static const float dx[6]={0,-0.75f,0.75f,-0.75f,0.75f,0};
  static const float dy[6]={0,-0.75f,-0.75f,0.75f,0.75f,0};
  int passes=filter==FILTER_SOFTER?4:(filter==FILTER_BLUR||filter==FILTER_BLURRIER)?5:filter==FILTER_SHARPEN?6:1;
  p->image_vertices=(unsigned)passes*30u;
  for(int pass=0;pass<passes;++pass) {
    float offset_x=dx[pass],offset_y=dy[pass];
    // Softer averages four bilinear samples; Blurrier uses 1.5 times Blur's radius.
    if(filter==FILTER_SOFTER){offset_x=dx[pass+1]*(2.f/3.f);offset_y=dy[pass+1]*(2.f/3.f);}
    if(filter==FILTER_BLURRIER){offset_x*=1.5f;offset_y*=1.5f;}
    ScaleVertex *vertices=&p->vertices[pass*30];
    for(int i=0;i<15;++i) {
      int a=i*32,b=a+32;
      float u0=0.5f+a*(479.f/480.f)+offset_x,u1=0.5f+b*(479.f/480.f)+offset_x;
      float v0=0.5f+offset_y,v1=271.5f+offset_y;
      if(u0<0.5f)u0=0.5f;
      if(u1>479.5f)u1=479.5f;
      if(v0<0.5f)v0=0.5f;
      if(v1>271.5f)v1=271.5f;
      vertices[i*2]=(ScaleVertex){u0,v0,left+a*width/480,0,0,0};
      vertices[i*2+1]=(ScaleVertex){u1,v1,left+b*width/480,canvas_height,0,0};
    }
    if(pass) {
      // Running average of this frame's samples; no previous-frame feedback.
      unsigned weight=(255u+(pass+1)/2)/(pass+1);
      Emit(p,0x21,1);
      if(filter==FILTER_SHARPEN) {
        // centre - 4*(neighbour/16) + centre/4, mildly sharpening edges.
        Emit(p,0xdf,pass==5?0xaa:0x2aa);
        Emit(p,0xe0,(pass==5?64u:16u)*0x010101u);Emit(p,0xe1,0xffffff);
      } else {
        Emit(p,0xdf,0xaa);Emit(p,0xe0,weight*0x010101u);Emit(p,0xe1,(255u-weight)*0x010101u);
      }
    }
    Emit(p,0x12,(1u<<23)|(2<<7)|3);Address(p,1,vertices);
    Emit(p,4,(6<<16)|30);
  }
  if(filter==FILTER_CRT||filter==FILTER_SOFT_CRT) {
    p->overlay_vertices=(unsigned)canvas_height;
    unsigned tint=filter==FILTER_SOFT_CRT?0x20000000u:0x40000000u;
    for(int i=0;i<canvas_height/2;++i) {
      p->scanlines[i*2]=(ClearVertex){tint,left,i*2+1,0,0};
      p->scanlines[i*2+1]=(ClearVertex){tint,left+width,i*2+2,0,0};
    }
    Emit(p,0x1e,0);Emit(p,0x21,1);Emit(p,0xdf,0x32); // src alpha / 1-src alpha.
    Emit(p,0x12,(1u<<23)|(2<<7)|(7<<2));Address(p,1,p->scanlines);
    Emit(p,4,(6<<16)|(unsigned)canvas_height);
  }
  if(filter==FILTER_RGB_MASK) {
    p->overlay_vertices=(unsigned)width*2u;
    static const unsigned tint[3]={0xffdfdfffu,0xffdfffdfu,0xffffdfdfu};
    for(int x=0;x<width;++x) {
      p->scanlines[x*2]=(ClearVertex){tint[x%3],left+x,0,0,0};
      p->scanlines[x*2+1]=(ClearVertex){tint[x%3],left+x+1,canvas_height,0,0};
    }
    Emit(p,0x1e,0);Emit(p,0x21,1);Emit(p,0xdf,0xa0);Emit(p,0xe1,0);
    Emit(p,0x12,(1u<<23)|(2<<7)|(7<<2));Address(p,1,p->scanlines);
    Emit(p,4,(6<<16)|(width*2));
  }
  if(filter==FILTER_POSTERISE) {
    p->post_vertices=4;
    p->post[0]=(ClearVertex){0xffe0e0e0u,left,0,0,0};
    p->post[1]=(ClearVertex){0xffe0e0e0u,left+width,canvas_height,0,0};
    p->post[2]=(ClearVertex){0xff242424u,left,0,0,0};
    p->post[3]=(ClearVertex){0xff242424u,left+width,canvas_height,0,0};
    // AND retains the upper three colour bits; a mild gain restores highlights.
    Emit(p,0x1e,0);Emit(p,0x21,0);Emit(p,0x28,1);Emit(p,0xe6,1);
    Emit(p,0x12,(1u<<23)|(2<<7)|(7<<2));Address(p,1,p->post);Emit(p,4,(6<<16)|2);
    Emit(p,0x28,0);Emit(p,0x21,1);Emit(p,0xdf,0xa0);Emit(p,0xe1,0xffffff);
    Address(p,1,p->post+2);Emit(p,4,(6<<16)|2);
  }
  Emit(p,0xf,0);Emit(p,0xc,0);
  return p->count<256?0:-1;
}

int BuildScalePacket(ScalePacket *p,Frame source,unsigned output,int aspect) {
  return BuildFilteredScalePacket(p,source,output,aspect,FILTER_SOFT);
}

int BuildFilteredScalePacket(ScalePacket *p,Frame s,unsigned output,int aspect,int filter){return BuildFilteredOutputPacket(p,s,output,aspect,filter,0);}

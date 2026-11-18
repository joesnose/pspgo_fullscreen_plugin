#include "effects.h"
#include <string.h>
const char *FilterName(int f){
 static const char *names[]={"Sharp","Soft","Softer","Blur","Blurrier","CRT","Soft CRT","RGB Mask","Sharpen","Posterise","Dither","Deposterise"};
 return f>=0&&f<FILTER_COUNT?names[f]:"Unknown";
}
static int Close(unsigned a,unsigned b){
 for(unsigned shift=0;shift<24;shift+=8){int d=(int)((a>>shift)&255)-(int)((b>>shift)&255);if(d>12||d< -12)return 0;}return 1;
}
unsigned SmoothBandPixel(unsigned a,unsigned b,unsigned c){
 if(!Close(a,b)||!Close(c,b))return b;
 unsigned out=b&0xff000000u;
 for(unsigned shift=0;shift<24;shift+=8)out|=((((a>>shift)&255)+2*((b>>shift)&255)+((c>>shift)&255)+2)>>2)<<shift;
 return out;
}
static unsigned Pixel(const unsigned char *row,unsigned x,int fmt){
 if(fmt==3){unsigned v;memcpy(&v,row+x*4,4);return v;}
 unsigned short v;memcpy(&v,row+x*2,2);unsigned r,g,b,a;
 if(fmt==0){r=(v&31)*255/31;g=((v>>5)&63)*255/63;b=((v>>11)&31)*255/31;a=255;}
 else if(fmt==1){r=(v&31)*255/31;g=((v>>5)&31)*255/31;b=((v>>10)&31)*255/31;a=(v>>15)*255;}
 else {r=(v&15)*17;g=((v>>4)&15)*17;b=((v>>8)&15)*17;a=(v>>12)*17;}
 return r|(g<<8)|(b<<16)|(a<<24);
}
/* CPU-only row ring; never submitted to the GPU or used as a GE context. */
static unsigned rows[3][480];
int DeposteriseFrame(Frame f,unsigned *dst,unsigned stride){
 if(!ValidFrame(f)||!dst||stride<480)return -1;
 uintptr_t phys=f.address&0x1fffffffu;
#ifdef HOST_TEST
 const unsigned char *src=(const unsigned char *)phys;
#else
 const unsigned char *src=(const unsigned char *)(phys|0x40000000u);
#endif
 unsigned bytes=f.format==3?4:2;
 unsigned char cached_line[480*4];
 for(unsigned y=0;y<272;y++){
  memcpy(cached_line,src+y*f.stride*bytes,480*bytes);
  unsigned a=Pixel(cached_line,0,f.format),b=a,c=Pixel(cached_line,1,f.format);
  for(unsigned x=0;x<480;x++){
   dst[y*stride+x]=SmoothBandPixel(a,b,c);a=b;b=c;
   c=Pixel(cached_line,x+2<480?x+2:479,f.format);
  }
 }
 memcpy(rows[0],dst,480*4);memcpy(rows[1],dst,480*4);memcpy(rows[2],dst+stride,480*4);
 for(unsigned y=0;y<272;y++){
  for(unsigned x=0;x<480;x++)dst[y*stride+x]=SmoothBandPixel(rows[y%3][x],rows[(y+1)%3][x],rows[(y+2)%3][x]);
  if(y+1<272)memcpy(rows[y%3],dst+(y+2<272?y+2:271)*stride,480*4);
 }
 return 0;
}
/* Tiny CPU-drawn notice, only for 1.5 seconds after a change. No GPU font data. */
static const unsigned char letters[26][5]={
 {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
 {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
 {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
 {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
 {62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
 {7,8,112,8,7},{97,81,73,69,67}};
void DrawFilterNotice(unsigned short *dst,unsigned pitch,const char *label){
 if(!dst||!label||pitch<720)return;
 unsigned n=0;while(label[n]&&n<24)n++;
 unsigned width=n*12+16;
 for(unsigned y=12;y<36;y++)for(unsigned x=12;x<12+width;x++)dst[y*pitch+x]=0x2104;
 for(unsigned i=0;i<n;i++){
  unsigned ch=(unsigned char)label[i];if(ch>='a'&&ch<='z')ch-=32;if(ch<'A'||ch>'Z')continue;
  for(unsigned x=0;x<5;x++)for(unsigned y=0;y<7;y++)if(letters[ch-'A'][x]&(1u<<y))
   for(unsigned yy=0;yy<2;yy++)for(unsigned xx=0;xx<2;xx++)dst[(17+y*2+yy)*pitch+20+i*12+x*2+xx]=0xffff;
 }
}

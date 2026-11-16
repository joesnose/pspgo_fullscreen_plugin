/* Generic progressive-component GPU scaler prototype. PSP Go / ARK 6.60/6.61. */
#ifdef HOST_TEST
#include "test_psp.h"
#else
#include <pspkernel.h>
#include <psploadcore.h>
#include <pspsysmem_kernel.h>
#include <pspintrman_kernel.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspsdk.h>
#include <pspiofilemgr.h>
#include <pspctrl.h>
#endif
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "render.h"
#include "effects.h"
PSP_MODULE_INFO("PSPGoTVScaleV1Auto",0x1000,1,7);
extern void sctrlHENPatchSyscall(void *,void *);
static const char *logpath;
#include "logger.inc"

typedef int (*SetFrameFn)(int,void *,int,int,int);
typedef int (*GetFrameFn)(int,void **,int *,int *,int);
typedef int (*SetModeFn)(int,int,int);
typedef int (*GetModeFn)(int *,int *,int *);
typedef int (*IntFn)(void);
typedef int (*SetSizeFn)(int);
typedef struct __attribute__((aligned(64))) {
  unsigned *address,saved[2],trampoline[4];
} Hook;
static Hook frame_hook,mode_hook;
static SetFrameFn real_frame;
static SetModeFn real_mode;
static GetFrameFn get_frame;
static GetModeFn get_mode;
static IntFn get_size,get_hw_size;
static SetSizeFn set_size;
static volatile int running=1,active,reserved,abort_requested;
static int hooks_installed,aspect,output_index,restore_mode=0x2d2;
static SceUID worker=-1;
static volatile Frame source,original0;
static volatile unsigned presentations;
static void *ready_buffer;
/* GE cannot normally fetch kernel-partition buffers. All GPU-consumed
 * commands, vertices and saved-context lists belong to one user-RAM block. */
typedef struct __attribute__((aligned(64))) {
  ScalePacket packet;
  PspGeContext context;
} GeWorkspace;
_Static_assert(offsetof(GeWorkspace,context)%64==0,"GE context alignment");
static SceUID gpu_memory=-1;
static GeWorkspace *gpu_workspace;
static ScalePacket *packet;
static PspGeContext *ge_context;
static int pending_list=-1;
static unsigned draws,timeouts,invalid_frames;
static uint64_t wait_total;
static unsigned max_wait;
static int filter_mode=FILTER_SOFT;
static IntFn home_status;
static volatile int home_paused,home_scaled_ready,reboot_exit,reboot_late;
static int reboot_joined,worker_done;
static int home_output_restored,auto_fault;
static unsigned next_auto,last_home_resolve,auto_ready_since;
static int auto_ready_wait;
static int MenuVisible(void){return home_status&&(home_status()&0x100)!=0;}
static int ScalingVisible(void){return running&&!reboot_exit&&active&&!home_paused&&!MenuVisible();}
static int MenuScalingVisible(void){return running&&!reboot_exit&&home_paused&&home_scaled_ready&&MenuVisible();}

#define DEPOSTER_BYTES (512u*272u*4u)
static SceUID dep_memory=-1;
static unsigned *dep_pixels;
static unsigned notice_started;
static int notice_active,triangle_down;
typedef int (*PadFn)(SceCtrlData *,int);
static PadFn merged_peek;
extern unsigned sctrlFindImportByNID(SceModule *,char *,unsigned);
static int FreeDepMemory(void) {
  if(pending_list>=0)return -1;
  if(dep_memory<0)return 0;
  int ret=sceKernelFreePartitionMemory(dep_memory);
  if(ret>=0){dep_memory=-1;dep_pixels=NULL;}
  return ret;
}
static int InitDepMemory(void) {
  if(dep_memory>=0)return 0;
  unsigned bytes=DEPOSTER_BYTES+63;
  SceUID uid=sceKernelAllocPartitionMemory(2,"GoTVScaleDep",PSP_SMEM_High,bytes,NULL);
  if(uid<0)return -1;
  uintptr_t head=(uintptr_t)sceKernelGetBlockHeadAddr(uid)&0x1fffffffu;
  uintptr_t aligned=(head+63u)&~(uintptr_t)63u;
  if(head<0x08800000u||head>=0x0c000000u||bytes>0x0c000000u-head||aligned<head||aligned+DEPOSTER_BYTES>head+bytes){sceKernelFreePartitionMemory(uid);return -1;}
  dep_memory=uid;dep_pixels=(unsigned *)aligned;return 0;
}
static void ResolveInput(void) {
  merged_peek=NULL;
  SceModule *m=sceKernelFindModuleByName("sceImpose_Driver");
  if(!m||m->text_size<8)return;
  unsigned stub=sctrlFindImportByNID(m,"sceCtrl_driver",0x2ba616af);
  if((stub&3)||stub<m->text_addr||stub-m->text_addr>m->text_size-8)return;
#ifdef HOST_TEST
  unsigned first=TestCodeWord(stub),second=TestCodeWord(stub+4);
#else
  unsigned first=*(volatile unsigned *)(uintptr_t)stub,second=*(volatile unsigned *)(uintptr_t)(stub+4);
#endif
  if((first>>26)!=2||second!=0)return;
  unsigned target=((stub+4)&0xf0000000u)|((first&0x03ffffffu)<<2);
  SceModule *owner=sceKernelFindModuleByAddress(target);
  if(!owner||!(owner->attribute&0x1000)||target<owner->text_addr||target-owner->text_addr>=owner->text_size)return;
  merged_peek=(PadFn)(uintptr_t)target;
}
static void PollFilter(int menu,unsigned now) {
  SceCtrlData pad;memset(&pad,0,sizeof(pad));
  int ret=merged_peek?merged_peek(&pad,1):sceCtrlPeekBufferPositive(&pad,1);
  if(ret<=0)return;
  int triangle=(pad.Buttons&PSP_CTRL_TRIANGLE)!=0;
  unsigned chord=PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER;
  if(!menu&&!reboot_exit&&pending_list<0&&triangle&&!triangle_down&&(pad.Buttons&chord)==chord) {
    int next=(filter_mode+1)%FILTER_COUNT;
    if(next==FILTER_DEPOSTERISE&&InitDepMemory()<0){logline("deposterise_memory_unavailable,skipped\n");next=FILTER_SHARP;}
    filter_mode=next;
    if(next!=FILTER_DEPOSTERISE)FreeDepMemory();
    notice_started=now;notice_active=1;logline("filter,%s\n",FilterName(next));
  }
  triangle_down=triangle;
}
static int InitGeMemory(void) {
  if(gpu_memory>=0)return 0;
  unsigned bytes=sizeof(GeWorkspace)+63u;
  SceUID uid=sceKernelAllocPartitionMemory(2,"GoTVScaleGE",PSP_SMEM_High,bytes,NULL);
  if(uid<0){logline("gpu_memory_alloc_error,0x%08X\n",(unsigned)uid);return -1;}
  uintptr_t head=(uintptr_t)sceKernelGetBlockHeadAddr(uid)&0x1fffffffu;
  uintptr_t aligned=(head+63u)&~(uintptr_t)63u;
  if(head<0x08800000u||head>=0x0c000000u||bytes>0x0c000000u-head||
     aligned<head||aligned+sizeof(GeWorkspace)>head+bytes) {
    sceKernelFreePartitionMemory(uid);logline("gpu_memory_bad_user_address\n");return -1;
  }
  gpu_memory=uid;gpu_workspace=(GeWorkspace *)aligned;
  memset(gpu_workspace,0,sizeof(*gpu_workspace));
  packet=&gpu_workspace->packet;ge_context=&gpu_workspace->context;
  logline("gpu_memory,user_partition,2,packet,0x%08X,context,0x%08X,bytes,%u\n",
          (unsigned)(uintptr_t)packet,(unsigned)(uintptr_t)ge_context,(unsigned)sizeof(*gpu_workspace));
  return 0;
}
static int FreeGeMemory(void) {
  if(pending_list>=0)return -1;
  if(gpu_memory<0)return 0;
  int ret=sceKernelFreePartitionMemory(gpu_memory);
  if(ret>=0){gpu_memory=-1;gpu_workspace=NULL;packet=NULL;ge_context=NULL;}
  return ret;
}
static unsigned Find(const char *module,const char *lib,unsigned nid) {
  SceModule *m=sceKernelFindModuleByName(module);
  if(!m)return 0;
  unsigned char *p=m->ent_top,*end=p+m->ent_size;
  while(p+sizeof(SceLibraryEntryTable)<=end) {
    SceLibraryEntryTable *e=(SceLibraryEntryTable *)p;
    unsigned bytes=e->len*4u,count=e->stubcount+e->vstubcount;
    if(bytes<sizeof(*e)||p+bytes>end)break;
    if(e->libname&&!strcmp(e->libname,lib)&&e->entrytable&&count<=1024) {
      unsigned *t=e->entrytable;
      for(unsigned i=0;i<e->stubcount;++i)if(t[i]==nid)return t[count+i];
    }
    p+=bytes;
  }
  return 0;
}
/* NODS handles Sony PS by opening native HOME and consumes the pad bit.
 * Read that same guarded status getter; do not change NODS or patch HOME. */
static void ResolveHomeStatus(void) {
  unsigned g=Find("sceImpose_Driver","sceImpose_driver",0xb3eae8f0);
  if(!g)return;
#ifdef HOST_TEST
  (void)g;
#else
  const unsigned *w=(const unsigned *)(uintptr_t)g;
  if((w[0]&0xffff0000u)==0x3c030000u&&(w[1]&0xffff0000u)==0x8c640000u&&
     w[2]==0x03e00008u&&w[3]==0x8c8200a4u)home_status=(IntFn)(uintptr_t)g;
#endif
}
static unsigned Read(unsigned off){
#ifdef HOST_TEST
  return TestRead(off);
#else
  return *(volatile const unsigned *)(0xBE140000u+off);
#endif
}
static int TvActive(void) {
  unsigned x=Read(0x1c);
  return (Read(0)&3)==3&&Read(0x2c)==480&&x>=1800&&x<=2200;
}
static Frame Snapshot(volatile Frame *from) {
  int intr=sceKernelCpuSuspendIntr();Frame f=*from;sceKernelCpuResumeIntr(intr);return f;
}
static void Capture(volatile Frame *to,void *ptr,int stride,int format) {
  int intr=sceKernelCpuSuspendIntr();
  to->address=(uintptr_t)ptr;to->stride=stride;to->format=format;to->serial++;
  sceKernelCpuResumeIntr(intr);
}
static int ValidOutputFrame(Frame f) {
  // Firmware HOME layers can use a kernel alias; GPU source rules stay v1.
  f.address&=0x1fffffffu;return ValidFrame(f);
}
static int FrameHook(int pri,void *ptr,int stride,int format,int sync) {
  // No GPU operations, waits or file IO in the display callback/interrupt path.
  if(pri==2){Capture(&source,ptr,stride,format);++presentations;}
  if(pri==0) {
    Frame proposed={(uintptr_t)ptr,stride,format,0};
    if(!reserved||!ptr||ValidOutputFrame(proposed))Capture(&original0,ptr,stride,format);
    if((ScalingVisible()||MenuScalingVisible())&&ptr&&ready_buffer){ptr=ready_buffer;stride=TV_STRIDE;format=0;}
  }
  return real_frame(pri,ptr,stride,format,sync);
}
static int ModeHook(int mode,int width,int height) {
  if(ScalingVisible()||MenuScalingVisible()) {
    if(mode==0xd2&&width==720&&height==480)return real_mode(mode,width,height);
    if((mode==0||mode==0x2d2||mode==0x3d2)&&width==480&&height==272) {
      unsigned k1=pspSdkSetK1(0);
      int ret=real_mode(0xd2,720,480);
      pspSdkSetK1(k1);return ret;
    }
    // Native TV-size video modes and other layouts are outside alpha 1.
    abort_requested=1;
  }
  return real_mode(mode,width,height);
}
static int GetModeHook(int *mode,int *width,int *height) {
  // Original validates caller pointers before any writes made here.
  int ret=get_mode(mode,width,height);
  if(ret>=0&&reserved){if(mode)*mode=0;if(width)*width=480;if(height)*height=272;}
  return ret;
}
static int GetSizeHook(void){return reserved?0x200000:get_size();}
static unsigned Jump(const void *p){return 0x08000000u|(((uintptr_t)p&0x0fffffffu)>>2);}
static void PrepareHook(Hook *h,unsigned address) {
  h->address=(unsigned *)address;
  h->saved[0]=h->address[0];h->saved[1]=h->address[1];
  h->trampoline[0]=h->saved[0];h->trampoline[1]=h->saved[1];
  h->trampoline[2]=Jump(h->address+2);h->trampoline[3]=0;
}
static void CacheHooks(void) {
  sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
}
static int InstallHooks(void) {
  unsigned f=Find("sceDisplay_Service","sceDisplay_driver",0x3e17fe8d);
  unsigned m=Find("sceDisplay_Service","sceDisplay_driver",0x0e20f177);
  get_frame=(GetFrameFn)Find("sceDisplay_Service","sceDisplay_driver",0xf338aae0);
  get_mode=(GetModeFn)Find("sceDisplay_Service","sceDisplay_driver",0xdea197d4);
  get_size=(IntFn)Find("sceGE_Manager","sceGe_driver",0x5d28a52b);
  get_hw_size=(IntFn)Find("sceGE_Manager","sceGe_driver",0x547ec5f0);
  set_size=(SetSizeFn)Find("sceGE_Manager","sceGe_driver",0xd8633888);
  if(!f||!m||!get_frame||!get_mode||!get_size||!get_hw_size||!set_size) {
    logline("missing_required_exports\n");return -1;
  }
  unsigned *fp=(unsigned *)f,*mp=(unsigned *)m;
  // Exact straight-line prologues checked against the Go firmware inspected.
  if(fp[0]!=0x27bdffe0||fp[1]!=0xafb10004||mp[0]!=0x27bdffc0||mp[1]!=0x001b12c0) {
    logline("prologue_mismatch,frame,0x%08X,0x%08X,mode,0x%08X,0x%08X\n",fp[0],fp[1],mp[0],mp[1]);return -1;
  }
  PrepareHook(&frame_hook,f);PrepareHook(&mode_hook,m);
  real_frame=(SetFrameFn)frame_hook.trampoline;real_mode=(SetModeFn)mode_hook.trampoline;
  int intr=sceKernelCpuSuspendIntr();
  fp[0]=Jump(FrameHook);fp[1]=0;mp[0]=Jump(ModeHook);mp[1]=0;CacheHooks();
  sceKernelCpuResumeIntr(intr);
  sctrlHENPatchSyscall((void *)get_mode,GetModeHook);
  sctrlHENPatchSyscall((void *)get_size,GetSizeHook);
  hooks_installed=1;return 0;
}
static void RemoveHooks(void) {
  if(!hooks_installed)return;
  sctrlHENPatchSyscall(GetModeHook,(void *)get_mode);
  sctrlHENPatchSyscall(GetSizeHook,(void *)get_size);
  int intr=sceKernelCpuSuspendIntr();
  frame_hook.address[0]=frame_hook.saved[0];frame_hook.address[1]=frame_hook.saved[1];
  mode_hook.address[0]=mode_hook.saved[0];mode_hook.address[1]=mode_hook.saved[1];
  CacheHooks();sceKernelCpuResumeIntr(intr);hooks_installed=0;
}
static int WaitList(unsigned timeout_us) {
  unsigned start=sceKernelGetSystemTimeLow();
  for(;;) {
    if(reboot_exit&&reboot_late)return -1;
    int status=sceGeListSync(pending_list,1);
    if(status==PSP_GE_LIST_DONE||status==PSP_GE_LIST_CANCEL_DONE){pending_list=-1;return 0;}
    if(status<0){logline("list_sync_error,0x%08X\n",(unsigned)status);return -1;}
    if((unsigned)(sceKernelGetSystemTimeLow()-start)>=timeout_us) {
      // A queued list can be removed without cancelling the game's work.
      if(status==PSP_GE_LIST_QUEUED&&sceGeListDeQueue(pending_list)>=0)pending_list=-1;
      return -1;
    }
    sceKernelDelayThread(250);
  }
}
static int RenderWithFilter(Frame f,unsigned target,int selected_filter) {
  if(!running||reboot_exit||pending_list>=0)return -1;
  if(!packet||!ge_context)return -1;
  if(BuildFilteredScalePacket(packet,f,target,aspect,selected_filter)<0){++invalid_frames;return -1;}
  sceKernelDcacheWritebackRange(packet,sizeof(*packet));
  PspGeListArgs args={sizeof(args),ge_context,0,NULL};
  pending_list=sceGeListEnQueue(packet->commands,NULL,-1,&args);
  if(pending_list<0){logline("enqueue_error,0x%08X\n",(unsigned)pending_list);return -1;}
  unsigned start=sceKernelGetSystemTimeLow();
  int ret=WaitList(100000);
  unsigned elapsed=sceKernelGetSystemTimeLow()-start;
  wait_total+=elapsed;if(elapsed>max_wait)max_wait=elapsed;
  if(ret<0){++timeouts;abort_requested=1;return -1;}
  ++draws;return 0;
}
static int Render(Frame f,unsigned target){
  if(!running||reboot_exit||pending_list>=0)return -1;
  if(filter_mode==FILTER_DEPOSTERISE) {
    if(InitDepMemory()<0||DeposteriseFrame(f,dep_pixels,512)<0)return -1;
    sceKernelDcacheWritebackRange(dep_pixels,DEPOSTER_BYTES);
    f=(Frame){(uintptr_t)dep_pixels,512,3,f.serial};
  }
  int ret=RenderWithFilter(f,target,filter_mode);
  if(ret>=0&&notice_active&&!MenuVisible()&&!reboot_exit) {
    if(sceKernelGetSystemTimeLow()-notice_started>=1500000u)notice_active=0;
#ifndef HOST_TEST
    else DrawFilterNotice((unsigned short *)(0x44000000u+TV_VRAM_OFFSET+target*TV_FRAME_BYTES),TV_STRIDE,FilterName(filter_mode));
#endif
  }
  return ret;
}
static int Deactivate(void) {
  if(pending_list>=0)return -1;
  active=0;
  if(reserved) {
    int ret=0;
    if(TvActive())ret=real_mode(restore_mode,480,272);
    Frame f=Snapshot(&original0);
    if(!ValidFrame(f))f=Snapshot(&source);
    if(f.address&&ValidFrame(f))real_frame(0,(void *)(uintptr_t)f.address,f.stride,f.format,1);
    // Never shrink EDRAM while a plugin GE list could still reference it.
    int intr=sceKernelCpuSuspendIntr();
    if(sceGeDrawSync(1)==PSP_GE_LIST_DONE&&ret>=0)ret=set_size(0x200000);
    else ret=-1;
    sceKernelCpuResumeIntr(intr);
    if(ret<0){logline("disable_pending_edram_cleanup\n");return -1;}
    reserved=0;
  }
  abort_requested=0;ready_buffer=NULL;logline("disabled\n");return 0;
}
static int Activate(void) {
  int mode=-1,width=0,height=0;
  get_mode(&mode,&width,&height);
  if(!TvActive()||width!=480||height!=272||(mode!=0x2d2&&mode!=0x3d2)) {
    logline("activation_rejected,mode,0x%X,width,%d,height,%d\n",mode,width,height);return -1;
  }
  if(get_size()!=0x200000||get_hw_size()<0x400000) {
    logline("activation_rejected,edram_size,%d,hardware_size,%d\n",get_size(),get_hw_size());return -1;
  }
  void *ptr=NULL;int stride=0,format=-1;
  get_frame(2,&ptr,&stride,&format,0);
  Capture(&source,ptr,stride,format);Frame f=Snapshot(&source);
  if(!ValidFrame(f)){logline("activation_rejected,source,0x%08X,stride,%d,format,%d\n",f.address,f.stride,f.format);return -1;}
  get_frame(0,&ptr,&stride,&format,0);Capture(&original0,ptr,stride,format);
  int ret=-1,intr=sceKernelCpuSuspendIntr();
  if(sceGeDrawSync(1)==PSP_GE_LIST_DONE){reserved=1;ret=set_size(0x400000);}
  else ret=1;
  sceKernelCpuResumeIntr(intr);
  if(ret>0)return 1;
  if(ret<0){reserved=0;logline("activation_rejected,ge_busy_or_edram_error\n");return -1;}
  restore_mode=mode;abort_requested=0;output_index=0;
  if(Render(f,0)<0){if(pending_list<0)Deactivate();return -1;}
  ready_buffer=(void *)(0x04000000u+TV_VRAM_OFFSET);
  if(MenuVisible()){home_paused=1;home_output_restored=1;return 0;}
  active=1;
  ret=real_mode(0xd2,720,480);
  if(ret>=0&&!MenuVisible())ret=real_frame(0,ready_buffer,TV_STRIDE,0,1);
  if(ret<0){logline("activation_mode_error,0x%08X\n",(unsigned)ret);Deactivate();return -1;}
  logline("enabled,source,0x%08X,stride,%d,format,%d,aspect,%d\n",f.address,f.stride,f.format,aspect);
  return 0;
}
static int ReadyToAutoStart(void) {
  int mode=-1,w=0,h=0,stride=0,format=-1;void *ptr=NULL;
  if(MenuVisible()||get_size()!=0x200000)return 0;
  if(get_mode(&mode,&w,&h)<0||!TvActive()||w!=480||h!=272||(mode!=0x2d2&&mode!=0x3d2))return 0;
  if(get_frame(2,&ptr,&stride,&format,0)<0)return 0;
  return ValidFrame((Frame){(uintptr_t)ptr,stride,format,0});
}
static int PauseForHome(void) {
  active=0;home_paused=1;
  if(home_output_restored||!reserved)return 0;
  if(pending_list>=0)return -1;
  // Impose reads the physical layout through its direct kernel import and
  // prepares its menu for that canvas. Do not reset it to the game geometry.
  Frame f=Snapshot(&original0);
  // NULL is the absence of our high-priority layer, not the game source.
  if(f.address&&!ValidOutputFrame(f))return -1;
  int ret=real_frame(0,(void *)(uintptr_t)f.address,f.stride,f.format,1);
  if(ret<0)return ret;
  home_output_restored=1;
  int mode=-1,w=0,h=0;get_mode(&mode,&w,&h);
  logline("home_paused,menu_layout_preserved,mode,0x%X,width,%d,height,%d,layer,0x%08X,stride,%d,format,%d\n",
          (unsigned)mode,w,h,f.address,f.stride,f.format);
  return 0;
}
static int RenderHome(void) {
  if(!home_paused||!reserved||!home_output_restored||!MenuVisible()||reboot_exit)return 1;
  Frame f=Snapshot(&original0);f.address&=0x1fffffffu;
  // Device log identifies a native 480x272 menu in a 512-stride buffer.
  if(f.stride!=512||!ValidFrame(f))return 1;
  unsigned target=output_index^1;
  if(RenderWithFilter(f,target,FILTER_SOFT)<0)return -1;
  if(!running||reboot_exit||!MenuVisible())return 1;
  int mode=-1,w=0,h=0;get_mode(&mode,&w,&h);
  if(mode!=0xd2||w!=720||h!=480) {
    int ret=real_mode(0xd2,720,480);if(ret<0)return ret;
  }
  ready_buffer=(void *)(uintptr_t)(0x04000000u+TV_VRAM_OFFSET+target*TV_FRAME_BYTES);
  home_scaled_ready=1;
  int ret=real_frame(0,ready_buffer,TV_STRIDE,0,1);
  if(ret<0){home_scaled_ready=0;return ret;}
  output_index=target;return 0;
}
static void ReleaseHomeOutput(void) {
  if(!home_scaled_ready)return;
  home_scaled_ready=0;
  Frame f=Snapshot(&original0);
  if(!f.address||ValidOutputFrame(f))real_frame(0,(void *)(uintptr_t)f.address,f.stride,f.format,1);
}
static int ResumeAfterHome(void) {
  ReleaseHomeOutput();
  if(!reserved)return -1;
  int mode=-1,w=0,h=0,stride=0,format=-1;void *ptr=NULL;
  get_mode(&mode,&w,&h);
  int game_layout=w==480&&h==272&&(mode==0||mode==0x2d2||mode==0x3d2);
  int tv_layout=w==720&&h==480&&mode==0xd2;
  if(!TvActive()||(!game_layout&&!tv_layout))return 1;
  if(get_frame(2,&ptr,&stride,&format,0)<0)return 1;
  Frame f={(uintptr_t)ptr,stride,format,0};if(!ValidFrame(f))return 1;
  Capture(&source,ptr,stride,format);
  if(get_frame(0,&ptr,&stride,&format,0)<0)return 1;
  Frame original={(uintptr_t)ptr,stride,format,0};
  if(original.address&&!ValidOutputFrame(original))return 1;
  Capture(&original0,ptr,stride,format);
  if(mode==0x2d2||mode==0x3d2)restore_mode=mode;
  unsigned target=output_index^1;
  if(Render(f,target)<0)return -1;
  if(MenuVisible())return 1;
  ready_buffer=(void *)(uintptr_t)(0x04000000u+TV_VRAM_OFFSET+target*TV_FRAME_BYTES);
  home_paused=0;home_scaled_ready=0;home_output_restored=0;active=1;
  int ret=real_mode(0xd2,720,480);
  if(ret>=0&&!MenuVisible())ret=real_frame(0,ready_buffer,TV_STRIDE,0,1);
  if(ret<0){active=0;home_paused=1;return -1;}
  output_index=target;logline("home_closed,scaling_resumed\n");return 0;
}
static void AutoControl(int menu,unsigned now) {
  if(auto_fault)return;
  if(menu&&(active||reserved)){active=0;home_paused=1;}
    if(menu) {
      if(home_paused)PauseForHome();
      auto_ready_wait=0;next_auto=now+300000u;
    } else if(home_paused) {
      if(!home_output_restored)PauseForHome();
      else if((int)(now-next_auto)>=0) {
        int ret=ResumeAfterHome();
        if(ret<0){auto_fault=1;home_paused=0;abort_requested=1;}
        next_auto=now+100000u;
      }
    } else if(!active&&!reserved&&!auto_fault) {
      if(!ReadyToAutoStart())auto_ready_wait=0;
      else if(!auto_ready_wait||now-auto_ready_since>=100000u) {
        // No startup wait. Only space retries when the GPU is busy.
        auto_ready_wait=1;auto_ready_since=now;
        logline("auto_enable_requested\n");
        int ret=Activate();
        if(ret<0){auto_fault=1;logline("auto_enable_failed,stopped_for_session\n");}
      }
    }
 }
static void CleanupBeforeReboot(void) {
  if(reboot_late){worker_done=1;return;}
  // Runs on our own early worker while display/GE drivers are still alive.
  active=0;home_scaled_ready=0;
  logline("reboot_worker_begin,pending,%d,reserved,%u\n",pending_list,reserved);
  if(reboot_late){worker_done=1;return;}
  if(pending_list>=0)WaitList(100000);
  if(reboot_late){worker_done=1;return;}
  if(pending_list<0&&reserved) {
    // Never leave the menu/user buffer or scaler layer selected for reboot.
    int frame_ret=real_frame(0,NULL,0,0,1);
    if(reboot_late){worker_done=1;return;}
    int mode_ret=real_mode(restore_mode,480,272);
    logline("reboot_restore,frame,%d,mode,%d\n",frame_ret,mode_ret);
    unsigned vcount=(unsigned)sceDisplayGetVcount(),start=sceKernelGetSystemTimeLow();
    do {sceKernelDelayThread(1000);}while(!reboot_late&&(unsigned)((unsigned)sceDisplayGetVcount()-vcount)<2&&
                                        (unsigned)(sceKernelGetSystemTimeLow()-start)<50000u);
    int intr=sceKernelCpuSuspendIntr();
    if(!reboot_late&&frame_ret>=0&&mode_ret>=0&&(unsigned)((unsigned)sceDisplayGetVcount()-vcount)>=2&&
       sceGeDrawSync(1)==PSP_GE_LIST_DONE&&set_size(0x200000)>=0)reserved=0;
    sceKernelCpuResumeIntr(intr);
  }
  if(!reboot_late)logline("reboot_worker_end,pending,%d,reserved,%u\n",pending_list,reserved);
  // Workspace/trampolines stay resident until firmware has stopped GE.
  worker_done=1;
}
static void LogMenuLayers(void) {
  Frame native=Snapshot(&original0);
  logline("menu_source,scaled,%u,address,0x%08X,stride,%d,format,%d\n",home_scaled_ready,native.address,native.stride,native.format);
  int m=-1,w=0,h=0;get_mode(&m,&w,&h);
  logline("menu_output,mode,0x%X,width,%d,height,%d,lcdc_x,%u,lcdc_y,%u\n",(unsigned)m,w,h,Read(0x1c),Read(0x2c));
  for(int pri=0;pri<3;++pri) {
    void *p=NULL;int stride=0,format=-1;
    int ret=get_frame(pri,&p,&stride,&format,1);
    logline("menu_layer,priority,%d,result,%d,address,0x%08X,stride,%d,format,%d\n",pri,ret,(unsigned)(uintptr_t)p,stride,format);
  }
}
static void Report(void) {
  Frame f=Snapshot(&source);
  logline("stats,ms,%u,active,%u,aspect,%u,presents,%u,draws,%u,queue_gpu_wait_avg_us,%u,max_wait_us,%u,timeouts,%u,invalid_frames,%u,source,0x%08X,stride,%d,format,%d\n",
          sceKernelGetSystemTimeLow()/1000,active,aspect,presentations,draws,
          draws?(unsigned)(wait_total/draws):0,max_wait,timeouts,invalid_frames,f.address,f.stride,f.format);
}
static int MainThread(SceSize argc,void *argv) {
  (void)argc;(void)argv;
  const char *paths[]={"ef0:/PSPGoTVScale.txt","ms0:/PSPGoTVScale.txt"};
  for(unsigned i=0;i<2;++i){SceUID fd=sceIoOpen(paths[i],PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);if(fd>=0){logpath=paths[i];sceIoClose(fd);break;}}
  if(!logpath)return 0;
  unsigned fw=sceKernelDevkitVersion();
  logline("PSPGoTVScale,base,alpha1,variant,auto_home_r7,model,%d,devkit,0x%08X\n",sceKernelGetModel(),fw);
  if(sceKernelGetModel()!=4||(fw!=0x06060010&&fw!=0x06060110)){logline("unsupported_model_or_firmware\n");return 0;}
  filter_mode=FILTER_SOFT;logline("filter,%s\n",FilterName(filter_mode));
  if(InitGeMemory()<0)return 0;
  if(InstallHooks()<0){FreeGeMemory();return 0;}
  ResolveHomeStatus();ResolveInput();
  logline("ready_default_scaled,filter_shortcut,L_R_Triangle,home_status,%u\n",home_status!=NULL);
  unsigned last_report=sceKernelGetSystemTimeLow();
  last_home_resolve=last_report;
  while(running) {
    unsigned now=sceKernelGetSystemTimeLow();
    if(now-last_home_resolve>=500000u){ResolveHomeStatus();ResolveInput();last_home_resolve=now;}
    int menu=MenuVisible();
    if(menu&&(active||reserved)){active=0;home_paused=1;}
    if(pending_list>=0) {
      if(WaitList(1000)<0){sceKernelDelayThread(10000);continue;}
    }
    PollFilter(menu,now);
    if(active&&!TvActive())abort_requested=1;
    if(abort_requested){auto_fault=1;Deactivate();sceKernelDelayThread(10000);continue;}
    AutoControl(menu,now);
    if(menu&&home_paused&&home_output_restored&&!auto_fault) {
      int ret=RenderHome();
      if(ret<0){auto_fault=1;home_scaled_ready=0;logline("menu_scaling_failed\n");}
    } else if(!menu)ReleaseHomeOutput();
    if(active) {
      Frame f=Snapshot(&source);
      if(f.address&&!ValidFrame(f)){++invalid_frames;abort_requested=1;continue;}
      if(f.address) {
        unsigned target=output_index^1;
        if(Render(f,target)>=0&&ScalingVisible()) {
          ready_buffer=(void *)(0x04000000u+TV_VRAM_OFFSET+target*TV_FRAME_BYTES);
          real_frame(0,ready_buffer,TV_STRIDE,0,1);output_index=target;
        }
      }
    }
    now=sceKernelGetSystemTimeLow();
    if(now-last_report>=1000000){Report();if(menu)LogMenuLayers();last_report=now;}
    // Worker never spins while waiting for input or the next output frame.
    if(active||MenuScalingVisible())sceDisplayWaitVblankStart();else sceKernelDelayThread(10000);
  }
  if(reboot_exit){CleanupBeforeReboot();return 0;}
  if(pending_list<0)Deactivate();
  Report();worker_done=1;return 0;
}
int module_start(SceSize args,void *argp) {
  (void)args;(void)argp;
  worker=sceKernelCreateThread("GoTVScale",MainThread,0x18,0x6000,0,NULL);
  if(worker<0)return worker;
  int ret=sceKernelStartThread(worker,0,NULL);
  if(ret<0){sceKernelDeleteThread(worker);worker=-1;}
  return ret;
}
int module_stop(SceSize args,void *argp) {
  (void)args;(void)argp;
  if(pending_list>=0)return -1; // Do not free a context/list still used by GE.
  running=0;
  if(worker>=0){sceKernelWaitThreadEnd(worker,NULL);sceKernelDeleteThread(worker);worker=-1;}
  if(reserved&&Deactivate()<0)return -1;
  RemoveHooks();if(FreeDepMemory()<0)return -1;return FreeGeMemory();
}

/* Early reboot quiesces this worker before user buffers/GE state are replaced.
 * No entry-code rewriting, memory freeing or late driver calls on this path. */
int module_reboot_phase(int phase,void *argp,int a2,int a3) {
  (void)argp;(void)a2;(void)a3;
  if(phase!=3||reboot_joined)return 0;
  reboot_exit=1;active=0;home_scaled_ready=0;running=0;
  if(worker>=0&&!worker_done){SceUInt timeout=500000;sceKernelWaitThreadEnd(worker,&timeout);}
  reboot_joined=1;return 0;
}
int module_reboot_before(void *argp,int a1,int a2,int a3) {
  (void)argp;(void)a1;(void)a2;(void)a3;
  reboot_late=1;reboot_exit=1;active=0;home_scaled_ready=0;running=0;return 0;
}

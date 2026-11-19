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
PSP_MODULE_INFO("PSPGoTVScaleV1Auto",0x1000,1,36);
extern void sctrlHENPatchSyscall(void *,void *);
#define LOGGING_ENABLED 0
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
static int lcd_output;
static int OutputStride(void){return lcd_output?512:TV_STRIDE;}
static unsigned OutputBytes(void){return (unsigned)OutputStride()*(lcd_output?272u:480u)*2u;}
static int OutputMode(void){return lcd_output?0:0xd2;}
static int OutputWidth(void){return lcd_output?480:720;}
static int OutputHeight(void){return lcd_output?272:480;}
static int hooks_installed,aspect,output_index,restore_mode=0x2d2;
static SceUID worker=-1;
static SceUID presentation_event=-1;
#define SCALER_WORKER_PRIORITY 0x18
static unsigned last_scaled_vcount;
static void NotifyWorker(void){
 unsigned k1=pspSdkSetK1(0);int intr=sceKernelCpuSuspendIntr();
 if(presentation_event>=0)sceKernelSetEventFlag(presentation_event,1);
 sceKernelCpuResumeIntr(intr);pspSdkSetK1(k1);
}
static void ClosePresentationEvent(void){
 int intr=sceKernelCpuSuspendIntr();SceUID event=presentation_event;presentation_event=-1;sceKernelCpuResumeIntr(intr);
 if(event>=0)sceKernelDeleteEventFlag(event);
}
static volatile Frame source,original0;
static volatile unsigned presentations;
static void *ready_buffer;
static Frame last_scaled_source;
static int last_scaled_valid,scaled_refresh_valid;
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
static volatile int pending_list=-1;
static int finish_callback=-1;
static unsigned finish_sequence;
static volatile unsigned finish_expected,finish_seen;
static unsigned finish_confirmed;
static volatile unsigned context_checks,context_clear1,context_clear2,context_nested;
static void SanitizeUnusedReturns(void){
 if(!ge_context)return;
 // 6.61 context header: EXEC, LADR, SADR, RADR1, RADR2.
 // Match the firmware's own nesting test: DEPTH2 preserves both returns;
 // DEPTH1 preserves only RADR1. Never inspect/dereference a return address.
 volatile unsigned *ctx=(volatile unsigned *)ge_context;
 unsigned exec=ctx[0];++context_checks;
 if(exec&0x300u)++context_nested;
 if(!(exec&0x200u)){
  if(ctx[4]){ctx[4]=0;++context_clear2;}
  if(!(exec&0x100u)&&ctx[3]){ctx[3]=0;++context_clear1;}
 }
}
static void GeFinish(int token,void *argument){
 (void)argument;
 // GE interrupt context: no I/O, allocation, waiting or driver calls.
 if((unsigned)token==finish_expected&&finish_expected){
  // Firmware invokes this callback before restoring this list's context.
  SanitizeUnusedReturns();
  finish_seen=(unsigned)token;
 }
}
static void InitFinishCallback(void){
 if(finish_callback>=0)return;
 PspGeCallbackData callbacks={NULL,NULL,GeFinish,NULL};
 finish_callback=sceGeSetCallback(&callbacks);
 logline("finish_callback,id,%d\n",finish_callback);
}
static int list_invalid_logged,list_paused_logged;
static unsigned draws,timeouts,invalid_frames,paused_lists;
static uint64_t wait_total;
static unsigned max_wait;
static int filter_mode=FILTER_SOFT;
static IntFn home_status,utility_type,utility_status;
static volatile int home_paused,home_scaled_ready,reboot_exit,reboot_late;
static int reboot_joined,worker_done;
static volatile int home_output_restored,utility_native_ready;
static int auto_fault;
static volatile int manual_native;
static int manual_native_ready,square_down;
static int native_test_keep4m=1,native_edram_held;
static unsigned native_test_draws;
static int hidden_test_enabled=1,hidden_failed,hidden_last_valid,hidden_index;
static Frame hidden_last_source;
static unsigned hidden_draws,hidden_last_vcount;

static unsigned next_auto,last_home_resolve,auto_ready_since;
static int auto_ready_wait;
static int HomeVisible(void){return home_status&&(home_status()&0x100)!=0;}
static int FirmwareUtilityVisible(void){
  if(!utility_type||!utility_status||utility_type()<=0)return 0;
  int status=utility_status();
  // 4 is completed shutdown; no display owner remains. 5 is active/abort.
  return status==1||status==2||status==3||status==5;
}
static int UtilityVisible(void){return FirmwareUtilityVisible();}
static int MenuVisible(void){return HomeVisible()||UtilityVisible();}
static int HiddenRendering(void){
 return hidden_test_enabled&&running&&!reboot_exit&&!reboot_late&&!auto_fault&&!hidden_failed&&manual_native&&manual_native_ready&&native_edram_held&&!reserved&&!MenuVisible();
}
static int ScalingVisible(void){return running&&!reboot_exit&&!manual_native&&active&&!home_paused&&!MenuVisible();}
static int MenuScalingVisible(void){return running&&!reboot_exit&&!manual_native&&home_paused&&home_scaled_ready&&MenuVisible();}

#define DEPOSTER_BYTES (512u*272u*4u)
static SceUID dep_memory=-1;
static unsigned *dep_pixels;
static unsigned notice_started;
static int notice_active,triangle_down;
static const char *notice_label;
typedef int (*PadFn)(SceCtrlData *,int);
static PadFn merged_peek;
extern unsigned sctrlFindImportByNID(SceModule *,char *,unsigned);
static int FreeDepMemory(void) {
  if(pending_list>=0)return -1;
  if(dep_memory<0)return 0;
  sceKernelDcacheWritebackInvalidateRange(dep_pixels,DEPOSTER_BYTES);
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
static void ProbeScanout(const char *);
static int NativeHardwareReady(void);
static void ArmManualProbe(void);
static void PollFilter(int menu,unsigned now) {
  SceCtrlData pad;memset(&pad,0,sizeof(pad));
  int ret=merged_peek?merged_peek(&pad,1):sceCtrlPeekBufferPositive(&pad,1);
  if(ret<=0)return;
  int triangle=(pad.Buttons&PSP_CTRL_TRIANGLE)!=0;
  int square=(pad.Buttons&PSP_CTRL_SQUARE)!=0;
  unsigned chord=PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER;
  if(!menu&&!reboot_exit&&pending_list<0&&square&&!square_down&&(pad.Buttons&chord)==chord){
    if(manual_native&&(!manual_native_ready||!NativeHardwareReady())){
      ProbeScanout("resume_not_ready");
      logline("native_resume_blocked,hardware_not_ready,1\n");
    } else {
      manual_native=!manual_native;manual_native_ready=0;next_auto=now;
      if(manual_native)ArmManualProbe();
      logline("diagnostic_native_requested,%u\n",manual_native);
    }
  }
  square_down=square;
  if(!manual_native&&!square&&!menu&&!reboot_exit&&pending_list<0&&triangle&&!triangle_down&&(pad.Buttons&chord)==chord) {
    int next=(filter_mode+1)%FILTER_COUNT;
    if(next==FILTER_DEPOSTERISE&&packet&&InitDepMemory()<0){logline("deposterise_memory_unavailable,skipped\n");next=FILTER_SHARP;}
    filter_mode=next;last_scaled_valid=0;
    if(next!=FILTER_DEPOSTERISE)FreeDepMemory();
    notice_label=FilterName(next);notice_started=now;notice_active=1;logline("filter,%s\n",FilterName(next));
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
  sceKernelDcacheWritebackInvalidateRange(gpu_workspace,sizeof(*gpu_workspace));
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
/* Firmware private getters are pure reads, unlike the public savedata status
 * API which can invoke an application callback. Guard the complete leaf body;
 * no utility init/update/shutdown calls or offsets into firmware data. */
static IntFn UtilityGetter(unsigned nid) {
  unsigned g=Find("sceUtility_Driver","sceUtility_private",nid);
  SceModule *m=sceKernelFindModuleByName("sceUtility_Driver");
  if(!g||!m||!(m->attribute&0x1000)||m->text_size<12||g<m->text_addr||g-m->text_addr>m->text_size-12)return NULL;
#ifdef HOST_TEST
  unsigned a=TestCodeWord(g),b=TestCodeWord(g+4),c=TestCodeWord(g+8);
#else
  const unsigned *w=(const unsigned *)(uintptr_t)g;unsigned a=w[0],b=w[1],c=w[2];
#endif
  if((a&0xffff0000u)!=0x3c040000u||b!=0x03e00008u||(c&0xffff0000u)!=0x8c820000u)return NULL;
  return (IntFn)(uintptr_t)g;
}
static void ResolveUtilityStatus(void) {
  utility_type=UtilityGetter(0xa25e0194);
  utility_status=UtilityGetter(0xea446cd5);
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
static int OutputActive(void){return lcd_output?!TvActive():TvActive();}
#include "scanout_probe.inc"
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
    if((ScalingVisible()||MenuScalingVisible())&&ready_buffer){ptr=ready_buffer;stride=OutputStride();format=0;sync=1;}
  }
  int result=real_frame(pri,ptr,stride,format,sync);
  if(pri==2&&result>=0&&running)NotifyWorker();
  return result;
}
static int ModeHook(int mode,int width,int height) {
  if(running&&!reboot_exit&&reserved&&UtilityVisible()&&!HomeVisible()) {
    // Utility dialogs own this transient mode change. Quiesce substitution
    // immediately; the worker drains GE and resumes through the menu path.
    active=0;home_paused=1;home_scaled_ready=0;
    return real_mode(mode,width,height);
  }
  if(ScalingVisible()||MenuScalingVisible()) {
    if(mode==OutputMode()&&width==OutputWidth()&&height==OutputHeight())return real_mode(mode,width,height);
    if((mode==0||mode==0x2d2||mode==0x3d2)&&width==480&&height==272) {
      unsigned k1=pspSdkSetK1(0);
      int ret=real_mode(OutputMode(),OutputWidth(),OutputHeight());
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
  if(ret>=0&&reserved){if(mode)*mode=restore_mode;if(width)*width=480;if(height)*height=272;}
  return ret;
}
static int GetSizeHook(void){return (reserved||native_edram_held)?0x200000:get_size();}
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
static void YieldForList(unsigned start,unsigned timeout_us,unsigned quantum){
 unsigned elapsed=sceKernelGetSystemTimeLow()-start;
 if(elapsed<timeout_us){unsigned remaining=timeout_us-elapsed;sceKernelDelayThread(remaining<quantum?remaining:quantum);}
}
static int WaitList(unsigned timeout_us) {
  unsigned start=sceKernelGetSystemTimeLow();
  for(;;) {
    // Finish callbacks run synchronously in the GE interrupt handler. On
    // this single CPU the worker cannot observe the flag until that handler
    // has also restored the saved GE context and returned. No global idle
    // wait is needed for a positively identified completed packet.
    int complete_intr=sceKernelCpuSuspendIntr();
    int completed=finish_callback>=0&&finish_expected&&finish_seen==finish_expected;
    if(completed){pending_list=-1;++finish_confirmed;}
    sceKernelCpuResumeIntr(complete_intr);
    if(completed)return 0;
    if(reboot_exit&&reboot_late)return -1;
    int status=sceGeListSync(pending_list,1);
    if(status==PSP_GE_LIST_DONE){
      if(list_paused_logged)logline("list_pause_finished,id,%d\n",pending_list);
      pending_list=-1;return 0;
    }
    // SDK calls value 4 CANCEL_DONE, but the firmware also returns it for a
    // resumable PAUSED list (sceGeBreak(0), or a previously running list).
    // Never overwrite packet/context or dequeue it on this result.
    if(status==4) {
      if(!list_paused_logged){logline("list_paused,id,%d,storage_retained,1\n",pending_list);list_paused_logged=1;++paused_lists;}
      if(sceKernelGetSystemTimeLow()-start>=timeout_us)return -3;
      YieldForList(start,timeout_us,1000);continue;
    }
    if((unsigned)status==0x80000100u) {
      // A utility GE queue reset can retire our ID. An invalid ID alone is
      // not a GPU fence: retain storage until a nonblocking global-idle poll.
      int intr=sceKernelCpuSuspendIntr();
      int idle=sceGeDrawSync(1)==PSP_GE_LIST_DONE;
      if(idle)pending_list=-1;
      sceKernelCpuResumeIntr(intr);
      if(!list_invalid_logged){logline("list_id_retired,waiting_for_idle,%u\n",!idle);list_invalid_logged=1;}
      if(idle){logline("list_id_retired,idle_fence_passed\n");return 1;}
      if(sceKernelGetSystemTimeLow()-start>=timeout_us)return -2;
      YieldForList(start,timeout_us,1000);continue;
    }
    if(status<0){logline("list_sync_error,0x%08X\n",(unsigned)status);return -1;}
    if((unsigned)(sceKernelGetSystemTimeLow()-start)>=timeout_us) {
      // A queued list can be removed without cancelling the game's work.
      if(status==PSP_GE_LIST_QUEUED&&sceGeListDeQueue(pending_list)>=0)pending_list=-1;
      return -1;
    }
    // Polling only: a blocking driver wait would bypass our timeout.
    YieldForList(start,timeout_us,200);
  }
}
static int RenderWithFilterCore(Frame f,unsigned target,int selected_filter,int hidden) {
  if(!running||reboot_exit||pending_list>=0)return -1;
  if((manual_native&&!hidden)||UtilityVisible()||(hidden&&!HiddenRendering()))return -2; // utility renderer may reset the shared GE queue
  if(!packet||!ge_context)return -1;
  if(BuildFilteredOutputPacket(packet,f,target,aspect,selected_filter,lcd_output)<0){++invalid_frames;return -1;}
  unsigned token=(++finish_sequence)&0xffffu;
  if(!token)token=(++finish_sequence)&0xffffu;
  finish_seen=0;finish_expected=token;
  if(finish_callback>=0)packet->commands[packet->count-2]=0x0f000000u|token;
  sceKernelDcacheWritebackRange(packet->commands,packet->count*sizeof(unsigned));
  sceKernelDcacheWritebackRange(packet->vertices,packet->image_vertices*sizeof(*packet->vertices));
  if(packet->overlay_vertices)sceKernelDcacheWritebackRange(packet->scanlines,packet->overlay_vertices*sizeof(*packet->scanlines));
  if(packet->post_vertices)sceKernelDcacheWritebackRange(packet->post,packet->post_vertices*sizeof(*packet->post));
  if(packet->clear_vertices)sceKernelDcacheWritebackRange(packet->clear,packet->clear_vertices*sizeof(*packet->clear));
  PspGeListArgs args={sizeof(args),ge_context,0,NULL};
  list_invalid_logged=list_paused_logged=0;
  // Recheck display ownership immediately before submitting scaler work.
  // The original enqueue supports nested interrupt guards.
  int intr=sceKernelCpuSuspendIntr();
  if((manual_native&&!hidden)||UtilityVisible()||(hidden&&!HiddenRendering())||!running||reboot_exit){sceKernelCpuResumeIntr(intr);return -2;}
  pending_list=sceGeListEnQueue(packet->commands,NULL,finish_callback,&args);
  sceKernelCpuResumeIntr(intr);
  if(pending_list<0){logline("enqueue_error,0x%08X\n",(unsigned)pending_list);return -1;}
  unsigned start=sceKernelGetSystemTimeLow();
  int ret=WaitList(100000);
  unsigned elapsed=sceKernelGetSystemTimeLow()-start;
  wait_total+=elapsed;if(elapsed>max_wait)max_wait=elapsed;
  if(ret==1||ret==-2||ret==-3) {
    // Discard the uncertain frame, not the last known-good scanout. A game
    // can retire an ID before our polling thread observes DONE; that alone
    // is not a HOME/utility transition. Storage is still fenced by WaitList.
    if(UtilityVisible()){
      active=0;home_paused=reserved;home_scaled_ready=0;home_output_restored=0;
      next_auto=sceKernelGetSystemTimeLow()+300000u;
    }
    return -2;
  }
  if(ret<0){++timeouts;abort_requested=1;return -1;}
  ++draws;return 0;
}
static int RenderWithFilter(Frame f,unsigned target,int selected_filter){return RenderWithFilterCore(f,target,selected_filter,0);}
static int RenderHiddenOutput(void){
 if(!HiddenRendering())return 1;
 Frame f=Snapshot(&source);if(!ValidFrame(f))return 1;
 if(hidden_last_valid&&f.serial==hidden_last_source.serial&&f.address==hidden_last_source.address&&f.stride==hidden_last_source.stride&&f.format==hidden_last_source.format)return 1;
 if(hidden_last_valid&&(unsigned)sceDisplayGetVcount()==hidden_last_vcount)return 1;
 unsigned target=(unsigned)hidden_index^1u;
 // Deliberately no SetMode/SetFrameBuf/ready_buffer publication here.
 int result=RenderWithFilterCore(f,target,FILTER_SOFT,1);
 if(result<0){if(result!=-2){hidden_failed=1;logline("hidden_test_failed,result,%d\n",result);}return result;}
 hidden_index=(int)target;hidden_last_source=f;hidden_last_valid=1;
 hidden_last_vcount=(unsigned)sceDisplayGetVcount();++hidden_draws;
 if(hidden_draws==1)ProbeScanout("hidden_render_started");
 return 0;
}
static int Render(Frame f,unsigned target){
  if(!running||reboot_exit||pending_list>=0)return -1;
  if(filter_mode==FILTER_DEPOSTERISE) {
    if(InitDepMemory()<0){filter_mode=FILTER_SHARP;logline("deposterise_memory_unavailable,skipped\n");}
    else {
      if(DeposteriseFrame(f,dep_pixels,512)<0)return -1;
      sceKernelDcacheWritebackRange(dep_pixels,DEPOSTER_BYTES);
      f=(Frame){(uintptr_t)dep_pixels,512,3,f.serial};
    }
  }
  int ret=RenderWithFilter(f,target,filter_mode);
  if(ret>=0&&notice_active&&!MenuVisible()&&!reboot_exit) {
    if(sceKernelGetSystemTimeLow()-notice_started>=1500000u)notice_active=0;
#ifndef HOST_TEST
    else DrawFilterNotice((unsigned short *)(0x44000000u+TV_VRAM_OFFSET+target*OutputBytes()),OutputStride(),notice_label?notice_label:FilterName(filter_mode));
#endif
  }
  return ret;
}
/* R35 comparison: refresh the existing source even without another game
 * framebuffer submission, retaining the one-render-per-TV-refresh fence. */
static int UpdateGameOutput(Frame f){
 // Never overwrite both scanout buffers within the same TV refresh if a
 // game presents several times before vblank. The newest source stays pending.
 if(scaled_refresh_valid&&(unsigned)sceDisplayGetVcount()==last_scaled_vcount)return 1;
 unsigned target=output_index^1;
 int result=Render(f,target);if(result<0)return result;
 if(!ScalingVisible())return -2;
 void *next_buffer=(void *)(0x04000000u+TV_VRAM_OFFSET+target*OutputBytes());
 // Publish at vblank and advertise the pointer only if the driver accepts it.
 int publish_intr=sceKernelCpuSuspendIntr();
 int published=real_frame(0,next_buffer,OutputStride(),0,1);
 if(published>=0)ready_buffer=next_buffer;
 sceKernelCpuResumeIntr(publish_intr);
 if(published<0){logline("publish_error,result,0x%08X\n",(unsigned)published);return -1;}
 output_index=target;last_scaled_source=f;last_scaled_valid=1;last_scaled_vcount=(unsigned)sceDisplayGetVcount();scaled_refresh_valid=1;return 0;
}
static int Deactivate(void) {
  if(pending_list>=0)return -1;
  active=0;
  if(reserved||native_edram_held) {
    int ret=0;
    if(OutputActive())ret=real_mode(restore_mode,480,272);
    Frame f=Snapshot(&original0);
    if(!ValidFrame(f))f=Snapshot(&source);
    if(f.address&&ValidFrame(f))real_frame(0,(void *)(uintptr_t)f.address,f.stride,f.format,1);
    // Never shrink EDRAM while a plugin GE list could still reference it.
    int intr=sceKernelCpuSuspendIntr();
    if(sceGeDrawSync(1)==PSP_GE_LIST_DONE&&ret>=0)ret=set_size(0x200000);
    else ret=-1;
    sceKernelCpuResumeIntr(intr);
    if(ret<0){logline("disable_pending_edram_cleanup\n");return -1;}
    reserved=native_edram_held=0;
  }
  abort_requested=0;ready_buffer=NULL;logline("disabled\n");return 0;
}
static int Activate(void) {
  int mode=-1,width=0,height=0;
  get_mode(&mode,&width,&height);
  lcd_output=!TvActive()&&mode==0&&width==480&&height==272;
  if(!OutputActive()||width!=480||height!=272||(lcd_output?mode!=0:(mode!=0x2d2&&mode!=0x3d2))) {
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
  ProbeScanout("cold_native");
  if(InitGeMemory()<0)return -1;
  int ret=-1,intr=sceKernelCpuSuspendIntr();
  if(sceGeDrawSync(1)==PSP_GE_LIST_DONE){reserved=1;ret=set_size(0x400000);}
  else ret=1;
  sceKernelCpuResumeIntr(intr);
  if(ret>0)return 1;
  if(ret<0){reserved=0;logline("activation_rejected,ge_busy_or_edram_error\n");return -1;}
  restore_mode=mode;abort_requested=0;output_index=0;
  if(lcd_output)native_test_keep4m=hidden_test_enabled=0;
  int rendered=Render(f,0);
  if(rendered==-2){home_paused=reserved;home_output_restored=0;next_auto=sceKernelGetSystemTimeLow()+300000u;return 0;}
  if(rendered<0){if(pending_list<0)Deactivate();return -1;}
  last_scaled_source=f;last_scaled_valid=1;last_scaled_vcount=(unsigned)sceDisplayGetVcount();scaled_refresh_valid=1;
  ready_buffer=(void *)(0x04000000u+TV_VRAM_OFFSET);
  if(MenuVisible()){home_paused=1;home_output_restored=1;return 0;}
  active=1;
  ret=real_mode(OutputMode(),OutputWidth(),OutputHeight());
  if(ret>=0&&!MenuVisible())ret=real_frame(0,ready_buffer,OutputStride(),0,1);
  if(ret<0){logline("activation_mode_error,0x%08X\n",(unsigned)ret);Deactivate();return -1;}
  ArmScanoutProbe();
  logline("enabled,source,0x%08X,stride,%d,format,%d,aspect,%d\n",f.address,f.stride,f.format,aspect);
  return 0;
}
static int ReadyToAutoStart(void) {
  int mode=-1,w=0,h=0,stride=0,format=-1;void *ptr=NULL;
  if(MenuVisible()||get_size()!=0x200000)return 0;
  if(get_mode(&mode,&w,&h)<0||w!=480||h!=272)return 0;
  if(TvActive()?(mode!=0x2d2&&mode!=0x3d2):mode!=0)return 0;
  if(get_frame(2,&ptr,&stride,&format,0)<0)return 0;
  return ValidFrame((Frame){(uintptr_t)ptr,stride,format,0});
}
#include "native_output.inc"
static int PauseForHome(void) {
  active=0;home_paused=1;
  int utility=UtilityVisible();
  if(!reserved||(home_output_restored&&(!utility||utility_native_ready)))return 0;
  if(pending_list>=0)return -1;
  // Impose reads the physical layout through its direct kernel import and
  // prepares its menu for that canvas. Do not reset it to the game geometry.
  Frame f=Snapshot(&original0);
  // NULL is the absence of our high-priority layer, not the game source.
  if(f.address&&!ValidOutputFrame(f))return -1;
  int ret=real_frame(0,(void *)(uintptr_t)f.address,f.stride,f.format,1);
  if(ret<0)return ret;
  if(utility&&!utility_native_ready) {
    // Use the ordinary game-sized TV scanout while the utility owns GE.
    // Keep the workspace reserved; neither enqueue jobs nor shrink EDRAM.
    ret=real_mode(restore_mode,480,272);if(ret<0)return ret;
    utility_native_ready=1;home_scaled_ready=0;
    ProbeScanout("utility_pause");
    logline("utility_native_output,mode,0x%X,width,480,height,272\n",restore_mode);
  }
  home_output_restored=1;
  int mode=-1,w=0,h=0;get_mode(&mode,&w,&h);
  logline("home_paused,menu_layout_preserved,mode,0x%X,width,%d,height,%d,layer,0x%08X,stride,%d,format,%d\n",
          (unsigned)mode,w,h,f.address,f.stride,f.format);
  return 0;
}
static int RenderHome(void) {
  if(lcd_output)return 1; // LCD HOME owns native output; no filter jobs.
  if(manual_native)return 1;
  if(UtilityVisible())return 1; // No scaler GE submissions during any utility phase.
  if(!home_paused||!reserved||!home_output_restored||!MenuVisible()||reboot_exit)return 1;
  Frame f=Snapshot(&original0);f.address&=0x1fffffffu;
  // Device log identifies a native 480x272 menu in a 512-stride buffer.
  if(f.stride!=512||!ValidFrame(f))return 1;
  unsigned target=output_index^1;
  int rendered=RenderWithFilter(f,target,FILTER_SOFT);
  if(rendered==-2)return 1;
  if(rendered<0)return -1;
  if(!running||reboot_exit||!MenuVisible())return 1;
  int mode=-1,w=0,h=0;get_mode(&mode,&w,&h);
  if(mode!=OutputMode()||w!=OutputWidth()||h!=OutputHeight()) {
    int ret=real_mode(OutputMode(),OutputWidth(),OutputHeight());if(ret<0)return ret;
  }
  ready_buffer=(void *)(uintptr_t)(0x04000000u+TV_VRAM_OFFSET+target*OutputBytes());
  home_scaled_ready=1;
  int ret=real_frame(0,ready_buffer,OutputStride(),0,1);
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
  if(MenuVisible())return 1;
  ReleaseHomeOutput();
  if(!reserved){
    if(native_edram_held&&ReleaseNativeEdram()!=0)return 1;
    if(!ReadyToAutoStart())return 1;
    int ret=Activate();
    if(ret<0)return -1;
    if(ret>0||!active)return 1;
    home_paused=home_scaled_ready=home_output_restored=utility_native_ready=0;
    logline("native_closed,fresh_scaler_resources,1\n");return 0;
  }
  int mode=-1,w=0,h=0,stride=0,format=-1;void *ptr=NULL;
  get_mode(&mode,&w,&h);
  int game_layout=w==480&&h==272&&(mode==0||mode==0x2d2||mode==0x3d2);
  int tv_layout=w==OutputWidth()&&h==OutputHeight()&&mode==OutputMode();
  if(!OutputActive()||(!game_layout&&!tv_layout))return 1;
  if(get_frame(2,&ptr,&stride,&format,0)<0)return 1;
  Frame f={(uintptr_t)ptr,stride,format,0};if(!ValidFrame(f))return 1;
  Capture(&source,ptr,stride,format);
  if(get_frame(0,&ptr,&stride,&format,0)<0)return 1;
  Frame original={(uintptr_t)ptr,stride,format,0};
  if(original.address&&!ValidOutputFrame(original))return 1;
  Capture(&original0,ptr,stride,format);
  if(mode==0x2d2||mode==0x3d2||(lcd_output&&mode==0))restore_mode=mode;
  unsigned target=output_index^1;
  int rendered=Render(f,target);
  if(rendered==-2)return 1;
  if(rendered<0)return -1;
  if(MenuVisible())return 1;
  ready_buffer=(void *)(uintptr_t)(0x04000000u+TV_VRAM_OFFSET+target*OutputBytes());
  home_paused=0;home_scaled_ready=0;home_output_restored=0;utility_native_ready=0;active=1;
  int ret=real_mode(OutputMode(),OutputWidth(),OutputHeight());
  if(ret>=0&&!MenuVisible())ret=real_frame(0,ready_buffer,OutputStride(),0,1);
  if(ret<0){active=0;home_paused=1;return -1;}
  last_scaled_source=f;last_scaled_valid=1;last_scaled_vcount=(unsigned)sceDisplayGetVcount();scaled_refresh_valid=1;
  output_index=target;ArmScanoutProbe();logline("system_dialog_closed,scaling_resumed\n");return 0;
}
static void AutoControl(int menu,unsigned now) {
  if(auto_fault)return;
  if(manual_native) {
    active=0;home_paused=1;
    if(!manual_native_ready){
      int ret=RestoreManualNativeOutput();
      if(ret!=0)return;
      manual_native_ready=1;native_test_draws=draws;
      hidden_failed=hidden_last_valid=hidden_index=0;hidden_draws=0;
      logline("hidden_handoff,ready,1,edram,%d,held,%u,gpu_uid,%d,dep_uid,%d,draws,%u,pending,%d\n",get_size(),native_edram_held,gpu_memory,dep_memory,draws,pending_list);
      logline("diagnostic_native_ready,edram,%d,gpu_memory,%d,hidden_jobs_requested,%u\n",get_size(),gpu_memory,hidden_test_enabled);
    }
    home_output_restored=1;utility_native_ready=1;
    return;
  }
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
  if(pending_list<0&&(reserved||native_edram_held)) {
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
       sceGeDrawSync(1)==PSP_GE_LIST_DONE&&set_size(0x200000)>=0)reserved=native_edram_held=0;
    sceKernelCpuResumeIntr(intr);
  }
  if(!reboot_late)logline("reboot_worker_end,pending,%d,reserved,%u\n",pending_list,reserved);
  // Workspace/trampolines stay resident until firmware has stopped GE.
  worker_done=1;
}
static void LogMenuLayers(void) {
 if(!LOGGING_ENABLED)return;
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
 if(!LOGGING_ENABLED)return;
 logline("context_returns,checks,%u,cleared_radr1,%u,cleared_radr2,%u,nested,%u\n",context_checks,context_clear1,context_clear2,context_nested);
 if(manual_native_ready){int mode=-1,w=0,h=0;get_mode(&mode,&w,&h);
  logline("hidden_test,ready,1,allowed,%u,failed,%u,edram,%d,held,%u,mode,0x%X,width,%d,height,%d,gpu_uid,%d,hidden_draws,%u,total_draws,%u,draws_at_handoff,%u,pending,%d\n",HiddenRendering(),hidden_failed,get_size(),native_edram_held,(unsigned)mode,w,h,gpu_memory,hidden_draws,draws,native_test_draws,pending_list);
 }
 if(manual_native_ready)logline("hidden_handoff,ready,1,edram,%d,held,%u,gpu_uid,%d,dep_uid,%d,draws,%u,draws_at_handoff,%u,pending,%d\n",get_size(),native_edram_held,gpu_memory,dep_memory,draws,native_test_draws,pending_list);
  logline("logger_stats,dropped,%u,write_errors,%u,finish_confirmed,%u\n",log_dropped,log_write_errors,finish_confirmed);
  Frame f=Snapshot(&source);
  logline("stats,ms,%u,active,%u,aspect,%u,presents,%u,draws,%u,queue_gpu_wait_avg_us,%u,max_wait_us,%u,timeouts,%u,invalid_frames,%u,paused_lists,%u,pending,%d,diagnostic_native,%u,source,0x%08X,stride,%d,format,%d\n",
          sceKernelGetSystemTimeLow()/1000,active,aspect,presentations,draws,
          draws?(unsigned)(wait_total/draws):0,max_wait,timeouts,invalid_frames,paused_lists,pending_list,manual_native,f.address,f.stride,f.format);
}
static void WaitForWork(void){
 if((active||HiddenRendering())&&presentation_event>=0){
  u32 bits=0;SceUInt timeout=16667;
  // The event is sticky: a present before this wait is not lost. Multiple
  // pending presents coalesce; UpdateGameOutput takes the newest source.
  int result=sceKernelWaitEventFlag(presentation_event,1,PSP_EVENT_WAITOR|PSP_EVENT_WAITCLEAR,&bits,&timeout);
  if(result<0&&(unsigned)result!=0x800201a8u)sceKernelDelayThread(1000);
 }else if(active||MenuScalingVisible())sceDisplayWaitVblankStart();
 else sceKernelDelayThread(10000);
}
static int MainThread(SceSize argc,void *argv) {
  (void)argc;(void)argv;
  if(LOGGING_ENABLED){
  log_worker=sceKernelGetThreadId();
  const char *paths[]={"ef0:/PSPGoTVScale.txt","ms0:/PSPGoTVScale.txt"};
  for(unsigned i=0;i<2;++i){SceUID fd=sceIoOpen(paths[i],PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);if(fd>=0){logpath=paths[i];sceIoClose(fd);break;}}
  if(!logpath)return 0;
  }
  unsigned fw=sceKernelDevkitVersion();
  logline("PSPGoTVScale,base,alpha1,variant,lcd_filters_r36,model,%d,devkit,0x%08X\n",sceKernelGetModel(),fw);
  if(sceKernelGetModel()!=4||(fw!=0x06060010&&fw!=0x06060110)){logline("unsupported_model_or_firmware\n");FlushLogs(LOG_QUEUE_CAPACITY);return 0;}
  logline("scheduler,event,%d,worker_priority,%u\n",presentation_event,SCALER_WORKER_PRIORITY);
  filter_mode=FILTER_SOFT;logline("filter,%s\n",FilterName(filter_mode));
  if(InitGeMemory()<0){FlushLogs(LOG_QUEUE_CAPACITY);return 0;}
  if(InstallHooks()<0){FreeGeMemory();FlushLogs(LOG_QUEUE_CAPACITY);return 0;}
  InitFinishCallback();
  ResolveHomeStatus();ResolveUtilityStatus();ResolveInput();ResolveScanoutProbe();
  logline("ready_default_scaled,filter_shortcut,L_R_Triangle,home_status,%u,utility_status,%u\n",home_status!=NULL,utility_type!=NULL&&utility_status!=NULL);
  unsigned last_report=sceKernelGetSystemTimeLow();
  last_home_resolve=last_report;
  logline("logger,queued,1,capacity,%u\n",LOG_QUEUE_CAPACITY);
  while(running) {
    FlushLogs(8);
    unsigned now=sceKernelGetSystemTimeLow();
    if(LOGGING_ENABLED)PollScanoutProbe(now);
    if(now-last_home_resolve>=500000u){ResolveHomeStatus();ResolveUtilityStatus();ResolveInput();last_home_resolve=now;}
    int menu=MenuVisible();
    static int last_utility;int utility=UtilityVisible();
    if(utility!=last_utility){logline("utility_dialog,active,%u,type,%d,status,%d\n",utility,utility_type?utility_type():-1,utility_status?utility_status():-1);last_utility=utility;if(utility)utility_native_ready=0;}
    if(menu&&(active||reserved)){active=0;home_paused=1;}
    if(pending_list>=0) {
      if(WaitList(1000)<0||pending_list>=0){sceKernelDelayThread(10000);continue;}
    }
    PollFilter(menu,now);
    if(active&&!OutputActive())abort_requested=1;
    if(abort_requested){auto_fault=1;Deactivate();sceKernelDelayThread(10000);continue;}
    AutoControl(menu,now);
    if(menu&&home_paused&&home_output_restored&&!auto_fault) {
      int ret=RenderHome();
      if(ret<0){auto_fault=1;home_scaled_ready=0;logline("menu_scaling_failed\n");}
    } else if(!menu)ReleaseHomeOutput();
    if(HiddenRendering())RenderHiddenOutput();
    if(active) {
      Frame f=Snapshot(&source);
      if(f.address&&!ValidFrame(f)){++invalid_frames;abort_requested=1;continue;}
      if(f.address) {
        UpdateGameOutput(f);
      }
    }
    now=sceKernelGetSystemTimeLow();
    if(now-last_report>=1000000){Report();if(menu)LogMenuLayers();last_report=now;}
    // Worker never spins while waiting for input or the next output frame.
    WaitForWork();
  }
  if(reboot_exit){CleanupBeforeReboot();if(!reboot_late)FlushLogs(LOG_QUEUE_CAPACITY);return 0;}
  if(pending_list<0)Deactivate();
  Report();FlushLogs(LOG_QUEUE_CAPACITY);worker_done=1;return 0;
}
int module_start(SceSize args,void *argp) {
  (void)args;(void)argp;
  presentation_event=sceKernelCreateEventFlag("GoTVPresent",0,0,NULL);
  worker=sceKernelCreateThread("GoTVScale",MainThread,SCALER_WORKER_PRIORITY,0x6000,0,NULL);
  if(worker<0){ClosePresentationEvent();return worker;}
  int ret=sceKernelStartThread(worker,0,NULL);
  if(ret<0){sceKernelDeleteThread(worker);worker=-1;ClosePresentationEvent();}
  return ret;
}
int module_stop(SceSize args,void *argp) {
  (void)args;(void)argp;
  if(pending_list>=0)return -1; // Do not free a context/list still used by GE.
  running=0;NotifyWorker();
  if(worker>=0){sceKernelWaitThreadEnd(worker,NULL);sceKernelDeleteThread(worker);worker=-1;}
  if((reserved||native_edram_held)&&Deactivate()<0)return -1;
  RemoveHooks();ClosePresentationEvent();
  if(finish_callback>=0){sceGeUnsetCallback(finish_callback);finish_callback=-1;}
  if(FreeDepMemory()<0)return -1;
  return FreeGeMemory();
}

/* Early reboot quiesces this worker before user buffers/GE state are replaced.
 * No entry-code rewriting, memory freeing or late driver calls on this path. */
int module_reboot_phase(int phase,void *argp,int a2,int a3) {
  (void)argp;(void)a2;(void)a3;
  if(phase!=3||reboot_joined)return 0;
  reboot_exit=1;active=0;home_scaled_ready=0;running=0;NotifyWorker();
  if(worker>=0&&!worker_done){SceUInt timeout=500000;sceKernelWaitThreadEnd(worker,&timeout);}
  reboot_joined=1;return 0;
}
int module_reboot_before(void *argp,int a1,int a2,int a3) {
  (void)argp;(void)a1;(void)a2;(void)a3;
  reboot_late=1;reboot_exit=1;active=0;home_scaled_ready=0;running=0;return 0;
}

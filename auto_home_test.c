/* Exercise the actual v1-based automatic/HOME control path with fake drivers. */
#ifndef HOST_TEST
#define HOST_TEST
#endif
#include "main.c"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
static unsigned clock_us,menu_bits,pad_buttons,current_k1;
static int utility_kind,utility_state;
static int FakeUtilityType(void){return utility_kind;}
static int FakeUtilityStatus(void){return utility_state;}
static SceModule input_module={.text_addr=0x88001000,.text_size=0x100,.attribute=0x1000};
static SceModule input_owner={.text_addr=0x88002000,.text_size=0x100,.attribute=0x1000};
static int input_fixture;
static unsigned utility_exports[]={0xa25e0194,0xea446cd5,0x88004000,0x88004010};
static SceLibraryEntryTable utility_entry={.len=sizeof(SceLibraryEntryTable)/4,.stubcount=2,.libname="sceUtility_private",.entrytable=utility_exports};
static SceModule utility_module={.ent_top=&utility_entry,.ent_size=sizeof(utility_entry),.text_addr=0x88004000,.text_size=0x100,.attribute=0x1000};
static int MergedPad(SceCtrlData *pad,int n){(void)n;pad->Buttons=PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_TRIANGLE;return 1;}
static void *dep_block;static unsigned dep_block_bytes;
static int mode_v,w_v,h_v,edram,ge_busy,resize_calls,enqueue_calls,fail_enqueue,open_during_render;
static int desired_width=480,hardware_width=480,hardware_stride=512,hardware_format=3,freeze_geometry,lcd_force_scaled;
static int frame_calls,mode_calls,sync_invalid,utility_during_render,sync_paused;
static Frame game_frame,layer_frame,display_frame;
static int slot_driver,hold_vblank,native_mode_with_old_layer,inject_native_frames,intr_depth,immediate_rejections;
static Frame slots[4][2],native_rebound[2];
static unsigned ge_allocations,ge_releases,probe_word_reads;
static int FakeProbeGe(int id){return id==19?(edram==0x400000?2:4):id==28?1:0;}
static Frame FakeSelected(void){
 if(slot_driver){for(int i=0;i<4;i++)if(slots[i][0].address)return slots[i][0];return (Frame){0};}
 return layer_frame.address?layer_frame:game_frame;
}
static int FakeProbeDma(int *w,int *stride,int *fmt){
 Frame f=FakeSelected();
 *w=slot_driver?hardware_width:mode_v==0xd2?720:480;
 *stride=slot_driver?hardware_stride:f.stride;*fmt=(0x39u>>((slot_driver?hardware_format:f.format)*2))&3u;return 0;
}
static void FakeLayoutTick(void){
 Frame f=FakeSelected();if(!f.address)return;
 if(f.format!=hardware_format||f.stride!=hardware_stride){
  if(!freeze_geometry)hardware_width=desired_width;
  hardware_format=f.format;hardware_stride=f.stride;
 }
}
static unsigned user_block_bytes;
static void *user_block;static int alloc_fail,bad_head,dep_only_fail;
static int FakeStatus(void){return menu_bits;}
static int FakeMode(int m,int w,int h){
 mode_calls++;desired_width=w;
 if(!slot_driver){hardware_width=w;hardware_stride=m==0xd2?768:512;hardware_format=m==0xd2?0:3;}
 if(slot_driver&&m==restore_mode){
  uintptr_t a=slots[0][0].address&0x1fffffffu;
  if(a>=0x04200000u&&a<0x04400000u)native_mode_with_old_layer++;
  if(inject_native_frames){slots[2][0]=(Frame){0x04000000,512,3,0};slots[2][1]=(Frame){0x04088000,512,3,0};}
 }
 mode_v=m;w_v=w;h_v=h;return 0;
}
static int FakeGetMode(int *m,int *w,int *h){if(m)*m=mode_v;if(w)*w=w_v;if(h)*h=h_v;return 0;}
static int FakeSize(void){return edram;}
static int FakeHwSize(void){return 0x400000;}
static int FakeSetSize(int n){edram=n;resize_calls++;return 0;}
static int FakeGetFrame(int pri,void **p,int *s,int *f,int sync){(void)sync;Frame v=slot_driver?slots[pri][sync]:pri==2?game_frame:layer_frame;*p=(void *)(uintptr_t)v.address;*s=v.stride;*f=v.format;return 0;}
static int FakeFrame(int pri,void *p,int stride,int format,int sync){
 frame_calls++;
 if(slot_driver){
  // Real firmware shares layout metadata between live and pending addresses.
  // Immediate mode rejects stride/format changes and updates both addresses.
  if(!sync&&(slots[pri][0].stride!=stride||slots[pri][0].format!=format)){
   immediate_rejections++;return (int)0x80000107u;
  }
  Frame v={(uintptr_t)p,stride,format,0};
  if(!sync)slots[pri][0]=slots[pri][1]=v;
  else {
   slots[pri][1]=v;
   slots[pri][0].stride=stride;slots[pri][0].format=format;
  }
  if(pri==2)native_rebound[sync]=v;
 }
 display_frame=(Frame){(uintptr_t)p,stride,format,0};
 if(pri==0)layer_frame=display_frame;
 return 0;
}
static void Reset(void){
 notice_label=NULL;
 probe_native_reference=0;freeze_geometry=lcd_force_scaled=0;desired_width=hardware_width=480;hardware_stride=512;hardware_format=3;
 probe_ge=FakeProbeGe;probe_cmd=NULL;probe_dma=FakeProbeDma;probe_active_pending=probe_manual_started=probe_bound_taken=probe_ui_pending=probe_ui_seen=0;probe_word_reads=0;
 assert(intr_depth==0);slot_driver=hold_vblank=native_mode_with_old_layer=inject_native_frames=immediate_rejections=0;native_wait_reason=NULL;
 manual_native=manual_native_ready=square_down=dep_only_fail=0;
 current_k1=0;
 pending_list=-1;assert(FreeDepMemory()==0);assert(FreeGeMemory()==0);alloc_fail=bad_head=0;
 running=1;active=reserved=abort_requested=home_paused=home_scaled_ready=reboot_exit=reboot_late=reboot_joined=worker_done=home_output_restored=auto_fault=auto_ready_wait=0;
 pending_list=-1;next_auto=auto_ready_since=clock_us=menu_bits=0;aspect=output_index=0;
 draws=timeouts=invalid_frames=resize_calls=enqueue_calls=fail_enqueue=open_during_render=frame_calls=mode_calls=ge_busy=0;filter_mode=FILTER_SOFT;triangle_down=notice_active=pad_buttons=0;merged_peek=NULL;
 mode_v=0x2d2;w_v=480;h_v=272;edram=0x200000;
 game_frame=(Frame){0x0948fb20,512,3,0};layer_frame=(Frame){0,512,3,0};source=game_frame;original0=layer_frame;
 real_mode=FakeMode;get_mode=FakeGetMode;get_size=FakeSize;get_hw_size=FakeHwSize;set_size=FakeSetSize;
 get_frame=FakeGetFrame;real_frame=FakeFrame;home_status=FakeStatus;utility_type=FakeUtilityType;utility_status=FakeUtilityStatus;utility_kind=utility_state=utility_native_ready=sync_invalid=utility_during_render=sync_paused=paused_lists=list_paused_logged=0;ready_buffer=NULL;logpath=NULL;
 assert(InitGeMemory()==0);
 assert((uintptr_t)packet>=0x08800000u&&(uintptr_t)packet+sizeof(*packet)<=0x0c000000u);
 assert((uintptr_t)ge_context>=(uintptr_t)packet+sizeof(*packet));
 assert(!((uintptr_t)packet&63)&&!((uintptr_t)ge_context&63));
}
int main(void){
 Reset();AutoControl(0,0); // First ready sample activates immediately.
 assert(active&&reserved&&draws==1&&mode_v==0xd2&&w_v==720&&h_v==480&&display_frame.address==0x04200000);
 // NODS consumes Sony PS. Native HOME status alone stops substitution.
 menu_bits=0x100;assert(!ScalingVisible());
 assert(FrameHook(0,(void *)0x88800000,512,3,1)==0&&display_frame.address==0x88800000);
 assert(Snapshot(&original0).address==0x88800000);
 int menu_mode_calls=mode_calls;
 AutoControl(1,2100000);assert(!active&&home_paused&&home_output_restored&&mode_v==0xd2&&w_v==720&&h_v==480);
 assert(mode_calls==menu_mode_calls); // Do not reconfigure the menu's canvas.
 int public_mode,public_w,public_h;
 assert(GetModeHook(&public_mode,&public_w,&public_h)==0&&public_mode==0&&public_w==480&&public_h==272);
 assert(display_frame.address==0x88800000&&resize_calls==1&&reserved); // no EDRAM resize during HOME
 AutoControl(1,2200000);assert(frame_calls==3); // restore once, not on every menu tick
 // Native HOME close clears its layer. Scaling returns automatically.
 menu_bits=0;layer_frame=(Frame){0,512,3,0};AutoControl(0,2499999);assert(!active);
 AutoControl(0,2500000);assert(active&&!home_paused&&draws==2&&resize_calls==1&&mode_v==0xd2);
 // Native firmware can also choose the ordinary game-sized TV layout.
 // Preserve it while HOME is shown and accept it when scaling resumes.
 Reset();AutoControl(0,0);menu_bits=0x100;
 FakeMode(0x2d2,480,272);menu_mode_calls=mode_calls;
 AutoControl(1,100);assert(mode_calls==menu_mode_calls&&mode_v==0x2d2&&w_v==480&&h_v==272);
 menu_bits=0;layer_frame=(Frame){0,512,3,0};AutoControl(0,300100);
 assert(active&&mode_v==0xd2&&w_v==720&&h_v==480);
 // Retain native menu stride/pixel format, including a full-width buffer.
 Reset();AutoControl(0,0);menu_bits=0x100;
 FrameHook(0,(void *)0x88800000,768,3,1);AutoControl(1,100);
 assert(display_frame.address==0x88800000&&display_frame.stride==768&&display_frame.format==3);
 // An absent original layer must remain absent; never cover HOME with source.
 Reset();AutoControl(0,0);AutoControl(0,2000000);menu_bits=0x100;
 AutoControl(1,2100000);assert(display_frame.address==0&&home_output_restored);
 // Startup waits for native TV game frames and does not use buttons.
 Reset();game_frame.address=0;AutoControl(0,0);AutoControl(0,2500000);assert(enqueue_calls==0);
 Reset();w_v=720;h_v=480;AutoControl(0,0);AutoControl(0,2500000);assert(enqueue_calls==0);
 Reset();menu_bits=0x100;AutoControl(1,0);AutoControl(1,2500000);assert(enqueue_calls==0);
 menu_bits=0;AutoControl(0,2600000);AutoControl(0,4600000);assert(active);
 // Busy GE is retried; an actual enqueue failure stops automatic attempts.
 Reset();ge_busy=1;AutoControl(0,0);assert(!active&&!auto_fault&&enqueue_calls==0);
 ge_busy=0;AutoControl(0,99999);assert(!active);
 AutoControl(0,100000);assert(active&&enqueue_calls==1);
 Reset();fail_enqueue=1;AutoControl(0,0);AutoControl(0,2000000);assert(auto_fault&&enqueue_calls==1);
 AutoControl(0,2000000);assert(enqueue_calls==1);
 // HOME appearing during the first list must not publish a scaled frame.
 Reset();open_during_render=1;AutoControl(0,0);AutoControl(0,2000000);
 assert(!active&&home_paused&&home_output_restored&&frame_calls==0&&mode_v==0x2d2);
 assert(!ValidFrame((Frame){0x88800000,512,3,0}));
 assert(ValidOutputFrame((Frame){0x88800000,512,3,0})); // kernel aliases are output-only
 for(int mode=FILTER_SHARP;mode<FILTER_DEPOSTERISE;++mode){Reset();filter_mode=mode;AutoControl(0,0);assert(active&&draws==1);}
 // Reproduce the uploaded r5 native menu tuple: uncached kernel alias,
 // 512-stride RGBA5551. It must be a texture, not direct 720x480 scanout.
 Reset();AutoControl(0,0);menu_bits=0x100;
 FrameHook(0,(void *)0xabbbb800,512,1,1);AutoControl(1,100);
 filter_mode=FILTER_CRT;assert(RenderHome()==0&&home_scaled_ready&&MenuScalingVisible());
 assert(display_frame.stride==768&&display_frame.format==0);
 unsigned tex=0,texwidth=0;int texfmt=-1,texfilter=-1,scan=0;
 for(unsigned i=0;i<packet->count;++i){unsigned c=packet->commands[i]>>24,v=packet->commands[i]&0xffffff;
  if(c==0xa0)tex=v;
  if(c==0xa8)texwidth=v;
  if(c==0xc3)texfmt=v;
  if(c==0xc6)texfilter=v;
  if(c==4&&v==((6u<<16)|480u))scan++;
 }
 assert((tex|((texwidth&0xf0000u)<<8))==0x0bbbb800u&&texfmt==1&&texfilter==0x101&&scan==0);
 FrameHook(0,(void *)0xabbbb800,512,1,1);
 assert(display_frame.address==(uintptr_t)ready_buffer&&Snapshot(&original0).address==0xabbbb800u);
 assert(ModeHook(0x2d2,480,272)==0&&mode_v==0xd2&&!abort_requested);
 menu_bits=0;ReleaseHomeOutput();assert(!home_scaled_ready&&display_frame.address==0xabbbb800u);
 AutoControl(0,300100);assert(active&&!home_paused);
 // Stop before reboot, clear the user/scaler layer, restore native TV mode,
 // wait for scanout, shrink EDRAM when idle, keep workspace alive for firmware.
 Reset();AutoControl(0,0);menu_bits=0x100;FrameHook(0,(void *)0xabbbb800,512,1,1);
 AutoControl(1,100);assert(RenderHome()==0);
 int jobs=enqueue_calls;
 assert(module_reboot_phase(3,NULL,0,0)==0&&!active&&!home_scaled_ready&&!running&&reboot_exit);
 assert(RenderHome()!=0&&Render(game_frame,0)<0&&enqueue_calls==jobs);
 CleanupBeforeReboot();assert(worker_done&&!reserved&&display_frame.address==0&&mode_v==0x2d2&&gpu_memory>=0);
 int frames=frame_calls,modes=mode_calls,resizes=resize_calls;
 assert(module_reboot_phase(2,NULL,0,0)==0&&module_reboot_phase(1,NULL,0,0)==0&&module_reboot_before(NULL,0,0,0)==0);
 CleanupBeforeReboot(); // Late fallback cannot re-enter shutdown drivers.
 assert(frame_calls==frames&&mode_calls==modes&&resize_calls==resizes);
 // The actual allocator uses partition 2 at the high end, validates bounds,
 // and never frees an in-flight list/context. Bad allocation cannot submit.
 Reset();pending_list=7;assert(FreeGeMemory()<0&&gpu_memory>=0);
 pending_list=-1;assert(FreeGeMemory()==0&&packet==NULL&&ge_context==NULL);
 alloc_fail=1;assert(InitGeMemory()<0&&gpu_memory<0);alloc_fail=0;
 bad_head=1;assert(InitGeMemory()<0&&gpu_memory<0&&user_block==NULL);bad_head=0;
 assert(InitGeMemory()==0&&FreeGeMemory()==0);
 // Only the exact read-only leaf getters may be used, never callback APIs.
 input_fixture=2;ResolveUtilityStatus();assert((uintptr_t)utility_type==0x88004000&&(uintptr_t)utility_status==0x88004010);
 input_fixture=3;ResolveUtilityStatus();assert(!utility_type&&!utility_status);input_fixture=0;
 // Burnout-style savedata: utility enters without the HOME status bit,
 // changes physical mode, renders into priority 2, then shuts down.
 for(int state=1;state<=5;state++){
  Reset();utility_kind=1;utility_state=state;assert(UtilityVisible()==(state!=4));
 }
 Reset();AutoControl(0,0);assert(active);
 utility_kind=1;utility_state=1;assert(MenuVisible()&&!ScalingVisible());
 assert(ModeHook(0x1d2,720,480)==0&&!abort_requested&&!active&&home_paused);
 AutoControl(MenuVisible(),100);assert(home_output_restored&&!auto_fault);
 game_frame=(Frame){0x09400000,512,3,0};utility_state=2;
 assert(RenderHome()==1&&!home_scaled_ready&&mode_v==0x2d2&&w_v==480&&h_v==272);
 assert(RenderHome()==1&&!home_scaled_ready&&mode_v==0x2d2&&w_v==480&&h_v==272);
 assert(ModeHook(0x2d2,480,272)==0&&mode_v==0x2d2&&!home_scaled_ready&&!auto_fault);
 int utility_jobs=enqueue_calls;
 for(int state=1;state<=5;state++)if(state!=4){
  utility_state=state;AutoControl(MenuVisible(),200);assert(RenderHome()==1&&enqueue_calls==utility_jobs);
 }
 utility_state=3;AutoControl(MenuVisible(),300);assert(!active&&home_paused);
 // A nested HOME menu must not resume the game while the utility is active.
 menu_bits=0x100;AutoControl(MenuVisible(),400);menu_bits=0;
 AutoControl(MenuVisible(),500);assert(!active&&home_paused);
 utility_state=4;assert(!MenuVisible());layer_frame=(Frame){0,512,3,0};
 AutoControl(MenuVisible(),300500);assert(active&&!home_paused&&!auto_fault&&mode_v==0xd2&&display_frame.stride==TV_STRIDE);
 // A subsequent dialog works too and cancellation resumes with the preset.
 utility_kind=3;utility_state=2;filter_mode=FILTER_CRT;
 AutoControl(MenuVisible(),400000);assert(!active);utility_state=0;utility_kind=0;
 AutoControl(MenuVisible(),700000);assert(active&&filter_mode==FILTER_CRT&&!auto_fault);
 // Utility already open at startup delays activation without a fault latch.
 Reset();utility_kind=1;utility_state=2;AutoControl(MenuVisible(),0);assert(!active&&!auto_fault&&!reserved);
 utility_state=0;utility_kind=0;AutoControl(MenuVisible(),100000);assert(active);
 // Reproduce R17 exactly: changing source layout at width720 first leaves
 // DMA at720 even after the software mode reports480. Both TV variants use
 // one physical mode, so the width request alone does not program geometry.
 Reset();AutoControl(0,0);slot_driver=1;memset(slots,0,sizeof(slots));
 slots[0][0]=slots[0][1]=(Frame){0x04200000,768,0,0};
 slots[2][0]=slots[2][1]=(Frame){0x04088000,512,3,0};
 assert(real_frame(0,NULL,768,0,0)==0&&real_frame(0,NULL,768,0,1)==0);
 assert(NativeFence(1)==0&&hardware_width==720&&hardware_stride==512&&hardware_format==3);
 assert(real_mode(restore_mode,480,272)==0&&w_v==480&&hardware_width==720&&!NativeHardwareReady());
 manual_native=manual_native_ready=1;pad_buttons=PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SQUARE;
 int unsafe_jobs=enqueue_calls;unsigned unsafe_allocs=ge_allocations;
 PollFilter(0,clock_us);assert(manual_native&&enqueue_calls==unsafe_jobs&&ge_allocations==unsafe_allocs);
 // Even if DMA width was corrected, stale LCDC timing must block success.
 hardware_width=480;assert(NativeHardwareReady());lcd_force_scaled=1;
 assert(!NativeHardwareReady());lcd_force_scaled=0;
 // A hardware path that refuses the requested geometry retains ownership;
 // it cannot free the buffers or permit the known-dangerous reactivation.
 Reset();AutoControl(0,0);slot_driver=1;memset(slots,0,sizeof(slots));
 slots[0][0]=slots[0][1]=(Frame){0x04200000,768,0,0};slots[2][0]=slots[2][1]=game_frame;
 freeze_geometry=1;manual_native=1;AutoControl(0,0);
 assert(!manual_native_ready&&reserved&&gpu_memory>=0&&edram==0x400000&&hardware_width==720);
 // Firmware-faithful model: separate live/pending addresses, shared layout,
 // and immediate-update restrictions. Reproduce the exact rejected R15 call.
 Reset();AutoControl(0,0);slot_driver=1;inject_native_frames=1;
 memset(slots,0,sizeof(slots));slots[0][0]=slots[0][1]=(Frame){0x04200000,TV_STRIDE,0,0};
 slots[2][0]=(Frame){0x04000000,512,3,0};slots[2][1]=(Frame){0x04088000,512,3,0};
 unsigned old_allocations=ge_allocations,old_releases=ge_releases;
 assert(real_frame(0,NULL,0,0,0)==(int)0x80000107u&&immediate_rejections==1);
 original0=(Frame){0xabbbb800,512,1,0};
 manual_native=1;AutoControl(0,0);
 assert(manual_native_ready&&!reserved&&packet==NULL&&ge_context==NULL&&gpu_memory<0&&edram==0x200000);
 assert(native_mode_with_old_layer==1&&immediate_rejections==1&&hardware_width==480);
 assert(slots[0][0].address==0xabbbb800&&slots[0][1].address==0xabbbb800&&slots[0][0].format==1&&slots[0][0].stride==512);
 assert(native_rebound[0].address==0x04088000&&native_rebound[1].address==0x04088000);
 assert(native_rebound[0].stride==512&&native_rebound[0].format==3&&native_rebound[1].stride==512&&native_rebound[1].format==3&&ge_releases==old_releases+1);
 assert(clock_us>=3*16667u&&NativeLayersDetached());
 slot_driver=0;manual_native=0;next_auto=clock_us;AutoControl(0,clock_us);
 assert(active&&reserved&&packet&&ge_allocations==old_allocations+1&&edram==0x400000);
 // A missing scanout fence never releases the bank/workspace.
 Reset();AutoControl(0,0);hold_vblank=1;manual_native=1;
 AutoControl(0,0);assert(!manual_native_ready&&reserved&&gpu_memory>=0&&edram==0x400000);
 hold_vblank=0;AutoControl(0,clock_us);assert(manual_native_ready&&!reserved&&gpu_memory<0);
 // Another live/pending layer in upper VRAM blocks release, even when our
 // own layer was detached. Do not unmap somebody else's active scanout.
 Reset();AutoControl(0,0);slot_driver=1;memset(slots,0,sizeof(slots));
 slots[0][0]=slots[0][1]=(Frame){0x04200000,TV_STRIDE,0,0};
 slots[2][0]=slots[2][1]=game_frame;slots[1][0]=slots[1][1]=(Frame){0x04300000,512,0,0};
 manual_native=1;AutoControl(0,0);assert(reserved&&gpu_memory>=0&&!manual_native_ready&&edram==0x400000);
 slots[1][0]=slots[1][1]=(Frame){0};AutoControl(0,clock_us);assert(!reserved&&gpu_memory<0&&manual_native_ready);
 // Global GE activity also postpones unmapping and freeing.
 Reset();AutoControl(0,0);ge_busy=1;manual_native=1;AutoControl(0,0);
 assert(reserved&&gpu_memory>=0&&!manual_native_ready);ge_busy=0;
 AutoControl(0,clock_us);assert(!reserved&&gpu_memory<0&&manual_native_ready);
 // Native handoff releases all scaler-owned RAM/VRAM after fences.
 Reset();AutoControl(0,0);int diagnostic_jobs=enqueue_calls;
 unsigned square_chord=PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SQUARE;
 pad_buttons=square_chord;PollFilter(0,100);assert(manual_native&&!ScalingVisible());
 AutoControl(0,100);assert(!active&&!reserved&&manual_native_ready&&mode_v==0x2d2);
 assert(gpu_memory<0&&packet==NULL&&ge_context==NULL&&edram==0x200000&&enqueue_calls==diagnostic_jobs&&ready_buffer==NULL);
 assert(RenderHome()==1&&RenderWithFilter(game_frame,0,FILTER_SOFT)==-2&&enqueue_calls==diagnostic_jobs);
 int diagnostic_modes=mode_calls;
 AutoControl(0,200);PollFilter(0,200);assert(manual_native&&mode_calls==diagnostic_modes);
 menu_bits=0x100;AutoControl(1,300);assert(RenderHome()==1&&enqueue_calls==diagnostic_jobs);
 menu_bits=0;game_frame=(Frame){0x04088000,512,3,0};
 FrameHook(0,NULL,0,3,1);assert(display_frame.address==0);
 pad_buttons=0;PollFilter(0,400);pad_buttons=square_chord;PollFilter(0,500);
 assert(!manual_native);AutoControl(0,500);
 assert(active&&mode_v==0xd2&&display_frame.stride==TV_STRIDE&&filter_mode==FILTER_SOFT);
 assert(gpu_memory>=0&&packet&&edram==0x400000&&enqueue_calls==diagnostic_jobs+1);
 // Fresh reactivation reallocates GPU storage; a now-unavailable optional
 // CPU filter falls back safely rather than stopping fullscreen activation.
 Reset();AutoControl(0,0);manual_native=1;AutoControl(0,100);assert(packet==NULL);
 filter_mode=FILTER_DEPOSTERISE;dep_only_fail=1;manual_native=0;next_auto=200;AutoControl(0,200);
 assert(active&&reserved&&gpu_memory>=0&&dep_memory<0&&filter_mode==FILTER_SHARP);
 // Native suppression can also be selected before automatic activation.
 Reset();pad_buttons=square_chord;PollFilter(0,0);AutoControl(0,0);assert(!active&&!reserved&&enqueue_calls==0);
 pad_buttons=0;PollFilter(0,1);pad_buttons=square_chord;PollFilter(0,2);AutoControl(0,2);assert(active);
 // Hardware snapshots read only the five known display-DMA words, with
 // no allocations, mode changes or GE submissions. Active capture waits 2 VB.
 Reset();AutoControl(0,0);int probe_jobs=enqueue_calls,probe_modes=mode_calls;
 unsigned probe_allocs=ge_allocations,prior_words=probe_word_reads;
 ProbeScanout("host_probe");assert(probe_word_reads==prior_words+5);
 assert(enqueue_calls==probe_jobs&&mode_calls==probe_modes&&ge_allocations==probe_allocs);
 probe_word_reads=0;PollScanoutProbe(clock_us);assert(probe_word_reads==0);
 clock_us+=33334;PollScanoutProbe(clock_us);assert(probe_word_reads==5&&!probe_active_pending);
 // Firmware state 4 is resumable PAUSED, not proof of completion.
 Reset();AutoControl(0,0);sync_paused=1;unsigned completed=draws;
 assert(Render(game_frame,1)==-2&&pending_list==7&&draws==completed);
 assert(!timeouts&&!abort_requested&&!auto_fault&&paused_lists==1);
 ScalePacket held_packet=*packet;PspGeContext held_context=*ge_context;
 assert(WaitList(1000)==-3&&pending_list==7&&paused_lists==1);
 assert(Render(game_frame,0)<0&&FreeGeMemory()<0&&FreeDepMemory()<0);
 assert(!memcmp(packet,&held_packet,sizeof(*packet))&&!memcmp(ge_context,&held_context,sizeof(*ge_context)));
 // Even a global idle report cannot release a known resumable paused list.
 assert(!ge_busy&&WaitList(1000)==-3&&pending_list==7);
 sync_paused=0;assert(WaitList(1000)==0&&pending_list<0);
 AutoControl(0,clock_us+300000);AutoControl(0,clock_us+600000);
 assert(active&&!auto_fault&&mode_v==0xd2);
 // Reset after pause uses R9's invalid-ID fence, never paused-ID reuse.
 Reset();AutoControl(0,0);sync_paused=1;assert(Render(game_frame,1)==-2);
 sync_paused=0;sync_invalid=ge_busy=1;assert(WaitList(1000)==-2&&pending_list==7);
 ge_busy=0;assert(WaitList(1000)==1&&pending_list<0);sync_invalid=0;
 // Exact uploaded failure: an invalid owned list ID must not cause an
 // endless wait or reuse memory while other GE work is still running.
 Reset();pending_list=7;sync_invalid=ge_busy=1;
 assert(WaitList(1000)==-2&&pending_list==7&&FreeGeMemory()<0);
 assert(!auto_fault&&!abort_requested);
 ge_busy=0;assert(WaitList(1000)==1&&pending_list<0&&gpu_memory>=0);
 sync_invalid=0;
 // Queue retirement during a scaler job must discard its image, then resume.
 Reset();AutoControl(0,0);int previous_draws=draws;
 utility_during_render=1;
 assert(Render(game_frame,1)==-2&&!active&&home_paused&&pending_list<0);
 assert(draws==(unsigned)previous_draws&&!timeouts&&!abort_requested&&!auto_fault);
 sync_invalid=0;AutoControl(MenuVisible(),100);assert(utility_native_ready&&home_output_restored);
 int native_jobs=enqueue_calls;assert(RenderHome()==1&&enqueue_calls==native_jobs);
 utility_state=4;layer_frame=(Frame){0,512,3,0};AutoControl(MenuVisible(),300100);
 assert(active&&!home_paused&&!auto_fault&&mode_v==0xd2&&display_frame.stride==TV_STRIDE);
 // A still-busy invalid ID retains the packet until a later idle poll.
 Reset();AutoControl(0,0);sync_invalid=ge_busy=1;
 assert(Render(game_frame,1)==-2&&pending_list==7&&!timeouts&&!abort_requested);
 assert(FreeGeMemory()<0);ge_busy=0;assert(WaitList(1000)==1);sync_invalid=0;
 AutoControl(0,clock_us+300000);assert(home_output_restored);
 AutoControl(0,clock_us+600000);assert(active&&!auto_fault);
 // Validate the same kernel merged positive-peek target as Impose uses.
 Reset();input_fixture=1;ResolveInput();assert((uintptr_t)merged_peek==0x88002000u);
 input_owner.attribute=0;ResolveInput();assert(!merged_peek);input_owner.attribute=0x1000;
 input_fixture=0;ResolveInput();assert(!merged_peek);
 merged_peek=MergedPad;pad_buttons=0;PollFilter(0,1);assert(filter_mode==FILTER_SOFTER);merged_peek=NULL;
 // Cycle once per Triangle press; HOME consumes the edge without changing.
 Reset();unsigned chord=PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_TRIANGLE;
 pad_buttons=chord;PollFilter(0,100);assert(filter_mode==FILTER_SOFTER);
 PollFilter(0,101);assert(filter_mode==FILTER_SOFTER);
 pad_buttons=0;PollFilter(0,102);pad_buttons=chord;PollFilter(1,103);assert(filter_mode==FILTER_SOFTER);
 PollFilter(0,104);assert(filter_mode==FILTER_SOFTER);
 for(int next=FILTER_BLUR;next<FILTER_COUNT;next++){
  pad_buttons=0;PollFilter(0,105);pad_buttons=chord;PollFilter(0,106);assert(filter_mode==next);
 }
 assert(dep_memory>=0&&dep_pixels);
 pending_list=7;assert(FreeDepMemory()<0);pending_list=-1;
 // Real CPU prepass in the real Render wrapper, all four source formats.
 void *pixels=mmap((void *)0x09200000,512*272*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(pixels!=MAP_FAILED);memset(pixels,0x44,512*272*4);
 for(int fmt=0;fmt<4;fmt++){
  assert(Render((Frame){0x09200000,512,fmt,0},0)==0);
  unsigned tex=0,high=0;
  for(unsigned i=0;i<packet->count;i++){unsigned c=packet->commands[i]>>24,v=packet->commands[i]&0xffffff;if(c==0xa0)tex=v;if(c==0xa8)high=(v&0xf0000)<<8;}
  assert((tex|high)==(uintptr_t)dep_pixels);
 }
 assert(munmap(pixels,512*272*4)==0);
 pad_buttons=0;PollFilter(0,107);pad_buttons=chord;PollFilter(0,108);
 assert(filter_mode==FILTER_SHARP&&dep_memory<0);
 filter_mode=FILTER_DITHER;pad_buttons=0;PollFilter(0,109);alloc_fail=1;
 pad_buttons=chord;PollFilter(0,110);assert(filter_mode==FILTER_SHARP&&dep_memory<0);alloc_fail=0;
 assert(FreeGeMemory()==0);
 puts("Simple R23 automatic startup/filter cycle/native toggle/utility dialog/HOME/paused-GE and GE user-memory checks passed: immediate default-on, TV/source gating, busy retry, fault latch, NODS native status, NULL/kernel output restoration, native menu RGBA5551 GPU scaling, reboot quiesce/scanout cleanup, preserved HOME canvas/stride/format, no HOME resize, automatic resume and in-flight HOME suppression.");
}
unsigned TestRead(unsigned off){return off==0?3:off==0x1c?(lcd_force_scaled?2160:slot_driver?(hardware_width==720?2160:2040):(mode_v==0xd2?2160:2040)):off==0x2c?480:0;}
int sceKernelIsIntrContext(void){return 0;}
int sceKernelIsCpuIntrEnable(void){return intr_depth==0;}
SceUID sceKernelGetThreadId(void){return 1;}
int sceKernelCpuSuspendIntr(void){int old=intr_depth;intr_depth++;return old;}void sceKernelCpuResumeIntr(int n){assert(n<intr_depth);intr_depth=n;}
SceModule *sceKernelFindModuleByName(const char *n){if(input_fixture>=2&&!strcmp(n,"sceUtility_Driver"))return &utility_module;return input_fixture==1&&!strcmp(n,"sceImpose_Driver")?&input_module:NULL;}
void sceKernelDcacheWritebackInvalidateAll(void){}void sceKernelIcacheInvalidateAll(void){}
void sceKernelDcacheWritebackRange(const void *p,unsigned n){(void)p;(void)n;}
void sceKernelDcacheWritebackInvalidateRange(const void *p,unsigned n){assert(p&&n);}
unsigned pspSdkSetK1(unsigned n){unsigned old=current_k1;current_k1=n;return old;}
unsigned sceKernelGetSystemTimeLow(void){return clock_us;}
int sceKernelDelayThread(unsigned n){assert(intr_depth==0);unsigned previous=clock_us/16667u;clock_us+=n;
 if(slot_driver&&!hold_vblank&&clock_us/16667u!=previous){for(int i=0;i<4;i++)slots[i][0]=slots[i][1];FakeLayoutTick();}
 return 0;}

int sceGeListSync(int a,int b){(void)a;(void)b;if(open_during_render){menu_bits=0x100;open_during_render=0;}if(utility_during_render){utility_kind=1;utility_state=1;sync_invalid=1;utility_during_render=0;}
 return sync_paused?4:sync_invalid?(int)0x80000100u:PSP_GE_LIST_DONE;}
int sceGeListDeQueue(int n){(void)n;return 0;}
int sceGeListEnQueue(const void *p,void *s,int cb,PspGeListArgs *a){
 (void)s;(void)cb;assert(intr_depth>0);assert(p==packet->commands&&a->context==ge_context);
 assert((uintptr_t)p>=0x08800000u&&(uintptr_t)p<0x0c000000u);
 assert((uintptr_t)packet->vertices>=0x08800000u&&(uintptr_t)packet->clear<0x0c000000u);
 assert((uintptr_t)packet->scanlines>=0x08800000u&&(uintptr_t)(packet->scanlines+480)<=0x0c000000u);
 assert((uintptr_t)a->context>=0x08800000u&&(uintptr_t)a->context+sizeof(*ge_context)<=0x0c000000u);
 enqueue_calls++;return fail_enqueue?-1:7;
}
int sceGeDrawSync(int n){(void)n;return ge_busy?1:0;}
int sceDisplayWaitVblankStart(void){clock_us+=16667;return 0;}
int sceIoOpen(const char *p,int f,int m){(void)p;(void)f;(void)m;return -1;}
int sceIoWrite(int fd,const void *p,unsigned n){(void)fd;(void)p;return n;}
int sceIoClose(int fd){(void)fd;return 0;}
unsigned sceKernelDevkitVersion(void){return 0x06060010;}int sceKernelGetModel(void){return 4;}
int sceKernelCreateThread(const char *n,int (*f)(SceSize,void *),int p,int s,int a,void *o){(void)n;(void)f;(void)p;(void)s;(void)a;(void)o;return -1;}
int sceKernelStartThread(int t,int n,void *p){(void)t;(void)n;(void)p;return 0;}
int sceKernelDeleteThread(int t){(void)t;return 0;}
int sceKernelWaitThreadEnd(int t,SceUInt *n){(void)t;(void)n;return 0;}
void sctrlHENPatchSyscall(void *a,void *b){(void)a;(void)b;}

SceUID sceKernelAllocPartitionMemory(SceUID partition,const char *name,int type,SceSize bytes,void *addr){
 assert(partition==2&&type==PSP_SMEM_High&&addr==NULL);
 if(!strcmp(name,"GoTVScaleDep")){
  assert(bytes==DEPOSTER_BYTES+63u);if(alloc_fail||dep_only_fail)return -1;
  assert(!dep_block);dep_block_bytes=(bytes+16+4095u)&~4095u;
  dep_block=mmap((void *)0x09100000,dep_block_bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
  assert(dep_block!=MAP_FAILED);return 43;
 }
 assert(bytes==sizeof(GeWorkspace)+63u);ge_allocations++;
 if(alloc_fail)return -1;
 assert(user_block==NULL);
 user_block_bytes=(bytes+16+4095u)&~4095u;
 user_block=mmap((void *)0x09000000,user_block_bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(user_block!=MAP_FAILED);return 42;
}
void *sceKernelGetBlockHeadAddr(SceUID uid){if(uid==43)return (char *)dep_block+16;assert(uid==42);return bad_head?(void *)0x88000000:(char *)user_block+16;}
int sceKernelFreePartitionMemory(SceUID uid){if(uid==43){assert(dep_block&&munmap(dep_block,dep_block_bytes)==0);dep_block=NULL;return 0;}assert(uid==42&&user_block);ge_releases++;assert(munmap(user_block,user_block_bytes)==0);user_block=NULL;return 0;}

int sceIoRead(int fd,void *data,unsigned n){(void)fd;(void)data;(void)n;return -1;}

int sceDisplayGetVcount(void){return hold_vblank?0:clock_us/16667u;}

int sceCtrlPeekBufferPositive(SceCtrlData *pad,int n){assert(n==1);pad->Buttons=pad_buttons;return 1;}
SceModule *sceKernelFindModuleByAddress(unsigned a){return input_fixture&&a==0x88002000?&input_owner:NULL;}
unsigned sctrlFindImportByNID(SceModule *m,char *lib,unsigned nid){(void)m;(void)lib;(void)nid;return input_fixture?0x88001000:0;}
unsigned TestCodeWord(unsigned a){
 if(input_fixture>=2&&a>=0x88004000&&a<0x88004020){unsigned off=(a-0x88004000)%16;
  if(off==0)return 0x3c048800;
  if(off==4)return input_fixture==3?0x0c000000:0x03e00008;
  if(off==8)return 0x8c8266cc;
 }
 return a==0x88001000?0x0a000800:0;
}

unsigned TestHardwareWord(unsigned address){
 assert(intr_depth>0&&address>=0xbc800100u&&address<=0xbc800110u&&!(address&3));
 probe_word_reads++;Frame f=FakeSelected();
 if(address==0xbc800100u)return f.address&0x1fffffffu;
 if(address==0xbc800104u)return (0x39u>>((slot_driver?hardware_format:f.format)*2))&3u;
 if(address==0xbc800108u)return slot_driver?(unsigned)hardware_width:mode_v==0xd2?720u:480u;
 if(address==0xbc80010cu)return slot_driver?(unsigned)hardware_stride:(unsigned)f.stride;
 return 3;
}

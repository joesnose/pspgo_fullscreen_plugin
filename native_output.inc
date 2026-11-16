/* Runs only on the scaler worker. Never wait or manipulate GE from a display
 * callback. Current and next-vblank layer bindings are separate firmware state. */
static const char *native_wait_reason;
static int native_block_pri=-1,native_block_sync;
static unsigned native_block_address;
static int NativeWait(const char *reason){
 if(native_wait_reason!=reason){logline("native_handoff_wait,reason,%s,layer,%d,sync,%d,address,0x%08X\n",reason,native_block_pri,native_block_sync,native_block_address);native_wait_reason=reason;}
 return 1;
}
static int NativeFence(unsigned frames){
 unsigned count=(unsigned)sceDisplayGetVcount(),start=sceKernelGetSystemTimeLow();
 while((unsigned)((unsigned)sceDisplayGetVcount()-count)<frames){
  if(!running||reboot_exit||reboot_late)return -1;
  if(sceKernelGetSystemTimeLow()-start>=50000u)return 1;
  sceKernelDelayThread(1000);
 }
 return 0;
}
static int ReadNativeGameFrame(int sync,Frame *f){
 void *p=NULL;int stride=0,format=-1;
 if(get_frame(2,&p,&stride,&format,sync)<0)return -1;
 *f=(Frame){(uintptr_t)p,stride,format,0};return ValidFrame(*f)?0:-1;
}
static int NativeLayersDetached(void){
 native_block_pri=-1;native_block_address=0;
 for(int pri=0;pri<4;pri++)for(int sync=0;sync<2;sync++){
  void *p=NULL;int stride=0,format=-1;
  if(get_frame(pri,&p,&stride,&format,sync)<0){native_block_pri=pri;native_block_sync=sync;return 0;}
  uintptr_t physical=(uintptr_t)p&0x1fffffffu;
  if(p&&physical>=0x04200000u&&physical<0x04400000u){native_block_pri=pri;native_block_sync=sync;native_block_address=(unsigned)physical;return 0;}
 }
 return 1;
}
static int RestoreNativeOutput(void){
 active=0;home_paused=1;home_scaled_ready=0;
 if(pending_list>=0)return NativeWait("owned_ge_list");
 if(!running||reboot_exit||reboot_late)return -1;
 if(reserved){
  Frame current,next;
  if(ReadNativeGameFrame(0,&current)<0||ReadNativeGameFrame(1,&next)<0)return NativeWait("native_game_buffer");
  // Remove both the live TV buffer and any queued replacement before changing
  // layout. A queued-only clear leaves RGB565/stride768 live during set-mode.
  if(real_frame(0,NULL,0,0,0)<0||real_frame(0,NULL,0,0,1)<0)return NativeWait("detach_layer");
  int fence=NativeFence(1);if(fence)return fence<0?-1:NativeWait("detach_vblank");
  if(real_mode(restore_mode,480,272)<0)return NativeWait("native_mode");
  // Re-read after the mode change. Keep current/pending game tuples distinct;
  // the game can present another buffer while the worker waits for scanout.
  int intr=sceKernelCpuSuspendIntr(),ret=-1;
  if(ReadNativeGameFrame(0,&current)==0&&ReadNativeGameFrame(1,&next)==0){
   Frame layer=Snapshot(&original0);
   if(!layer.address||ValidOutputFrame(layer)){
    ret=real_frame(2,(void *)(uintptr_t)current.address,current.stride,current.format,0);
    if(ret>=0)ret=real_frame(2,(void *)(uintptr_t)next.address,next.stride,next.format,1);
    if(ret>=0)ret=real_frame(0,(void *)(uintptr_t)layer.address,layer.stride,layer.format,0);
    if(ret>=0)ret=real_frame(0,(void *)(uintptr_t)layer.address,layer.stride,layer.format,1);
   }
  }
  sceKernelCpuResumeIntr(intr);
  if(ret<0)return NativeWait("native_bind");
  fence=NativeFence(2);if(fence)return fence<0?-1:NativeWait("native_vblank");
  int mode=-1,w=0,h=0;
  if(get_mode(&mode,&w,&h)<0||mode!=restore_mode||w!=480||h!=272)return NativeWait("native_mode_verify");
  // Verify references and idle state atomically with restoring the 2 MiB bank.
  intr=sceKernelCpuSuspendIntr();ret=1;
  if(NativeLayersDetached()&&sceGeDrawSync(1)==PSP_GE_LIST_DONE){
   ret=set_size(0x200000);
   if(ret>=0&&get_size()==0x200000){reserved=0;ready_buffer=NULL;ret=0;}else ret=1;
  }
  sceKernelCpuResumeIntr(intr);
  if(ret)return NativeWait("scanout_or_ge_busy");
  logline("native_output_bound,current,0x%08X,stride,%d,format,%d,next,0x%08X,next_stride,%d,next_format,%d,mode,0x%X\n",current.address,current.stride,current.format,next.address,next.stride,next.format,(unsigned)mode);
 }
 if(reboot_exit||reboot_late)return -1;
 // No current/queued scanout or GE list references the scaler's old memory.
 if(FreeDepMemory()<0||FreeGeMemory()<0)return NativeWait("workspace_release");
 home_output_restored=1;utility_native_ready=1;native_wait_reason=NULL;
 logline("native_handoff_complete,edram,%d,reserved,%u,gpu_memory,%d,dep_memory,%d\n",get_size(),reserved,gpu_memory,dep_memory);
 return 0;
}

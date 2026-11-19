/* Diagnostic reads only. These five LCD DMA registers are used by this Go
 * firmware's own framebuffer/format setters; no registers are written here. */
typedef int (*ProbeGeFn)(int);
typedef int (*ProbeDmaFn)(int *,int *,int *);
static ProbeGeFn probe_ge,probe_cmd;
static int probe_native_reference;
static unsigned probe_native_lcd_x,probe_native_lcd_y,probe_native_dma_format;
static int probe_native_mode;
static ProbeDmaFn probe_dma;
static int probe_active_pending,probe_manual_started,probe_bound_taken;
static unsigned probe_active_vcount;
static int probe_ui_pending,probe_ui_seen;
static unsigned probe_ui_at;
static void ArmManualProbe(void){probe_manual_started=probe_bound_taken=0;}
static unsigned ProbeWord(unsigned address){
#ifdef HOST_TEST
 return TestHardwareWord(address);
#else
 return *(volatile const unsigned *)(uintptr_t)address;
#endif
}
static void ResolveScanoutProbe(void){
 probe_ge=(ProbeGeFn)(uintptr_t)Find("sceGE_Manager","sceGe_driver",0x1fdb4c62);
 probe_cmd=(ProbeGeFn)(uintptr_t)Find("sceGE_Manager","sceGe_driver",0xe6ee2394);
 probe_dma=(ProbeDmaFn)(uintptr_t)Find("sceLowIO_Driver","sceDmacplus_driver",0x33cd36dd);
 logline("probe_support,ge_getter,%u,cmd_getter,%u,dma_getter,%u\n",probe_ge!=NULL,probe_cmd!=NULL,probe_dma!=NULL);
}
static void ProbeScanout(const char *point){
 if(!LOGGING_ENABLED&&strcmp(point,"cold_native"))return;
 if(!TvActive()){logline("scanout_probe_skipped,point,%s,tv_inactive,1\n",point);return;}
 unsigned dma[5],lcd[3];int mode=-1,w=0,h=0,software=-1,ge[3]={-1,-1,-1},cache[3]={-1,-1,-1};
 int commands[3]={-1,-1,-1};
 Frame frames[4][2];memset(frames,0,sizeof(frames));
 int intr=sceKernelCpuSuspendIntr();
 get_mode(&mode,&w,&h);software=get_size();
 if(probe_ge){ge[0]=probe_ge(19);ge[1]=probe_ge(28);ge[2]=probe_ge(29);}
 if(probe_cmd){commands[0]=probe_cmd(0x9c);commands[1]=probe_cmd(0x9d);commands[2]=probe_cmd(0xd2);}
 if(probe_dma)probe_dma(&cache[0],&cache[1],&cache[2]);
 for(unsigned i=0;i<5;i++)dma[i]=ProbeWord(0xbc800100u+i*4u);
 lcd[0]=Read(0);lcd[1]=Read(0x1c);lcd[2]=Read(0x2c);
 for(int layer=0;layer<4;layer++)for(int sync=0;sync<2;sync++){
  void *address=NULL;int stride=0,format=-1;
  get_frame(layer,&address,&stride,&format,sync);
  frames[layer][sync]=(Frame){(uintptr_t)address,stride,format,0};
 }
 if(!probe_native_reference&&!strcmp(point,"cold_native")&&w==480&&h==272&&dma[2]==480u){
  probe_native_reference=1;probe_native_lcd_x=lcd[1];probe_native_lcd_y=lcd[2];probe_native_dma_format=dma[1];probe_native_mode=mode;
 }
 sceKernelCpuResumeIntr(intr);
 logline("scanout_probe,point,%s,ms,%u,mode,0x%X,width,%d,height,%d,software_size,%d,ge_enabled,%d,ge_trans_disabled,%d,ge_trans_value,%d\n",point,sceKernelGetSystemTimeLow()/1000,(unsigned)mode,w,h,software,ge[0],ge[1],ge[2]);
 logline("scanout_dma,point,%s,base,0x%08X,format,0x%08X,width,0x%08X,stride,0x%08X,control,0x%08X,cached_width,%d,cached_stride,%d,cached_format,%d\n",point,dma[0],dma[1],dma[2],dma[3],dma[4],cache[0],cache[1],cache[2]);
 logline("scanout_ge_framebuffer,point,%s,address_word,0x%08X,stride_word,0x%08X,format_word,0x%08X\n",point,(unsigned)commands[0],(unsigned)commands[1],(unsigned)commands[2]);
 logline("scanout_lcdc,point,%s,state,0x%08X,x,%u,y,%u\n",point,lcd[0],lcd[1],lcd[2]);
 for(int layer=0;layer<4;layer++)for(int sync=0;sync<2;sync++){
  Frame f=frames[layer][sync];
  logline("scanout_layer,point,%s,priority,%d,sync,%d,address,0x%08X,stride,%d,format,%d\n",point,layer,sync,f.address,f.stride,f.format);
 }
}
static void ArmScanoutProbe(void){probe_active_pending=1;probe_active_vcount=(unsigned)sceDisplayGetVcount();}
static void PollScanoutProbe(unsigned now){
 if(probe_active_pending&&active&&(unsigned)((unsigned)sceDisplayGetVcount()-probe_active_vcount)>=2){ProbeScanout("scaled_settled");probe_active_pending=0;}
 int visible=utility_type&&utility_status&&utility_type()==1&&utility_status()==2;
 if(visible&&!probe_ui_seen){probe_ui_seen=1;probe_ui_pending=1;probe_ui_at=now;}
 if(!visible){probe_ui_seen=0;probe_ui_pending=0;}
 if(probe_ui_pending&&now-probe_ui_at>=100000u){ProbeScanout("utility_visible");probe_ui_pending=0;}
}

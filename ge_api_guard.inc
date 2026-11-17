/* User syscalls only: driver-internal context management and this worker's
 * kernel imports bypass these wrappers. Never inspect caller context memory. */
typedef int (*GeContextFn)(PspGeContext *);
typedef int (*GeBreakFn)(int,void *);
static GeContextFn real_ge_save,real_ge_restore;
static GeBreakFn real_ge_break;
static int ge_api_hooked;
typedef struct {unsigned k1;int trace;} GeApiTicket;
static unsigned ge_api_calls[3],ge_api_logged[3];
static GeApiTicket BeginGeApi(const char *name,int index,int drain,int argument){
 unsigned k1=pspSdkSetK1(0);
 int intr=sceKernelCpuSuspendIntr();++ge_api_entries;drain=drain&&(test_profile&1u);sceKernelCpuResumeIntr(intr);
 unsigned start=sceKernelGetSystemTimeLow();
 int trace=!ge_api_calls[index]||(unsigned)(start-ge_api_logged[index])>=1000000u;
 ++ge_api_calls[index];if(trace)ge_api_logged[index]=start;
 if(trace)logline("ge_api_enter,name,%s,ms,%u,argument,%d,pending,%d,reserved,%u\n",name,start/1000,argument,pending_list,reserved);
 // A context snapshot/restore requires GE idle. Let the worker retire its
 // owned list, while forbidding new submissions. Never cancel game work.
 // Break must run immediately: it may be needed to unstall the queue.
 if(drain)while(running&&!reboot_exit&&pending_list>=0&&sceKernelGetSystemTimeLow()-start<100000u)sceKernelDelayThread(250);
 if(drain&&trace)logline("ge_api_handoff,name,%s,own_list_idle,%u,wait_us,%u\n",name,pending_list<0,sceKernelGetSystemTimeLow()-start);
 pspSdkSetK1(k1);return (GeApiTicket){k1,trace};
}
static void EndGeApi(const char *name,int result,GeApiTicket ticket){
 pspSdkSetK1(0);
 if(ticket.trace)logline("ge_api_return,name,%s,ms,%u,result,0x%08X,pending,%d\n",name,sceKernelGetSystemTimeLow()/1000,(unsigned)result,pending_list);
 int intr=sceKernelCpuSuspendIntr();--ge_api_entries;sceKernelCpuResumeIntr(intr);
 pspSdkSetK1(ticket.k1);
}
static int GuardGeContext(PspGeContext *context,int restore){
 GeContextFn original=restore?real_ge_restore:real_ge_save;
 if(!running||reboot_exit||!reserved)return original(context);
 const char *name=restore?"restore_context":"save_context";
 GeApiTicket ticket=BeginGeApi(name,restore,1,0);
 int result=original(context);EndGeApi(name,result,ticket);return result;
}
static int GeSaveHook(PspGeContext *context){return GuardGeContext(context,0);}
static int GeRestoreHook(PspGeContext *context){return GuardGeContext(context,1);}
static int GeBreakHook(int mode,void *argument){
 if(!running||reboot_exit||!reserved)return real_ge_break(mode,argument);
 GeApiTicket ticket=BeginGeApi("break",2,0,mode);
 int result=real_ge_break(mode,argument);EndGeApi("break",result,ticket);return result;
}
static void InstallGeApiHooks(void){
 real_ge_save=(GeContextFn)(uintptr_t)Find("sceGE_Manager","sceGe_user",0x438a385a);
 real_ge_restore=(GeContextFn)(uintptr_t)Find("sceGE_Manager","sceGe_user",0x0bf608fb);
 real_ge_break=(GeBreakFn)(uintptr_t)Find("sceGE_Manager","sceGe_user",0xb448ec0d);
 // User-library NIDs above match the inspected driver export table.
 if(!real_ge_save||!real_ge_restore||!real_ge_break){logline("ge_api_guard,0,missing_exports,1\n");return;}
 sctrlHENPatchSyscall((void *)real_ge_save,GeSaveHook);
 sctrlHENPatchSyscall((void *)real_ge_restore,GeRestoreHook);
 sctrlHENPatchSyscall((void *)real_ge_break,GeBreakHook);
 ge_api_hooked=1;logline("ge_api_guard,1\n");
}
static void RemoveGeApiHooks(void){
 if(!ge_api_hooked)return;
 sctrlHENPatchSyscall(GeSaveHook,(void *)real_ge_save);
 sctrlHENPatchSyscall(GeRestoreHook,(void *)real_ge_restore);
 sctrlHENPatchSyscall(GeBreakHook,(void *)real_ge_break);
 ge_api_hooked=0;
}

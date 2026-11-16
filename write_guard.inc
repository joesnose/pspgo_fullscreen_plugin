/* Only user syscalls are redirected. The plugin's kernel log writes bypass
 * these wrappers. Buffers and asynchronous result pointers remain untouched. */
typedef int (*FileWriteFn)(SceUID,const void *,SceSize);
typedef int (*FileAsyncFn)(SceUID,int64_t *);
typedef int (*FileStatFn)(SceUID,int,int64_t *);
typedef int (*FileCloseFn)(SceUID);
static int FileWriteHook(SceUID,const void *,SceSize);
static int FileWriteAsyncHook(SceUID,const void *,SceSize);
static int FilePollHook(SceUID,int64_t *);
static int FileWaitHook(SceUID,int64_t *);
static int FileWaitCBHook(SceUID,int64_t *);
static int FileStatHook(SceUID,int,int64_t *);
static int FileCloseHook(SceUID);
typedef struct {unsigned nid;uintptr_t original,replacement;} FileHook;
static FileHook file_hooks[]={
 {0x42ec03ac,0,(uintptr_t)FileWriteHook},{0x0facab19,0,(uintptr_t)FileWriteAsyncHook},
 {0x3251ea56,0,(uintptr_t)FilePollHook},{0xe23eec33,0,(uintptr_t)FileWaitHook},
 {0x35dbd746,0,(uintptr_t)FileWaitCBHook},{0xcb05f8d6,0,(uintptr_t)FileStatHook},
 {0x810c4bc3,0,(uintptr_t)FileCloseHook}};
static int file_hooks_installed;
static SceUID async_writes[128];
static int async_overflow;
static int HasPendingWrites(void){
 if(async_overflow)return 1;
 for(unsigned i=0;i<128;i++)if(async_writes[i])return 1;
 return 0;
}
static void TrackWrite(SceUID fd,int add){
 int intr=sceKernelCpuSuspendIntr();int empty=-1,found=0;
 for(unsigned i=0;i<128;i++){
  if(async_writes[i]==fd){found=1;if(!add)async_writes[i]=0;}
  else if(!async_writes[i]&&empty<0)empty=(int)i;
 }
 if(add&&!found){if(empty>=0)async_writes[empty]=fd;else async_overflow=1;}
 sceKernelCpuResumeIntr(intr);
}
static int IsTrackedWrite(SceUID fd){
 int intr=sceKernelCpuSuspendIntr(),found=0;
 for(unsigned i=0;i<128;i++)if(async_writes[i]==fd)found=1;
 sceKernelCpuResumeIntr(intr);return found;
}
static unsigned BeginFileWrite(SceUID fd,unsigned bytes,int asynchronous){
 unsigned caller_k1=pspSdkSetK1(0);int intr=sceKernelCpuSuspendIntr();
 ++save_entries;
 if(!save_gate){save_gate=1;utility_native_ready=0;}
 sceKernelCpuResumeIntr(intr);
 logline("file_write_enter,fd,%d,bytes,%u,async,%u,pending,%d\n",fd,bytes,asynchronous,pending_list);
 unsigned start=sceKernelGetSystemTimeLow();
 while(running&&!reboot_exit&&(pending_list>=0||!utility_native_ready||!home_output_restored)&&
       sceKernelGetSystemTimeLow()-start<250000u)sceKernelDelayThread(1000);
 logline("file_write_handoff,native_ready,%u,wait_us,%u\n",pending_list<0&&utility_native_ready&&home_output_restored,sceKernelGetSystemTimeLow()-start);
 if(asynchronous)TrackWrite(fd,1);
 pspSdkSetK1(caller_k1);return caller_k1;
}
static int GuardWrite(SceUID fd,const void *data,SceSize bytes,int asynchronous){
 FileWriteFn original=(FileWriteFn)file_hooks[asynchronous?1:0].original;
 if(!running||reboot_exit||(!reserved&&!save_gate)||fd<3||!bytes)return original(fd,data,bytes);
 int already_pending=asynchronous&&IsTrackedWrite(fd);
 unsigned caller_k1=BeginFileWrite(fd,bytes,asynchronous);
 int ret=original(fd,data,bytes);pspSdkSetK1(0);
 if(asynchronous&&ret<0&&!already_pending)TrackWrite(fd,0);
 int intr=sceKernelCpuSuspendIntr();save_quiet_until=sceKernelGetSystemTimeLow()+300000u;--save_entries;sceKernelCpuResumeIntr(intr);
 if(!reboot_late)logline("file_write_return,fd,%d,async,%u,result,0x%08X\n",fd,asynchronous,(unsigned)ret);
 pspSdkSetK1(caller_k1);return ret;
}
static int FileWriteHook(SceUID fd,const void *data,SceSize bytes){return GuardWrite(fd,data,bytes,0);}
static int FileWriteAsyncHook(SceUID fd,const void *data,SceSize bytes){return GuardWrite(fd,data,bytes,1);}
static void CompletedFileWrite(SceUID fd,int ret){
 if(ret!=0||fd<3)return;
 unsigned caller_k1=pspSdkSetK1(0);
 if(IsTrackedWrite(fd)){
  TrackWrite(fd,0);int intr=sceKernelCpuSuspendIntr();save_quiet_until=sceKernelGetSystemTimeLow()+300000u;sceKernelCpuResumeIntr(intr);
  if(!reboot_late)logline("file_write_async_complete,fd,%d\n",fd);
 }
 pspSdkSetK1(caller_k1);
}
static int FilePollHook(SceUID fd,int64_t *result){int ret=((FileAsyncFn)file_hooks[2].original)(fd,result);CompletedFileWrite(fd,ret);return ret;}
static int FileWaitHook(SceUID fd,int64_t *result){int ret=((FileAsyncFn)file_hooks[3].original)(fd,result);CompletedFileWrite(fd,ret);return ret;}
static int FileWaitCBHook(SceUID fd,int64_t *result){int ret=((FileAsyncFn)file_hooks[4].original)(fd,result);CompletedFileWrite(fd,ret);return ret;}
static int FileStatHook(SceUID fd,int poll,int64_t *result){int ret=((FileStatFn)file_hooks[5].original)(fd,poll,result);CompletedFileWrite(fd,ret);return ret;}
static int FileCloseHook(SceUID fd){int ret=((FileCloseFn)file_hooks[6].original)(fd);CompletedFileWrite(fd,ret);return ret;}
static void InstallFileHooks(void){
 for(unsigned i=0;i<sizeof(file_hooks)/sizeof(*file_hooks);i++){
  file_hooks[i].original=Find("sceIOFileManager","IoFileMgrForUser",file_hooks[i].nid);
  if(!file_hooks[i].original){logline("file_write_guard,0,missing,0x%08X\n",file_hooks[i].nid);return;}
 }
 for(unsigned i=0;i<sizeof(file_hooks)/sizeof(*file_hooks);i++)sctrlHENPatchSyscall((void *)file_hooks[i].original,(void *)file_hooks[i].replacement);
 file_hooks_installed=1;logline("file_write_guard,1\n");
}
static void RemoveFileHooks(void){
 if(!file_hooks_installed)return;
 for(unsigned i=0;i<sizeof(file_hooks)/sizeof(*file_hooks);i++)sctrlHENPatchSyscall((void *)file_hooks[i].replacement,(void *)file_hooks[i].original);
 file_hooks_installed=0;
}

#ifndef GO_TV_TEST_PSP_H
#define GO_TV_TEST_PSP_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;
typedef int SceUID;
typedef unsigned SceSize;
typedef unsigned SceUInt;
typedef struct { unsigned context[512]; } PspGeContext;
typedef struct { unsigned size;PspGeContext *context;unsigned numStacks;void *stacks; } PspGeListArgs;
typedef struct { unsigned Buttons; } SceCtrlData;
typedef struct { void *ent_top;unsigned ent_size,text_addr,text_size,attribute; } SceModule;
typedef struct { unsigned short len,stubcount,vstubcount;const char *libname;void *entrytable; } SceLibraryEntryTable;
#define PSP_MODULE_INFO(a,b,c,d)
#define PSP_GE_LIST_DONE 0
#define PSP_GE_LIST_QUEUED 1
#define PSP_GE_LIST_CANCEL_DONE 4
#define PSP_SMEM_High 1
#define PSP_O_RDONLY 0
#define PSP_O_WRONLY 1
#define PSP_O_APPEND 2
#define PSP_O_CREAT 4
#define PSP_O_TRUNC 8
#define PSP_CTRL_LTRIGGER 0x100
#define PSP_CTRL_RTRIGGER 0x200
#define PSP_CTRL_TRIANGLE 0x1000
#define PSP_CTRL_CIRCLE 0x2000
#define PSP_CTRL_SQUARE 0x8000
#define PSP_CTRL_HOME 0x10000
unsigned TestRead(unsigned);
unsigned TestCodeWord(unsigned);
unsigned TestHardwareWord(unsigned);
SceUID sceKernelAllocPartitionMemory(SceUID,const char *,int,SceSize,void *);
void *sceKernelGetBlockHeadAddr(SceUID);
int sceKernelFreePartitionMemory(SceUID);
int sceKernelIsIntrContext(void);
int sceKernelIsCpuIntrEnable(void);
SceUID sceKernelGetThreadId(void);
int sceKernelCpuSuspendIntr(void);
void sceKernelCpuResumeIntr(int);
SceModule *sceKernelFindModuleByName(const char *);
SceModule *sceKernelFindModuleByAddress(unsigned);
void sceKernelDcacheWritebackInvalidateAll(void);
void sceKernelIcacheInvalidateAll(void);
void sceKernelDcacheWritebackRange(const void *,unsigned);
void sceKernelDcacheWritebackInvalidateRange(const void *,unsigned);
unsigned pspSdkSetK1(unsigned);
unsigned sceKernelGetSystemTimeLow(void);
int sceKernelDelayThread(unsigned);
int sceGeListSync(int,int);
int sceGeListDeQueue(int);
int sceGeListEnQueue(const void *,void *,int,PspGeListArgs *);
int sceGeDrawSync(int);
int sceDisplayGetVcount(void);
int sceDisplayWaitVblankStart(void);
int sceIoOpen(const char *,int,int);
int sceIoWrite(int,const void *,unsigned);
int sceIoRead(int,void *,unsigned);
int sceIoClose(int);
unsigned sceKernelDevkitVersion(void);
int sceKernelGetModel(void);
int sceCtrlPeekBufferPositive(SceCtrlData *,int);
int sceKernelCreateThread(const char *,int (*)(SceSize,void *),int,int,int,void *);
int sceKernelStartThread(int,int,void *);
int sceKernelDeleteThread(int);
int sceKernelWaitThreadEnd(int,SceUInt *);
#endif

typedef void (*PspGeCallback)(int,void *);
typedef struct {PspGeCallback signal_func;void *signal_arg;PspGeCallback finish_func;void *finish_arg;} PspGeCallbackData;
int sceGeSetCallback(PspGeCallbackData *);
int sceGeUnsetCallback(int);

#define PSP_EVENT_WAITOR 1
#define PSP_EVENT_WAITCLEAR 0x20
SceUID sceKernelCreateEventFlag(const char *,int,int,void *);
int sceKernelSetEventFlag(int,unsigned);
int sceKernelDeleteEventFlag(int);
int sceKernelWaitEventFlag(int,unsigned,unsigned,unsigned *,SceUInt *);

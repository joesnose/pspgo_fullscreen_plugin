/* Exercise the production logger with fake I/O and interrupt/thread state. */
#define HOST_TEST
#include "test_psp.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static const char *logpath="test";
#include "logger.inc"
static int irq_depth,in_interrupt,thread_id=7,open_result=9,opens,writes,closes;
static int write_limit,write_failure,enqueue_during_write;
static char output[65536];static unsigned output_size;
int sceKernelCpuSuspendIntr(void){int old=irq_depth;irq_depth++;return old;}
void sceKernelCpuResumeIntr(int old){assert(old<irq_depth);irq_depth=old;}
int sceKernelIsIntrContext(void){return in_interrupt;}
int sceKernelIsCpuIntrEnable(void){return irq_depth==0;}
SceUID sceKernelGetThreadId(void){return thread_id;}
static void CheckIo(void){assert(!irq_depth&&!in_interrupt&&thread_id==log_worker);}
int sceIoOpen(const char *p,int flags,int mode){CheckIo();assert(!strcmp(p,"test")&&flags==(PSP_O_WRONLY|PSP_O_APPEND)&&!mode);opens++;return open_result;}
int sceIoWrite(int fd,const void *p,unsigned n){
 CheckIo();assert(fd==9);writes++;
 if(enqueue_during_write){enqueue_during_write=0;logline("during\n");}
 if(write_failure)return write_failure;
 if(write_limit&&n>(unsigned)write_limit)n=write_limit;
 assert(output_size+n<sizeof(output));memcpy(output+output_size,p,n);output_size+=n;output[output_size]=0;return n;
}
int sceIoClose(int fd){CheckIo();assert(fd==9);closes++;return 0;}
static void Reset(void){
 assert(!irq_depth);log_head=log_count=log_dropped=log_write_errors=0;
 opens=writes=closes=output_size=in_interrupt=write_limit=write_failure=enqueue_during_write=0;
 thread_id=log_worker=7;open_result=9;output[0]=0;
}
int main(void){
 Reset();
 // A producer can run with interrupts disabled or on another thread. It
 // must never open/write/close a file, and must preserve interrupt state.
 irq_depth=1;in_interrupt=1;thread_id=8;
 logline("hook,%d,%08X,%s\n",-12,0x123u,"ok");
 assert(irq_depth==1&&log_count==1&&!opens&&!writes&&!closes);
 FlushLogs(8);assert(!opens);
 in_interrupt=0;irq_depth=0;FlushLogs(8);assert(!opens); // wrong thread
 thread_id=7;irq_depth=1;FlushLogs(8);assert(!opens);irq_depth=0;
 FlushLogs(8);assert(!strcmp(output,"hook,-12,00000123,ok\n")&&!log_count&&opens==1&&closes==1);
 // Failed open retains data. Partial writes complete without duplication.
 Reset();logline("abcdef\n");open_result=-1;FlushLogs(8);
 assert(log_count==1&&opens==1&&!writes&&!closes);
 open_result=9;write_limit=2;FlushLogs(8);assert(!strcmp(output,"abcdef\n")&&writes==4&&!log_count);
 // Worker budgets are bounded and producers can enqueue during I/O safely.
 Reset();logline("before\n");enqueue_during_write=1;FlushLogs(1);
 assert(!strcmp(output,"before\n")&&log_count==1);FlushLogs(1);assert(!strcmp(output,"before\nduring\n"));
 // A full queue drops new messages instead of waiting or overwriting old ones.
 Reset();for(unsigned i=0;i<LOG_QUEUE_CAPACITY+3;i++)logline("%u\n",i);
 assert(log_count==LOG_QUEUE_CAPACITY&&log_dropped==3&&!opens);
 FlushLogs(8);assert(log_count==LOG_QUEUE_CAPACITY-8);
 for(unsigned i=0;i<8;i++)logline("new\n");
 assert(log_count==LOG_QUEUE_CAPACITY);
 FlushLogs(1000);assert(!log_count);assert(strstr(output,"31\nnew\n")&&!strstr(output,"32\n"));
 // Zero/error writes terminate; the next record is not consumed on failure.
 Reset();logline("first\n");logline("second\n");write_failure=-5;FlushLogs(8);
 assert(writes==1&&closes==1&&log_write_errors==1&&log_count==1);
 write_failure=0;FlushLogs(8);assert(!strcmp(output,"second\n"));
 // Formatting truncation remains within the fixed record boundary.
 Reset();char long_text[2048];memset(long_text,'x',sizeof(long_text));long_text[2047]=0;
 logline("%s",long_text);assert(log_queue[log_head].length==LOG_LINE_BYTES-1);
 FlushLogs(1);assert(output_size==LOG_LINE_BYTES-1);
 puts("Production queued logger tests passed: producer has no I/O, worker-only drain, bounded queue/budget, FIFO/wrap/overflow, failed open, partial/error writes and truncation.");
}

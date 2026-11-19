/* Producers only format and copy into fixed kernel memory. File I/O belongs
 * exclusively to the worker; full queues drop new records rather than wait. */
#define LOG_QUEUE_CAPACITY 32u
#define LOG_LINE_BYTES 512u
typedef struct {unsigned length;char text[LOG_LINE_BYTES];} LogRecord;
static LogRecord log_queue[LOG_QUEUE_CAPACITY];
static unsigned log_head,log_count,log_dropped,log_write_errors;
static SceUID log_worker=-1;
static void logline(const char *format, ...) {
    if (!logpath) return;
    char line[LOG_LINE_BYTES];
    va_list ap;
    va_start(ap, format);
    unsigned n = 0;
    /* Small bounded formatter avoids extra kernel/CFW imports. */
    while (*format && n < sizeof(line)-1) {
        if (*format != '%') { line[n++] = *format++; continue; }
        ++format;
        unsigned width = 0;
        while (*format >= '0' && *format <= '9')
            width = width*10 + (*format++ - '0');
        if (*format == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s="(null)";
            while (*s && n < sizeof(line)-1) line[n++] = *s++;
        } else if (*format == 'd' || *format == 'u' || *format == 'X') {
            unsigned v;
            if (*format == 'd') {
                int d = va_arg(ap, int);
                v = (unsigned)d;
                if (d < 0) { line[n++] = '-'; v = 0u-v; }
            } else v = va_arg(ap, unsigned);
            unsigned base = *format == 'X' ? 16 : 10;
            char digits[32]; unsigned count = 0;
            do { digits[count++] = "0123456789ABCDEF"[v%base]; v /= base; } while (v);
            while (width > count && n < sizeof(line)-1) { line[n++]='0'; --width; }
            while (count && n < sizeof(line)-1) line[n++] = digits[--count];
        }
        if (*format) ++format;
    }
    va_end(ap);
    if (!n) return;
    int intr=sceKernelCpuSuspendIntr();
    if(log_count==LOG_QUEUE_CAPACITY)++log_dropped;
    else {
        unsigned tail=(log_head+log_count)%LOG_QUEUE_CAPACITY;
        memcpy(log_queue[tail].text,line,n);log_queue[tail].length=n;++log_count;
    }
    sceKernelCpuResumeIntr(intr);
}
/* Called only by MainThread. Never perform I/O with interrupts disabled, in
 * an interrupt handler, or from a hook that happens to run on another thread. */
static int LogInterruptsEnabled(void){
#ifdef HOST_TEST
    return sceKernelIsCpuIntrEnable();
#else
    unsigned enabled;__asm__ volatile("mfic %0, $0" : "=r"(enabled));return enabled!=0;
#endif
}
static void FlushLogs(unsigned budget){
    if(!logpath||log_worker<0||sceKernelIsIntrContext()||!LogInterruptsEnabled()||sceKernelGetThreadId()!=log_worker)return;
    if(budget>LOG_QUEUE_CAPACITY)budget=LOG_QUEUE_CAPACITY;
    int intr=sceKernelCpuSuspendIntr();unsigned available=log_count;sceKernelCpuResumeIntr(intr);
    if(!available||!budget)return;
    SceUID fd=sceIoOpen(logpath,PSP_O_WRONLY|PSP_O_APPEND,0);
    if(fd<0)return; // Keep queued records for the next worker iteration.
    while(budget--){
        LogRecord record;
        intr=sceKernelCpuSuspendIntr();
        if(!log_count){sceKernelCpuResumeIntr(intr);break;}
        record=log_queue[log_head];log_head=(log_head+1u)%LOG_QUEUE_CAPACITY;--log_count;
        sceKernelCpuResumeIntr(intr);
        unsigned offset=0;
        while(offset<record.length){
            int written=sceIoWrite(fd,record.text+offset,record.length-offset);
            if(written<=0||(unsigned)written>record.length-offset){++log_write_errors;break;}
            offset+=(unsigned)written;
        }
        if(offset<record.length)break; // Bounded failure; never spin on zero/error.
    }
    sceIoClose(fd);
}

static void logline(const char *format, ...) {
    char line[512];
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
    if (!logpath) return;
    SceUID fd = sceIoOpen(logpath, PSP_O_WRONLY | PSP_O_APPEND, 0);
    if (fd < 0) return;
    sceIoWrite(fd, line, n);
    sceIoClose(fd);
}


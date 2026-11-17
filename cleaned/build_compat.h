/* Supplied support includes still reference the old logger. This no-op keeps
 * uploaded sources unchanged and performs no log file I/O. */
static inline void logline(const char *format, ...){(void)format;}

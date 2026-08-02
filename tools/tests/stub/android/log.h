#ifndef STUB_ANDROID_LOG_H
#define STUB_ANDROID_LOG_H
#include <stdarg.h>
#include <stdio.h>
enum { ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5, ANDROID_LOG_ERROR = 6 };
static inline int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    (void)prio; (void)tag;
    va_list ap; va_start(ap, fmt);
    fputs("        [log] ", stdout); vprintf(fmt, ap); fputc('\n', stdout);
    va_end(ap); return 0;
}
#endif

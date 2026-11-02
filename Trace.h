#pragma once
// Diagnostic breadcrumbs: one line per step to ~/Library/Application Support/COMANCHE/trace.log.
// Each line is written and the file closed straight away, so the last line survives a host crash.
// Enabled with COMANCHE_TRACE 1 in config.h; compiles to nothing otherwise.

#include "config.h"

#if COMANCHE_TRACE
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <pthread.h>
#include <sys/time.h>

inline void ctrace(const char* fmt, ...)
{
    const char* home = getenv("HOME");
    if (!home) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/Library/Application Support/COMANCHE/trace.log", home);
    FILE* f = fopen(path, "a");
    if (!f) return;
    timeval tv; gettimeofday(&tv, nullptr);
    tm lt; localtime_r(&tv.tv_sec, &lt);
    fprintf(f, "%02d:%02d:%02d.%03d [%s] ", lt.tm_hour, lt.tm_min, lt.tm_sec, (int)(tv.tv_usec / 1000),
            pthread_main_np() ? "main" : "aux ");
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}
#else
inline void ctrace(const char*, ...) {}
#endif

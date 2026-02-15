#include "util/log.h"
#include <SDL3/SDL.h>
#include <stdarg.h>

static void vlog(SDL_LogPriority prio, const char* tag, const char* fmt, va_list ap) {
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, prio, fmt, ap);
    (void)tag;
}

void log_fatal(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vlog(SDL_LOG_PRIORITY_ERROR, "FATAL", fmt, ap);
    va_end(ap);
}

void log_info(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vlog(SDL_LOG_PRIORITY_INFO, "INFO", fmt, ap);
    va_end(ap);
}

void log_warn(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vlog(SDL_LOG_PRIORITY_WARN, "WARN", fmt, ap);
    va_end(ap);
}

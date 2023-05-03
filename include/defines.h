#ifndef DEFINES_H
#define DEFINES_H

#include <windows.h>
#include <stdio.h>
#include <pthread.h>

#define _LOG_ONLY_INTERNAL(lvl, fmt, ...) \
    fprintf(glbl.logfile, "[%s] %s %s:%s:%d: " fmt "\n", (lvl), GetDateTimeStr(), __BASE_FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#ifdef DEBUG_BUILD
#define _LOG_INTERNAL(lvl, fmt, ...)                    \
    do {                                                \
        pthread_mutex_lock(&glbl.loglock);              \
        _LOG_ONLY_INTERNAL((lvl), fmt, ##__VA_ARGS__);  \
        fflush(glbl.logfile);                           \
        pthread_mutex_unlock(&glbl.loglock);            \
    } while (0)
#else
#define _LOG_INTERNAL(lvl, fmt, ...)                    \
    do {                                                \
        pthread_mutex_lock(&glbl.loglock);              \
        _LOG_ONLY_INTERNAL((lvl), fmt, ##__VA_ARGS__);  \
        pthread_mutex_unlock(&glbl.loglock);            \
    } while (0)
#endif

#define LOG(fmt, ...) _LOG_INTERNAL("INF", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) _LOG_INTERNAL("WRN", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) _LOG_INTERNAL("ERR", fmt, ##__VA_ARGS__)

#define MSG_LEN (1 << 10)

// Each module should have its own static function called Panic which takes a LPTSTR. This lets you pass format strings to that function.
#define PANIC(fmt, ...)                                                     \
    do {                                                                    \
        TCHAR bufPanic[MSG_LEN];                                            \
        _stprintf_s(bufPanic, _countof(bufPanic), (fmt), ##__VA_ARGS__);    \
        Panic(bufPanic);                                                    \
    } while (0)

// Each module should have its own static function called Warn which takes a LPTSTR. This lets you pass format strings to that function.
// lock is an optional lock to release before issuing the warning. Pass NULL to ignore.
#define WARN(lock, fmt, ...)                                                \
    do {                                                                    \
        TCHAR bufWarn[MSG_LEN];                                             \
        _stprintf_s(bufWarn, _countof(bufWarn), (fmt), ##__VA_ARGS__);      \
        if ((lock) != NULL) pthread_mutex_unlock((lock));                   \
        Warn(bufWarn);                                                      \
    } while (0)

#ifdef _UNICODE
#define TCS_FMT TEXT("%ls")
#define TC_FMT TEXT("%lc")
#else
#define TCS_FMT TEXT("%s")
#define TC_FMT TEXT("%c")
#endif

typedef struct
{
    char isDisabled;
    char isRefresh;
    char fixerDied;
    char issueWarning;
    TCHAR errorMsg[MSG_LEN];
    TCHAR warningMsg[MSG_LEN];
    pthread_mutex_t lock; // Lock for all the above.

    FILE *logfile;
    pthread_mutex_t loglock; // Lock for logfile.
} GlobalCb;

extern GlobalCb glbl;

void *FixerLoop(void *arg);
char *GetDateTimeStr();

#endif
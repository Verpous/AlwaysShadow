#ifndef DEFINES_H
#define DEFINES_H

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <pthread.h>

// Macros defined via CFLAGS need a default value to please the IDE.
#ifndef VERSION_BRANCH_AND_FILE
#define VERSION_BRANCH_AND_FILE ""
#endif

#ifndef GITHUB_NAME_WITH_OWNER
#define GITHUB_NAME_WITH_OWNER ""
#endif

#ifdef DEBUG_BUILD
#define FFLUSH_DEBUG(file) fflush(file)
#else
#define FFLUSH_DEBUG(file)
#endif

#define _LOG_ONLY_INTERNAL(lvl, fmt, ...) \
    fprintf(glbl.logfile, "[%s] %s %s:%s:%d: " fmt "\n", (lvl), GetDateTimeStaticStr(), __BASE_FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define _LOG_INTERNAL(lvl, fmt, ...)                    \
    do {                                                \
        pthread_mutex_lock(&glbl.loglock);              \
        _LOG_ONLY_INTERNAL((lvl), fmt, ##__VA_ARGS__);  \
        FFLUSH_DEBUG(glbl.logfile);                     \
        pthread_mutex_unlock(&glbl.loglock);            \
    } while (0)

#define LOG(fmt, ...) _LOG_INTERNAL("INF", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) _LOG_INTERNAL("WRN", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) _LOG_INTERNAL("ERR", fmt, ##__VA_ARGS__)

#define MSG_LEN (1 << 12)

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
#define TCS_FMT "%ls"
#define TC_FMT "%lc"
#else
#define TCS_FMT "%s"
#define TC_FMT "%c"
#endif

#define T_TCS_FMT TEXT(TCS_FMT)
#define T_TC_FMT TEXT(TC_FMT)

#define HANDLE_CURL_ERROR(label, curlCode, desc)                                            \
    do {                                                                                    \
        CURLcode _res = (curlCode);                                                         \
        if (_res != CURLE_OK)                                                               \
        {                                                                                   \
            LOG_WARN("Failed to %s with error: %s", (desc), curl_easy_strerror(_res));      \
            goto label;                                                                     \
        }                                                                                   \
    } while (0)

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
extern const char *tags[];
extern const size_t tagsLen;

void *FixerLoop(void *arg);
char *GetDateTimeStaticStr();
char *GetLastErrorStaticStr();

#endif
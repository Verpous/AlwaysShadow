// AlwaysShadow - a program for forcing Shadowplay's Instant Replay to stay on.
// Copyright (C) 2024 Aviv Edery.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "defines.h"
#include "cJSON.h"      // For parsing the file with the port and secret for Shadowplay's local server.
#include <tchar.h>      // For dealing with unicode and ANSI strings.
#include <pthread.h>    // For multithreading.
#include <unistd.h>     // For sleep.
#include <wbemidl.h>    // For getting the command line of running processes.
#include <oleauto.h>    // For working with BSTRs.
#include <regex.h>      // For parsing the whitelist.
#include <curl/curl.h>  // For sending requests to Shadowplay's local server which toggles recording on and off.

#define _WIN32_DCOM // This came with the whitelisting function which I dare not touch.

#define MIN_STREAK_FOR_CONFLICT 3
_Static_assert(MIN_STREAK_FOR_CONFLICT >= 2, "At least 2 attempts (1 retry) are needed to identify a conflict.");

// Support high frequency polling option for debugging.
#ifdef HIGH_FREQUENCY_POLLING
#define POLLING_FREQUENCY_SEC 5
#define POLLING_FREQUENCY_IN_CONFLICT_SEC 30
#else
#define POLLING_FREQUENCY_SEC 10
#define POLLING_FREQUENCY_IN_CONFLICT_SEC 800
#endif

typedef enum
{
    PROCFIELD_NAME,
    PROCFIELD_CMDLINE,
    PROCFIELD_NUMOF,
} ProcessField;

typedef struct
{
    BSTR checkValue;
    ProcessField checkField;
    char isSubstring;
    char isExclusive;
} WhitelistEntry;

typedef struct
{
    size_t ninputs;
    INPUT *inputs;

    size_t nwhitelist;
    WhitelistEntry *whitelist;
    char isExclusiveExists;

    struct curl_slist *headers;
    CURL *curl;

    char comInitialized;
    IWbemLocator *wbemLocator;
    IWbemServices *wbemServices;
} FixerCb;

static void Panic(LPTSTR msg);
static void Warn(LPTSTR msg);
static void ReleaseResources(char freeWmi);
static void LoadResources(char loadWmi);
static char IsInstantReplayOn();

static INPUT *FetchToggleShortcut(size_t *ninputs);
static void CreateInput(INPUT *input, WORD vkey, char isDown);
static void ToggleInstantReplay(char currentState);
static void ToggleInstantReplayByKeyboardShortcut();

static void ReleaseCurlResources();
static char FetchServerInfo(CURL **handleOut, struct curl_slist **headersOut);
static char SetInstantReplayByPostRequest(char state);

static void InitializeWmi();
static WhitelistEntry *FetchWhitelist(LPTSTR filename, size_t *nwhitelist);
static char IsExclusiveExists(WhitelistEntry *whitelist, size_t nwhitelist);
static void PollRunningProcesses(WhitelistEntry *whitelist, size_t nwhitelist, char *isWhitelistedRunning, char *isExclusiveRunning);
static wchar_t *StripLeadingTrailingWhitespaceWide(wchar_t *str);
static char IsWhitelistMatch(BSTR *fields, WhitelistEntry *entry);

// Not just any str rep will do, it needs to be the name that Win32_Process knows the field by.
static const wchar_t* procfield_str[] = {
    [PROCFIELD_NAME]        L"Name",
    [PROCFIELD_CMDLINE]     L"CommandLine",
};

static FixerCb cb = {0};

// TODO: See about detecting that in-game overlay is off and notifying the user to turn it on.
// TODO: See about detecting apps that are running but suspended (maybe instead detect apps that currently have no window or no foregrounded window?).
// TODO: Apparently the Shadowplay server has TONS of functions, we could use it to get the settings, maybe finally check if in-game overlay is off?
//       They're all in C:\Program Files (x86)\NVIDIA Corporation\NvNode\NvShadowPlayAPI.js

void *FixerLoop(void *arg)
{
    int toggleStreak = 0;
    int conflictStart = 0;

    // Making thread cancellable.
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    // Loading whitelist, shortcut, wmi, everything.
    LoadResources(TRUE);

    for (int cycle = 0;; cycle++)
    {
        sleep(POLLING_FREQUENCY_SEC);

        pthread_mutex_lock(&glbl.lock);
        char isRefresh = glbl.isRefresh;
        char isDisabled = glbl.isDisabled;
        glbl.isRefresh = FALSE;
        pthread_mutex_unlock(&glbl.lock);

        if (isRefresh)
        {
            LOG("Received refresh signal. Refreshing.");
            ReleaseResources(FALSE);
            LoadResources(FALSE);
        }

        if (isDisabled) goto end_streak_and_continue;

        // If we find ourselves in conflict with some program that also tries to control Shadowplay,
        // we'll "yield" by reducing the polling frequency so we don't fight it as much.
        if (toggleStreak >= MIN_STREAK_FOR_CONFLICT)
        {
            // Skip many cycles, written this way because we can't just sleep for longer between cycles; I don't want to stall Refresh so much.
            if ((cycle - conflictStart) * POLLING_FREQUENCY_SEC < POLLING_FREQUENCY_IN_CONFLICT_SEC) continue;

            // On cycles where we want to make an attempt despite being in a streak, we'll need 2 attempts to know if we are still in conflict.
            toggleStreak = MIN_STREAK_FOR_CONFLICT - 2;
            LOG("Attempting to break out of conflict. cycle=%d", cycle);
        }

        char isInstantReplayOn = IsInstantReplayOn();

        // When these conditions are met there is no reason to waste cpu time polling running processes.
        if (!cb.isExclusiveExists && isInstantReplayOn) goto end_streak_and_continue;

        char isWhitelistedRunning, isExclusiveRunning;
        PollRunningProcesses(cb.whitelist, cb.nwhitelist, &isWhitelistedRunning, &isExclusiveRunning);

        // Whitelist disables AlwaysShadow, taking precedence over Exclusives list.
        if (isWhitelistedRunning) goto end_streak_and_continue;

        if ((!isInstantReplayOn && (!cb.isExclusiveExists || isExclusiveRunning)) || // Conditions for toggling ON.
            (isInstantReplayOn && cb.isExclusiveExists && !isExclusiveRunning)) // Conditions for toggling OFF.
        {
            LOG("Should toggle because: isInstantReplayOn %d, isExclusiveExists %d, isExclusiveRunning %d", isInstantReplayOn, cb.isExclusiveExists, isExclusiveRunning);

            if (++toggleStreak == MIN_STREAK_FOR_CONFLICT)
            {
                LOG("Entered into conflict! Won't toggle. cycle=%d", cycle);
                conflictStart = cycle;
            }
            else
            {
                ToggleInstantReplay(isInstantReplayOn);
            }
            
            continue; // Skip ending the streak.
        }

end_streak_and_continue:
        toggleStreak = 0;
    }
    
    return 0;
}

static void Panic(LPTSTR msg)
{
    ReleaseResources(TRUE);

    pthread_mutex_lock(&glbl.lock);
    _tcscpy_s(glbl.errorMsg, _countof(glbl.errorMsg), msg);
    glbl.fixerDied = TRUE;
    pthread_mutex_unlock(&glbl.lock);
    pthread_exit(NULL);
}

static void Warn(LPTSTR msg)
{
    for (;;)
    {
        pthread_mutex_lock(&glbl.lock);

        // If previous warning hasn't been displayed yet, release lock and try again later.
        if (glbl.issueWarning)
        {
            pthread_mutex_unlock(&glbl.lock);
            sleep(1);
            continue;
        }

        _tcscpy_s(glbl.warningMsg, _countof(glbl.warningMsg), msg);
        glbl.issueWarning = TRUE;
        pthread_mutex_unlock(&glbl.lock);
        break;
    }
}

static void ReleaseCurlResources()
{
    // These are safe to call with NULL.
    curl_slist_free_all(cb.headers);
    curl_easy_cleanup(cb.curl);
    
    cb.headers = NULL;
    cb.curl = NULL;
}

static void ReleaseResources(char freeWmi)
{
    // This program does a sloppy job of cleanup.
    // When this thread has an error, we clean up its resources but not the main thread's.
    // When the main thread has an error, we don't clean up shit.
    // When the program exits normally, we clean up the main thread's shit, but not this thread's.
    // But you know what? Fuck it.
    if (freeWmi)
    {
        if (cb.wbemServices != NULL) cb.wbemServices->lpVtbl->Release(cb.wbemServices);
        if (cb.wbemLocator != NULL) cb.wbemLocator->lpVtbl->Release(cb.wbemLocator);
        if (cb.comInitialized) CoUninitialize();

        cb.wbemServices = NULL;
        cb.wbemLocator = NULL;
        cb.comInitialized = FALSE;
    }

    ReleaseCurlResources();

    for (size_t i = 0; i < cb.nwhitelist; i++) SysFreeString(cb.whitelist[i].checkValue);
    free(cb.whitelist);
    free(cb.inputs);

    cb.whitelist = NULL;
    cb.inputs = NULL;
    cb.nwhitelist = 0;
    cb.ninputs = 0;
}

static void LoadResources(char loadWmi)
{
    if (loadWmi) InitializeWmi();
    cb.inputs = FetchToggleShortcut(&cb.ninputs);
    cb.whitelist = FetchWhitelist(TEXT("Whitelist.txt"), &cb.nwhitelist);
    cb.isExclusiveExists = IsExclusiveExists(cb.whitelist, cb.nwhitelist);
    FetchServerInfo(&cb.curl, &cb.headers);
}

#pragma region Checking-Active

static char IsInstantReplayOn()
{
    // There's a registry key which will tell us if it's on.
    DWORD isActive;
    DWORD bufsz = sizeof(isActive);
    LSTATUS ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"),
        TEXT("{1B1D3DAA-601D-49E5-8508-81736CA28C6D}"), RRF_RT_ANY, NULL, (PVOID)&isActive, &bufsz);

    if (ret != ERROR_SUCCESS)
    {
        LOG_WARN("Failed to read registry key check if Instant Replay is on with error code %#lx", ret);
        return TRUE;
    }

    // Technically isActive is already 0/1 but since it's a DWORD and we want to return a char, !! will do it safely.
    return !!isActive;
}

#pragma endregion // Checking-Active

#pragma region Toggling-Active

static const char *cJSON_GetErrorPtrSafe()
{
    const char *error = cJSON_GetErrorPtr();
    return error == NULL ? "N/A" : error;
}

// Big thanks to PolicyPuma 4 for this function: https://github.com/Verpous/AlwaysShadow/issues/1#issuecomment-1474938711.
static char FetchServerInfo(CURL **handleOut, struct curl_slist **headersOut)
{
    HANDLE mapHandle = OpenFileMapping(FILE_MAP_READ, FALSE, TEXT("{8BA1E16C-FC54-4595-9782-E370A5FBE8DA}"));
    LPVOID mapView = NULL;
    cJSON *infoJson = NULL;
    CURL *handle = NULL;
    struct curl_slist *headers = NULL;
    char success = TRUE;

    if (mapHandle == NULL)
    {
        LOG_WARN("Failed to open the file with the port and secret, error: %s", GetLastErrorStaticStr());
        goto error;
    }

    mapView = MapViewOfFile(mapHandle, FILE_MAP_READ, 0, 0, 0);

    if (mapView == NULL)
    {
        LOG_WARN("Failed to map view of the file with the port and secret, error: %s", GetLastErrorStaticStr());
        goto error;
    }

    infoJson = cJSON_Parse((char *)mapView);

    if (infoJson == NULL)
    {
        LOG_WARN("Failed to parse JSON with error: %s", cJSON_GetErrorPtrSafe());
        goto error;
    }
    
    cJSON *portJson = cJSON_GetObjectItem(infoJson, "port");
    cJSON *secretJson = cJSON_GetObjectItem(infoJson, "secret");

    if (portJson == NULL || secretJson == NULL)
    {
        LOG_WARN("Failed to get port and/or secret from the JSON. port = %p, secret = %p", portJson, secretJson);
        goto error;
    }
    
    if (!cJSON_IsNumber(portJson) || !cJSON_IsString(secretJson))
    {
        LOG_WARN("One of port or secret has the wrong type. port type = %#x (should be %#x), secret type = %#x (should be %#x)",
            portJson->type, cJSON_Number, secretJson->type, cJSON_String);
        goto error;
    }

    int port = portJson->valueint;
    char *secret = secretJson->valuestring;
    LOG("Shadowplay server port: %d, secret: '%s'", port, secret);

    handle = curl_easy_init();

    if (handle == NULL)
    {
        LOG_WARN("Failed to init curl handle");
        goto error;
    }

    HANDLE_CURL_ERROR(error, curl_easy_setopt(handle, CURLOPT_TIMEOUT, 5), "set CURLOPT_TIMEOUT");

    char buf[256];
    sprintf(buf, "http://localhost:%d/ShadowPlay/v.1.0/InstantReplay/Enable", port);
    HANDLE_CURL_ERROR(error, curl_easy_setopt(handle, CURLOPT_URL, buf), "set CURLOPT_URL");

    sprintf(buf, "X_LOCAL_SECURITY_COOKIE: %s", secret);
    headers = curl_slist_append(NULL, buf);
    HANDLE_CURL_ERROR(error, curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers), "set CURLOPT_HTTPHEADER");

    *headersOut = headers;
    *handleOut = handle;
    goto cleanup;

error:
    success = FALSE;
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
cleanup:
    if (mapHandle != NULL) CloseHandle(mapHandle);
    if (mapView != NULL) UnmapViewOfFile(mapView);
    cJSON_Delete(infoJson);
    return success;
}

static char SetInstantReplayByPostRequest(char state)
{
    if (cb.curl == NULL && !FetchServerInfo(&cb.curl, &cb.headers))
    {
        LOG_WARN("Failed to set state: %d because the handle info couldn't be fetched", state);
        return FALSE;
    }

    char buf[256];
    sprintf(buf, "{\"status\": %s}", state ? "true" : "false");
    curl_easy_setopt(cb.curl, CURLOPT_POSTFIELDS, buf);
    CURLcode res = curl_easy_perform(cb.curl);

    if (res != CURLE_OK)
    {
        LOG_WARN("Failed to set state: %d due to POST error: %s", state, curl_easy_strerror(res));
        ReleaseCurlResources();
        return FALSE;
    }

    return TRUE;
}

static INPUT *FetchToggleShortcut(size_t *ninputs)
{
    INPUT *shortcut;

    DWORD hkeyCount;
    DWORD bufsz = sizeof(hkeyCount);
    LSTATUS ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"),
        TEXT("IRToggleHKeyCount"), RRF_RT_ANY, NULL, (PVOID)&hkeyCount, &bufsz);

    // Defaulting to Alt+Shift+F10.
    if (ret != ERROR_SUCCESS)
    {
        LOG_WARN("Resorting to default toggle shortcut.");

        *ninputs = 6;
        size_t halfinputs = (*ninputs) / 2;
        shortcut = calloc(*ninputs, sizeof(INPUT));
        
        CreateInput(shortcut + 0, VK_MENU, TRUE);
        CreateInput(shortcut + 1, VK_SHIFT, TRUE);
        CreateInput(shortcut + 2, VK_F10, TRUE);

        CreateInput(shortcut + halfinputs + 0, VK_MENU, FALSE);
        CreateInput(shortcut + halfinputs + 1, VK_SHIFT, FALSE);
        CreateInput(shortcut + halfinputs + 2, VK_F10, FALSE);
    }
    else // Reading from registry.
    {
        LOG("Shortcut length: %ld", hkeyCount);

        *ninputs = hkeyCount * 2;
        size_t halfinputs = (*ninputs) / 2;
        shortcut = calloc(*ninputs, sizeof(INPUT));

        // Reading each character of the shortcut from its own registry entry.
        for (int i = 0; i < halfinputs; i++)
        {
            // Creating string of registry name (Should be IRToggleHKey0, IRToggleHKey1, etc.).
            TCHAR valueName[1 << 8];
            if (_sntprintf_s(valueName, _countof(valueName), _TRUNCATE, TEXT("IRToggleHKey%d"), i) == -1)
            {
                LOG_ERROR("Error fitting registry key %d in this buffer!", i);
                PANIC(TEXT("Failed to prepare for reading the toggle shortcut. Quitting."));
            }

            // Reading the registry entry.
            DWORD vkey;
            bufsz = sizeof(vkey);
            ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"), valueName, RRF_RT_ANY, NULL, (PVOID)&vkey, &bufsz);

            if (ret != ERROR_SUCCESS)
            {
                LOG_ERROR("Failed to read hotkey %d with error code %#lx", i, ret);
                PANIC(TEXT("Failed to read toggle shortcut key %d with error code %#lx. Quitting."), i, ret);
            }

            LOG("Adding vkey %#lx to shortcut.", vkey);
            CreateInput(shortcut + i, *((WORD *)(&vkey)), TRUE);
            CreateInput(shortcut + halfinputs + i, *((WORD *)(&vkey)), FALSE);
        }
    }

    return shortcut;
}

static void CreateInput(INPUT *input, WORD vkey, char isDown)
{
    input->type = INPUT_KEYBOARD;
    input->ki.wVk = vkey;
    input->ki.dwFlags = isDown ? 0 : KEYEVENTF_KEYUP;
}

static void ToggleInstantReplayByKeyboardShortcut()
{
    LOG("Toggling by keyboard shortcut.");
    SendInput((UINT)cb.ninputs, cb.inputs, sizeof(INPUT));
}

static void ToggleInstantReplay(char currentState)
{
    // The CURL method is preferable because the keyboard shortcut might have inadvertent side effects,
    // like cycling the user's keyboard language (the default shortcut Alt+Shift+F10 has Alt+Shift in it).
    // But since the keyboard method was already implemented, might as well keep it as a fallback.
    if (!SetInstantReplayByPostRequest(!currentState))
    {
        ToggleInstantReplayByKeyboardShortcut();
    }
}

# pragma endregion // Toggling-Active

# pragma region Whitelisting

static void InitializeWmi()
{
    const TCHAR *error = TEXT("Failed to initialize WMI: ") T_TCS_FMT TEXT(" returned %d. Quitting.");
    HRESULT res;

    // Initializate the Windows security.
    if (FAILED(res = CoInitializeEx(0, COINIT_MULTITHREADED)))
    {
        LOG_ERROR("CoInitializeEx failed with res %ld.", res);
        PANIC(error, TEXT("CoInitializeEx"), res);
    }

    cb.comInitialized = TRUE;

    if (FAILED(res = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL)))
    {
        LOG_ERROR("CoInitializeSecurity failed with res %ld.", res);
        PANIC(error, TEXT("CoInitializeSecurity"), res);
    }

    if (FAILED(res = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (LPVOID *)&cb.wbemLocator)))
    {
        LOG_ERROR("CoCreateInstance failed with res %ld.", res);
        PANIC(error, TEXT("CoCreateInstance"), res);
    }

    if (FAILED(res = cb.wbemLocator->lpVtbl->ConnectServer(cb.wbemLocator, L"ROOT\\CIMV2", NULL, NULL, NULL, 0, NULL, NULL, &cb.wbemServices)))
    {
        LOG_ERROR("ConnectServer failed with res %ld.", res);
        PANIC(error, TEXT("ConnectServer"), res);
    }
}

// fmt must end with a %s where the error string goes.
#define LOG_REGERROR(errcode, compiled, fmt, ...)                                   \
    do {                                                                            \
        char bufLogRegerror[MSG_LEN];                                               \
        regerror((errcode), (compiled), bufLogRegerror, sizeof(bufLogRegerror));    \
        LOG_WARN(fmt, ##__VA_ARGS__, bufLogRegerror);                               \
    } while (FALSE)

static WhitelistEntry *FetchWhitelist(LPTSTR filename, size_t *nwhitelist)
{
    FILE *file = NULL;
    WhitelistEntry *whitelist = NULL;
    // Have to initialize these to *something* that isn't REG_OK.
    int emptylineCompRes = REG_BADPAT;
    int modlineCompRes = REG_BADPAT;
    int normlineCompRes = REG_BADPAT;
    int res;
    *nwhitelist = 0;

    if ((res = _tfopen_s(&file, filename, TEXT("r"))) != 0)
    {
        LOG_WARN("Couldn't open whitelist with error: %s.", strerror(res));
        if (res != ENOENT) WARN(NULL, TEXT("Failed to open whitelist: ") T_TCS_FMT TEXT(". Fix the problem then refresh."), _tcserror(res));
        file = NULL; // Just to be sure.
        goto error;
    }

    regex_t emptylineRegex;
    regex_t modlineRegex;
    regex_t normlineRegex;
    emptylineCompRes = regcomp(&emptylineRegex, "^\\s*$", REG_EXTENDED | REG_NOSUB);
    modlineCompRes = regcomp(&modlineRegex, "^\\s*\\?\\?(\\S*)\\s*((\\s*\\S*)*)\\s*$", REG_EXTENDED);
    normlineCompRes = regcomp(&normlineRegex, "^\\s*(\\S+(\\s*\\S+)*)\\s*$", REG_EXTENDED);

    if (emptylineCompRes != REG_OK || modlineCompRes != REG_OK || normlineCompRes != REG_OK)
    {
        LOG_REGERROR(emptylineCompRes, &emptylineRegex, "emptylineRegex compilation result: %s.");
        LOG_REGERROR(modlineCompRes, &modlineRegex, "modlineRegex compilation result: %s.");
        LOG_REGERROR(normlineCompRes, &normlineRegex, "normlineRegex compilation result: %s.");
        WARN(NULL, TEXT("Failed to load whitelist due to an internal problem. You can retry by hitting refresh."));
        goto error;
    }

    char buffer[1 << 13];
    wchar_t wbuffer[_countof(buffer)];
    int linenum = 0;

    // Iterate over the file line by line.
    while (fgets(buffer, sizeof(buffer), file) != NULL)
    {
        linenum++;

        // fgets writes a newline to the buffer and this is the easiest way I was able to shake it off.
        for (int i = strlen(buffer) - 1; i >= 0 && (buffer[i] == '\n' || buffer[i] == '\r'); i--) buffer[i] = '\0';
        LOG("Checking whitelist line: %d line length: %lld contents: '%s'.", linenum, strlen(buffer), buffer);

        // Check for empty lines, skip them.
        res = regexec(&emptylineRegex, buffer, 0, NULL, 0);

        if (res == REG_OK)
        {
            LOG("Skipping line %d because it is empty.", linenum);
            continue;
        }

        if (res != REG_NOMATCH)
        {
            LOG_REGERROR(res, &emptylineRegex, "Line %d emptylineRegex exec result: %s.", linenum);
            WARN(NULL, TEXT("Failed to load the whitelist due to an internal problem at line: %d. You can retry by hitting refresh."), linenum);
            goto error;
        }

        // We'll write the entry to this variable then create a dynamically allocated copy.
        // The reason is so if we get an error in the middle we don't have to worry about freeing that allocation.
        WhitelistEntry entry = {0};
        entry.checkField = PROCFIELD_CMDLINE;

        // Plenty of array size just to be safe.
        regmatch_t matches[16];
        regmatch_t *commandMatch = NULL;
        res = regexec(&modlineRegex, buffer, _countof(matches), matches, 0);

        if (res == REG_OK)
        {
            regmatch_t *flagsMatch = &matches[1];
            commandMatch = &matches[2];

            if (flagsMatch->rm_so == flagsMatch->rm_eo)
            {
                LOG_WARN("Line %d in the whitelist starts with ?? but has no flags.", linenum);
                WARN(NULL, TEXT("Invalid whitelist line: %d - line starts with ?? but has no flags. Fix the problem then refresh."), linenum);
                goto error;
            }

            char isComment = FALSE;

            for (char *c = &buffer[flagsMatch->rm_so]; c != &buffer[flagsMatch->rm_eo]; c++)
            {
                switch (*c)
                {
                    case 'S':
                        entry.isSubstring = TRUE;
                        break;
                    case 'E':
                        entry.isExclusive = TRUE;
                        break;
                    case 'N':
                        entry.checkField = PROCFIELD_NAME;
                        break;
                    case 'I':
                        // Keep iterating over flag characters even if this is a comment.
                        isComment = TRUE;
                        break;
                    default:
                        LOG_WARN("Invalid flag character: %c in line: %d.", *c, linenum);
                        WARN(NULL, TEXT("Invalid whitelist line: %d - flag character: '%c' is unrecognized. Fix the problem then refresh."), linenum, *c);
                        goto error;
                }
            }

            // If this is a comment line, skip it.
            if (isComment)
            {
                LOG("Skipping line %d because it is a comment.", linenum);
                continue;
            }

            if (commandMatch->rm_so == commandMatch->rm_eo)
            {
                LOG_WARN("Line %d in the whitelist has flags but no command.", linenum);
                WARN(NULL, TEXT("Invalid whitelist line: %d - command is missing. Fix the problem then refresh."), linenum);
                goto error;
            }
        }
        else if (res != REG_NOMATCH)
        {
            LOG_REGERROR(res, &modlineRegex, "Line %d modlineRegex exec result: %s.", linenum);
            WARN(NULL, TEXT("Failed to load the whitelist due to an internal problem at line: %d. You can retry by hitting refresh."), linenum);
            goto error;
        }
        else // res == REG_NOMATCH. We'll try normlineRegex.
        {
            res = regexec(&normlineRegex, buffer, _countof(matches), matches, 0);
            commandMatch = &matches[1];

            if (res != REG_OK)
            {
                LOG_REGERROR(res, &modlineRegex, "Line %d normlineRegex exec result: %s.", linenum);
                WARN(NULL, TEXT("Failed to load the whitelist due to an internal problem at line: %d. You can retry by hitting refresh."), linenum);
                goto error;
            }
        }

        // Converting command to wchar.
        if ((res = mbstowcs_s(NULL, wbuffer, _countof(wbuffer), &buffer[commandMatch->rm_so], commandMatch->rm_eo - commandMatch->rm_so)) != 0)
        {
            LOG_WARN("Received error '%s' when trying to convert line %d command %.*s.",
                strerror(res), linenum, commandMatch->rm_eo - commandMatch->rm_so, &buffer[commandMatch->rm_so]);
            WARN(NULL, TEXT("Failed to load the whitelist due to a problem at line: %d. It may contain unsupported characters. Fix the problem then refresh."), linenum);
            goto error;
        }

        entry.checkValue = SysAllocString(wbuffer);

        // Allocate new whitelist entry.
        whitelist = realloc(whitelist, ((*nwhitelist) + 1) * sizeof(*whitelist));
        whitelist[*nwhitelist] = entry;
        (*nwhitelist)++;

        LOG("Added to the whitelist: line: %d, isSubstring: %d, isExclusive: %d, field: %ls, value length: %lld value: '%ls'.",
            linenum, entry.isSubstring, entry.isExclusive, procfield_str[entry.checkField], wcslen(wbuffer), wbuffer);
    }

    // Skip bad.
    goto exit;

error:
    // Safe to pass NULL to SysFreeString (also to free).
    for (size_t i = 0; whitelist != NULL && i < *nwhitelist; i++) SysFreeString(whitelist[i].checkValue);
    free(whitelist);
    *nwhitelist = 0;
    whitelist = NULL;
exit:
    if (emptylineCompRes == REG_OK) regfree(&emptylineRegex);
    if (modlineCompRes == REG_OK) regfree(&modlineRegex);
    if (normlineCompRes == REG_OK) regfree(&normlineRegex);
    if (file != NULL) fclose(file);
    return whitelist;
}

static char IsExclusiveExists(WhitelistEntry *whitelist, size_t nwhitelist)
{
    for (int i = 0; i < cb.nwhitelist; i++)
    {
        if (whitelist[i].isExclusive) return TRUE;
    }

    return FALSE;
}

// Thank god for StackOverflow for delivering this holy function : https://stackoverflow.com/a/9589788/12553917.
static void PollRunningProcesses(WhitelistEntry *whitelist, size_t nwhitelist, char *isWhitelistedRunning, char *isExclusiveRunning)
{
    *isWhitelistedRunning = FALSE;
    *isExclusiveRunning = FALSE;
    
    if (nwhitelist == 0)
    {
        return;
    }

    // Run the WQL Query.
    // TODO: instead of iterating over all processes, be informed when a process is created or destroyed. Some interesting reading material on that:
    // https://stackoverflow.com/questions/3556048/how-to-detect-win32-process-creation-termination-in-c
    // https://www.codeproject.com/Articles/11985/Hooking-the-native-API-and-controlling-process-cre
    // https://docs.microsoft.com/en-us/windows/win32/wmisdk/example--receiving-event-notifications-through-wmi-?redirectedfrom=MSDN
    // If this is too difficult to do from C, I can write it in C++ and call it from C. It shouldn't be too hard to set up.
    IEnumWbemClassObject *enumWbem = NULL;

    // CBA to compose this string using procfield_str.
    if (FAILED(cb.wbemServices->lpVtbl->ExecQuery(cb.wbemServices, L"WQL", L"SELECT Name,CommandLine FROM Win32_Process", WBEM_FLAG_FORWARD_ONLY, NULL, &enumWbem)))
    {
        return;
    }

    // Iterate over the enumerator.
    IWbemClassObject *result = NULL;
    ULONG returnedCount = 0;

    while (enumWbem->lpVtbl->Next(enumWbem, WBEM_INFINITE, 1, &result, &returnedCount) == S_OK)
    {
        VARIANT field_variants[PROCFIELD_NUMOF];
        BSTR field_bstrs[PROCFIELD_NUMOF] = {0};
        BSTR field_trimmed_bstrs[PROCFIELD_NUMOF] = {0};

        for (int i = 0; i < PROCFIELD_NUMOF; i++)
        {
            if (FAILED(result->lpVtbl->Get(result, procfield_str[i], 0, &field_variants[i], 0, 0)))
            {
                // Mark failed fields empty so we know not to free them.
                field_variants[i].vt = VT_EMPTY;
                continue;
            }

            if (field_variants[i].vt == VT_NULL) {
                // the bstr arrays default to NULL so leave it that way in this case.
                continue;
            }

            field_bstrs[i] = SysAllocString(field_variants[i].bstrVal);
            field_trimmed_bstrs[i] = StripLeadingTrailingWhitespaceWide(field_bstrs[i]);
        }

        for (size_t i = 0; i < nwhitelist; i++)
        {
            WhitelistEntry *entry = &whitelist[i];

            if (IsWhitelistMatch(field_trimmed_bstrs, entry))
            {
                if (entry->isExclusive)
                {
                    *isExclusiveRunning = TRUE;
                }
                else
                {
                    *isWhitelistedRunning = TRUE;
                }
            }
        }

        for (int i = 0; i < PROCFIELD_NUMOF; i++)
        {
            if (field_bstrs[i] != NULL) SysFreeString(field_bstrs[i]);
            if (field_variants[i].vt != VT_EMPTY) VariantClear(&field_variants[i]);
        }

        result->lpVtbl->Release(result);
    }

    enumWbem->lpVtbl->Release(enumWbem);
}

static wchar_t *StripLeadingTrailingWhitespaceWide(wchar_t *str)
{
    while (iswspace(*str)) str++;
    size_t len = wcslen(str);

    if (len > 0)
    {
        wchar_t *endstr = str + (len - 1);
        while (iswspace(*endstr)) *(endstr--) = L'\0';
    }

    return str;
}

static char IsWhitelistMatch(BSTR *fields, WhitelistEntry *entry)
{
    BSTR field = fields[entry->checkField];
    char isMatch;

    if (field == NULL)
    {
        return FALSE;
    }

    if (entry->isSubstring)
    {
        isMatch = wcsstr(field, entry->checkValue) != NULL;
    }
    else
    {
        isMatch = wcscmp(field, entry->checkValue) == 0;
    }

    if (isMatch)
    {
        LOG("Whitelist match! list type: %s, field: %ls,\n\tWhitelist: %ls\n\tProcess:   %ls",
            entry->isExclusive ? "exclusive" : "whitelist", procfield_str[entry->checkField],
            entry->checkValue, field);
    }

    return isMatch;
}

#pragma endregion // Whitelisting.

// AlwaysShadow - a program for forcing Shadowplay's Instant Replay to stay on.
// Copyright (C) 2020 Aviv Edery.

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
#include <stdio.h>      // For printing errors and such.
#include <tchar.h>      // For dealing with unicode and ANSI strings.
#include <pthread.h>    // For multithreading.
#include <unistd.h>     // For sleep.

#pragma region Macros

// This is the filder in the temp files path where recordings are saved (plus the file names with a wildcard).
#define TEMP_PATH_SUFFIX L"9343b833-e7af-42ea-8a61-31bc41eefe2b\\Sha*.tmp"

#pragma endregion // Macros.

// TODO: allow the user to define a list of processes that cause this program to disable itself when they run. Probably identify them using their command line.
// Using WMI sounds best because other methods are not guaranteed to always work by MS. Write it in other language if C is too inconvenient. Can use python with py2exe maybe?
// TODO: auto turn on in-game overlay if it is turned off?

volatile TCHAR* errorMsg = NULL;

void* FixerLoop(void* arg)
{
    // Making thread cancellable.
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    WCHAR tempFilesPath[MAX_PATH];
	FetchTempFilesPath(tempFilesPath, sizeof(tempFilesPath), _countof(tempFilesPath));

    // The inputs array is never freed. Some would consider this a memory leak, I say it's ok since it will only ever not be needed anymore when the program exits.
    UINT arraySize;
    INPUT* inputs = FetchToggleShortcut(&arraySize);

    for (;;)
    {
        sleep(5);

        if (!isDisabled)
        {
            if (!IsInstantReplayOn(tempFilesPath))
            {
                ToggleInstantReplay(inputs, arraySize);
            }
        }
    }

    return 0;
}

// For some reason if we try to compute bufsz/sizeof(*buffer) instead of countof, wcscat fails.
void FetchTempFilesPath(WCHAR* buffer, DWORD bufsz, rsize_t countof)
{
	LSTATUS ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"), TEXT("TempFilePath"), RRF_RT_ANY, NULL, (PVOID)buffer, &bufsz);

    if (ret != ERROR_SUCCESS)
    {
        fprintf(stderr, "Failed to fetch temp file path with error code 0x%lX\n", ret);
        ThreadError(TEXT("Failed to detect settings for identifying if Instant Replay is on. Quitting."));
    }
    
    if (wcscat_s(buffer, countof, TEMP_PATH_SUFFIX) != 0)
    {
        fprintf(stderr, "Failed to append file path suffix.\n");
        ThreadError(TEXT("Failed to detect settings for identifying if Instant Replay is on. Quitting."));
    }
}

char IsInstantReplayOn(WCHAR* tempFilesPath)
{
    // We detect if Instant Replay is on by checking if its temp files exist.
    WIN32_FIND_DATA fileData;
    HANDLE fileHandle = FindFirstFile(tempFilesPath, &fileData);

    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    
    FindClose(fileHandle);
    return TRUE;
}

INPUT* FetchToggleShortcut(UINT* arraySize)
{
    INPUT* shortcut;

    DWORD hkeyCount;
    DWORD bufsz = sizeof(hkeyCount);
    LSTATUS ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"), TEXT("IRToggleHKeyCount"), RRF_RT_ANY, NULL, (PVOID)&hkeyCount, &bufsz);

    // Defaulting to Alt+Shift+F10.
    if (ret != ERROR_SUCCESS)
    {
        *arraySize = 3;
        shortcut = calloc((*arraySize) * 2, sizeof(INPUT));
        
        CreateInput(shortcut + 0, VK_MENU, TRUE);
        CreateInput(shortcut + 1, VK_SHIFT, TRUE);
        CreateInput(shortcut + 2, VK_F10, TRUE);

        CreateInput(shortcut + (*arraySize) + 0, VK_MENU, FALSE);
        CreateInput(shortcut + (*arraySize) + 1, VK_SHIFT, FALSE);
        CreateInput(shortcut + (*arraySize) + 2, VK_F10, FALSE);
    }
    else // Reading from registry.
    {
        *arraySize = hkeyCount;
        shortcut = calloc((*arraySize) * 2, sizeof(INPUT));

        // Reading each character of the shortcut from its own registry entry.
        for (int i = 0; i < *arraySize; i++)
        {
            // Creating string of registry name (Should be IRToggleHKey0, IRToggleHKey1, etc.).
            TCHAR valueName[256];
            if (_sntprintf_s(valueName, _countof(valueName), _TRUNCATE, TEXT("IRToggleHKey%d"), i) == -1)
            {
                fprintf(stderr, "This shortcut must be hella long because I can't fit the number in this buffer!\n");
                ThreadError(TEXT("Failed to detect settings for being able to turn on Instant Replay. Quitting."));
            }

            // Reading the registry entry.
            DWORD vkey;
            bufsz = sizeof(vkey);
            ret = RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS"), valueName, RRF_RT_ANY, NULL, (PVOID)&vkey, &bufsz);

            if (ret != ERROR_SUCCESS)
            {
                fprintf(stderr, "Failed to read hotkey %d with error code 0x%lX\n", i, ret);
                ThreadError(TEXT("Failed to detect settings for being able to turn on Instant Replay. Quitting."));
            }

            CreateInput(shortcut + i, *((WORD*)(&vkey)), TRUE);
            CreateInput(shortcut + (*arraySize) + i, *((WORD*)(&vkey)), FALSE);
        }
    }

    *arraySize *= 2;
    return shortcut;
}

void CreateInput(INPUT* input, WORD vkey, char isDown)
{
    input->type = INPUT_KEYBOARD;
    input->ki.wVk = vkey;
    input->ki.dwFlags = isDown ? 0 : KEYEVENTF_KEYUP;
}

void ToggleInstantReplay(INPUT* inputs, UINT arraySize)
{
    SendInput(arraySize, inputs, sizeof(INPUT));
}

void ThreadError(TCHAR* msg)
{
    size_t len = _tcslen(msg);
    errorMsg = malloc((len + 1) * sizeof(*errorMsg));

    if (errorMsg != NULL && _tcscpy_s(errorMsg, len + 1, msg) != 0)
    {
        free(errorMsg);
        errorMsg = NULL;
    }
    
    fixerDied = TRUE;
    pthread_exit(NULL);
}
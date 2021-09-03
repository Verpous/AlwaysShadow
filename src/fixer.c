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

// The name of this program.
#define SCANCODE_LALT 0x38 
#define SCANCODE_LSHIFT 0x2A
#define SCANCODE_F10 0x44

#pragma endregion // Macros.

// TODO: Get shadowplay temp files path from registry (it's stored in Computer\HKEY_CURRENT_USER\SOFTWARE\NVIDIA Corporation\Global\ShadowPlay\NVSPCAPS\TempFilePath)
// TODO: Get shadowplay toggle shortcut from registry (this may be harder to do, maybe just give up or let the user manually assign the same shortcut.)
// TODO: Investigate potential conflicts with Netflix.

void* FixerLoop(void* arg)
{
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    for (;;)
    {
        sleep(3);

        if (!isDisabled)
        {
            if (!IsInstantReplayOn())
            {
                ToggleInstantReplay();
            }
        }
    }

    return 0;
}

char IsInstantReplayOn()
{
    WIN32_FIND_DATA fileData;
    TCHAR filePath[MAX_PATH];
    _tcscpy(filePath, _tgetenv(TEXT("TEMP")));
    _tcscat(filePath, TEXT("\\9343b833-e7af-42ea-8a61-31bc41eefe2b\\Sha*.tmp"));

    HANDLE fileHandle = FindFirstFile(filePath, &fileData);

    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    
    FindClose(fileHandle);
    return TRUE;
}

INPUT CreateInput(WORD scancode, char isDown)
{
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scancode;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (isDown ? 0 : KEYEVENTF_KEYUP);
    return input;
}

void ToggleInstantReplay()
{
    // Simulating Alt+Shift+F10.
    INPUT inputs[6] = {};
    ZeroMemory(inputs, sizeof(inputs));

    inputs[0] = CreateInput(SCANCODE_LALT, TRUE);
    inputs[1] = CreateInput(SCANCODE_LSHIFT, TRUE);
    inputs[2] = CreateInput(SCANCODE_F10, TRUE);

    inputs[3] = CreateInput(SCANCODE_LALT, FALSE);
    inputs[4] = CreateInput(SCANCODE_LSHIFT, FALSE);
    inputs[5] = CreateInput(SCANCODE_F10, FALSE);

    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
}
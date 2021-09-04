# AlwaysShadow
Shadowplay's Instant Replay feature is unreliable. You often find out it is turned off when you need it most. This is despite enabling the fact that it's supposed to run on startup. This is a simple Windows program which will make sure Instant Replay is on at all times.

## Usage instructions
Run AlwaysShadow.exe. I recommend [adding it to your startup programs](https://support.microsoft.com/en-us/windows/add-an-app-to-run-automatically-at-startup-in-windows-10-150da165-dcd9-7230-517b-cf3c295d89dd) so it will always be running when you turn on your computer. The program will make sure to turn Instant Replay back on should it ever turn off. Additionally, there is a system tray icon with options to disable or exit the program.

For this program to work, you have to turn on In-Game Overlay in your GeForce Experience settings.

Note that some programs (namely Netflix) prevent Instant Replay from being active, which conflicts with this program. In the future there may be a feature for dealing with this, but for now, you will have to manually disable this program when running one of those programs, or disable the open/close Instant Replay notification in your GeForce Experience settings to hide the bug.

If you do change your shortcut for toggling Instant Replay on/off in your GeForce Experience settings, you will need to exit and relaunch this program.

Likewise, if you change the location where Instant Replay stores temp files, you will also need to exit and relaunch this program.

To use this program you will need to turn on Desktop Capture in your GeForce Experience settings otherwise Instant Replay will constantly be getting toggled on/off when you're in the desktop. If Desktop Capture being off is important to you, you can disable the open/close Instant Replay notification to at least hide the fact that the program is bugging out.

## Compilation instructions
1. Install [MSYS2](https://www.msys2.org/)
2. Install mingw-w64 and [make](https://www.gnu.org/software/make/) for 64-bit using MSYS2
3. Add mingw-w64 and make's bin folders to PATH (should look something like "C:\msys64\mingw64\bin" and "C:\msys64\usr\bin", respectively)
4. Clone this repository
5. Run make inside the root directory of the repository. This will create the program executable named "AlwaysShadow.exe" inside a folder named "bin"

You can run "make clean" to empty the bin folder if you want to recompile everything.

The makefile includes some additional targets which are explained inside the makefile via comments.

I am not affiliated with NVidia in any way.
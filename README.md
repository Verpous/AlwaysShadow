# AlwaysShadow

Shadowplay's Instant Replay feature is unreliable. You often find out it is turned off when you need it most. This is despite the fact that it's supposed to run on startup. This is a simple Windows program which will make sure Instant Replay is on at all times.

## Usage instructions

Run AlwaysShadow.exe. I recommend [adding it to your startup programs](https://support.microsoft.com/en-us/windows/add-an-app-to-run-automatically-at-startup-in-windows-10-150da165-dcd9-7230-517b-cf3c295d89dd) so it will always be running when you turn on your computer. The program will make sure to turn Instant Replay back on should it ever turn off. Additionally, there is a system tray icon with options to disable or exit the program.

For this program to work, you have to turn on In-Game Overlay in your GeForce Experience settings.

Some programs (such as Netflix) prevent Instant Replay from being active, which conflicts with this program. You can define a list of programs that will cause this program to disable itself while they are running. To define your own list, create a file named **exactly** Whitelist.txt in the same folder where you run the executable. For every program you want to add to the list, add its command line to Whitelist.txt in its own line.

To find out a program's command line, run it and go to the Task Manager. Right click the top bar and make sure "Command line" is enabled, like so:

![Screenshot (44)](https://user-images.githubusercontent.com/30209851/132571330-e7a0415e-78b2-42d2-9607-4f8e8759c4cd.png)

Once enabled, Task Manager will show each process's command line. Run the process you want to add to the list, find it in the Task Manager, and copy-paste its command line to your Whitelist.txt in its own line. 

The repo includes an example Whitelist.txt with Netflix and Prime Video in it but **this is only for example**, as the exact command lines may be different on your PC.

You will need to exit and relaunch this program if you do one of the following things:
1. Change the shortcut for toggling Instant Replay on/off in your GeForce Experience settings
2. Change the location where Instant Replay stores temp files
3. Create, delete, or modify your Whitelist.txt file

It is recommended to turn on Desktop Capture in your GeForce Experience settings otherwise Instant Replay will constantly be getting toggled on/off when you're in the desktop. If Desktop Capture being off is important to you, you can disable the open/close Instant Replay notification to at least hide the fact that the program is bugging out.

AlwaysShadow turns on Instant Replay by simulating the keypresses for the shortcut that toggles it in GeForce Experience, which by default is Alt+Shift+F10. This may cause AlwaysShadow to change your keyboard language because the default shortcut for cycling between languages in Windows is Alt+Shift. To resolve this issue, it is recommended to go to your GeForce Experience settings and change the shortcut for toggling Instant Replay. I use Ctrl+Shift+F10. Remember that after changing the shortcut you will need to exit and relaunch this program.

## Download

You can download the compiled program [here](https://github.com/Verpous/AlwaysShadow/releases/download/v1.2/AlwaysShadow.zip), or you can go to [Releases](https://github.com/Verpous/AlwaysShadow/releases) and choose what to download. And of course, you can always clone the repo and compile it yourself.

## Compilation instructions

1. Install [MSYS2](https://www.msys2.org/)
2. Install mingw-w64 and [make](https://www.gnu.org/software/make/) for 64-bit using MSYS2
3. Add mingw-w64 and make's bin folders to PATH (should look something like "C:\msys64\mingw64\bin" and "C:\msys64\usr\bin", respectively)
4. Clone this repository
5. Run make inside the root directory of the repository. This will create the program executable named "AlwaysShadow.exe" inside a folder named "bin"

You can run "make clean" to empty the bin folder if you want to recompile everything.

The makefile includes some additional targets which are explained inside the makefile via comments.

I am not affiliated with NVidia in any way.

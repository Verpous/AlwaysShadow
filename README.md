# AlwaysShadow

Shadowplay's Instant Replay feature is unreliable. You often find out it is turned off when you need it most. This is despite the fact that it's supposed to run on startup. This is a simple Windows program which will make sure Instant Replay is on at all times.

## Usage instructions

Run AlwaysShadow.exe. I recommend [adding it to your startup programs](https://support.microsoft.com/en-us/windows/add-an-app-to-run-automatically-at-startup-in-windows-10-150da165-dcd9-7230-517b-cf3c295d89dd) so it will always be running when you turn on your computer. The program will make sure to turn Instant Replay back on should it ever turn off. Additionally, there is a system tray icon with options to disable or exit the program.

For this program to work, you have to turn on In-Game Overlay in your GeForce Experience settings.

### Whitelisting

Some programs (such as Netflix) prevent Instant Replay from being active, which conflicts with this program. You can define a list of programs that will cause this program to disable itself while they are running. To define your own list, create a file named **exactly** Whitelist.txt in the same folder where you run the executable. For every program you want to add to the list, add its command line to Whitelist.txt in its own line.

To find out a program's command line, run it and go to the Task Manager. Right click the top bar and make sure "Command line" is enabled, like so:

![Screenshot (44)](https://user-images.githubusercontent.com/30209851/132571330-e7a0415e-78b2-42d2-9607-4f8e8759c4cd.png)

Once enabled, Task Manager will show each process's command line. Run the process you want to add to the list, find it in the Task Manager, and copy-paste its command line to your Whitelist.txt in its own line.

The whitelist supports a few additional, more advanced features. Each line in the whitelist may begin with an expression of the form: `??<flags> `. That is, a `??`, followed by one or more flag characters, and then a space character. The flag characters modify the behavior of the line. For example, your whitelist may contain the following line:

```
??ES Hades.exe
```

This causes AlwaysShadow to only run while a process with "Hades.exe" *anywhere* within its command line is running. The order of the flags doesn't matter. The complete list of flags is:

`E` - Adds this line to the "exclusives" list, not the whitelist. This program will force Shadowplay to be disabled when no "exclusive" command is running, and enabled while at least one is running. If there are whitelisted programs running too, the whitelist wins and AlwaysShadow is disabled.

`S` - Makes this line match any command lines of which it is a substring, as opposed to the default behavior where the lines must match exactly. This means that instead of copying long ugly command lines, you can pick a pretty part of the command line to use (like the exe). But **make sure** that the line won't also match things you don't want it to.

`I` - Makes this line be ignored. You can use this to add comments.

The repo includes an example Whitelist.txt but **it is only for example**, as the exact command lines may be different on your PC.

## Notes

You will need to refresh this program (right click the icon in the notification bar and hit Refresh) if you do one of the following things:
1. Change the shortcut for toggling Instant Replay on/off in your GeForce Experience settings
2. Create, delete, or modify your Whitelist.txt file

AlwaysShadow turns on Instant Replay by simulating the keypresses for the shortcut that toggles it in GeForce Experience, which by default is Alt+Shift+F10. This may cause AlwaysShadow to change your keyboard language because the default shortcut for cycling between languages in Windows is Alt+Shift. To resolve this issue, it is recommended to go to your GeForce Experience settings and change the shortcut for toggling Instant Replay. I use Ctrl+Shift+F10. Remember that after changing the shortcut you will need to exit and relaunch this program.

## Download

Simply click [here](https://github.com/Verpous/AlwaysShadow/releases/download/v1.3/AlwaysShadow.zip), or you can go to [Releases](https://github.com/Verpous/AlwaysShadow/releases) and choose what to download. And of course, you can always clone the repo and compile it yourself.

## Issues

Feel free to open an issue for any request or problem. If you have a problem, please include the log file located at:

```
%LOCALAPPDATA%/AlwaysShadow/output.log
```

You should exit or refresh AlwaysShadow before using this file to make sure the logs are all flushed.

## Compilation instructions

1. Install [MSYS2](https://www.msys2.org/)
2. Install mingw-w64 and [make](https://www.gnu.org/software/make/) for 64-bit using MSYS2
3. Add mingw-w64 and make's bin folders to PATH (should look something like "C:\msys64\mingw64\bin" and "C:\msys64\usr\bin", respectively)
4. Clone this repository
5. Run make inside the root directory of the repository. This will create the program executable named "AlwaysShadow.exe" inside a folder named "bin"

You can run "make clean" to empty the bin folder if you want to recompile everything.

The makefile includes some additional targets which are explained inside the makefile via comments.

I am not affiliated with NVidia in any way.

# THIS PROGRAM IS A WORK IN PROGRESS. IT'S NOT DONE YET.

# AlwaysShadow
Shadowplay's Instant Replay feature is unreliable. You often find out it is turned off when you need it most. This is despite enabling the feature which is supposed to launch it on startup. This is a simple Windows program which will make sure Instant Replay is on at all times.

### Compilation instructions:
1. Install [MSYS2](https://www.msys2.org/)
2. Install mingw-w64 and [make](https://www.gnu.org/software/make/) for 64-bit using MSYS2
3. Add mingw-w64 and make's bin folders to PATH (should look something like "C:\msys64\mingw64\bin" and "C:\msys64\usr\bin", respectively)
4. Clone this repository
5. Run make inside the root directory of the repository. This will create the program executable named "AlwaysShadow.exe" inside a folder named "bin"

You can run "make clean" to empty the bin folder if you want to recompile everything.

The makefile includes some additional targets which are explained inside the makefile via comments.
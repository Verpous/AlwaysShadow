# AlwaysShadow - a program for forcing Shadowplay's Instant Replay to stay on.
# Copyright (C) 2023 Aviv Edery.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

CC:=gcc
BIN:=bin
SRC:=src
RESRC:=resources
PROG:=$(BIN)/AlwaysShadow.exe
RELEASE:="$$HOME"/Desktop/AlwaysShadow.zip
FLAGFILE:=$(BIN)/cflags.txt

# The style of commenting below may seem funny but there's a reason, it's so the alignment spacing doesn't make the output ugly.
# C compiler flags.
CFlags += -c #                          Compile, duh.
CFlags += -Iinclude #                   Search for #includes in the include folder.
CFlags += -Wall #                       All warnings (minus the ones we'll subtract now).
CFlags += -Wno-unknown-pragmas #        For getting rid of warnings about regions in the code.
CFlags += -fmacro-prefix-map=$(SRC)/= # Makes it so in the logs only basenames of files are printed.

# Linker flags.
LFlags += -Wall #     All warnings.
LFlags += -mwindows # Makes it so when you run the program it doesn't open cmd.
LFlags += -static #   For static linking so people don't have problems (they've had a few).

# Libraries that we link.
LIBS += -lpthread #   For multithreading.
LIBS += -lwbemuuid #  For WMI to get the command line of processes. 
LIBS += -lole32 #     For COM to get the command line of processes.
LIBS += -loleaut32 #  For working with BSTRs.
LIBS += -lregex #     For regex to parse the whitelist.
LIBS += -ltre #       Dependency of regex.
LIBS += -lintl #      Dependency of regex.
LIBS += -liconv #     Dependency of regex.
LIBS += -luuid #      For FOLDERID_LocalAppData.
LIBS += -lshlwapi #   For path functions.

# Object files we generate and link.
OBJS += $(BIN)/main.o
OBJS += $(BIN)/Resources.o
OBJS += $(BIN)/fixer.o

# Pass variables debug=yes/no, unicode=yes/no to control build settings.
unicode = yes
debug = no

ifeq ($(strip $(unicode)),yes)
	CFlags += -D UNICODE -D _UNICODE
endif

ifeq ($(strip $(debug)),yes)
	CFlags += -D DEBUG_BUILD
endif

.PHONY: all release run runx log write_flagfile clean

# Makes a build.
all: write_flagfile $(PROG)

# Prepares a zip for release.
release: clean all
	rm -f $(RELEASE)
	7z a -tzip $(RELEASE) ./$(PROG)

# Compiles and runs. Output streams are redirected to a log.
run: all runx

# "Run exclusively". Same as run, but won't try to compile it.
runx:
	$(PROG)

log:
	tail -f "$$LOCALAPPDATA"/AlwaysShadow/output.log

# Writes CFlags to a file only if it's changed from the last run. We use this to recompile binaries when changing to/from debug builds.
write_flagfile:
	if [[ ! -f $(FLAGFILE) ]] || ! diff -q <(echo "$(CFlags)") $(FLAGFILE); then echo "$(CFlags)" > $(FLAGFILE); fi

# Empties the bin folder.
clean:
	rm -f $(BIN)/*

# The following targets do the actual job of compiling and linking all the different files. You'll probably never run them directly.
$(PROG): $(OBJS)
	$(CC) $(LFlags) $(OBJS) $(LIBS) -o $@

$(BIN)/%.o: $(SRC)/%.c $(FLAGFILE) | $(BIN)
	$(CC) $(CFlags) -o $@ $<

$(BIN)/%.o: $(RESRC)/%.rc | $(BIN)
	windres -Iinclude -o $@ $<

$(BIN):
	mkdir -p $@

# ShadowplayFixer - a program for modifying the weights of different frequencies in a wave file.
# Copyright (C) 2020 Aviv Edery.

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

# C Compiler.
CC:=gcc
BIN:=bin
SRC:=src

# C compiler flags.
# -Wno-unknown-pragmas gets rid of warnings about regions in the code.
CFlags=-Wall -Wno-comment -Wno-unknown-pragmas -c -Iinclude -D UNICODE -D _UNICODE

# Linker flags.
# -mwindows makes it so when you run the program it doesn't open cmd.
LFlags=-Wall -mwindows

# Libraries that we link.
LinkedLibs:= 

# Object files we generate and link.
OBJS += $(BIN)/main.o
OBJS += $(BIN)/Resources.o

PROG=$(BIN)/ShadowplayFixer.exe

.PHONY: all run runx clean

all: $(PROG)

# Compiles and runs. Output streams are redirected to a log.
run: $(PROG) runx

# "Run exclusively". Same as run, but won't try to compile it.
runx:
	$(PROG) >>errors.log 2>&1

# Empties the bin folder.
clean:
	rm -f $(BIN)/*

# The following targets do the actual job of compiling and linking all the different files. You'll probably never run them directly.
$(PROG): $(BIN) $(OBJS)
	$(CC) $(LFlags) $(OBJS) $(LinkedLibs) -o $@

$(BIN):
	mkdir -p $(BIN)

# Important that the source file be the first prerequisite.
$(BIN)/%.o: $(SRC)/%.c $(BIN)
	$(CC) $(CFlags) -o $@ $<

$(BIN)/%.o: resources/%.rc $(BIN)
	windres -Iinclude -o $@ $<
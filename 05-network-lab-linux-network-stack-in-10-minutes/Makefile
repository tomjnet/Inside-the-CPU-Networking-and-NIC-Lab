# Inside the CPU: Networking and NIC Lab - code samples (one program per .cpp file, and its C twin per .c file)
#
#   make                  build every sample, C++ and C
#   make cpp              build only the C++ samples     make c    build only the C samples
#   make run              build and run every sample, one after the other
#   make 01_vector        build one C++ sample (Windows: make 01_vector.exe)
#   make 01_vector_c      build its C twin, from 01_vector.c (Windows: make 01_vector_c.exe)
#   make run-01_vector    build and run one sample (run-01_vector_c for the C one)
#   make clean            remove binaries and object files
#   make STD=c++20        pick the C++ standard (default c++26; MSVC always uses /std:c++latest)
#   make CSTD=c11         pick the C standard (default c17)
#
# Compilers by operating system (override with make CXX=<compiler> CC=<compiler>,
# e.g. make CXX=g++ CC=gcc on Windows with MinGW):
#   Windows  -> cl (MSVC) for both.  Run make from a "Developer Command Prompt" / after vcvars64.bat so cl is in PATH.
#   Linux    -> g++ and gcc
#   macOS    -> clang++ and clang
# A C program NN_name.c builds NN_name_c, so it never collides with the C++ program NN_name.

STD  ?= c++26
CSTD ?= c17

# only the rules below apply: the built-in "%: %.c" rule would otherwise build NN_name from NN_name.c
MAKEFLAGS += --no-builtin-rules
.SUFFIXES:

ifeq ($(OS),Windows_NT)
  PLATFORM    := windows
  DEFAULT_CXX := cl
  DEFAULT_CC  := cl
  EXE         := .exe
  # GNU make on Windows may run recipes through cmd.exe or through an sh.exe found in PATH. Probe which one:
  # cmd expands %ComSpec% to its own path, sh leaves the text untouched.
  ifneq (,$(findstring cmd.exe,$(shell echo %ComSpec%)))
    RM  := del /q
    # .\ also works where cmd skips the current folder (NoDefaultCurrentDirectoryInExePath)
    RUN := .\$(EMPTY)
  else
    RM  := rm -f
    RUN := ./
  endif
else
  UNAME := $(shell uname -s)
  ifeq ($(UNAME),Darwin)
    PLATFORM    := macos
    DEFAULT_CXX := clang++
    DEFAULT_CC  := clang
  else
    PLATFORM    := linux
    DEFAULT_CXX := g++
    DEFAULT_CC  := gcc
  endif
  EXE :=
  RUN := ./
  RM  := rm -f
endif

# make defines CXX=g++ and CC=cc on its own, so "?=" would never apply the platform default; only a user-given one wins
ifeq ($(origin CXX),default)
  CXX := $(DEFAULT_CXX)
endif
ifeq ($(origin CC),default)
  CC := $(DEFAULT_CC)
endif

# flag syntax follows the compiler actually used
ifneq ($(filter cl cl.exe,$(notdir $(CXX))),)
  CXXFLAGS ?= /nologo /std:c++latest /W4 /EHsc /O2 /permissive-
  OUTFLAG   = /Fe:$@ /Fo:$(basename $@).obj
else
  CXXFLAGS ?= -std=$(STD) -Wall -Wextra -pedantic -O2 -pthread
  OUTFLAG   = -o $@
endif
# MSVC C: C17 plus <stdatomic.h>, which it still ships behind /experimental:c11atomics
ifneq ($(filter cl cl.exe,$(notdir $(CC))),)
  CFLAGS   ?= /nologo /std:c17 /experimental:c11atomics /W4 /O2
  COUTFLAG  = /Fe:$@ /Fo:$(basename $@).obj
else
  CFLAGS   ?= -std=$(CSTD) -Wall -Wextra -pedantic -O2 -pthread
  COUTFLAG  = -o $@ -lm              # libm: <math.h> functions such as pow, sqrt
endif

SRCS  := $(sort $(wildcard *.cpp))
CSRCS := $(sort $(wildcard *.c))
HDRS  := $(wildcard *.h)
BINS  := $(SRCS:.cpp=$(EXE))
CBINS := $(CSRCS:.c=_c$(EXE))
JUNK  := $(sort $(wildcard $(BINS) $(CBINS) $(SRCS:.cpp=) $(CSRCS:.c=_c) *.obj *.o *.pdb *.ilk))

all: cpp c

cpp: $(BINS)
	@echo built $(words $(BINS)) C++ samples on $(PLATFORM) with $(CXX)

c: $(CBINS)
	@echo built $(words $(CBINS)) C samples on $(PLATFORM) with $(CC)

%_c$(EXE): %.c $(HDRS)
	$(CC) $(CFLAGS) $< $(COUTFLAG)

%$(EXE): %.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) $< $(OUTFLAG)

# run targets carry no extension (run-01_vector), otherwise "run-x.exe" would match the %.exe: %.cpp rule.
# "|| exit 1" is valid in sh and cmd alike and makes make hand the line to the shell instead of spawning it itself.
# (no ">" in the echo: sh would treat it as a redirect and overwrite the file named after it)
run: $(addprefix run-,$(SRCS:.cpp=) $(CSRCS:.c=_c))

run-%: %$(EXE)
	@echo == $< ==
	$(RUN)$< || exit 1

clean:
ifneq (,$(JUNK))
	$(RM) $(JUNK)
endif

.PHONY: all cpp c run clean

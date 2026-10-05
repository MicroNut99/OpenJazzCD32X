# OpenJazz CD32X - J3a: FIRST SH2 COMPILE (compile + trial link only; nothing runs yet).
# Compiles the same game + port files as the PC build with the Sega toolchain's sh-elf-g++,
# then does a trial link (no start-up code; missing 32X platform functions allowed) to
# measure the real SH2 code size, and lists the functions the 32X platform layer must provide.
#   make -k -C mars 2>&1 | tee mars_log.txt    (-k: try every file, list all failures)
# Results: mars/report.txt (sizes) and mars/undefined.txt (functions still to write).

TC      ?= /opt/toolchains/sega/sh-elf/bin/sh-elf-
CC      := $(TC)gcc
CXX     := $(TC)g++
SIZE    := $(TC)size
NM      := $(TC)nm
ARCH    ?= -m2 -mb
# SH ELF puts an underscore before C names (PC dry run: MAIN=main)
MAIN    ?= _main

ROOT := ..
SRC  := $(ROOT)/oj/src
PORT := $(ROOT)/port
EXT  := $(ROOT)/oj/ext

# the same game files as host/Makefile
GAME_SRCS := \
	game/game.cpp game/gamemode.cpp game/localgame.cpp game/clientgame.cpp game/servergame.cpp \
	io/controls.cpp io/file.cpp io/log.cpp io/network.cpp io/sound.cpp \
	io/gfx/anim.cpp io/gfx/font.cpp io/gfx/paletteeffects.cpp io/gfx/sprite.cpp io/gfx/video.cpp \
	level/level.cpp level/movable.cpp main.cpp debug.cpp \
	menu/debugmenu.cpp menu/filemenu.cpp menu/gamemenu.cpp menu/mainmenu.cpp menu/menu.cpp \
	menu/plasma.cpp menu/setupmenu.cpp platforms/platform_interface.cpp player/player.cpp \
	setup.cpp util.cpp version.cpp \
	jj1/jj1episodeutils.cpp jj1/bonuslevel/jj1bonuslevel.cpp jj1/bonuslevel/jj1bonuslevelplayer.cpp \
	jj1/level/jj1bird.cpp jj1/level/jj1levelplayer.cpp jj1/level/jj1levelplayerframe.cpp \
	jj1/level/event/jj1bridge.cpp jj1/level/event/jj1event.cpp jj1/level/event/jj1guardians.cpp \
	jj1/level/event/jj1standardevent.cpp jj1/level/jj1bullet.cpp jj1/level/jj1demolevel.cpp \
	jj1/level/jj1level.cpp jj1/level/jj1levelframe.cpp jj1/level/jj1levelload.cpp \
	jj1/planet/jj1planet.cpp jj1/save/jj1save.cpp jj1/scene/jj1scene.cpp jj1/scene/jj1sceneload.cpp
EXT_SRCS := argparse/argparse.c miniz/miniz.c stb/stb_rect_pack.c
XMP_SRCS := $(notdir $(wildcard $(EXT)/xmp/src/*.c)) $(addprefix loaders/,$(notdir $(wildcard $(EXT)/xmp/src/loaders/*.c)))
PORT_CXX := sdl_oj.cpp
PORT_C   := mixer.c
# newlib system calls for the SH2 (keeps libc.a's simulator syscalls out: `errno' clash).
# Not in a PC dry run (TC=), where they would clash with the PC's C library.
ifneq ($(TC),)
PORT_C   += mars_syscalls.c
endif

OBJDIR := obj
OPT    := $(ARCH) -Os -ffunction-sections -fdata-sections
DEFS   := -DOJ32X -DOJ32X_MARS -DOJ_SDL1 -DPORTABLE -DNDEBUG \
          -DOJ_VERSION=\"git-oj32x\" -DOJ_DATE=\"2026-10-06\" \
          -DOJ_URL=\"http://alister.eu/jazz/oj/\" -DOJ_BUGREPORT=\"https://github.com/AlisterT/openjazz/issues\" \
          -DLIBXMP_CORE_PLAYER -DLIBXMP_STATIC -DXMP_SYM_VISIBILITY
INCS   := -I$(PORT)/include -I$(PORT) -I$(SRC) -I$(EXT)/argparse -I$(EXT)/miniz -I$(EXT)/xmp -I$(EXT)/xmp/src -I$(EXT)/stb
CXXFLAGS := -std=gnu++14 $(OPT) $(DEFS) $(INCS) -fno-threadsafe-statics -Wall -Wno-unused-result -Wno-narrowing
CFLAGS   := -std=gnu11 $(OPT) $(DEFS) $(INCS) -w

GAME_OBJS := $(addprefix $(OBJDIR)/game/,$(GAME_SRCS:.cpp=.o))
EXT_OBJS  := $(addprefix $(OBJDIR)/ext/,$(EXT_SRCS:.c=.o))
XMP_OBJS  := $(addprefix $(OBJDIR)/xmp/,$(XMP_SRCS:.c=.o))
PORT_OBJS := $(addprefix $(OBJDIR)/port/,$(PORT_CXX:.cpp=.o) $(PORT_C:.c=.o))
OBJS := $(GAME_OBJS) $(EXT_OBJS) $(XMP_OBJS) $(PORT_OBJS)

all: report.txt undefined.txt
	@echo; cat report.txt; echo; echo "==> $$(wc -l < undefined.txt) functions still to provide: mars/undefined.txt"

# -k style: every file is tried, the failures are listed at the end
report.txt: $(OBJS) $(OBJDIR)/trial_stub.o
	@echo "==> trial link (no start-up code, missing functions allowed)"
	$(CXX) $(ARCH) -Os -nostartfiles -Wl,-e,$(MAIN) -Wl,-u,$(MAIN) -Wl,--gc-sections -Wl,--unresolved-symbols=ignore-all \
		-o trial.elf $(OBJS) $(OBJDIR)/trial_stub.o -lstdc++ -lm -lc -lgcc -Wl,-Map,trial.map
	@{ echo "== SH2 code size per part (-Os, before removing unused code)"; \
	   echo "           text    data     bss     dec     hex"; \
	   echo "game:   $$($(SIZE) -t $(GAME_OBJS) | tail -1)"; \
	   echo "ext:    $$($(SIZE) -t $(EXT_OBJS) | tail -1)"; \
	   echo "xmp:    $$($(SIZE) -t $(XMP_OBJS) | tail -1)"; \
	   echo "port:   $$($(SIZE) -t $(PORT_OBJS) | tail -1)"; \
	   echo "== trial link, unused code removed, with libstdc++/newlib (text data bss dec hex)"; \
	   $(SIZE) trial.elf | tail -1; } > $@

undefined.txt: $(OBJS) $(OBJDIR)/trial_stub.o
	$(CXX) $(ARCH) -nostdlib -r -o partial.o $(OBJS)
	$(NM) -u -C partial.o | awk '{$$1=""; print substr($$0,2)}' | sort > all_undefined.txt
	@# what the libraries do not cover = what the 32X platform layer must provide
	$(CXX) $(ARCH) -Os -nostartfiles -Wl,-e,$(MAIN) -Wl,--unresolved-symbols=report-all \
		-o /dev/null $(OBJS) $(OBJDIR)/trial_stub.o -lstdc++ -lm -lc -lgcc 2>&1 | \
		grep -o "undefined reference to \`[^']*'" | sed "s/undefined reference to \`//; s/'$$//" | sort -u > $@ || true

$(OBJDIR)/game/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/ext/%.o: $(EXT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/xmp/%.o: $(EXT)/xmp/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/trial_stub.o: trial_stub.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/port/%.o: $(PORT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/port/%.o: $(PORT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf obj trial.elf trial.map partial.o report.txt undefined.txt all_undefined.txt
.PHONY: all clean

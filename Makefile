# Overzealous native ET: Legacy / ET 2.60b modules (Linux, Windows, macOS)
CC ?= cc
CXX ?= c++
PYTHON ?= python3
WITH_LUA ?= 1
WITH_OMNIBOT ?= 1
# Inherited, spoofable key fallback; experimental and disabled by default.
WITH_AUTO_GUID ?= 0
ARCH ?= x86_64
# Cross-compilation: set PLATFORM=linux|windows|macos and ARCH=x86_64|i386
# (plus arm64|universal on macOS) to cross-compile from Linux using zig.
# When PLATFORM is empty, auto-detect from the host OS.
PLATFORM ?=

# Auto-discover zig in .cache, legacy .tools, or PATH; fall back to .cache bootstrap.
ZIG_CACHE_BIN := $(CURDIR)/.cache/zig-pip/ziglang/zig
ZIG_LEGACY_BIN := $(CURDIR)/.tools/zig-pip/extracted/ziglang/zig
ZIG_PATH_BIN := $(shell command -v zig 2>/dev/null)
ifneq ($(wildcard $(ZIG_CACHE_BIN)),)
  ZIG ?= $(ZIG_CACHE_BIN)
else ifneq ($(wildcard $(ZIG_LEGACY_BIN)),)
  ZIG ?= $(ZIG_LEGACY_BIN)
else ifneq ($(ZIG_PATH_BIN),)
  ZIG ?= $(ZIG_PATH_BIN)
else
  ZIG ?= $(ZIG_CACHE_BIN)
endif

# Overzealous, Arena.ai: reject empty/multiple values and unsupported options.
ifneq ($(WITH_LUA),0)
  ifneq ($(WITH_LUA),1)
    $(error WITH_LUA must be 0 or 1)
  endif
endif
ifneq ($(WITH_OMNIBOT),0)
  ifneq ($(WITH_OMNIBOT),1)
    $(error WITH_OMNIBOT must be 0 or 1)
  endif
endif
ifneq ($(WITH_AUTO_GUID),0)
  ifneq ($(WITH_AUTO_GUID),1)
    $(error WITH_AUTO_GUID must be 0 or 1)
  endif
endif

# --- Platform detection / cross-compilation setup ----------------------------
HOST_UNAME := $(shell uname -s 2>/dev/null)
HOST_ARCH := $(shell uname -m 2>/dev/null)
ifeq ($(HOST_ARCH),i686)
  HOST_ARCH := i386
endif
ifeq ($(HOST_ARCH),amd64)
  HOST_ARCH := x86_64
endif

ifeq ($(PLATFORM),)
  ifeq ($(HOST_UNAME),Linux)
    PLATFORM := linux
  else ifeq ($(HOST_UNAME),Darwin)
    PLATFORM := macos
  else ifneq (,$(findstring MINGW,$(HOST_UNAME)))
    PLATFORM := windows
  else
    $(error Unsupported host platform: $(HOST_UNAME). Set PLATFORM=linux|windows|macos.)
  endif
endif

# Validate ARCH per platform (Linux/Windows: x86_64 or i386; macOS: x86_64, arm64, universal).
ifneq ($(words $(ARCH)),1)
  $(error ARCH must be a single architecture: x86_64 or i386 (or arm64/universal for macos))
endif
ifeq ($(PLATFORM),macos)
  ifeq ($(filter $(ARCH),x86_64 arm64 universal),)
    $(error Unsupported ARCH '$(ARCH)' for PLATFORM=macos; supported: x86_64, arm64, universal)
  endif
else
  ifeq ($(filter $(ARCH),x86_64 i386),)
    $(error Unsupported ARCH '$(ARCH)'; supported: x86_64, i386)
  endif
endif

CROSS_PERMISSIVE_FLAGS := -fpermissive -Wno-unknown-warning-option -Wno-error \
  -Wno-error=int-conversion -Wno-error=incompatible-pointer-types \
  -Wno-error=date-time -Wno-error=narrowing

ifeq ($(PLATFORM),linux)
  SHLIB_FLAG := -shared
  LDLIBS := -lm -ldl
  GAME_MOD_NAME := qagame.mp.$(ARCH).so
  CGAME_MOD_NAME := cgame.mp.$(ARCH).so
  UI_MOD_NAME := ui.mp.$(ARCH).so
  ifeq ($(HOST_UNAME)-$(HOST_ARCH),Linux-$(ARCH))
    COMMON_FLAGS := -O2 -g -fPIC -fcommon -fno-strict-aliasing -D_GNU_SOURCE -DLUA_USE_POSIX
    CROSS :=
  else
    # Portable glibc 2.17 baseline + static libc++ so 32-bit/cross Linux
    # servers (e.g. LinuxGSM i386 etlded) need only libc6, not libstdc++.
    ifeq ($(ARCH),i386)
      CROSS_TARGET := x86-linux-gnu.2.17
    else
      CROSS_TARGET := x86_64-linux-gnu.2.17
    endif
    CROSS := $(ZIG) c++ -target $(CROSS_TARGET)
    COMMON_FLAGS := -O2 -fPIC -fcommon -fno-strict-aliasing -D_GNU_SOURCE -DLUA_USE_POSIX $(CROSS_PERMISSIVE_FLAGS)
  endif
else ifeq ($(PLATFORM),macos)
  SHLIB_FLAG := -dynamiclib
  GAME_MOD_NAME := qagame_mac
  CGAME_MOD_NAME := cgame_mac
  UI_MOD_NAME := ui_mac
  ifeq ($(ARCH),universal)
    CROSS :=
    COMMON_FLAGS :=
    LDLIBS :=
  else ifeq ($(HOST_UNAME),Darwin)
    LDLIBS := -lm
    COMMON_FLAGS := -O2 -fPIC -fno-strict-aliasing -DLUA_USE_POSIX
    CROSS :=
  else
    ifeq ($(ARCH),arm64)
      CROSS_TARGET := aarch64-macos
    else
      CROSS_TARGET := x86_64-macos
    endif
    CROSS := $(ZIG) c++ -target $(CROSS_TARGET)
    LDLIBS :=
    COMMON_FLAGS := -O2 -fPIC -fno-strict-aliasing -DLUA_USE_POSIX $(CROSS_PERMISSIVE_FLAGS)
  endif
else ifeq ($(PLATFORM),windows)
  SHLIB_FLAG := -shared
  LDLIBS :=
  ifeq ($(ARCH),i386)
    WIN_ARCH := x86
    CROSS_TARGET := x86-windows-gnu
  else
    WIN_ARCH := x64
    CROSS_TARGET := x86_64-windows-gnu
  endif
  GAME_MOD_NAME := qagame_mp_$(WIN_ARCH).dll
  CGAME_MOD_NAME := cgame_mp_$(WIN_ARCH).dll
  UI_MOD_NAME := ui_mp_$(WIN_ARCH).dll
  ifneq (,$(findstring MINGW,$(HOST_UNAME)))
    COMMON_FLAGS := -O2 -fPIC -fcommon -fno-strict-aliasing
    CROSS :=
  else
    CROSS := $(ZIG) c++ -target $(CROSS_TARGET)
    COMMON_FLAGS := -O2 -fPIC -fcommon -fno-strict-aliasing $(CROSS_PERMISSIVE_FLAGS)
  endif
else
  $(error Unsupported PLATFORM: $(PLATFORM). Use linux, windows, or macos.)
endif

# Use the cross-compiler driver when cross-compiling; auto-bootstrap zig if needed.
ifneq ($(CROSS),)
  CC  := $(CROSS)
  CXX := $(CROSS)
  ifeq ($(ZIG),$(ZIG_CACHE_BIN))
    ZIG_BOOTSTRAP_DEP := $(ZIG_CACHE_BIN)
  endif
endif

OUT := build/obj-$(PLATFORM)-$(ARCH)-lua$(WITH_LUA)-bot$(WITH_OMNIBOT)-uid$(WITH_AUTO_GUID)
MODDIR := build/overzealous
# Keep linked variants separate; switching options must not reuse stale modules.
VARIANT_DIR := build/variant-$(PLATFORM)-$(ARCH)-lua$(WITH_LUA)-bot$(WITH_OMNIBOT)-uid$(WITH_AUTO_GUID)

CFLAGS ?= -std=gnu11
CXXFLAGS ?= -std=gnu++11 -fpermissive -Wno-write-strings -Wno-return-type
CPPFLAGS += -Isrc/game -Isrc/lua

GAME_SRC := src/game/bg_animation.c \
    src/game/bg_animgroup.c \
    src/game/bg_campaign.c \
    src/game/bg_character.c \
    src/game/bg_classes.c \
    src/game/bg_misc.c \
    src/game/bg_pmove.c \
    src/game/bg_slidemove.c \
    src/game/bg_sscript.c \
    src/game/bg_stats.c \
    src/game/bg_tracemap.c \
    src/game/bg_weapons.c \
    src/game/bg_profiler_hook.c \
    src/game/etpro_mdx.c \
    src/game/et-antiwarp.c \
    src/game/g_active.c \
    src/game/g_alarm.c \
    src/game/g_antilag.c \
    src/game/g_buddy_list.c \
    src/game/g_character.c \
    src/game/g_client.c \
    src/game/g_cmds.c \
    src/game/g_cmds_ext.c \
    src/game/g_combat.c \
    src/game/g_config.c \
    src/game/g_fireteams.c \
    src/game/g_items.c \
    src/game/g_main.c \
    src/game/g_newmod.c \
    src/game/g_match.c \
    src/game/g_match_tokens.c \
    src/game/g_mem.c \
    src/game/g_misc.c \
    src/game/g_missile.c \
    src/game/g_mover.c \
    src/game/g_multiview.c \
    src/game/g_osfile.c \
    src/game/g_props.c \
    src/game/g_referee.c \
    src/game/g_script.c \
    src/game/g_script_actions.c \
    src/game/g_session.c \
    src/game/g_shrubbot.c \
    src/game/g_spawn.c \
    src/game/g_stats.c \
    src/game/g_strparse.c \
    src/game/g_svcmds.c \
    src/game/g_syscalls.c \
    src/game/g_systemmsg.c \
    src/game/g_target.c \
    src/game/g_team.c \
    src/game/g_teammapdata.c \
    src/game/g_time.c \
    src/game/g_trigger.c \
    src/game/g_utils.c \
    src/game/g_vote.c \
    src/game/g_weapon.c \
    src/game/q_math.c \
    src/game/q_shared.c \
    src/game/g_xpsave.c \
    src/game/g_crash.c \
    src/game/geoip.c
CGAME_SRC := src/game/bg_animation.c \
    src/game/bg_animgroup.c \
    src/game/bg_campaign.c \
    src/game/bg_character.c \
    src/game/bg_classes.c \
    src/game/bg_misc.c \
    src/game/bg_pmove.c \
    src/game/bg_slidemove.c \
    src/game/bg_sscript.c \
    src/game/bg_stats.c \
    src/game/bg_tracemap.c \
    src/game/bg_weapons.c \
    src/game/g_match_tokens.c \
    src/game/g_strparse.c \
    src/cgame/cg_atmospheric.c \
    src/cgame/cg_character.c \
    src/cgame/cg_commandmap.c \
    src/cgame/cg_consolecmds.c \
    src/cgame/cg_crash.c \
    src/cgame/cg_debriefing.c \
    src/cgame/cg_draw.c \
    src/cgame/cg_drawtools.c \
    src/cgame/cg_effects.c \
    src/cgame/cg_ents.c \
    src/cgame/cg_event.c \
    src/cgame/cg_fireteamoverlay.c \
    src/cgame/cg_fireteams.c \
    src/cgame/cg_flamethrower.c \
    src/cgame/cg_info.c \
    src/cgame/cg_limbopanel.c \
    src/cgame/cg_loadpanel.c \
    src/cgame/cg_localents.c \
    src/cgame/cg_main.c \
    src/cgame/cg_marks.c \
    src/cgame/cg_missionbriefing.c \
    src/cgame/cg_missionmessages.c \
    src/cgame/cg_multiview.c \
    src/cgame/cg_newDraw.c \
    src/cgame/cg_particles.c \
    src/cgame/cg_players.c \
    src/cgame/cg_playerstate.c \
    src/cgame/cg_polybus.c \
    src/cgame/cg_popupmessages.c \
    src/cgame/cg_predict.c \
    src/cgame/cg_scoreboard.c \
    src/cgame/cg_servercmds.c \
    src/cgame/cg_snapshot.c \
    src/cgame/cg_sound.c \
    src/cgame/cg_spawn.c \
    src/cgame/cg_statsranksmedals.c \
    src/cgame/cg_syscalls.c \
    src/cgame/cg_trails.c \
    src/cgame/cg_view.c \
    src/cgame/cg_weapons.c \
    src/cgame/cg_window.c \
    src/game/q_math.c \
    src/game/q_shared.c \
    src/ui/ui_shared.c
UI_SRC := src/game/bg_campaign.c \
    src/game/bg_classes.c \
    src/game/bg_misc.c \
    src/game/q_math.c \
    src/game/q_shared.c \
    src/ui/ui_atoms.c \
    src/ui/ui_gameinfo.c \
    src/ui/ui_loadpanel.c \
    src/ui/ui_main.c \
    src/ui/ui_shared.c \
    src/ui/ui_syscalls.c

ifeq ($(WITH_LUA),1)
  GAME_DEFS += -DET_LUA
  GAME_SRC += src/game/g_lua.c src/game/g_sha1.c src/sha-1/sha1.c
  LUA_SRC := $(filter-out src/lua/lua.c src/lua/luac.c src/lua/print.c,$(wildcard src/lua/*.c))
  GAME_SRC += $(LUA_SRC)
endif
ifeq ($(WITH_OMNIBOT),1)
  GAME_DEFS += -DOMNIBOTS
  GAME_SRC += src/game/g_etbot_interface.cpp Omnibot/Common/BotLoadLibrary.cpp
endif
ifeq ($(WITH_AUTO_GUID),1)
  GAME_DEFS += -DAUTO_GUID
  CGAME_DEFS += -DAUTO_GUID
  CGAME_SRC += src/cgame/cg_uid.c src/cgame/md5.c
endif

obj = $(addprefix $(OUT)/$(1)/,$(addsuffix .o,$(basename $(2))))
GAME_OBJ := $(call obj,game,$(GAME_SRC))
CGAME_OBJ := $(call obj,cgame,$(CGAME_SRC))
UI_OBJ := $(call obj,ui,$(UI_SRC))
MODULES := $(VARIANT_DIR)/$(GAME_MOD_NAME) $(VARIANT_DIR)/$(CGAME_MOD_NAME) $(VARIANT_DIR)/$(UI_MOD_NAME)

.PHONY: all clean test test-matrix pk3 pk3-all server-dist
all: $(MODULES)
	@mkdir -p $(MODDIR)
	@cp -f $(MODULES) $(MODDIR)/

$(ZIG_CACHE_BIN):
	@echo "Bootstrapping zig cross-compiler into .cache/zig-pip..."
	@$(PYTHON) -m pip install --quiet --target $(CURDIR)/.cache/zig-pip ziglang

SHLIB_LINK_FLAGS :=

ifeq ($(PLATFORM)-$(ARCH),macos-universal)
MAC_X64_DIR := build/variant-macos-x86_64-lua$(WITH_LUA)-bot$(WITH_OMNIBOT)-uid$(WITH_AUTO_GUID)
MAC_ARM64_DIR := build/variant-macos-arm64-lua$(WITH_LUA)-bot$(WITH_OMNIBOT)-uid$(WITH_AUTO_GUID)
.PHONY: universal-build
$(VARIANT_DIR)/.universal.stamp: universal-build
	$(MAKE) --no-print-directory all PLATFORM=macos ARCH=x86_64 WITH_LUA="$(WITH_LUA)" WITH_OMNIBOT="$(WITH_OMNIBOT)" WITH_AUTO_GUID="$(WITH_AUTO_GUID)"
	$(MAKE) --no-print-directory all PLATFORM=macos ARCH=arm64 WITH_LUA="$(WITH_LUA)" WITH_OMNIBOT="$(WITH_OMNIBOT)" WITH_AUTO_GUID="$(WITH_AUTO_GUID)"
	@mkdir -p $(VARIANT_DIR)
	@for mod in qagame_mac cgame_mac ui_mac; do \
		$(PYTHON) tools/lipo_universal.py $(VARIANT_DIR)/$$mod $(MAC_X64_DIR)/$$mod $(MAC_ARM64_DIR)/$$mod; \
	done
	@touch $@
$(MODULES): $(VARIANT_DIR)/.universal.stamp
	@:
else
$(VARIANT_DIR)/$(GAME_MOD_NAME): $(GAME_OBJ)
	@mkdir -p $(@D)
	$(CXX) $(SHLIB_FLAG) $(SHLIB_LINK_FLAGS) -o $@ $^ $(LDLIBS)
$(VARIANT_DIR)/$(CGAME_MOD_NAME): $(CGAME_OBJ)
	@mkdir -p $(@D)
	$(CXX) $(SHLIB_FLAG) $(SHLIB_LINK_FLAGS) -o $@ $^ $(LDLIBS)
$(VARIANT_DIR)/$(UI_MOD_NAME): $(UI_OBJ)
	@mkdir -p $(@D)
	$(CXX) $(SHLIB_FLAG) $(SHLIB_LINK_FLAGS) -o $@ $^ $(LDLIBS)
endif

# Overzealous, Arena.ai, 2026-11-27: force C++ mode for all sources (the mod
# is C++-compiled). zig c++ requires -x c++ for .c files; g++/clang++
# accept -x c++ without issue.
CXX_FLAGS_COMPILE := $(CPPFLAGS) $(COMMON_FLAGS) $(CXXFLAGS) -x c++

# etpro_mdx.c is pure C with void* casts that C++ rejects; compile it as C.
ifneq ($(CROSS),)
$(OUT)/game/src/game/etpro_mdx.o: src/game/etpro_mdx.c | $(ZIG_BOOTSTRAP_DEP)
	@mkdir -p $(@D)
	$(ZIG) cc -target $(CROSS_TARGET) $(CPPFLAGS) $(COMMON_FLAGS) -DGAMEDLL $(GAME_DEFS) -MMD -MP -c $< -o $@
else
$(OUT)/game/src/game/etpro_mdx.o: src/game/etpro_mdx.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) -DGAMEDLL $(GAME_DEFS) -MMD -MP -c $< -o $@
endif

$(OUT)/game/%.o: %.c | $(ZIG_BOOTSTRAP_DEP)
	@mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS_COMPILE) -DGAMEDLL $(GAME_DEFS) -MMD -MP -c $< -o $@
$(OUT)/game/%.o: %.cpp | $(ZIG_BOOTSTRAP_DEP)
	@mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS_COMPILE) -DGAMEDLL $(GAME_DEFS) -MMD -MP -c $< -o $@
$(OUT)/cgame/%.o: %.c | $(ZIG_BOOTSTRAP_DEP)
	@mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS_COMPILE) -DCGAMEDLL $(CGAME_DEFS) -MMD -MP -c $< -o $@
$(OUT)/ui/%.o: %.c | $(ZIG_BOOTSTRAP_DEP)
	@mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS_COMPILE) -DUIDLL -MMD -MP -c $< -o $@

-include $(GAME_OBJ:.o=.d) $(CGAME_OBJ:.o=.d) $(UI_OBJ:.o=.d)

clean:
	rm -rf build

# Overzealous, Arena.ai, 2026-11-27: regressions run against the selected variant.
test: all
	PLATFORM="$(PLATFORM)" ARCH="$(ARCH)" WITH_LUA="$(WITH_LUA)" WITH_OMNIBOT="$(WITH_OMNIBOT)" WITH_AUTO_GUID="$(WITH_AUTO_GUID)" CC="$(CC)" CXX="$(CXX)" ZIG="$(ZIG)" $(PYTHON) -m unittest discover -s tests -v

# Build/test all four Lua/Bot variants with the selected UID setting, then
# restore the requested combination in build/overzealous.
test-matrix:
	@set -e; for lua in 0 1; do for bot in 0 1; do \
		$(MAKE) --no-print-directory test WITH_LUA=$$lua WITH_OMNIBOT=$$bot WITH_AUTO_GUID="$(WITH_AUTO_GUID)" ARCH="$(ARCH)"; \
	done; done
	@$(MAKE) --no-print-directory all WITH_LUA="$(WITH_LUA)" WITH_OMNIBOT="$(WITH_OMNIBOT)" WITH_AUTO_GUID="$(WITH_AUTO_GUID)" ARCH="$(ARCH)"

# Pack pak/ contents and built client modules (cgame + ui) into a single-target
# pk3 archive (zip format). Dedicated and listen servers never extract qagame
# from a pk3 (ET/ET: Legacy require qagame loose in <fs_game>/), so omitting
# qagame keeps client downloads lean.
PK3 := build/overzealous-$(PLATFORM)-$(ARCH).pk3
pk3: all
	@mkdir -p build/pk3-stage
	@rm -rf $(PK3) build/pk3-stage/*
	@cp -f $(VARIANT_DIR)/$(CGAME_MOD_NAME) $(VARIANT_DIR)/$(UI_MOD_NAME) build/pk3-stage/
	@if [ "$(PLATFORM)" = "linux" ] && command -v strip >/dev/null 2>&1; then \
		strip --strip-debug build/pk3-stage/*.so 2>/dev/null || true; \
	fi
	cd pak && zip -r -q ../$(PK3) . -x '.*'
	zip -j -q $(PK3) build/pk3-stage/*
	@rm -rf build/pk3-stage
	@echo "Created $(PK3) ($$(du -h $(PK3) | cut -f1))"

# Build ALL supported platforms and architectures (Linux x86_64 + i386,
# Windows x64 + x86, macOS universal x86_64 + arm64) into one universal client
# pk3, and stage loose server qagame binaries alongside it in build/overzealous/
# because dedicated servers (etlded) load qagame directly from the mod folder
# on disk rather than extracting it from a pk3.
VARIANT_SUFFIX := lua$(WITH_LUA)-bot$(WITH_OMNIBOT)-uid$(WITH_AUTO_GUID)
PK3_ALL := build/overzealous-all.pk3
pk3-all:
	@mkdir -p build/pk3-stage $(MODDIR)
	@rm -rf $(PK3_ALL) build/pk3-stage/*
	$(MAKE) --no-print-directory all PLATFORM=linux ARCH=x86_64
	$(MAKE) --no-print-directory all PLATFORM=linux ARCH=i386
	$(MAKE) --no-print-directory all PLATFORM=windows ARCH=x86_64
	$(MAKE) --no-print-directory all PLATFORM=windows ARCH=i386
	$(MAKE) --no-print-directory all PLATFORM=macos ARCH=universal
	@cp -f build/variant-linux-x86_64-$(VARIANT_SUFFIX)/cgame.mp.x86_64.so build/variant-linux-x86_64-$(VARIANT_SUFFIX)/ui.mp.x86_64.so build/pk3-stage/
	@if command -v strip >/dev/null 2>&1; then \
		strip --strip-debug build/pk3-stage/*.so 2>/dev/null || true; \
	fi
	@cp -f build/variant-linux-i386-$(VARIANT_SUFFIX)/cgame.mp.i386.so build/variant-linux-i386-$(VARIANT_SUFFIX)/ui.mp.i386.so build/pk3-stage/
	@cp -f build/variant-windows-x86_64-$(VARIANT_SUFFIX)/cgame_mp_x64.dll build/variant-windows-x86_64-$(VARIANT_SUFFIX)/ui_mp_x64.dll build/pk3-stage/
	@cp -f build/variant-windows-i386-$(VARIANT_SUFFIX)/cgame_mp_x86.dll build/variant-windows-i386-$(VARIANT_SUFFIX)/ui_mp_x86.dll build/pk3-stage/
	@cp -f build/variant-macos-universal-$(VARIANT_SUFFIX)/cgame_mac build/variant-macos-universal-$(VARIANT_SUFFIX)/ui_mac build/pk3-stage/
	cd pak && zip -r -q ../$(PK3_ALL) . -x '.*'
	zip -j -q $(PK3_ALL) build/pk3-stage/*
	@rm -rf build/pk3-stage
	@cp -f build/variant-linux-x86_64-$(VARIANT_SUFFIX)/*.so $(MODDIR)/
	@cp -f build/variant-linux-i386-$(VARIANT_SUFFIX)/*.so $(MODDIR)/
	@cp -f build/variant-windows-x86_64-$(VARIANT_SUFFIX)/*.dll $(MODDIR)/
	@cp -f build/variant-windows-i386-$(VARIANT_SUFFIX)/*.dll $(MODDIR)/
	@cp -f build/variant-macos-universal-$(VARIANT_SUFFIX)/*_mac $(MODDIR)/
	@cp -f $(PK3_ALL) $(MODDIR)/
	@echo "Created $(PK3_ALL) ($$(du -h $(PK3_ALL) | cut -f1))"
	@echo "Server-ready files staged in $(MODDIR)/: overzealous-all.pk3, qagame.mp.i386.so, qagame.mp.x86_64.so"

# Create a turnkey dedicated-server tarball containing overzealous-all.pk3 +
# both 32-bit (qagame.mp.i386.so) and 64-bit (qagame.mp.x86_64.so) server VMs.
SERVER_BUNDLE := build/overzealous-server-linux.tar.gz
server-dist: pk3-all
	@rm -f $(SERVER_BUNDLE)
	tar -C $(MODDIR) -czf $(SERVER_BUNDLE) overzealous-all.pk3 qagame.mp.i386.so qagame.mp.x86_64.so
	@echo "Created $(SERVER_BUNDLE) ($$(du -h $(SERVER_BUNDLE) | cut -f1))"

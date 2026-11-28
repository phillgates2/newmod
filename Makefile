# Overzealous native ET: Legacy modules (Linux, Windows, macOS)
CC ?= cc
CXX ?= c++
PYTHON ?= python3
WITH_LUA ?= 1
WITH_OMNIBOT ?= 1
# Inherited, spoofable key fallback; experimental and disabled by default.
WITH_AUTO_GUID ?= 0
ARCH ?= x86_64
# Cross-compilation: set PLATFORM=windows or PLATFORM=macos to cross-compile
# from Linux using zig. When empty, auto-detect from the host OS.
PLATFORM ?=
ZIG ?= /tmp/zig-extract/ziglang/zig
# Overzealous, Arena.ai, 2026-11-27: reject empty/multiple values and avoid
# silently labeling a native build as an unsupported architecture.
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
ifneq ($(ARCH),x86_64)
  $(error only ARCH=x86_64 is currently supported)
endif

# --- Platform detection / cross-compilation setup ----------------------------
# Overzealous, Arena.ai, 2026-11-27: if PLATFORM is not set, auto-detect from
# the host OS. When PLATFORM is set explicitly (e.g. PLATFORM=windows), use
# zig as a cross-compiler.
ifeq ($(PLATFORM),)
  UNAME_S := $(shell uname -s 2>/dev/null)
  ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
  else ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
  else ifneq (,$(findstring MINGW,$(UNAME_S)))
    PLATFORM := windows
  else
    $(error Unsupported host platform: $(UNAME_S). Set PLATFORM=linux|windows|macos.)
  endif
endif

ifeq ($(PLATFORM),linux)
  SHLIB_FLAG := -shared
  SHLIB_EXT := so
  LDLIBS := -lm -ldl
  COMMON_FLAGS := -O2 -g -fPIC -fcommon -fno-strict-aliasing -D_GNU_SOURCE -DLUA_USE_POSIX
  CROSS :=
else ifeq ($(PLATFORM),macos)
  SHLIB_FLAG := -dynamiclib
  SHLIB_EXT := dylib
  LDLIBS := -lm
  COMMON_FLAGS := -O2 -g -fPIC -fno-strict-aliasing -DLUA_USE_POSIX
  ifeq ($(HOST_UNAME),)
    HOST_UNAME := $(shell uname -s)
  endif
  ifeq ($(HOST_UNAME),Linux)
    # Cross-compile from Linux: use zig with macOS SDK
    CROSS_TARGET := x86_64-macos
    CROSS := $(ZIG) c++ -target $(CROSS_TARGET)
    SHLIB_FLAG := -dynamiclib
    # zig provides libSystem and libm automatically
    LDLIBS :=
    # clang needs permissive flags for g++-style enum conversions
    COMMON_FLAGS += -fpermissive -Wno-error -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=int-to-enum-conversion -Wno-error=date-time -Wno-error=narrowing
  else
    CROSS :=
  endif
else ifeq ($(PLATFORM),windows)
  SHLIB_FLAG := -shared
  SHLIB_EXT := dll
  LDLIBS :=
  COMMON_FLAGS := -O2 -g -fPIC -fcommon -fno-strict-aliasing
  ifeq ($(HOST_UNAME),)
    HOST_UNAME := $(shell uname -s)
  endif
  ifeq ($(HOST_UNAME),Linux)
    # Cross-compile from Linux: use zig with Windows target
    CROSS_TARGET := x86_64-windows
    CROSS := $(ZIG) c++ -target $(CROSS_TARGET)
    # clang needs permissive flags for g++-style enum conversions
    COMMON_FLAGS += -fpermissive -Wno-error -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=int-to-enum-conversion -Wno-error=date-time -Wno-error=narrowing
  else
    CROSS :=
  endif
else
  $(error Unsupported PLATFORM: $(PLATFORM). Use linux, windows, or macos.)
endif

# Use the cross-compiler driver when cross-compiling
ifneq ($(CROSS),)
  CC  := $(CROSS)
  CXX := $(CROSS)
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
MODULES := $(VARIANT_DIR)/qagame.mp.$(ARCH).$(SHLIB_EXT) $(VARIANT_DIR)/cgame.mp.$(ARCH).$(SHLIB_EXT) $(VARIANT_DIR)/ui.mp.$(ARCH).$(SHLIB_EXT)

.PHONY: all clean test test-matrix pk3
all: $(MODULES)
	@mkdir -p $(MODDIR)
	@cp -f $(MODULES) $(MODDIR)/

ifneq ($(CROSS),)
  # zig's lld-link errors on undefined symbols that the engine provides at
  # runtime (client-side rendering functions). Allow them for DLLs.
  SHLIB_LINK_FLAGS := -Wl,--allow-shlib-undefined
else
  SHLIB_LINK_FLAGS :=
endif

$(VARIANT_DIR)/qagame.mp.$(ARCH).$(SHLIB_EXT): $(GAME_OBJ)
	@mkdir -p $(@D)
	$(CXX) $(SHLIB_FLAG) $(SHLIB_LINK_FLAGS) -o $@ $^ $(LDLIBS)
$(VARIANT_DIR)/cgame.mp.$(ARCH).$(SHLIB_EXT): $(CGAME_OBJ)
	@mkdir -p $(@D)
	$(CXX) $(SHLIB_FLAG) $(SHLIB_LINK_FLAGS) -o $@ $^ $(LDLIBS)
$(VARIANT_DIR)/ui.mp.$(ARCH).$(SHLIB_EXT): $(UI_OBJ)
	@mkdir -p $(@D)
	$(CXX) $(SHLIB_FLAG) $(SHLIB_LINK_FLAGS) -o $@ $^ $(LDLIBS)

# Overzealous, Arena.ai, 2026-11-27: force C++ mode for all sources (the mod
# is C++-compiled). zig c++ requires -x c++ for .c files; g++/clang++
# accept -x c++ without issue.
CXX_FLAGS_COMPILE := $(CPPFLAGS) $(COMMON_FLAGS) $(CXXFLAGS) -x c++

# etpro_mdx.c is pure C with void* casts that C++ rejects; compile it as C.
ifneq ($(CROSS),)
$(OUT)/game/src/game/etpro_mdx.o: src/game/etpro_mdx.c
	@mkdir -p $(@D)
	$(ZIG) cc -target $(CROSS_TARGET) $(CPPFLAGS) $(COMMON_FLAGS) -DGAMEDLL $(GAME_DEFS) -MMD -MP -c $< -o $@
else
$(OUT)/game/src/game/etpro_mdx.o: src/game/etpro_mdx.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(COMMON_FLAGS) -DGAMEDLL $(GAME_DEFS) -MMD -MP -c $< -o $@
endif

$(OUT)/game/%.o: %.c
	@mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS_COMPILE) -DGAMEDLL $(GAME_DEFS) -MMD -MP -c $< -o $@
$(OUT)/game/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS_COMPILE) -DGAMEDLL $(GAME_DEFS) -MMD -MP -c $< -o $@
$(OUT)/cgame/%.o: %.c
	@mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS_COMPILE) -DCGAMEDLL $(CGAME_DEFS) -MMD -MP -c $< -o $@
$(OUT)/ui/%.o: %.c
	@mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS_COMPILE) -DUIDLL -MMD -MP -c $< -o $@

-include $(GAME_OBJ:.o=.d) $(CGAME_OBJ:.o=.d) $(UI_OBJ:.o=.d)

clean:
	rm -rf build

# Overzealous, Arena.ai, 2026-11-27: regressions run against the selected variant.
test: all
	PLATFORM="$(PLATFORM)" ARCH="$(ARCH)" WITH_LUA="$(WITH_LUA)" WITH_OMNIBOT="$(WITH_OMNIBOT)" WITH_AUTO_GUID="$(WITH_AUTO_GUID)" CC="$(CC)" CXX="$(CXX)" $(PYTHON) -m unittest discover -s tests -v

# Build/test all four Lua/Bot variants with the selected UID setting, then
# restore the requested combination in build/overzealous.
test-matrix:
	@set -e; for lua in 0 1; do for bot in 0 1; do \
		$(MAKE) --no-print-directory test WITH_LUA=$$lua WITH_OMNIBOT=$$bot WITH_AUTO_GUID="$(WITH_AUTO_GUID)" ARCH="$(ARCH)"; \
	done; done
	@$(MAKE) --no-print-directory all WITH_LUA="$(WITH_LUA)" WITH_OMNIBOT="$(WITH_OMNIBOT)" WITH_AUTO_GUID="$(WITH_AUTO_GUID)" ARCH="$(ARCH)"

# Pack pak/ contents and built modules into a pk3 archive (zip format)
PK3 := build/overzealous-$(PLATFORM)-$(ARCH).pk3
pk3: all
	@mkdir -p build
	@rm -f $(PK3)
	cd pak && zip -r -q ../$(PK3) . -x '.*'
	zip -j -q $(PK3) $(VARIANT_DIR)/*.dll $(VARIANT_DIR)/*.so $(VARIANT_DIR)/*.dylib $(VARIANT_DIR)/*.bundle 2>/dev/null || true
	@echo "Created $(PK3) ($$(du -h $(PK3) | cut -f1))"

# Build all three platforms and pack everything into a single pk3.
# The engine picks the correct module (.so / .dll / .dylib) per platform.
PK3_ALL := build/overzealous-all-$(ARCH).pk3
pk3-all:
	@mkdir -p build
	@rm -f $(PK3_ALL)
	$(MAKE) --no-print-directory all
	cd pak && zip -r -q ../$(PK3_ALL) . -x '.*'
	$(MAKE) --no-print-directory all PLATFORM=windows
	zip -j -q $(PK3_ALL) build/variant-windows-$(ARCH)-lua$(WITH_LUA)-bot$(WITH_OMNIBOT)-uid$(WITH_AUTO_GUID)/*.dll
	$(MAKE) --no-print-directory all PLATFORM=macos
	zip -j -q $(PK3_ALL) build/variant-macos-$(ARCH)-lua$(WITH_LUA)-bot$(WITH_OMNIBOT)-uid$(WITH_AUTO_GUID)/*.dylib
	zip -j -q $(PK3_ALL) build/variant-linux-$(ARCH)-lua$(WITH_LUA)-bot$(WITH_OMNIBOT)-uid$(WITH_AUTO_GUID)/*.so
	@echo "Created $(PK3_ALL) ($$(du -h $(PK3_ALL) | cut -f1))"

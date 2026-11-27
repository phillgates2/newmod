# NewMod native ET: Legacy modules (Linux x86_64)
CC ?= cc
CXX ?= c++
PYTHON ?= python3
WITH_LUA ?= 1
WITH_OMNIBOT ?= 1
# Inherited, spoofable key fallback; experimental and disabled by default.
WITH_AUTO_GUID ?= 0
ARCH ?= x86_64
# NewMod, Arena.ai, 2026-11-21: reject empty/multiple values and avoid
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
OUT := build/obj-$(ARCH)-lua$(WITH_LUA)-bot$(WITH_OMNIBOT)-uid$(WITH_AUTO_GUID)
MODDIR := build/newmod
# Keep linked variants separate; switching options must not reuse stale modules.
VARIANT_DIR := build/variant-$(ARCH)-lua$(WITH_LUA)-bot$(WITH_OMNIBOT)-uid$(WITH_AUTO_GUID)
COMMON_FLAGS := -O2 -g -fPIC -fcommon -fno-strict-aliasing -D_GNU_SOURCE -DLUA_USE_POSIX
CFLAGS ?= -std=gnu11
CXXFLAGS ?= -std=gnu++11 -fpermissive -Wno-write-strings -Wno-return-type
CPPFLAGS += -Isrc/game -Isrc/lua
LDLIBS := -lm -ldl

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
MODULES := $(VARIANT_DIR)/qagame.mp.$(ARCH).so $(VARIANT_DIR)/cgame.mp.$(ARCH).so $(VARIANT_DIR)/ui.mp.$(ARCH).so

.PHONY: all clean test test-matrix
all: $(MODULES)
	@mkdir -p $(MODDIR)
	@cp -f $(MODULES) $(MODDIR)/

$(VARIANT_DIR)/qagame.mp.$(ARCH).so: $(GAME_OBJ)
	@mkdir -p $(@D)
	$(CXX) -shared -o $@ $^ $(LDLIBS)
$(VARIANT_DIR)/cgame.mp.$(ARCH).so: $(CGAME_OBJ)
	@mkdir -p $(@D)
	$(CXX) -shared -o $@ $^ $(LDLIBS)
$(VARIANT_DIR)/ui.mp.$(ARCH).so: $(UI_OBJ)
	@mkdir -p $(@D)
	$(CXX) -shared -o $@ $^ $(LDLIBS)

$(OUT)/game/%.o: %.c
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(COMMON_FLAGS) $(CXXFLAGS) -DGAMEDLL $(GAME_DEFS) -MMD -MP -c $< -o $@
$(OUT)/game/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(COMMON_FLAGS) $(CXXFLAGS) -DGAMEDLL $(GAME_DEFS) -MMD -MP -c $< -o $@
$(OUT)/cgame/%.o: %.c
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(COMMON_FLAGS) $(CXXFLAGS) -DCGAMEDLL $(CGAME_DEFS) -MMD -MP -c $< -o $@
$(OUT)/ui/%.o: %.c
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(COMMON_FLAGS) $(CXXFLAGS) -DUIDLL -MMD -MP -c $< -o $@

-include $(GAME_OBJ:.o=.d) $(CGAME_OBJ:.o=.d) $(UI_OBJ:.o=.d)

clean:
	rm -rf build

# NewMod, Arena.ai, 2026-11-21: regressions run against the selected variant.
test: all
	ARCH="$(ARCH)" WITH_LUA="$(WITH_LUA)" WITH_OMNIBOT="$(WITH_OMNIBOT)" WITH_AUTO_GUID="$(WITH_AUTO_GUID)" CC="$(CC)" CXX="$(CXX)" $(PYTHON) -m unittest discover -s tests -v

# Build/test all four Lua/Bot variants with the selected UID setting, then
# restore the requested combination in build/newmod.
test-matrix:
	@set -e; for lua in 0 1; do for bot in 0 1; do \
		$(MAKE) --no-print-directory test WITH_LUA=$$lua WITH_OMNIBOT=$$bot WITH_AUTO_GUID="$(WITH_AUTO_GUID)" ARCH="$(ARCH)"; \
	done; done
	@$(MAKE) --no-print-directory all WITH_LUA="$(WITH_LUA)" WITH_OMNIBOT="$(WITH_OMNIBOT)" WITH_AUTO_GUID="$(WITH_AUTO_GUID)" ARCH="$(ARCH)"

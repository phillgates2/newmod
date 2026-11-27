/* NewMod, Arena.ai, 2026-11-21: synthetic Omni-Bot ABI fixture for the
 * optional dedicated-server smoke test. This is not a bot implementation and
 * must not be installed or loaded on a real server. */
#include "Omnibot/Common/BotExports.h"
#include "Omnibot/Common/Base_Messages.h"
#include "Omnibot/ET/ET_Config.h"
#include "Omnibot/ET/ET_Messages.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static IEngineInterface *engine;

static omnibot_error init(IEngineInterface *interface, int version) {
    (void)version;
    engine = interface;
    engine->PrintMessage("FIXTURE_BOT_INIT");
    return BOT_ERROR_NONE;
}
static void update() {}
static void shutdown() { engine->PrintMessage("FIXTURE_BOT_SHUTDOWN"); }

static void console(const Arguments &args) {
    if (args.m_NumArgs < 2) return;
    if (!std::strcmp(args.m_Args[1], "fixtureadd") && args.m_NumArgs >= 3) {
        Msg_Addbot bot;
        std::snprintf(bot.m_Name, sizeof(bot.m_Name), "%s", args.m_Args[2]);
        int id = engine->AddBot(MessageHelper(GEN_MSG_ADDBOT, &bot, sizeof(bot)));
        char text[128];
        std::snprintf(text, sizeof(text), "FIXTURE_ADD %s %d", bot.m_Name, id);
        engine->PrintMessage(text);
        if (id >= 0) {
            /* ChangeTeam drives the game module's ClientBegin for these
             * synthetic server-side clients; AddBot alone does not. */
            obResult result = engine->ChangeTeam(id, id == 2 ? ET_TEAM_ALLIES : ET_TEAM_AXIS, 0);
            std::snprintf(text, sizeof(text), "FIXTURE_TEAM %d %d", id, (int)result);
            engine->PrintMessage(text);
        }
    } else if (!std::strcmp(args.m_Args[1], "fixtureclient") && args.m_NumArgs >= 4) {
        int id = std::atoi(args.m_Args[2]);
        /* The ET: Legacy bot syscall invokes the regular client-command
         * dispatcher; these are still synthetic bots, not network clients. */
        engine->BotCommand(id, args.m_Args[3]);
        char text[128];
        std::snprintf(text, sizeof(text), "FIXTURE_CLIENT_COMMAND %d %.80s", id, args.m_Args[3]);
        engine->PrintMessage(text);
    } else if (!std::strcmp(args.m_Args[1], "fixturelimbo") && args.m_NumArgs >= 3) {
        int id = std::atoi(args.m_Args[2]);
        ET_GoLimbo message = { False };
        obResult result = engine->InterfaceSendMessage(
            MessageHelper(ET_MSG_GOTOLIMBO, &message, sizeof(message)), engine->EntityFromID(id));
        char text[128];
        std::snprintf(text, sizeof(text), "FIXTURE_LIMBO %d %d %d", id, (int)result, (int)message.m_GoLimbo);
        engine->PrintMessage(text);
    } else if (!std::strcmp(args.m_Args[1], "fixtureremove") && args.m_NumArgs >= 3) {
        Msg_Kickbot bot;
        bot.m_GameId = std::atoi(args.m_Args[2]);
        engine->PrintMessage("FIXTURE_REMOVING");
        engine->RemoveBot(MessageHelper(GEN_MSG_KICKBOT, &bot, sizeof(bot)));
    }
}

static void trigger(const TriggerInfo &) {}
static void blackboard(BlackBoard_Key, int, int, obUserData *) {}
static void event(int, const MessageHelper &) {}
static void global(const MessageHelper &) {}
static void goal(const MapGoalDef &) {}
static void delgoal(const char *) {}
static void entity(GameEntity, GameEntity) {}

extern "C" eomnibot_error ExportBotFunctionsFromDLL(Bot_EngineFuncs_t *functions, int size) {
    if (!functions || size != sizeof(*functions)) return BOT_ERROR_BAD_INTERFACE;
    std::memset(functions, 0, sizeof(*functions));
    functions->pfnInitialize = init;
    functions->pfnUpdate = update;
    functions->pfnShutdown = shutdown;
    functions->pfnConsoleCommand = console;
    functions->pfnSendTrigger = trigger;
    functions->pfnAddBlackboardRecord = blackboard;
    functions->pfnSendEvent = event;
    functions->pfnSendGlobalEvent = global;
    functions->pfnAddGoal = goal;
    functions->pfnDeleteGoal = delgoal;
    functions->pfnUpdateEntity = entity;
    return BOT_ERROR_NONE;
}

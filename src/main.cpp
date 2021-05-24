// ModInfo
#include "modloader/shared/modloader.hpp"
// Logger & logging
#include "beatsaber-hook/shared/utils/logging.hpp"

// runmethod & findmethod, as well as field values
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"

// access to our config variable, plus saving and loading the config
#include "config.hpp"

// registering our settings view needs it included
#include "SpaceMonkeSettingsView.hpp"
#include "SpaceMonkeWatchView.hpp"

#include "custom-types/shared/register.hpp"
#include "monkecomputer/shared/Register.hpp"
#include "monkecomputer/shared/GorillaUI.hpp"
#include "monkecomputer/shared/CustomQueues.hpp"

// we need vector3
#include "UnityEngine/Vector3.hpp"

// only use `using namespace` in .cpp files, never in headers!
using namespace UnityEngine;

// our modinfo, used for making the logger
static ModInfo modInfo;

// some defines to shorten what we have to type for logging
#define INFO(value...) getLogger().info(value)
#define ERROR(value...) getLogger().error(value)

// making and getting a logger, this makes it so we can log to logcat in a standardized way
Logger& getLogger()
{
    static Logger* logger = new Logger(modInfo, LoggerOptions(false, true));
    return *logger;
}

// some bools to keep track whether things should be done
bool allowSpaceMonke = false;
bool resetSpeed = false;

// store where the player started
Vector3 startPos;

// this is how to make a method hook, it follows the convention hookname, returntype, if an instance method, a reference to self, and then the args
MAKE_HOOK_OFFSETLESS(Player_Awake, void, Il2CppObject* self)
{
    // telling us in the log that this method was called
    INFO("Player Awake Hook Called!");

    // getting the field value of jump multiplier on the player
    //                                                    return type V     V the object we want to check
    //                                                                V     V          V the field name
    float jumpMultiplier = CRASH_UNLESS(il2cpp_utils::GetFieldValue<float>(self, "jumpMultiplier"));
    float velocityLimit = CRASH_UNLESS(il2cpp_utils::GetFieldValue<float>(self, "velocityLimit"));
    float maxJumpSpeed = CRASH_UNLESS(il2cpp_utils::GetFieldValue<float>(self, "maxJumpSpeed"));
    
    // logging it so we know what it was
    INFO("jumpMultiplier was: %.2f\nVelocityLimit was: %.2f\nMaxJumpSpeed was: %.2f", jumpMultiplier, velocityLimit, maxJumpSpeed);
    
    // we also want to store the place where the player started, so we can return to that when the player jumps out of bounds
    Il2CppObject* transform = CRASH_UNLESS(il2cpp_utils::RunMethod(self, "get_transform"));
    startPos = CRASH_UNLESS(il2cpp_utils::RunMethod<Vector3>(transform, "get_position"));

    // run the original code, because otherwise the player awake code will never be ran and this could cause issues, tthis can be done at anytime during the hook
    Player_Awake(self);
}

MAKE_HOOK_OFFSETLESS(Player_Update, void, Il2CppObject* self)
{
    // we don't log in Update methods, due to this causing a flood of log messages (we only do so when debugging)

    // get the player transform
    Il2CppObject* transform = CRASH_UNLESS(il2cpp_utils::RunMethod(self, "get_transform"));
    // get the position from the player transfrom
    Vector3 pos = CRASH_UNLESS(il2cpp_utils::RunMethod<Vector3>(transform, "get_position"));

    // if the position is below -100 (well below the map) teleport the player back to it's starting point, this is to prevent players from having to restart due to jumping out of the map
    if (pos.y < - 100.0f)
    {
        CRASH_UNLESS(il2cpp_utils::RunMethod(transform, "set_position", startPos));
    }

    // if allowed, set these values, also check if the player even wants the mod enabled by reading the config
    if (allowSpaceMonke && config.enabled)
    {
        resetSpeed = true;
        float jumpMultiplier = (10.0f * (config.multiplier / 10.0f));
        float maxJumpSpeed = (40.0f * (config.multiplier / 10.0f));
        float velocityLimit = 0.3f / (config.multiplier / 10.0f);

        if (config.multiplier == 1.0f)
        {
            jumpMultiplier = 1.1f;
            velocityLimit = 0.3f;
            maxJumpSpeed = 6.5f;
        }
        
        CRASH_UNLESS(il2cpp_utils::SetFieldValue(self, "jumpMultiplier", jumpMultiplier));
        CRASH_UNLESS(il2cpp_utils::SetFieldValue(self, "velocityLimit", velocityLimit));
        CRASH_UNLESS(il2cpp_utils::SetFieldValue(self, "maxJumpSpeed", maxJumpSpeed));
        //CRASH_UNLESS(il2cpp_utils::SetFieldValue(self, "disableMovement", false));
    }
    else // if not allowed
    {
        // speed will be reset
        if (resetSpeed)
        {
            resetSpeed = false;
            // This is not really an error, but this is more to show off the different log types, also this log only happens after the speed is reset, so we can log this in the Update method
            ERROR("Speed was reset!");
            CRASH_UNLESS(il2cpp_utils::SetFieldValue(self, "jumpMultiplier", 1.1f));
            CRASH_UNLESS(il2cpp_utils::SetFieldValue(self, "velocityLimit", 0.3f));
            CRASH_UNLESS(il2cpp_utils::SetFieldValue(self, "maxJumpSpeed", 6.5f));
        }
    }

    // call original update code
    Player_Update(self);
}

MAKE_HOOK_OFFSETLESS(PhotonNetworkController_OnJoinedRoom, void, Il2CppObject* self)
{
    PhotonNetworkController_OnJoinedRoom(self);

    Il2CppObject* currentRoom = CRASH_UNLESS(il2cpp_utils::RunMethod("Photon.Pun", "PhotonNetwork", "get_CurrentRoom"));

    if (currentRoom)
    {
        // get wether or not this is a private room
        allowSpaceMonke = !CRASH_UNLESS(il2cpp_utils::RunMethod<bool>(currentRoom, "get_IsVisible"));
        
        // create the c# string which will be the value we want to get from player prefs
        static Il2CppString* currentQueue = il2cpp_utils::createcsstr("currentQueue", il2cpp_utils::StringType::Manual);

        // get the game type (= queue), we registered a game type to the custom pc so if that one is active the mod can be active too
        Il2CppString* queueCS = CRASH_UNLESS(il2cpp_utils::RunMethod<Il2CppString*>("UnityEngine", "PlayerPrefs", "GetString", currentQueue, il2cpp_utils::createcsstr("DEFAULT")));

        // convert the c# string to a c++ string
        std::string queue = to_utf8(csstrtostr(queueCS));

        // as said before, if the queue is SPACEMONKE, we can allow the mod
        if (queue.find("SPACEMONKE") != std::string::npos)
        {
            allowSpaceMonke = true;
        }
    }
    else allowSpaceMonke = true;

    // ? construction to switch what is logged, logs work like printf in C with the % placeholders
    INFO("Room Joined! %s", allowSpaceMonke ? "Room Was Private" : "Room Was not private");
}

// setup lets the modloader know the mod ID and version as defined in android.mk
extern "C" void setup(ModInfo& info)
{
    info.id = ID;
    info.version = VERSION;
    modInfo = info;
    INFO("Hellowo World from setup!");
}

// load needs to be declared like this so the modloader can call it
extern "C" void load()
{
    INFO("Hello World From Load!");

    // we require the computer mod to be inited before we can properly load our mod
    GorillaUI::Init();
    
    // if config could not be loaded correctly, save the config so the file is there
    if (!LoadConfig()) 
            SaveConfig();

    INFO("config:\n\t multiplier is: %.2f", config.multiplier);
    // store a reference to the logger
    Logger& logger = getLogger();

    // best to call the init method
    il2cpp_functions::Init();
    INFO("Installing hooks...");

    // installing a hook follows the principle of logger, hookname, findmethod, where findmethod takes args namespace, class, method, argcount
    INSTALL_HOOK_OFFSETLESS(logger, Player_Awake, il2cpp_utils::FindMethodUnsafe("GorillaLocomotion", "Player", "Awake", 0));
    INSTALL_HOOK_OFFSETLESS(logger, Player_Update, il2cpp_utils::FindMethodUnsafe("GorillaLocomotion", "Player", "Update", 0));
    INSTALL_HOOK_OFFSETLESS(logger, PhotonNetworkController_OnJoinedRoom, il2cpp_utils::FindMethodUnsafe("", "PhotonNetworkController", "OnJoinedRoom", 0));

    // Registering our custom type for the screen
    custom_types::Register::RegisterType<SpaceMonke::SpaceMonkeSettingsView>(); 
    custom_types::Register::RegisterType<SpaceMonke::SpaceMonkeWatchView>(); 

    // Registering our custom screen to the monke computer
    GorillaUI::Register::RegisterSettingsView<SpaceMonke::SpaceMonkeSettingsView*>("Space Monke", VERSION);
    GorillaUI::Register::RegisterWatchView<SpaceMonke::SpaceMonkeWatchView*>("Space Monke", VERSION);
    
    // register a custom game type to the monke pc so that we can allow people to find public matches with space monke
    GorillaUI::CustomQueues::add_queue("SPACEMONKE", "Space Monke", "<size=40>\n    A queue that allows users to use the Space Monke mod without it disabling.\n    Entering this queue means you give your consent to people using\n    the space monke mod.\n</size>");
    
    INFO("Installed hooks!");
}
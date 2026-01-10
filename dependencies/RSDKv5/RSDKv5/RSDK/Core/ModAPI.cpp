#include "RSDK/Core/RetroEngine.hpp"

#if RETRO_USE_MOD_LOADER

using namespace RSDK;

#if RETRO_REV0U
#include "Legacy/ModAPILegacy.cpp"
#endif

// Filesystem include and namespace definition
#if !defined(__PS3__) && (!defined(__GNUC__) || __GNUC__ >= 8) // GCC 8 for std::filesystem generally
#include <filesystem>
namespace fs = std::filesystem;
#else
// PS3 specific includes (or for older GCC)
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h> // For S_ISDIR, though often in sys/stat.h
#include <vector>   // For std::vector
#include <algorithm> // For std::transform
// Define dummy fs::path and related types if absolutely necessary to minimize changes,
// but prefer direct string operations.
// For fs::filesystem_error, we'll rely on function return codes and manual logging.
namespace fs {
    // Define a minimal path like class or use strings directly
    // For now, we will aim to replace fs::path with std::string directly.
    // Define dummy directory_options if it's used in a way that's hard to #ifdef out
    enum class directory_options { follow_directory_symlink = 0 }; // Mimic if needed
#if RETRO_PLATFORM == RETRO_ANDROID
    // Keep Android JNI fs functions within this conditional fs namespace
    // These are defined below, after the PS3 helpers, to maintain existing structure
#else
    // For other non-std::filesystem platforms (like PS3 or older GCC),
    // we'll use PS3_ prefixed helper functions instead of trying to mimic fs:: perfectly.
#endif
} // namespace fs (dummy for non-std::filesystem or Android specific fs)
#endif

#if defined(__PS3__) || (defined(__GNUC__) && __GNUC__ < 8 && !defined(__clang__))
// PS3 specific includes (or for older GCC)
// Includes <cstdio>, <cstdlib>, <string> were used by helpers now in RetroEngine.hpp.
// Filesystem specific includes <dirent.h>, <sys/stat.h>, <unistd.h> remain for PS3_PathExists etc.
// <vector>, <algorithm> are used by filesystem helpers.

// RSDK_USE_PS3_STRING_CONVERSIONS macro and std:: namespace overrides for string conversions
// have been moved to RetroEngine.hpp to be alongside their PS3_ helper function definitions.

// PS3 Filesystem Helper Functions (also for older GCC)
// These remain here as they are more specific to ModAPI's directory scanning needs.
static bool PS3_PathExists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

static bool PS3_IsDirectory(const std::string& path) {
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0) {
        return false;
    }
    return S_ISDIR(buffer.st_mode);
}

// Non-recursive listing
static bool PS3_ListDirectory(const std::string& path, std::vector<std::pair<std::string, bool>>& entries) {
    entries.clear();
    DIR *dir = opendir(path.c_str());
    if (!dir) {
        RSDK::PrintLog(RSDK::PRINT_ERROR, "[MODS] PS3_ListDirectory: Cannot open directory: %s", path.c_str());
        return false;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string entryName = entry->d_name;
        if (entryName == "." || entryName == "..") {
            continue;
        }
        std::string fullEntryPath = path + "/" + entryName;
        bool isDir = PS3_IsDirectory(fullEntryPath);
        entries.push_back(std::make_pair(entryName, isDir));
    }
    closedir(dir);
    return true;
}

// Recursive listing for files
static bool PS3_ListDirectoryRecursive(
    const std::string& basePath,
    const std::string& currentRelPath,
    std::vector<std::string>& foundFilePaths
) {
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_FS_PS3] RecursiveScan START: Path='%s', RelPath='%s'", basePath.c_str(), currentRelPath.c_str());
    std::string currentAbsolutePath = basePath;
    if (!currentRelPath.empty()) {
        if (basePath.back() == '/' && currentRelPath.front() == '/') { // Avoid double slashes
            currentAbsolutePath = basePath + currentRelPath.substr(1);
        } else if (basePath.back() != '/' && currentRelPath.front() != '/') {
            currentAbsolutePath = basePath + "/" + currentRelPath;
        } else {
            currentAbsolutePath = basePath + currentRelPath;
        }
    }
    
    DIR *dir = opendir(currentAbsolutePath.c_str());
    if (!dir) {
        RSDK::PrintLog(RSDK::PRINT_ERROR, "[MODS] PS3_ListDirectoryRecursive: Cannot open directory: %s", currentAbsolutePath.c_str());
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_FS_PS3]   Found entry: '%s' in '%s'", name.c_str(), currentAbsolutePath.c_str());
        std::string entryAbsolutePath = currentAbsolutePath + "/" + name;
        std::string entryRelativePath = currentRelPath.empty() ? name : currentRelPath + "/" + name;
        
        struct stat entryStat;
        if (stat(entryAbsolutePath.c_str(), &entryStat) == 0) {
            if (S_ISDIR(entryStat.st_mode)) {
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_FS_PS3]     Descending into: '%s'", entryRelativePath.c_str());
                PS3_ListDirectoryRecursive(basePath, entryRelativePath, foundFilePaths);
            } else {
                // Apply path normalization (lowercase, convert '\' to '/')
                std::string normalizedPath = entryRelativePath;
                std::transform(normalizedPath.begin(), normalizedPath.end(), normalizedPath.begin(),
                                [](unsigned char c) { return c == '\\' ? '/' : std::tolower(c); });
                foundFilePaths.push_back(normalizedPath);
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_FS_PS3]       Added file: '%s'", normalizedPath.c_str());
            }
        } else {
            RSDK::PrintLog(RSDK::PRINT_ERROR, "[MODS] PS3_ListDirectoryRecursive: Cannot stat file/directory: %s", entryAbsolutePath.c_str());
        }
    }
    closedir(dir);
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_FS_PS3] RecursiveScan END: Path='%s', RelPath='%s'", basePath.c_str(), currentRelPath.c_str());
    return true;
}
#endif // __PS3__ or older GCC

#if RETRO_PLATFORM == RETRO_ANDROID
// Definitions for Android JNI fs functions, now potentially within the fs namespace from above
// Ensure these are correctly scoped, especially if fs namespace is defined conditionally
namespace fs { // This might need adjustment based on how the above fs namespace is structured
bool exists(fs::path path)
{
    auto *jni        = GetJNISetup();
    jbyteArray array = jni->env->NewByteArray(path.string().length());
    jni->env->SetByteArrayRegion(array, 0, path.string().length(), (jbyte *)path.string().c_str());
    return jni->env->CallBooleanMethod(jni->thiz, fsExists, array);
}

bool is_directory(fs::path path)
{
    auto *jni        = GetJNISetup();
    jbyteArray array = jni->env->NewByteArray(path.string().length());
    jni->env->SetByteArrayRegion(array, 0, path.string().length(), (jbyte *)path.string().c_str());
    return jni->env->CallBooleanMethod(jni->thiz, fsIsDir, array);
}

fs::path_list directory_iterator(fs::path path)
{
    auto *jni        = GetJNISetup();
    jbyteArray array = jni->env->NewByteArray(path.string().length());
    jni->env->SetByteArrayRegion(array, 0, path.string().length(), (jbyte *)path.string().c_str());
    return fs::path_list((jobjectArray)jni->env->CallObjectMethod(jni->thiz, fsDirIter, array));
}
} // namespace fs for Android
#endif

#include <stdexcept> // Moved down to ensure it's not affecting fs namespace logic
#include <functional> // Moved down

#include "iniparser/iniparser.h"

int32 RSDK::currentObjectID = 0;
std::vector<ObjectClass *> allocatedInherits;

// this helps later on just trust me
#define MODAPI_ENDS_WITH(str) buf.length() > strlen(str) && !buf.compare(buf.length() - strlen(str), strlen(str), std::string(str))

ModSettings RSDK::modSettings;
std::vector<ModInfo> RSDK::modList;
std::vector<ModCallbackSTD> RSDK::modCallbackList[MODCB_MAX];
std::vector<StateHook> RSDK::stateHookList;
std::vector<ObjectHook> RSDK::objectHookList;
ModVersionInfo RSDK::targetModVersion = { RETRO_REVISION, 0, RETRO_MOD_LOADER_VER };

char RSDK::customUserFileDir[0x100];

RSDK::ModInfo *RSDK::currentMod;

std::vector<RSDK::ModPublicFunctionInfo> gamePublicFuncs;

void *RSDK::modFunctionTable[RSDK::ModTable_Count];

std::map<uint32, uint32> RSDK::superLevels;
int32 RSDK::inheritLevel = 0;

#define ADD_MOD_FUNCTION(id, func) modFunctionTable[id] = (void *)func;

// https://www.techiedelight.com/trim-string-cpp-remove-leading-trailing-spaces/
std::string trim(const std::string &s)
{
    auto start = s.begin();
    while (start != s.end() && std::isspace(*start)) {
        start++;
    }

    auto end = s.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::isspace(*end));

    return std::string(start, end + 1);
}

void RSDK::InitModAPI(bool32 getVersion)
{
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] InitModAPI: Entered. getVersion = %d", getVersion);
    memset(modFunctionTable, 0, sizeof(modFunctionTable));

    // ============================
    // Mod Function Table
    // ============================

    // Registration & Core
    ADD_MOD_FUNCTION(ModTable_RegisterGlobals, ModRegisterGlobalVariables);
    ADD_MOD_FUNCTION(ModTable_RegisterObject, ModRegisterObject);
    ADD_MOD_FUNCTION(ModTable_RegisterObjectSTD, ModRegisterObject_STD);
    ADD_MOD_FUNCTION(ModTable_RegisterObjectHook, ModRegisterObjectHook);
    ADD_MOD_FUNCTION(ModTable_FindObject, ModFindObject);
    ADD_MOD_FUNCTION(ModTable_GetGlobals, GetGlobals);
    ADD_MOD_FUNCTION(ModTable_Super, Super);

    // Mod Info
    ADD_MOD_FUNCTION(ModTable_LoadModInfo, LoadModInfo);
    ADD_MOD_FUNCTION(ModTable_GetModPath, GetModPath);
    ADD_MOD_FUNCTION(ModTable_GetModCount, GetModCount);
    ADD_MOD_FUNCTION(ModTable_GetModIDByIndex, GetModIDByIndex);
    ADD_MOD_FUNCTION(ModTable_ForeachModID, ForeachModID);

    // Mod Callbacks & Public Functions
    ADD_MOD_FUNCTION(ModTable_AddModCallback, AddModCallback);
    ADD_MOD_FUNCTION(ModTable_AddModCallbackSTD, AddModCallback_STD);
    ADD_MOD_FUNCTION(ModTable_AddPublicFunction, AddPublicFunction);
    ADD_MOD_FUNCTION(ModTable_GetPublicFunction, GetPublicFunction);

    // Mod Settings
    ADD_MOD_FUNCTION(ModTable_GetSettingsBool, GetSettingsBool);
    ADD_MOD_FUNCTION(ModTable_GetSettingsInt, GetSettingsInteger);
    ADD_MOD_FUNCTION(ModTable_GetSettingsFloat, GetSettingsFloat);
    ADD_MOD_FUNCTION(ModTable_GetSettingsString, GetSettingsString);
    ADD_MOD_FUNCTION(ModTable_SetSettingsBool, SetSettingsBool);
    ADD_MOD_FUNCTION(ModTable_SetSettingsInt, SetSettingsInteger);
    ADD_MOD_FUNCTION(ModTable_SetSettingsFloat, SetSettingsFloat);
    ADD_MOD_FUNCTION(ModTable_SetSettingsString, SetSettingsString);
    ADD_MOD_FUNCTION(ModTable_SaveSettings, SaveSettings);

    // Config
    ADD_MOD_FUNCTION(ModTable_GetConfigBool, GetConfigBool);
    ADD_MOD_FUNCTION(ModTable_GetConfigInt, GetConfigInteger);
    ADD_MOD_FUNCTION(ModTable_GetConfigFloat, GetConfigFloat);
    ADD_MOD_FUNCTION(ModTable_GetConfigString, GetConfigString);
    ADD_MOD_FUNCTION(ModTable_ForeachConfig, ForeachConfig);
    ADD_MOD_FUNCTION(ModTable_ForeachConfigCategory, ForeachConfigCategory);

    // Achievements
    ADD_MOD_FUNCTION(ModTable_RegisterAchievement, RegisterAchievement);
    ADD_MOD_FUNCTION(ModTable_GetAchievementInfo, GetAchievementInfo);
    ADD_MOD_FUNCTION(ModTable_GetAchievementIndexByID, GetAchievementIndexByID);
    ADD_MOD_FUNCTION(ModTable_GetAchievementCount, GetAchievementCount);

    // Shaders
    ADD_MOD_FUNCTION(ModTable_LoadShader, RenderDevice::LoadShader);

    // StateMachine
    ADD_MOD_FUNCTION(ModTable_StateMachineRun, StateMachineRun);
    ADD_MOD_FUNCTION(ModTable_RegisterStateHook, RegisterStateHook);
    ADD_MOD_FUNCTION(ModTable_HandleRunState_HighPriority, HandleRunState_HighPriority);
    ADD_MOD_FUNCTION(ModTable_HandleRunState_LowPriority, HandleRunState_LowPriority);

#if RETRO_MOD_LOADER_VER >= 2
    // Mod Settings (Part 2)
    ADD_MOD_FUNCTION(ModTable_ForeachSetting, ForeachSetting);
    ADD_MOD_FUNCTION(ModTable_ForeachSettingCategory, ForeachSettingCategory);

    // Files
    ADD_MOD_FUNCTION(ModTable_ExcludeFile, ExcludeFile);
    ADD_MOD_FUNCTION(ModTable_ExcludeAllFiles, ExcludeAllFiles);
    ADD_MOD_FUNCTION(ModTable_ReloadFile, ReloadFile);
    ADD_MOD_FUNCTION(ModTable_ReloadAllFiles, ReloadAllFiles);

    // Graphics
    ADD_MOD_FUNCTION(ModTable_GetSpriteAnimation, GetSpriteAnimation);
    ADD_MOD_FUNCTION(ModTable_GetSpriteSurface, GetSpriteSurface);
    ADD_MOD_FUNCTION(ModTable_GetPaletteBank, GetPaletteBank);
    ADD_MOD_FUNCTION(ModTable_GetActivePaletteBuffer, GetActivePaletteBuffer);
    ADD_MOD_FUNCTION(ModTable_GetRGB32To16Buffer, GetRGB32To16Buffer);
    ADD_MOD_FUNCTION(ModTable_GetBlendLookupTable, GetBlendLookupTable);
    ADD_MOD_FUNCTION(ModTable_GetSubtractLookupTable, GetSubtractLookupTable);
    ADD_MOD_FUNCTION(ModTable_GetTintLookupTable, GetTintLookupTable);
    ADD_MOD_FUNCTION(ModTable_GetMaskColor, GetMaskColor);
    ADD_MOD_FUNCTION(ModTable_GetScanEdgeBuffer, GetScanEdgeBuffer);
    ADD_MOD_FUNCTION(ModTable_GetCamera, GetCamera);
    ADD_MOD_FUNCTION(ModTable_GetShader, GetShader);
    ADD_MOD_FUNCTION(ModTable_GetModel, GetModel);
    ADD_MOD_FUNCTION(ModTable_GetScene3D, GetScene3D);
    ADD_MOD_FUNCTION(ModTable_DrawDynamicAniTile, DrawDynamicAniTile);

    // Audio
    ADD_MOD_FUNCTION(ModTable_GetSfx, GetSfxEntry);
    ADD_MOD_FUNCTION(ModTable_GetChannel, GetChannel);

    // Objects/Entities
    ADD_MOD_FUNCTION(ModTable_GetGroupEntities, GetGroupEntities);

    // Collision
    ADD_MOD_FUNCTION(ModTable_SetPathGripSensors, SetPathGripSensors);
    ADD_MOD_FUNCTION(ModTable_FloorCollision, FloorCollision);
    ADD_MOD_FUNCTION(ModTable_LWallCollision, LWallCollision);
    ADD_MOD_FUNCTION(ModTable_RoofCollision, RoofCollision);
    ADD_MOD_FUNCTION(ModTable_RWallCollision, RWallCollision);
    ADD_MOD_FUNCTION(ModTable_FindFloorPosition, FindFloorPosition);
    ADD_MOD_FUNCTION(ModTable_FindLWallPosition, FindLWallPosition);
    ADD_MOD_FUNCTION(ModTable_FindRoofPosition, FindRoofPosition);
    ADD_MOD_FUNCTION(ModTable_FindRWallPosition, FindRWallPosition);
    ADD_MOD_FUNCTION(ModTable_CopyCollisionMask, CopyCollisionMask);
    ADD_MOD_FUNCTION(ModTable_GetCollisionInfo, GetCollisionInfo);
#endif

    superLevels.clear();
    inheritLevel = 0;
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] InitModAPI: Calling LoadMods().");
    LoadMods(false, getVersion);
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] InitModAPI: Returned from LoadMods().");
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] InitModAPI: Exiting.");
}

void RSDK::SortMods()
{
    if (ENGINE_VERSION) {
        for (int32 m = 0; m < modList.size(); ++m) {
            int32 targetVersion = modList[m].forceVersion ? modList[m].forceVersion : modList[m].targetVersion;

            if (modList[m].active && targetVersion != -1 && targetVersion != ENGINE_VERSION) {
                PrintLog(PRINT_NORMAL, "[MOD] Mod %s disabled due to target version mismatch", modList[m].id.c_str());
                modList[m].active = false;
            }
        }
    }

    std::stable_sort(modList.begin(), modList.end(), [](const ModInfo &a, const ModInfo &b) {
        if (!(a.active && b.active))
            return a.active;
        // keep it unsorted i guess
        return false;
    });
}

void RSDK::LoadModSettings()
{
    customUserFileDir[0] = 0;

    modSettings.redirectSaveRAM  = false;
    modSettings.disableGameLogic = false;

#if RETRO_REV0U
    modSettings.versionOverride = 0;
    modSettings.forceScripts    = customSettings.forceScripts;
#endif

    if (modList.empty())
        return;

    // Iterate backwards to find the last active mod in the list
    int32 start = modList.size() - 1;
    while ((start != -1) && !modList[start].active) {
        --start;
    }

    // No active mod in the list
    if (start == -1)
        return;

    for (int32 i = start; i >= 0; --i) {
        ModInfo *mod = &modList[i];

        if (mod->redirectSaveRAM) {
            if (SKU::userFileDir[0])
                sprintf(customUserFileDir, "%smods/%s/", SKU::userFileDir, mod->folderName.c_str());
            else
                sprintf(customUserFileDir, "mods/%s/", mod->folderName.c_str());
        }

        modSettings.redirectSaveRAM |= mod->redirectSaveRAM ? 1 : 0;
        modSettings.disableGameLogic |= mod->disableGameLogic ? 1 : 0;

#if RETRO_REV0U
        if (mod->forceVersion)
            modSettings.versionOverride = mod->forceVersion;
        modSettings.forceScripts |= mod->forceScripts ? 1 : 0;
#endif
    }
}

void RSDK::ApplyModChanges()
{
#if RETRO_REV0U
    uint32 category                      = sceneInfo.activeCategory;
    uint32 scene                         = sceneInfo.listPos;
    dataStorage[DATASET_SFX].usedStorage = 0;
    RefreshModFolders(true);
    LoadModSettings();
    DetectEngineVersion();
    if (!engine.version)
        engine.version = devMenu.startingVersion;

    switch (engine.version) {
        case 5:
            globalVarsInitCB = NULL;
            LoadGameConfig();
            sceneInfo.state  = ENGINESTATE_DEVMENU;
            Legacy::gameMode = Legacy::ENGINE_MAINGAME;
            break;

        case 4:
            Legacy::v4::LoadGameConfig("Data/Game/GameConfig.bin");
            strcpy(gameVerInfo.version, "Legacy v4 Mode");

            sceneInfo.state  = ENGINESTATE_NONE; // i think this is fine ??? lmk if otherwise // rmg seal of approval // WAIT THIS WAS ME
            Legacy::gameMode = Legacy::ENGINE_DEVMENU;
            break;

        case 3:
            Legacy::v3::LoadGameConfig("Data/Game/GameConfig.bin");
            strcpy(gameVerInfo.version, "Legacy v3 Mode");

            sceneInfo.state  = ENGINESTATE_NONE;
            Legacy::gameMode = Legacy::ENGINE_DEVMENU;
            break;
    }
    if (engine.version == devMenu.startingVersion) {
        sceneInfo.activeCategory = category;
        sceneInfo.listPos        = scene;
    }
#else
    uint32 category                      = sceneInfo.activeCategory;
    uint32 scene                         = sceneInfo.listPos;
    dataStorage[DATASET_SFX].usedStorage = 0;
    RefreshModFolders(true);
    LoadModSettings();
    LoadGameConfig();
    sceneInfo.activeCategory = category;
    sceneInfo.listPos        = scene;
#endif
    RenderDevice::SetWindowTitle();
}

void DrawStatus(const char *str)
{
    int32 dy = currentScreen->center.y - 32;
    DrawRectangle(currentScreen->center.x - 128, dy + 52, 0x100, 0x8, 0x80, 0xFF, INK_NONE, true);
    DrawDevString(str, currentScreen->center.x, dy + 52, ALIGN_CENTER, 0xFFFFFF);

    RenderDevice::CopyFrameBuffer();
    RenderDevice::FlipScreen();
}

#if RETRO_RENDERDEVICE_EGL
// egl devices are slower in I/O so render more increments
#define BAR_THRESHOLD (10.F)
#define RENDER_COUNT  (200)
#else
#define BAR_THRESHOLD (100.F)
#define RENDER_COUNT  (200)
#endif

bool32 RSDK::ScanModFolder(ModInfo *info, const char *targetFile, bool32 fromLoadMod, bool32 loadingBar)
{
    if (!info)
        return false;

    const std::string modDir = info->path;

    if (!targetFile)
        info->fileMap.clear();

    std::string targetFileStr = "";
    if (targetFile) {
        char pathLower[0x100];
        memset(pathLower, 0, sizeof(char) * 0x100);
        for (int32 c = 0; c < strlen(targetFile); ++c) pathLower[c] = tolower(targetFile[c]);

        targetFileStr = std::string(pathLower);
    }

    std::string dataPathStr = modDir; // fs::path dataPath(modDir);
    int32 dy = currentScreen->center.y - 32;
    int32 dx = currentScreen->center.x;

    // Normalize targetFileStr early if it's used
    if (targetFile && targetFileStr.empty()) { // Should have been done before, but ensure
        char pathLower[0x100];
        memset(pathLower, 0, sizeof(char) * 0x100);
        for (int32 c = 0; c < strlen(targetFile); ++c) pathLower[c] = tolower(targetFile[c]);
        targetFileStr = std::string(pathLower);
        // Normalize slashes for consistency if needed, though primarily for map keys
        std::replace(targetFileStr.begin(), targetFileStr.end(), '\\', '/');
    }
    
#if !defined(__PS3__) && (!defined(__GNUC__) || __GNUC__ >= 8)
    if (targetFile) {
        if (fs::exists(fs::path(modDir + "/" + targetFileStr))) { // fs::path used for fs::exists
            info->fileMap.insert(std::pair<std::string, std::string>(targetFileStr, modDir + "/" + targetFileStr));
            return true;
        }
        else
            return false;
    }

    if (fs::exists(fs::path(dataPathStr)) && fs::is_directory(fs::path(dataPathStr))) { // fs::path used here
        try {
            if (loadingBar) {
                currentScreen = &screens[0];
                DrawRectangle(dx - 0x80 + 0x10, dy + 48, 0x100 - 0x20, 0x10, 0x000000, 0xFF, INK_NONE, true);
                DrawDevString(fromLoadMod ? "Getting count..." : ("Scanning " + info->id + "...").c_str(), currentScreen->center.x, dy + 52,
                              ALIGN_CENTER, 0xFFFFFF);
                RenderDevice::CopyFrameBuffer();
                RenderDevice::FlipScreen();
            }

            auto dirIterator = fs::recursive_directory_iterator(fs::path(dataPathStr), fs::directory_options::follow_directory_symlink); // fs::path used

            std::vector<fs::directory_entry> files;
            int32 renders = 1;
            int32 size    = 0;

            for (auto dirFile : dirIterator) {
#if RETRO_PLATFORM != RETRO_ANDROID
                if (!dirFile.is_directory()) {
#endif
                    files.push_back(dirFile);
                    if (loadingBar && ++size >= RENDER_COUNT * renders) {
                        DrawRectangle(dx - 0x80 + 0x10, dy + 48, 0x100 - 0x20, 0x10, 0x000000, 0xFF, INK_NONE, true);
                        DrawDevString((std::to_string(size) + " files").c_str(), currentScreen->center.x, dy + 52, ALIGN_CENTER, 0xFFFFFF);
                        RenderDevice::CopyFrameBuffer();
                        RenderDevice::FlipScreen();
                        renders++;
                    }
#if RETRO_PLATFORM != RETRO_ANDROID
                }
#endif
            }

            int32 i    = 0;
            int32 bars = 1;

            for (auto dirFile : files) {
                std::string folderPath = dirFile.path().string().substr(fs::path(dataPathStr).string().length() + 1); // fs::path used
                std::transform(folderPath.begin(), folderPath.end(), folderPath.begin(),
                               [](unsigned char c) { return c == '\\' ? '/' : std::tolower(c); });

                info->fileMap.insert(std::pair<std::string, std::string>(folderPath, dirFile.path().string()));
                if (loadingBar && (size * bars) / BAR_THRESHOLD < ++i) {
                    DrawRectangle(dx - 0x80 + 0x10, dy + 48, 0x100 - 0x20, 0x10, 0x000000, 0xFF, INK_NONE, true);
                    DrawRectangle(dx - 0x80 + 0x10 + 2, dy + 50, (int32)((0x100 - 0x20 - 4) * (i / (float)size)), 0x10 - 4, 0x00FF00, 0xFF, INK_NONE,
                                  true);
                    while ((size * bars) / BAR_THRESHOLD < i) bars++;
                    DrawDevString((std::to_string(i) + "/" + std::to_string(size)).c_str(), currentScreen->center.x, dy + 52, ALIGN_CENTER, 0xFFFFFF);
                    RenderDevice::CopyFrameBuffer();
                    RenderDevice::FlipScreen();
                }
            }
        } catch (fs::filesystem_error &fe) {
            PrintLog(PRINT_ERROR, "Mod File Scanning Error: %s", fe.what());
        }
    }
#else // PS3 or older GCC path
    if (targetFile) {
        if (PS3_PathExists(modDir + "/" + targetFileStr)) {
            info->fileMap.insert(std::pair<std::string, std::string>(targetFileStr, modDir + "/" + targetFileStr));
            return true;
        }
        else
            return false;
    }

    if (PS3_PathExists(dataPathStr) && PS3_IsDirectory(dataPathStr)) {
        if (loadingBar) {
            currentScreen = &screens[0];
            DrawRectangle(dx - 0x80 + 0x10, dy + 48, 0x100 - 0x20, 0x10, 0x000000, 0xFF, INK_NONE, true);
            DrawDevString(fromLoadMod ? "Getting count..." : ("Scanning " + info->id + "...").c_str(), currentScreen->center.x, dy + 52,
                          ALIGN_CENTER, 0xFFFFFF);
            RenderDevice::CopyFrameBuffer();
            RenderDevice::FlipScreen();
        }

        std::vector<std::string> foundFiles;
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_SCAN] Scanning mod folder (PS3): Mod='%s', DataPath='%s'", info->id.c_str(), dataPathStr.c_str());
        // dataPathStr here is the mod's specific data folder, e.g., "mods/MyMod/Data"
        // Pass dataPathStr as basePath, and an empty string for currentRelPath to start.
        if (PS3_ListDirectoryRecursive(dataPathStr, "", foundFiles)) {
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_SCAN]   PS3_ListDirectoryRecursive completed. Found %d files for mod '%s'.", (int)foundFiles.size(), info->id.c_str());
            int count = 0;
            if (foundFiles.empty()) {
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_SCAN]   No files found by PS3_ListDirectoryRecursive for mod '%s'. Skipping fileMap population.", info->id.c_str());
            }

            for (const std::string& relativeFilePath : foundFiles) {
                count++;
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_MAP][%d/%d] Loop entry for: '%s'", count, (int)foundFiles.size(), relativeFilePath.c_str());

                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_MAP][%d/%d]   Creating fullPathToModFile for '%s'...", count, (int)foundFiles.size(), relativeFilePath.c_str());
                std::string fullPathToModFile = dataPathStr + "/" + relativeFilePath;
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_MAP][%d/%d]     fullPathToModFile created: '%s'", count, (int)foundFiles.size(), fullPathToModFile.c_str());
                
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_MAP][%d/%d]   Preparing pair for: '%s'", count, (int)foundFiles.size(), relativeFilePath.c_str());
                std::pair<std::string, std::string> mapEntry(relativeFilePath, fullPathToModFile);
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_MAP][%d/%d]     Pair created. Key: '%s', Value: '%s'", count, (int)foundFiles.size(), mapEntry.first.c_str(), mapEntry.second.c_str());
                
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_MAP][%d/%d]   Attempting map.insert for: '%s'", count, (int)foundFiles.size(), relativeFilePath.c_str());
                info->fileMap.insert(mapEntry);
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_MAP][%d/%d]   Map.insert successful for: '%s'", count, (int)foundFiles.size(), relativeFilePath.c_str());
            }
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_SCAN]   Finished populating fileMap for mod '%s'. Processed %d file(s).", info->id.c_str(), count);
        } else {
            RSDK::PrintLog(PRINT_ERROR, "[MODS] ScanModFolder: Failed to recursively list directory: %s", dataPathStr.c_str());
        }
    }
#endif // !__PS3__ && GCC >=8 ELSE

    if (loadingBar && fromLoadMod) {
        DrawRectangle(dx - 0x80 + 0x10, dy + 48, 0x100 - 0x20, 0x10, 0x000080, 0xFF, INK_NONE, true);

        RenderDevice::CopyFrameBuffer();
        RenderDevice::FlipScreen();
    }

    return true;
}

void RSDK::UnloadMods()
{
    for (ModInfo &mod : modList) {
        if (mod.unloadMod)
            mod.unloadMod();

        for (Link::Handle &handle : mod.modLogicHandles) {
            Link::Close(handle);
        }

        mod.modLogicHandles.clear();
    }

    modList.clear();
    for (int32 c = 0; c < MODCB_MAX; ++c) modCallbackList[c].clear();
    stateHookList.clear();
    objectHookList.clear();

    for (int32 i = 0; i < (int32)allocatedInherits.size(); ++i) {
        ObjectClass *inherit = allocatedInherits[i];
        if (inherit)
            delete inherit;
    }
    allocatedInherits.clear();

#if RETRO_REV0U
    memset(Legacy::modTypeNames, 0, sizeof(Legacy::modTypeNames));
    memset(Legacy::modTypeNames, 0, sizeof(Legacy::modScriptPaths));
    memset(Legacy::modScriptFlags, 0, sizeof(Legacy::modScriptFlags));
    Legacy::modObjCount = 0;

    memset(modSettings.playerNames, 0, sizeof(modSettings.playerNames));
    modSettings.playerCount = 0;

    modSettings.versionOverride = 0;
    modSettings.activeMod       = -1;
#endif

    customUserFileDir[0] = 0;

    // Clear storage
    dataStorage[DATASET_STG].usedStorage = 0;
    DefragmentAndGarbageCollectStorage(DATASET_MUS);
    dataStorage[DATASET_SFX].usedStorage = 0;
    dataStorage[DATASET_STR].usedStorage = 0;
    dataStorage[DATASET_TMP].usedStorage = 0;

#if RETRO_REV02
    // Clear out any userDBs
    if (SKU::userDBStorage)
        SKU::userDBStorage->ClearAllUserDBs();
#endif
}

void RSDK::LoadMods(bool newOnly, bool32 getVersion)
{
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Entered. newOnly = %d, getVersion = %d", newOnly, getVersion);
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_LOAD] LoadMods START. SKU::userFileDir = '%s'", RSDK::SKU::userFileDir);
    if (!newOnly) {
        UnloadMods();

        if (AudioDevice::initializedAudioChannels) {
            // Stop all sounds
            for (int32 c = 0; c < CHANNEL_COUNT; ++c) StopChannel(c);

            // we're about to reload these, so clear anything we already have
            ClearGlobalSfx();
        }
    }

    using namespace std;
    char modBuf[0x100];
    sprintf_s(modBuf, sizeof(modBuf), "%smods", SKU::userFileDir);
    std::string modPathStr = modBuf; // fs::path modPath(modBuf);
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_LOAD]   Mods directory path constructed as: '%s'", modPathStr.c_str());

#if !defined(__PS3__) && (!defined(__GNUC__) || __GNUC__ >= 8)
    fs::path modPath(modPathStr); // Convert to fs::path for std::filesystem operations
    if (fs::exists(modPath) && fs::is_directory(modPath)) {
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Processing modconfig.ini entries...");
        string mod_config  = modPath.string() + "/modconfig.ini";
        FileIO *configFile = fOpen(mod_config.c_str(), "r");
        if (configFile) {
            fClose(configFile);
            auto ini = iniparser_load(mod_config.c_str());

            int32 c           = iniparser_getsecnkeys(ini, "Mods");
            const char **keys = new const char *[c];
            iniparser_getseckeys(ini, "Mods", keys);

            for (int32 m = 0; m < c; ++m) {
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: Iteration %d for key '%s'", m, keys[m]);
                if (newOnly && std::find_if(modList.begin(), modList.end(), [&keys, &m](ModInfo mod) {
                                   return mod.folderName == string(keys[m] + 5);
                               }) != modList.end())
                    continue;
                std::string folderNameStr = string(keys[m] + 5);
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_LOAD]   Processing mod entry/folder (from modconfig.ini): '%s'", folderNameStr.c_str());
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: Initializing ModInfo for '%s'.", (keys[m] + 5));
                ModInfo info  = {};
                bool32 active = iniparser_getboolean(ini, keys[m], false);
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: Calling LoadMod() for folder '%s', active=%d.", (keys[m] + 5), active);
                bool32 loaded = LoadMod(&info, modPath.string(), folderNameStr, active, getVersion);
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: LoadMod() returned %d for '%s'. ID set to '%s'.", loaded, (keys[m] + 5), info.id.c_str());
                if (info.id.empty()) {
                    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD] Mod %s doesn't exist or mod.ini missing/invalid ID!", keys[m] + 5);
                    continue;
                }
                else if (!loaded) {
                    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD] Failed to load mod %s (LoadMod returned false). Active: %s", info.id.c_str(), active ? "Y" : "N");
                    info.active = false;
                }
                else
                    PrintLog(PRINT_NORMAL, "[MOD] Loaded mod %s! Active: %s", info.id.c_str(), active ? "Y" : "N");
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: Attempting modList.push_back for mod ID '%s'.", info.id.c_str());
                modList.push_back(info);
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: modList.push_back successful for mod ID '%s'. modList size: %d", info.id.c_str(), (int)modList.size());
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: Finished iteration %d for key '%s'.", m, keys[m]);
            }
            delete[] keys;
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Finished processing ALL modconfig.ini entries. Before iniparser_freedict.");
            iniparser_freedict(ini);
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: After iniparser_freedict.");
        }

        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Processing direct mod folder scan...");
        try {
            auto rdi = fs::directory_iterator(modPath);
            for (auto de : rdi) {
                if (de.is_directory()) {
                    fs::path modDirPath = de.path();
                    ModInfo info        = {};
                    std::string folder  = modDirPath.filename().string();
                    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_LOAD]   Processing mod entry/folder (direct scan): '%s'", folder.c_str());

                    if (std::find_if(modList.begin(), modList.end(), [&folder](ModInfo m) { return m.folderName == folder; }) == modList.end()) {

                        const std::string modDir = modPath.string() + "/" + folder;

                        FileIO *f = fOpen((modDir + "/mod.ini").c_str(), "r");
                        if (f) {
                            fClose(f);
                            LoadMod(&info, modPath.string(), folder, false, getVersion);
                            modList.push_back(info);
                        }
                    }
                }
            }
        } catch (fs::filesystem_error &fe) {
            PrintLog(PRINT_ERROR, "Mods folder scanning error: %s", fe.what());
        }
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Finished direct mod folder scan.");
    }
#else // PS3 or older GCC path
    const char *mod_paths[] = {
        "/dev_usb000/mods/",
        "/dev_usb001/mods/",
        "/dev_usb002/mods/",
        "/dev_usb003/mods/",
    };
    for (const char* path : mod_paths) {
        std::string modPathStr = path;
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_LOAD]   Scanning mods directory: '%s'", modPathStr.c_str());
        if (PS3_PathExists(modPathStr) && PS3_IsDirectory(modPathStr)) {
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Processing modconfig.ini entries...");
            string mod_config_path = modPathStr + "/modconfig.ini";
        FileIO *configFile = fOpen(mod_config_path.c_str(), "r");
        if (configFile) {
            fClose(configFile);
            auto ini = iniparser_load(mod_config_path.c_str());

            int32 c           = iniparser_getsecnkeys(ini, "Mods");
            const char **keys = new const char *[c];
            iniparser_getseckeys(ini, "Mods", keys);

            for (int32 m = 0; m < c; ++m) {
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: Iteration %d for key '%s'", m, keys[m]);
                if (newOnly && std::find_if(modList.begin(), modList.end(), [&keys, &m](ModInfo mod) {
                                   return mod.folderName == string(keys[m] + 5);
                               }) != modList.end())
                    continue;
                std::string folderNameStr = string(keys[m] + 5);
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_LOAD]   Processing mod entry/folder (from modconfig.ini, PS3_Path): '%s'", folderNameStr.c_str());
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: Initializing ModInfo for '%s'.", (keys[m] + 5));
                ModInfo info  = {};
                bool32 active = iniparser_getboolean(ini, keys[m], false);
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: Calling LoadMod() for folder '%s', active=%d.", (keys[m] + 5), active);
                bool32 loaded = LoadMod(&info, modPathStr, folderNameStr, active, getVersion);
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: LoadMod() returned %d for '%s'. ID set to '%s'.", loaded, (keys[m] + 5), info.id.c_str());
                 if (info.id.empty()) { // Check if LoadMod actually populated the id
                    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD] Mod entry %s in modconfig.ini seems to point to a non-existent or invalid mod folder (or mod.ini missing/invalid ID)!", folderNameStr.c_str());
                    // Do not add to modList if it's essentially a dud
                    continue;
                }
                else if (!loaded) {
                    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD] Failed to load mod %s (from modconfig.ini, LoadMod returned false). Active: %s", info.id.c_str(), active ? "Y" : "N");
                    info.active = false; // Ensure it's marked inactive if loading failed
                }
                else {
                    PrintLog(PRINT_NORMAL, "[MOD] Loaded mod %s (from modconfig.ini)! Active: %s", info.id.c_str(), active ? "Y" : "N");
                }
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: Attempting modList.push_back for mod ID '%s'.", info.id.c_str());
                modList.push_back(info);
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: modList.push_back successful for mod ID '%s'. modList size: %d", info.id.c_str(), (int)modList.size());
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods/modconfig_loop: Finished iteration %d for key '%s'.", m, keys[m]);
            }
            delete[] keys;
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Finished processing ALL modconfig.ini entries. Before iniparser_freedict.");
            iniparser_freedict(ini);
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: After iniparser_freedict.");
        }
        
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Processing direct mod folder scan...");
        // Iterate through directories for mods not in modconfig.ini
        std::vector<std::pair<std::string, bool>> dirEntries;
        if (PS3_ListDirectory(modPathStr, dirEntries)) {
            for (const auto& de_pair : dirEntries) {
                if (de_pair.second) { // Check if it's a directory
                    std::string folder = de_pair.first;
                    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MODS_LOAD]   Processing mod entry/folder (direct scan, PS3_Path): '%s'", folder.c_str());
                    // Check if this mod folder was already processed via modconfig.ini
                    if (std::find_if(modList.begin(), modList.end(), [&folder](const ModInfo& m_info){ return m_info.folderName == folder; }) == modList.end()) {
                        ModInfo info = {};
                        const std::string modDir = modPathStr + "/" + folder;
                        FileIO *f = fOpen((modDir + "/mod.ini").c_str(), "r");
                        if (f) {
                            fClose(f);
                            // LoadMod will set active to false by default if not in modconfig.ini
                            LoadMod(&info, modPathStr, folder, false, getVersion);
                            if (!info.id.empty()) { // Ensure mod.ini was valid and id was set
                                modList.push_back(info);
                                PrintLog(PRINT_NORMAL, "[MOD] Found and loaded mod %s (not in modconfig.ini, defaulting to inactive).", info.id.c_str());
                            } else {
                                PrintLog(PRINT_NORMAL, "[MOD] Found folder %s which looks like a mod, but its mod.ini is invalid or missing ID.", folder.c_str());
                            }
                        }
                    }
                }
            }
        } else {
            PrintLog(PRINT_ERROR, "Mods folder scanning error: Could not list directory %s", modPathStr.c_str());
        }
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Finished direct mod folder scan.");
		}
    }
#endif // !__PS3__ && GCC >=8 ELSE

    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: All mod folder processing complete.");

    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Attempting 'Mod loading done!' screen draw...");
    int32 dy = currentScreen->center.y - 32;
    DrawRectangle(currentScreen->center.x - 128, dy, 0x100, 0x48, 0x80, 0xFF, INK_NONE, true);
    DrawDevString("Mod loading done!", currentScreen->center.x, dy + 28, ALIGN_CENTER, 0xFFFFFF);
    RenderDevice::CopyFrameBuffer();
    RenderDevice::FlipScreen();
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: 'Mod loading done!' screen draw finished.");

    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Calling SortMods() (after drawing).");
    SortMods();
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Returned from SortMods().");
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Calling LoadModSettings() (after SortMods).");
    LoadModSettings();
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Returned from LoadModSettings().");
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMods: Exiting.");
}

void loadCfg(ModInfo *info, const char *path)
{ // Start of new function body
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] loadCfg: Entered (Minimal Test). ModID: '%s', Path: '%s'", (info ? info->id.c_str() : "NULL_INFO"), path);
    // Intentionally doing nothing else for this specific test.
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] loadCfg: Exiting (Minimal Test). ModID: '%s', Path: '%s'", (info ? info->id.c_str() : "NULL_INFO"), path);
} // End of new function body

bool32 RSDK::LoadMod(ModInfo *info, const std::string &modsPath, const std::string &folder, bool32 active, bool32 getVersion)
{
    if (!info)
        return false;

    ModInfo *cur = currentMod;

    PrintLog(PRINT_NORMAL, "[MOD] Trying to load mod %s...", folder.c_str());

    info->fileMap.clear();
    info->excludedFiles.clear();
    info->modLogicHandles.clear();
    info->name             = "";
    info->desc             = "";
    info->author           = "";
    info->version          = "";
    info->id               = "";
    info->active           = false;
    info->redirectSaveRAM  = false;
    info->disableGameLogic = false;

    const std::string modDir = modsPath + "/" + folder;

    FileIO *f = fOpen((modDir + "/mod.ini").c_str(), "r");
    if (f) {
        int32 dy = currentScreen->center.y - 32;
        DrawRectangle(currentScreen->center.x - 128, dy, 0x100, 0x48, 0x80, 0xFF, INK_NONE, true);

        DrawDevString("Loading mod", currentScreen->center.x, dy + 16, ALIGN_CENTER, 0xFFFFFF);
        DrawDevString((folder + "...").c_str(), currentScreen->center.x, dy + 28, ALIGN_CENTER, 0xFFFFFF);

        DrawStatus("Parsing INI...");

        fClose(f);
        auto modIni = iniparser_load((modDir + "/mod.ini").c_str());

        info->path       = modDir;
        info->folderName = folder;
        info->id         = iniparser_getstring(modIni, ":ModID", folder.c_str());
        info->active     = active;

        info->name    = iniparser_getstring(modIni, ":Name", "Unnamed Mod");
        info->desc    = iniparser_getstring(modIni, ":Description", "");
        info->author  = iniparser_getstring(modIni, ":Author", "Unknown Author");
        info->version = iniparser_getstring(modIni, ":Version", "1.0.0");

        info->redirectSaveRAM  = iniparser_getboolean(modIni, ":RedirectSaveRAM", false);
        info->disableGameLogic = iniparser_getboolean(modIni, ":DisableGameLogic", false);

        info->forceVersion = iniparser_getint(modIni, ":ForceVersion", 0);
        if (!info->forceVersion) {
            info->targetVersion = iniparser_getint(modIni, ":TargetVersion", 5);
            if (info->targetVersion != -1 && ENGINE_VERSION) {
                if (info->targetVersion < 3 || info->targetVersion > 5) {
                    PrintLog(PRINT_NORMAL, "[MOD] Invalid target version. Should be 3, 4, or 5");
                    return false;
                }
                else if (info->targetVersion != ENGINE_VERSION) {
                    PrintLog(PRINT_NORMAL, "[MOD] Target version does not match current engine version.");
                    return false;
                }
            }
        }
        else
            info->targetVersion = info->forceVersion;
        info->forceScripts = iniparser_getboolean(modIni, ":TxtScripts", false);

        if (!active) {
            iniparser_freedict(modIni);
            return true;
        }

        // ASSETS
        DrawStatus("Scanning mod folder...");
        ScanModFolder(info, getVersion ? "Data/Game/GameConfig.bin" : nullptr, true);
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Returned from ScanModFolder for mod '%s'.", info->id.c_str());

        if (!getVersion) {
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Processing LOGIC section for mod '%s'.", info->id.c_str());
            // LOGIC
            std::string logic(iniparser_getstring(modIni, ":LogicFile", ""));
            if (logic.length()) {
                std::istringstream stream(logic);
                std::string buf;
                while (std::getline(stream, buf, ',')) {
                    buf = trim(buf);
                    DrawStatus(("Starting logic " + buf + "...").c_str());
                    bool linked = false;
                    
                    std::string logicFilePath = modDir + "/" + buf;
#if !defined(__PS3__) && (!defined(__GNUC__) || __GNUC__ >= 8)
                    fs::path file(logicFilePath); // fs::path for std::filesystem
                    Link::Handle linkHandle = Link::Open(file.string().c_str());
#else
                    Link::Handle linkHandle = Link::Open(logicFilePath.c_str()); // std::string for PS3/older GCC
#endif

                    if (linkHandle) {
                        modLink linkModLogic          = (modLink)Link::GetSymbol(linkHandle, "LinkModLogic");
                        const ModVersionInfo *modInfo = (const ModVersionInfo *)Link::GetSymbol(linkHandle, "modInfo");
                        if (!modInfo) {
                            PrintLog(PRINT_NORMAL, "[MOD] ERROR: Failed to find modInfo in logic file '%s'", logicFilePath.c_str());
                            // ... (rest of error handling)
                            iniparser_freedict(modIni);
                            currentMod = cur;
                            return false;
                        }

                        if (modInfo->engineVer != targetModVersion.engineVer) {
                            PrintLog(PRINT_NORMAL, "[MOD] ERROR: Logic file '%s' engineVer %d does not match expected engineVer of %d",
                                     logicFilePath.c_str(), modInfo->engineVer, targetModVersion.engineVer);
                            // ... (rest of error handling)
                            iniparser_freedict(modIni);
                            currentMod = cur;
                            return false;
                        }

                        if (modInfo->modLoaderVer != targetModVersion.modLoaderVer) {
                            PrintLog(PRINT_NORMAL, "[MOD] WARNING: Logic file '%s' modLoaderVer %d does not match expected modLoaderVer of %d",
                                     logicFilePath.c_str(), modInfo->modLoaderVer, targetModVersion.modLoaderVer);
                            // Not returning false, just a warning.
                        }

                        if (linkModLogic) {
                            info->linkModLogic.push_back(linkModLogic);
                            linked = true;
                        }
                        else {
                            PrintLog(PRINT_ERROR, "[MOD] ERROR: Failed to find 'LinkModLogic' -> %s", Link::GetError());
                        }
                        info->unloadMod = (void (*)())Link::GetSymbol(linkHandle, "UnloadMod");
                        info->modLogicHandles.push_back(linkHandle);
                    }
                    else {
                        PrintLog(PRINT_ERROR, "[MOD] ERROR: Failed to open mod logic file -> %s", Link::GetError());
                    }

                    if (!linked) {
                        PrintLog(PRINT_NORMAL, "[MOD] ERROR: Failed to link logic from file '%s'", logicFilePath.c_str());
                        // ... (rest of error handling)
                        iniparser_freedict(modIni);
                        currentMod = cur;
                        return false;
                    }
                }
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Finished LOGIC section for mod '%s'.", info->id.c_str());
            } else {
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: No LogicFile specified for mod '%s'.", info->id.c_str());
            }

            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Processing SETTINGS section for mod '%s'.", info->id.c_str());
            // SETTINGS
            FileIO *set = fOpen((modDir + "/modSettings.ini").c_str(), "r");
            if (set) {
                DrawStatus("Reading settings...");

                fClose(set);
                using namespace std;
                auto modSettingsIni = iniparser_load((modDir + "/modSettings.ini").c_str());
                int32 sec           = iniparser_getnsec(modSettingsIni);
                if (sec) {
                    for (int32 i = 0; i < sec; ++i) {
                        const char *secn  = iniparser_getsecname(modSettingsIni, i);
                        int32 len         = iniparser_getsecnkeys(modSettingsIni, secn);
                        const char **keys = new const char *[len];
                        iniparser_getseckeys(modSettingsIni, secn, keys);
                        map<string, string> secset;
                        for (int32 j = 0; j < len; ++j)
                            secset.insert(pair<string, string>(keys[j] + strlen(secn) + 1, iniparser_getstring(modSettingsIni, keys[j], "")));
                        info->settings.insert(pair<string, map<string, string>>(secn, secset));
                        delete[] keys;
                    }
                }
                else {
                    // either you use categories or you don't, i don't make the rules
                    map<string, string> secset;
                    for (int32 j = 0; j < modSettingsIni->n; ++j)
                        secset.insert(pair<string, string>(modSettingsIni->key[j] + 1, modSettingsIni->val[j]));
                    info->settings.insert(pair<string, map<string, string>>("", secset));
                }
                iniparser_freedict(modSettingsIni);
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Finished SETTINGS section for mod '%s'.", info->id.c_str());
            } else {
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: No modSettings.ini found for mod '%s'.", info->id.c_str());
            }
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Processing CONFIG section for mod '%s'.", info->id.c_str()); // Existing

            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Preparing path string for modConfig.cfg, Mod: '%s'", info->id.c_str()); 
            std::string configFilePath_modConfig = modDir + "/modConfig.cfg"; // Renamed variable for clarity
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod:   Path string created: '%s', Mod: '%s'", configFilePath_modConfig.c_str(), info->id.c_str()); 

            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Calling loadCfg with pre-built path for modConfig.cfg, Mod: '%s'", info->id.c_str()); 
            loadCfg(info, configFilePath_modConfig.c_str()); // Use the new variable
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Returned from loadCfg for modConfig.cfg, Mod: '%s'", info->id.c_str()); // Existing

            std::string cfg_ini(iniparser_getstring(modIni, ":ConfigFile", ""));
            bool saveCfg = false;
            if (cfg_ini.length()) {
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Processing :ConfigFile list '%s' for mod '%s'", cfg_ini.c_str(), info->id.c_str());
                std::istringstream stream(cfg_ini);
                std::string buf;
                while (std::getline(stream, buf, ',')) {
                    buf = trim(buf);
                    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod:   Processing :ConfigFile entry '%s'", buf.c_str());
                    DrawStatus(("Reading config " + buf + "...").c_str());

                    int32 mode = 0;
                    std::string configFilePath; // Used for messages and fOpen

                    if (MODAPI_ENDS_WITH(".ini")) {
                        configFilePath = modDir + "/" + buf; // buf already includes .ini
                        mode = 1;
                    }
                    else if (MODAPI_ENDS_WITH(".cfg")) {
                        configFilePath = modDir + "/" + buf; // buf already includes .cfg
                        mode = 2;
                    }
                    // The original code had a typo here, it should check buf directly, not buf + ".ini"
                    // Assuming MODAPI_ENDS_WITH is correct and buf contains the full filename including extension.

#if !defined(__PS3__) && (!defined(__GNUC__) || __GNUC__ >= 8)
                    fs::path fsPathForExistenceCheck; // Only for fs::exists
                    if (!mode) { // If mode not determined by extension, try checking existence
                        fsPathForExistenceCheck = fs::path(modDir + "/" + buf + ".ini"); // Try .ini
                        if (fs::exists(fsPathForExistenceCheck)) {
                            mode = 1;
                            configFilePath = modDir + "/" + buf + ".ini";
                        }
                    }
                    if (!mode) { // Then try .cfg
                        fsPathForExistenceCheck = fs::path(modDir + "/" + buf + ".cfg");
                        if (fs::exists(fsPathForExistenceCheck)) {
                            mode = 2;
                            configFilePath = modDir + "/" + buf + ".cfg";
                        }
                    }
#else // PS3 or older GCC
                    if (!mode) { // If mode not determined by extension, try checking existence
                        std::string tempPathIni = modDir + "/" + buf + ".ini";
                        if (PS3_PathExists(tempPathIni)) {
                            mode = 1;
                            configFilePath = tempPathIni;
                        }
                    }
                    if (!mode) {
                        std::string tempPathCfg = modDir + "/" + buf + ".cfg";
                        if (PS3_PathExists(tempPathCfg)) {
                            mode = 2;
                            configFilePath = tempPathCfg;
                        }
                    }
#endif
                    if (!mode) { // if still no mode, cannot determine file type
                        PrintLog(PRINT_NORMAL, "[MOD] Could not determine type or find config file: %s (or .ini/.cfg variant)", buf.c_str());
                        continue;
                    }
                    
                    if (mode == 1) { // .ini file
                        FileIO *set = fOpen(configFilePath.c_str(), "r");
                        if (set) {
                            saveCfg = true; // Mark that we might need to save back to combined modConfig.cfg
                            fClose(set);
                            using namespace std;
                            auto cfgIni = iniparser_load(configFilePath.c_str());
                            int32 sec   = iniparser_getnsec(cfgIni);
                            for (int32 i = 0; i < sec; ++i) {
                                const char *secn  = iniparser_getsecname(cfgIni, i);
                                int32 len         = iniparser_getsecnkeys(cfgIni, secn);
                                const char **keys = new const char *[len];
                                iniparser_getseckeys(cfgIni, secn, keys);
                                for (int32 j = 0; j < len; ++j)
                                    info->config[secn][keys[j] + strlen(secn) + 1] = iniparser_getstring(cfgIni, keys[j], "");
                                delete[] keys;
                            }
                            iniparser_freedict(cfgIni);
                        } else {
                             PrintLog(PRINT_ERROR, "[MOD] Failed to open .ini config file: %s", configFilePath.c_str());
                        }
                    }
                    else if (mode == 2) { // .cfg file
                        loadCfg(info, configFilePath.c_str()); // Assumes loadCfg handles its own fOpen/exists checks if necessary
                    }
                    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod:   Finished :ConfigFile entry '%s'", buf.c_str());
                }
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Finished processing :ConfigFile list for mod '%s'.", info->id.c_str());
            } else {
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: No :ConfigFile specified in mod.ini for mod '%s'.", info->id.c_str());
            }

            if (saveCfg && info->config.size()) {
                DrawStatus("Saving config...");
                FileIO *cfg = fOpen((modDir + "/modConfig.cfg").c_str(), "wb");
                uint8 ct    = info->config.size();
                fWrite(&ct, 1, 1, cfg);
                for (auto kv : info->config) {
                    if (!kv.first.length())
                        continue; // don't save no-categories
                    uint8 len = kv.first.length();
                    fWrite(&len, 1, 1, cfg);
                    WriteText(cfg, kv.first.c_str());
                    uint8 kt = kv.second.size();
                    fWrite(&kt, 1, 1, cfg);
                    for (auto kkv : kv.second) {
                        uint8 len    = (uint8)(kkv.first.length()) & 0x7F;
                        bool32 isint = false;
                        int32 r      = 0;
                        try {
                            r     = std::stoi(kkv.second, nullptr, 0);
                            isint = true;
                            len |= 0x80;
                        } catch (...) {
                        }
                        fWrite(&len, 1, 1, cfg);
                        WriteText(cfg, kkv.first.c_str());
                        if (isint)
                            fWrite(&r, sizeof(int32), 1, cfg);
                        else {
                            uint8 len = kkv.second.length();
                            fWrite(&len, 1, 1, cfg);
                            WriteText(cfg, kkv.second.c_str());
                        }
                    }
                }
                fClose(cfg);
            }
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Finished CONFIG section for mod '%s'.", info->id.c_str());
        } else {
             RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Skipping LOGIC/SETTINGS/CONFIG due to getVersion=1 for mod '%s'.", info->id.c_str());
        }

        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Attempting iniparser_freedict(modIni) for mod '%s'.", info->id.c_str());
        iniparser_freedict(modIni);
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Successfully returned from iniparser_freedict(modIni) for mod '%s'.", info->id.c_str());
        currentMod = cur;
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[MOD_FLOW] LoadMod: Exiting function for mod '%s', returning true.", info->id.c_str());
        return true;
    }
    return false;
}

void RSDK::SaveMods()
{
    ModInfo *cur = currentMod;
    char modBuf[0x100];
    sprintf_s(modBuf, sizeof(modBuf), "%smods/", SKU::userFileDir);
    std::string modPathStr = modBuf; // fs::path modPath(modBuf);

    SortMods();

    PrintLog(PRINT_NORMAL, "[MOD] Saving mods...");

#if !defined(__PS3__) && (!defined(__GNUC__) || __GNUC__ >= 8)
    fs::path modPath(modPathStr); // For fs::exists and fs::is_directory
    if (fs::exists(modPath) && fs::is_directory(modPath)) {
        std::string mod_config = modPath.string() + "/modconfig.ini";
        FileIO *file           = fOpen(mod_config.c_str(), "w");
#else
    if (PS3_PathExists(modPathStr) && PS3_IsDirectory(modPathStr)) {
        std::string mod_config_path = modPathStr + "/modconfig.ini";
        FileIO *file                = fOpen(mod_config_path.c_str(), "w");
#endif

        WriteText(file, "[Mods]\n");

        for (int32 m = 0; m < modList.size(); ++m) {
            currentMod = &modList[m];
            SaveSettings();
            WriteText(file, "%s=%c\n", currentMod->folderName.c_str(), currentMod->active ? 'y' : 'n');
        }
        fClose(file);
    }
    currentMod = cur;
}

void RSDK::RunModCallbacks(int32 callbackID, void *data)
{
    if (callbackID < 0 || callbackID >= MODCB_MAX)
        return;

    for (auto &c : modCallbackList[callbackID]) {
        if (c)
            c(data);
    }
}

// Mod API
bool32 RSDK::LoadModInfo(const char *id, String *name, String *description, String *version, bool32 *active)
{
    if (!id) { // NULL == "Internal" Logic
        if (name)
            InitString(name, gameVerInfo.gameTitle, 0);
        if (description)
            InitString(description, gameVerInfo.gameSubtitle, 0);
        if (version)
            InitString(version, gameVerInfo.version, 0);
        if (active)
            *active = true;

        return true;
    }
    else if (!strlen(id) && currentMod) { // "" == Current Mod
        if (name)
            InitString(name, currentMod->name.c_str(), 0);
        if (description)
            InitString(description, currentMod->desc.c_str(), 0);
        if (version)
            InitString(version, currentMod->version.c_str(), 0);
        if (active)
            *active = currentMod->active;

        return true;
    }

    for (int32 m = 0; m < modList.size(); ++m) {
        if (modList[m].id == id) {
            if (name)
                InitString(name, modList[m].name.c_str(), 0);
            if (description)
                InitString(description, modList[m].desc.c_str(), 0);
            if (version)
                InitString(version, modList[m].version.c_str(), 0);
            if (active)
                *active = modList[m].active;

            return true;
        }
    }
    return false;
}

int32 RSDK::GetModCount(bool32 active)
{
    int32 c = 0;
    for (auto &m : modList) {
        if (++c && active && !m.active)
            return c - 1;
    }
    return c;
}

const char *RSDK::GetModIDByIndex(uint32 index)
{
    if (index >= modList.size())
        return NULL;
    return modList[index].id.c_str();
}

bool32 RSDK::ForeachModID(String *id)
{
    if (!id)
        return false;

    using namespace std;

    if (id->chars)
        ++foreachStackPtr->id;
    else {
        ++foreachStackPtr;
        foreachStackPtr->id = 0;
    }

    if (foreachStackPtr->id >= modList.size()) {
        foreachStackPtr--;
        return false;
    }
    string set = modList[foreachStackPtr->id].id;
    InitString(id, set.c_str(), 0);
    return true;
}

void RSDK::AddModCallback(int32 callbackID, ModCallback callback) { return AddModCallback_STD(callbackID, callback); }

void RSDK::AddModCallback_STD(int32 callbackID, ModCallbackSTD callback)
{
    if (callbackID < 0 || callbackID >= MODCB_MAX)
        return;

    modCallbackList[callbackID].push_back(callback);
}

void RSDK::AddPublicFunction(const char *functionName, void *functionPtr)
{
    if (!currentMod)
        return gamePublicFuncs.push_back({ functionName, functionPtr });
    if (!currentMod->active)
        return;
    currentMod->functionList.push_back({ functionName, functionPtr });
}

void *RSDK::GetPublicFunction(const char *id, const char *functionName)
{
    if (!id) {
        for (auto &f : gamePublicFuncs) {
            if (f.name == functionName)
                return f.ptr;
        }

        return NULL;
    }

    if (!strlen(id) && currentMod)
        id = currentMod->id.c_str();

    for (ModInfo &m : modList) {
        if (m.active && m.id == id) {
            for (auto &f : m.functionList) {
                if (f.name == functionName)
                    return f.ptr;
            }

            return NULL;
        }
    }

    return NULL;
}

std::string GetModPath_i(const char *id)
{
    int32 m;
    for (m = 0; m < modList.size(); ++m) {
        if (modList[m].active && modList[m].id == id)
            break;
    }

    if (m == modList.size())
        return std::string();

    return modList[m].path;
}

void RSDK::GetModPath(const char *id, String *result)
{
    std::string modPath = GetModPath_i(id);

    if (modPath.empty())
        return;

    InitString(result, modPath.c_str(), 0);
}

std::string GetModSettingsValue(const char *id, const char *key)
{
    std::string skey(key);
    if (!strchr(key, ':'))
        skey = std::string(":") + key;

    std::string cat  = skey.substr(0, skey.find(":"));
    std::string rkey = skey.substr(skey.find(":") + 1);

    for (ModInfo &m : modList) {
        if (m.active && m.id == id) {
            try {
                return m.settings.at(cat).at(rkey);
            } catch (std::out_of_range) {
                return std::string();
            }
        }
    }
    return std::string();
}

bool32 RSDK::GetSettingsBool(const char *id, const char *key, bool32 fallback)
{
    if (!id) {
        // TODO: allow user to get values from settings.ini?
    }
    else if (!strlen(id)) {
        if (!currentMod)
            return fallback;

        id = currentMod->id.c_str();
    }

    std::string v = GetModSettingsValue(id, key);

    if (!v.length()) {
        if (currentMod->id == id)
            SetSettingsBool(key, fallback);
        return fallback;
    }
    char first = v.at(0);
    if (first == 'y' || first == 'Y' || first == 't' || first == 'T' || (first = GetSettingsInteger(id, key, 0)))
        return true;
    if (first == 'n' || first == 'N' || first == 'f' || first == 'F' || !first)
        return false;
    if (currentMod->id == id)
        SetSettingsBool(key, fallback);
    return fallback;
}

int32 RSDK::GetSettingsInteger(const char *id, const char *key, int32 fallback)
{
    if (!id) {
        // TODO: allow user to get values from settings.ini?
    }
    else if (!strlen(id)) {
        if (!currentMod)
            return fallback;

        id = currentMod->id.c_str();
    }

    std::string v = GetModSettingsValue(id, key);

    if (!v.length()) {
        if (currentMod->id == id)
            SetSettingsInteger(key, fallback);
        return fallback;
    }
    try {
        return std::stoi(v, nullptr, 0);
    } catch (...) {
        if (currentMod->id == id)
            SetSettingsInteger(key, fallback);
        return fallback;
    }
}

float RSDK::GetSettingsFloat(const char *id, const char *key, float fallback)
{
    if (!id) {
        // TODO: allow user to get values from settings.ini?
    }
    else if (!strlen(id)) {
        if (!currentMod)
            return fallback;

        id = currentMod->id.c_str();
    }

    std::string v = GetModSettingsValue(id, key);

    if (!v.length()) {
        if (currentMod->id == id)
            SetSettingsFloat(key, fallback);
        return fallback;
    }
    try {
        return std::stof(v, nullptr);
    } catch (...) {
        if (currentMod->id == id)
            SetSettingsFloat(key, fallback);
        return fallback;
    }
}

void RSDK::GetSettingsString(const char *id, const char *key, String *result, const char *fallback)
{
    if (!id) {
        // TODO: allow user to get values from settings.ini?
    }
    else if (!strlen(id)) {
        if (!currentMod) {
            InitString(result, fallback, 0);
            return;
        }

        id = currentMod->id.c_str();
    }

    std::string v = GetModSettingsValue(id, key);
    if (!v.length()) {
        if (currentMod->id == id)
            SetSettingsString(key, result);
        InitString(result, fallback, 0);
        return;
    }
    InitString(result, v.c_str(), 0);
}

std::string GetNidConfigValue(const char *key)
{
    if (!currentMod || !currentMod->active)
        return std::string();
    std::string skey(key);
    if (!strchr(key, ':'))
        skey = std::string(":") + key;

    std::string cat  = skey.substr(0, skey.find(":"));
    std::string rkey = skey.substr(skey.find(":") + 1);

    try {
        return currentMod->config.at(cat).at(rkey);
    } catch (std::out_of_range) {
        return std::string();
    }
    return std::string();
}

bool32 RSDK::GetConfigBool(const char *key, bool32 fallback)
{
    std::string v = GetNidConfigValue(key);
    if (!v.length())
        return fallback;
    char first = v.at(0);
    if (first == 'y' || first == 'Y' || first == 't' || first == 'T' || (first = GetConfigInteger(key, 0)))
        return true;
    if (first == 'n' || first == 'N' || first == 'f' || first == 'F' || !first)
        return false;
    return fallback;
}

int32 RSDK::GetConfigInteger(const char *key, int32 fallback)
{
    std::string v = GetNidConfigValue(key);
    if (!v.length())
        return fallback;
    try {
        return std::stoi(v, nullptr, 0);
    } catch (...) {
        return fallback;
    }
}

float RSDK::GetConfigFloat(const char *key, float fallback)
{
    std::string v = GetNidConfigValue(key);
    if (!v.length())
        return fallback;
    try {
        return std::stof(v, nullptr);
    } catch (...) {
        return fallback;
    }
}

void RSDK::GetConfigString(const char *key, String *result, const char *fallback)
{
    std::string v = GetNidConfigValue(key);
    if (!v.length()) {
        InitString(result, fallback, 0);
        return;
    }
    InitString(result, v.c_str(), 0);
}

bool32 RSDK::ForeachConfigCategory(String *category)
{
    if (!category || !currentMod)
        return false;

    using namespace std;
    if (!currentMod->config.size())
        return false;

    if (category->chars)
        ++foreachStackPtr->id;
    else {
        ++foreachStackPtr;
        foreachStackPtr->id = 0;
    }
    int32 sid = 0;
    string cat;
    bool32 set = false;
    if (currentMod->config[""].size() && foreachStackPtr->id == sid++) {
        set = true;
        cat = "";
    }
    if (!set) {
        for (pair<string, map<string, string>> kv : currentMod->config) {
            if (!kv.first.length())
                continue;
            if (kv.second.size() && foreachStackPtr->id == sid++) {
                set = true;
                cat = kv.first;
                break;
            }
        }
    }
    if (!set) {
        foreachStackPtr--;
        return false;
    }
    InitString(category, cat.c_str(), 0);
    return true;
}

bool32 RSDK::ForeachConfig(String *config)
{
    if (!config || !currentMod)
        return false;
    using namespace std;
    if (!currentMod->config.size())
        return false;

    if (config->chars)
        ++foreachStackPtr->id;
    else {
        ++foreachStackPtr;
        foreachStackPtr->id = 0;
    }
    int32 sid = 0;
    string key, cat;
    if (currentMod->config[""].size()) {
        for (pair<string, string> pair : currentMod->config[""]) {
            if (foreachStackPtr->id == sid++) {
                cat = "";
                key = pair.first;
                break;
            }
        }
    }
    if (!key.length()) {
        for (pair<string, map<string, string>> kv : currentMod->config) {
            if (!kv.first.length())
                continue;
            for (pair<string, string> pair : kv.second) {
                if (foreachStackPtr->id == sid++) {
                    cat = kv.first;
                    key = pair.first;
                    break;
                }
            }
        }
    }
    if (!key.length()) {
        foreachStackPtr--;
        return false;
    }
    string r = cat + ":" + key;
    InitString(config, r.c_str(), 0);
    return true;
}

#if RETRO_MOD_LOADER_VER >= 2
bool32 RSDK::ForeachSettingCategory(const char *id, String *category)
{
    if (!id) {
        // TODO: allow user to get values from settings.ini?
    }
    else if (!strlen(id)) {
        if (!currentMod)
            return false;

        id = currentMod->id.c_str();
    }

    if (!category)
        return false;

    int32 m;
    for (m = 0; m < modList.size(); ++m) {
        if (modList[m].active && modList[m].id == id)
            break;
    }

    if (m == modList.size())
        return false;

    ModInfo *mod = &modList[m];

    using namespace std;
    if (!mod->settings.size())
        return false;

    if (category->chars)
        ++foreachStackPtr->id;
    else {
        ++foreachStackPtr;
        foreachStackPtr->id = 0;
    }
    int32 sid = 0;
    string cat;
    bool32 set = false;
    if (mod->settings[""].size() && foreachStackPtr->id == sid++) {
        set = true;
        cat = "";
    }
    if (!set) {
        for (pair<string, map<string, string>> kv : mod->settings) {
            if (!kv.first.length())
                continue;
            if (kv.second.size() && foreachStackPtr->id == sid++) {
                set = true;
                cat = kv.first;
                break;
            }
        }
    }
    if (!set) {
        foreachStackPtr--;
        return false;
    }
    InitString(category, cat.c_str(), 0);
    return true;
}

bool32 RSDK::ForeachSetting(const char *id, String *setting)
{
    if (!id) {
        // TODO: allow user to get values from settings.ini?
    }
    else if (!strlen(id)) {
        if (!currentMod)
            return false;

        id = currentMod->id.c_str();
    }

    if (!setting)
        return false;
    using namespace std;
    if (!currentMod->settings.size())
        return false;

    int32 m;
    for (m = 0; m < modList.size(); ++m) {
        if (modList[m].active && modList[m].id == id)
            break;
    }

    if (m == modList.size())
        return false;

    ModInfo *mod = &modList[m];

    if (setting->chars)
        ++foreachStackPtr->id;
    else {
        ++foreachStackPtr;
        foreachStackPtr->id = 0;
    }
    int32 sid = 0;
    string key, cat;
    if (mod->settings[""].size()) {
        for (pair<string, string> pair : mod->settings[""]) {
            if (foreachStackPtr->id == sid++) {
                cat = "";
                key = pair.first;
                break;
            }
        }
    }
    if (!key.length()) {
        for (pair<string, map<string, string>> kv : mod->settings) {
            if (!kv.first.length())
                continue;
            for (pair<string, string> pair : kv.second) {
                if (foreachStackPtr->id == sid++) {
                    cat = kv.first;
                    key = pair.first;
                    break;
                }
            }
        }
    }
    if (!key.length()) {
        foreachStackPtr--;
        return false;
    }
    string r = cat + ":" + key;
    InitString(setting, r.c_str(), 0);
    return true;
}
#endif

void SetModSettingsValue(const char *key, const std::string &val)
{
    if (!currentMod)
        return;
    std::string skey(key);
    if (!strchr(key, ':'))
        skey = std::string(":") + key;

    std::string cat  = skey.substr(0, skey.find(":"));
    std::string rkey = skey.substr(skey.find(":") + 1);

    currentMod->settings[cat][rkey] = val;
}

void RSDK::SetSettingsBool(const char *key, bool32 val) { SetModSettingsValue(key, val ? "Y" : "N"); }
void RSDK::SetSettingsInteger(const char *key, int32 val) { SetModSettingsValue(key, std::to_string(val)); }
void RSDK::SetSettingsFloat(const char *key, float val) { SetModSettingsValue(key, std::to_string(val)); }
void RSDK::SetSettingsString(const char *key, String *val)
{
    char *buf = new char[val->length];
    GetCString(buf, val);
    SetModSettingsValue(key, buf);
    delete[] buf;
}

void RSDK::SaveSettings()
{
    using namespace std;
    if (!currentMod || !currentMod->settings.size() || !currentMod->active)
        return;

    FileIO *file = fOpen((GetModPath_i(currentMod->id.c_str()) + "/modSettings.ini").c_str(), "w");

    if (currentMod->settings[""].size()) {
        for (pair<string, string> pair : currentMod->settings[""]) WriteText(file, "%s = %s\n", pair.first.c_str(), pair.second.c_str());
    }
    for (pair<string, map<string, string>> kv : currentMod->settings) {
        if (!kv.first.length())
            continue;
        WriteText(file, "\n[%s]\n", kv.first.c_str());
        for (pair<string, string> pair : kv.second) WriteText(file, "%s = %s\n", pair.first.c_str(), pair.second.c_str());
    }
    fClose(file);
    PrintLog(PRINT_NORMAL, "[MOD] Saved mod settings for mod %s", currentMod->id.c_str());
    return;
}

// i'm going to hell for this
// nvm im actually so proud of this func yall have no idea i'm insane
void SuperInternal(RSDK::ObjectClass *super, RSDK::ModSuper callback, void *data)
{
    using namespace RSDK;

    ModInfo *curMod = currentMod;
    bool32 override = false;
    if (!super->inherited)
        return; // Mod.Super on an object that's literally an original object why did you do this
    ++superLevels[inheritLevel];
    if (HASH_MATCH_MD5(super->hash, super->inherited->hash)) {
        // entity override
        override = true;
        for (int32 i = 0; i < superLevels[inheritLevel]; i++) {
            if (!super->inherited)
                break; // *do not* cap superLevel because if we do we'll break things even more than what we had to do to get here
            super = super->inherited;
        }
    }
    else {
        // basic entity inherit
        inheritLevel++;
        super = super->inherited;
    }

    switch (callback) {
        case SUPER_UPDATE:
            if (super->update)
                super->update();
            break;

        case SUPER_LATEUPDATE:
            if (super->lateUpdate)
                super->lateUpdate();
            break;

        case SUPER_STATICUPDATE:
            if (super->staticUpdate)
                super->staticUpdate();
            break;

        case SUPER_DRAW:
            if (super->draw)
                super->draw();
            break;

        case SUPER_CREATE:
            if (super->create)
                super->create(data);
            break;

        case SUPER_STAGELOAD:
            if (super->stageLoad)
                super->stageLoad();
            break;

        case SUPER_EDITORDRAW:
            if (super->editorDraw)
                super->editorDraw();
            break;

        case SUPER_EDITORLOAD:
            if (super->editorLoad)
                super->editorLoad();
            break;

        case SUPER_SERIALIZE:
            if (super->serialize)
                super->serialize();
            break;
    }

    if (!override)
        inheritLevel--;
    superLevels[inheritLevel]--;
    currentMod = curMod;
}

void RSDK::Super(int32 objectID, ModSuper callback, void *data) { return SuperInternal(&objectClassList[stageObjectIDs[objectID]], callback, data); }

void *RSDK::GetGlobals() { return (void *)globalVarsPtr; }

void RSDK::ModRegisterGlobalVariables(const char *globalsPath, void **globals, uint32 size)
{
    AllocateStorage(globals, size, DATASET_STG, true);
    FileInfo info;
    InitFileInfo(&info);

    int32 *varPtr = *(int32 **)globals;
    if (LoadFile(&info, globalsPath, FMODE_RB)) {
        uint8 varCount = ReadInt8(&info);
        for (int32 i = 0; i < varCount && globalVarsPtr; ++i) {
            int32 offset = ReadInt32(&info, false);
            int32 count  = ReadInt32(&info, false);
            for (int32 v = 0; v < count; ++v) {
                varPtr[offset + v] = ReadInt32(&info, false);
            }
        }

        CloseFile(&info);
    }
}

#if RETRO_REV0U
void RSDK::ModRegisterObject(Object **staticVars, Object **modStaticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize,
                             uint32 modClassSize, void (*update)(), void (*lateUpdate)(), void (*staticUpdate)(), void (*draw)(),
                             void (*create)(void *), void (*stageLoad)(), void (*editorDraw)(), void (*editorLoad)(), void (*serialize)(),
                             void (*staticLoad)(Object *), const char *inherited)
{
    return ModRegisterObject_STD(staticVars, modStaticVars, name, entityClassSize, staticClassSize, modClassSize, update, lateUpdate, staticUpdate,
                                 draw, create, stageLoad, editorDraw, editorLoad, serialize, staticLoad, inherited);
}

void RSDK::ModRegisterObject_STD(Object **staticVars, Object **modStaticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize,
                                 uint32 modClassSize, std::function<void()> update, std::function<void()> lateUpdate,
                                 std::function<void()> staticUpdate, std::function<void()> draw, std::function<void(void *)> create,
                                 std::function<void()> stageLoad, std::function<void()> editorDraw, std::function<void()> editorLoad,
                                 std::function<void()> serialize, std::function<void(Object *)> staticLoad, const char *inherited)
#else

void RSDK::ModRegisterObject(Object **staticVars, Object **modStaticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize,
                             uint32 modClassSize, void (*update)(), void (*lateUpdate)(), void (*staticUpdate)(), void (*draw)(),
                             void (*create)(void *), void (*stageLoad)(), void (*editorDraw)(), void (*editorLoad)(), void (*serialize)(),
                             const char *inherited)
{
    return ModRegisterObject_STD(staticVars, modStaticVars, name, entityClassSize, staticClassSize, modClassSize, update, lateUpdate, staticUpdate,
                                 draw, create, stageLoad, editorDraw, editorLoad, serialize, inherited);
}

void RSDK::ModRegisterObject_STD(Object **staticVars, Object **modStaticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize,
                                 uint32 modClassSize, std::function<void()> update, std::function<void()> lateUpdate,
                                 std::function<void()> staticUpdate, std::function<void()> draw, std::function<void(void *)> create,
                                 std::function<void()> stageLoad, std::function<void()> editorDraw, std::function<void()> editorLoad,
                                 std::function<void()> serialize, const char *inherited)
#endif
{
    ModInfo *curMod = currentMod;
    int32 preCount  = objectClassCount + 1;
    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(name, hash);

    ObjectClass *inherit = NULL;
    for (int32 i = 0; i < objectClassCount; ++i) {
        if (HASH_MATCH_MD5(objectClassList[i].hash, hash)) {
            objectClassCount = i;
            inherit          = new ObjectClass(objectClassList[i]);
            allocatedInherits.push_back(inherit);
            --preCount;
            if (!inherited)
                inherited = name;
            break;
        }
    }

    if (inherited) {
        RETRO_HASH_MD5(hash);
        GEN_HASH_MD5(inherited, hash);
        if (!inherit) {
            for (int32 i = 0; i < preCount; ++i) {
                if (HASH_MATCH_MD5(objectClassList[i].hash, hash)) {
                    inherit = new ObjectClass(objectClassList[i]);
                    allocatedInherits.push_back(inherit);
                    break;
                }
            }
        }

        if (!inherit)
            inherited = NULL;
    }

    if (inherited) {
        if (inherit->entityClassSize > entityClassSize)
            entityClassSize = inherit->entityClassSize;
    }

#if RETRO_REV0U
    RegisterObject_STD(staticVars, name, entityClassSize, staticClassSize, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                       nullptr, nullptr);
#else
    RegisterObject_STD(staticVars, name, entityClassSize, staticClassSize, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                       nullptr);
#endif

    ObjectClass *info = &objectClassList[objectClassCount - 1];

    // clang-format off
    if (update)       info->update       = [curMod, update]()                       { currentMod = curMod; update();                currentMod = NULL; };
    if (lateUpdate)   info->lateUpdate   = [curMod, lateUpdate]()                   { currentMod = curMod; lateUpdate();            currentMod = NULL; };
    if (staticUpdate) info->staticUpdate = [curMod, staticUpdate]()                 { currentMod = curMod; staticUpdate();          currentMod = NULL; };
    if (draw)         info->draw         = [curMod, draw]()                         { currentMod = curMod; draw();                  currentMod = NULL; };
    if (create)       info->create       = [curMod, create](void* data)             { currentMod = curMod; create(data);            currentMod = NULL; };
    if (stageLoad)    info->stageLoad    = [curMod, stageLoad]()                    { currentMod = curMod; stageLoad();             currentMod = NULL; };
#if RETRO_REV0U
    if (staticLoad)   info->staticLoad   = [curMod, staticLoad](Object *staticVars) { currentMod = curMod; staticLoad(staticVars);  currentMod = NULL; };
#endif
    if (editorDraw)   info->editorDraw   = [curMod, editorDraw]()                   { currentMod = curMod; editorDraw();            currentMod = NULL; };
    if (editorLoad)   info->editorLoad   = [curMod, editorLoad]()                   { currentMod = curMod; editorLoad();            currentMod = NULL; };
    if (serialize)    info->serialize    = [curMod, serialize]()                    { currentMod = curMod; serialize();             currentMod = NULL; };
    // clang-format on

    if (inherited) {
        info->inherited = inherit;

        if (HASH_MATCH_MD5(info->hash, inherit->hash)) {
            // we override an obj and lets set staticVars
            info->staticVars      = inherit->staticVars;
            info->staticClassSize = inherit->staticClassSize;
            if (staticVars) {
                // give them a hook
                ModRegisterObjectHook(staticVars, name);
            }
            // lets also setup mod static vars
            if (modStaticVars && modClassSize) {
                curMod->staticVars[info->hash] = { curMod->id + "_" + name, modStaticVars, modClassSize };
            }
        }

        // clang-format off
        if (!update)       info->update       = [curMod, info]()                    { currentMod = curMod; SuperInternal(info, SUPER_UPDATE, NULL);            currentMod = NULL; };
        if (!lateUpdate)   info->lateUpdate   = [curMod, info]()                    { currentMod = curMod; SuperInternal(info, SUPER_LATEUPDATE, NULL);        currentMod = NULL; };
        if (!staticUpdate) info->staticUpdate = [curMod, info]()                    { currentMod = curMod; SuperInternal(info, SUPER_STATICUPDATE, NULL);      currentMod = NULL; };
        if (!draw)         info->draw         = [curMod, info]()                    { currentMod = curMod; SuperInternal(info, SUPER_DRAW, NULL);              currentMod = NULL; };
        if (!create)       info->create       = [curMod, info](void* data)          { currentMod = curMod; SuperInternal(info, SUPER_CREATE, data);            currentMod = NULL; };
        if (!stageLoad)    info->stageLoad    = [curMod, info]()                    { currentMod = curMod; SuperInternal(info, SUPER_STAGELOAD, NULL);         currentMod = NULL; };
#if RETRO_REV0U
        // Don't inherit staticLoad, that should be per-struct
        // if (!staticLoad)   info->staticLoad   = [curMod, info](Object *staticVars)  { currentMod = curMod; SuperInternal(info, SUPER_STATICLOAD, staticVars);  currentMod = NULL; };
#endif
        if (!editorDraw)   info->editorDraw   = [curMod, info]()                    { currentMod = curMod; SuperInternal(info, SUPER_EDITORDRAW, NULL);        currentMod = NULL; };
        if (!editorLoad)   info->editorLoad   = [curMod, info]()                    { currentMod = curMod; SuperInternal(info, SUPER_EDITORLOAD, NULL);        currentMod = NULL; };
        if (!serialize)    info->serialize    = [curMod, info]()                    { currentMod = curMod; SuperInternal(info, SUPER_SERIALIZE, NULL);         currentMod = NULL; };
        // clang-format on
    }

    objectClassCount = preCount;
}

void RSDK::ModRegisterObjectHook(Object **staticVars, const char *staticName)
{
    if (!staticVars || !staticName)
        return;

    ObjectHook hook;
    GEN_HASH_MD5(staticName, hook.hash);
    hook.staticVars = staticVars;

    objectHookList.push_back(hook);
}

Object *RSDK::ModFindObject(const char *name)
{
    if (int32 o = FindObject(name))
        return *objectClassList[stageObjectIDs[o]].staticVars;

    return NULL;
}

void RSDK::GetAchievementInfo(uint32 id, String *name, String *description, String *identifer, bool32 *achieved)
{
    if (id >= achievementList.size())
        return;

    if (name)
        InitString(name, achievementList[id].name.c_str(), 0);

    if (description)
        InitString(description, achievementList[id].description.c_str(), 0);

    if (identifer)
        InitString(identifer, achievementList[id].identifier.c_str(), 0);

    if (achieved)
        *achieved = achievementList[id].achieved;
}

int32 RSDK::GetAchievementIndexByID(const char *id)
{
    for (int32 i = 0; i < achievementList.size(); ++i) {
        if (achievementList[i].identifier == std::string(id))
            return i;
    }

    return -1;
}
int32 RSDK::GetAchievementCount() { return (int32)achievementList.size(); }

void RSDK::StateMachineRun(void (*state)())
{
    bool32 skipState = false;

    for (int32 h = 0; h < (int32)stateHookList.size(); ++h) {
        if (stateHookList[h].priority && stateHookList[h].state == state && stateHookList[h].hook)
            skipState |= stateHookList[h].hook(skipState);
    }

    if (!skipState && state)
        state();

    for (int32 h = 0; h < (int32)stateHookList.size(); ++h) {
        if (!stateHookList[h].priority && stateHookList[h].state == state && stateHookList[h].hook)
            stateHookList[h].hook(skipState);
    }
}

bool32 RSDK::HandleRunState_HighPriority(void *state)
{
    bool32 skipState = false;

    for (int32 h = 0; h < (int32)stateHookList.size(); ++h) {
        if (stateHookList[h].priority && stateHookList[h].state == state && stateHookList[h].hook)
            skipState |= stateHookList[h].hook(skipState);
    }

    return skipState;
}

void RSDK::HandleRunState_LowPriority(void *state, bool32 skipState)
{
    for (int32 h = 0; h < (int32)stateHookList.size(); ++h) {
        if (!stateHookList[h].priority && stateHookList[h].state == state && stateHookList[h].hook)
            stateHookList[h].hook(skipState);
    }
}

void RSDK::RegisterStateHook(void (*state)(), bool32 (*hook)(bool32 skippedState), bool32 priority)
{
    if (!state)
        return;

    StateHook stateHook;
    stateHook.state    = state;
    stateHook.hook     = hook;
    stateHook.priority = priority;

    stateHookList.push_back(stateHook);
}

#if RETRO_MOD_LOADER_VER >= 2

// Files
bool32 RSDK::ExcludeFile(const char *id, const char *path)
{
    if (!id)
        return false;

    if (!strlen(id) && currentMod)
        id = currentMod->id.c_str();

    int32 m;
    for (m = 0; m < modList.size(); ++m) {
        if (modList[m].active && modList[m].id == id)
            break;
    }

    if (m == modList.size())
        return false;

    char pathLower[0x100];
    memset(pathLower, 0, sizeof(pathLower));
    for (int32 c = 0; c < strlen(path); ++c) pathLower[c] = tolower(path[c]);

    auto &excludeList = modList[m].excludedFiles;
    if (std::find(excludeList.begin(), excludeList.end(), pathLower) == excludeList.end()) {
        excludeList.push_back(std::string(pathLower));

        return true;
    }

    return false;
}
bool32 RSDK::ExcludeAllFiles(const char *id)
{
    if (!id)
        return false;

    if (!strlen(id) && currentMod)
        id = currentMod->id.c_str();

    int32 m;
    for (m = 0; m < modList.size(); ++m) {
        if (modList[m].active && modList[m].id == id)
            break;
    }

    if (m == modList.size())
        return false;

    auto &excludeList = modList[m].excludedFiles;
    for (auto file : modList[m].fileMap) {
        excludeList.push_back(file.first);
    }

    modList[m].fileMap.clear();

    return true;
}
bool32 RSDK::ReloadFile(const char *id, const char *path)
{
    if (!id)
        return false;

    if (!strlen(id) && currentMod)
        id = currentMod->id.c_str();

    int32 m;
    for (m = 0; m < modList.size(); ++m) {
        if (modList[m].active && modList[m].id == id)
            break;
    }

    if (m == modList.size())
        return false;

    char pathLower[0x100];
    memset(pathLower, 0, sizeof(pathLower));
    for (int32 c = 0; c < strlen(path); ++c) pathLower[c] = tolower(path[c]);

    auto &excludeList = modList[m].excludedFiles;
    if (std::find(excludeList.begin(), excludeList.end(), pathLower) != excludeList.end()) {
        excludeList.erase(std::remove(excludeList.begin(), excludeList.end(), pathLower), excludeList.end());

        return true;
    }

    ScanModFolder(&modList[m], path);

    return true;
}
bool32 RSDK::ReloadAllFiles(const char *id)
{
    if (!id)
        return false;

    if (!strlen(id) && currentMod)
        id = currentMod->id.c_str();

    int32 m;
    for (m = 0; m < modList.size(); ++m) {
        if (modList[m].active && modList[m].id == id)
            break;
    }

    if (m == modList.size())
        return false;

    modList[m].excludedFiles.clear();
    ScanModFolder(&modList[m]);

    return true;
}

// Objects & Entities
bool32 RSDK::GetGroupEntities(uint16 group, void **entity)
{
    if (group >= TYPEGROUP_COUNT)
        return false;

    if (!entity)
        return false;

    if (*entity) {
        ++foreachStackPtr->id;
    }
    else {
        foreachStackPtr++;
        foreachStackPtr->id = 0;
    }

    for (Entity *nextEntity = &objectEntityList[typeGroups[group].entries[foreachStackPtr->id]]; foreachStackPtr->id < typeGroups[group].entryCount;
         ++foreachStackPtr->id, nextEntity = &objectEntityList[typeGroups[group].entries[foreachStackPtr->id]]) {
        if (nextEntity->group == group) {
            *entity = nextEntity;
            return true;
        }
    }

    foreachStackPtr--;

    return false;
}

#endif

#endif

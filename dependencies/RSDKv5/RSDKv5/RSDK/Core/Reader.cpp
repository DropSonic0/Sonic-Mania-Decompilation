#include "RSDK/Core/RetroEngine.hpp"
#include <string.h> // For string functions
#include <ctype.h>  // For tolower, toupper

using namespace RSDK;

// Helper functions for string case conversion
static void StrToLower(char *str) {
    if (!str) return;
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

static void StrToUpper(char *str) {
    if (!str) return;
    for (int i = 0; str[i]; i++) {
        str[i] = toupper((unsigned char)str[i]);
    }
}

static void StrToTitleCase(char *str) {
    if (!str || str[0] == '\0') return;
    str[0] = toupper((unsigned char)str[0]);
    for (int i = 1; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

RSDKFileInfo RSDK::dataFileList[DATAFILE_COUNT];
RSDKContainer RSDK::dataPacks[DATAPACK_COUNT];

uint8 RSDK::dataPackCount      = 0;
uint16 RSDK::dataFileListCount = 0;

char RSDK::gameLogicName[0x200];

bool32 RSDK::useDataPack = false;

#if RETRO_REV0U
void RSDK::DetectEngineVersion()
{
    bool32 readDataPack = useDataPack;
#if RETRO_USE_MOD_LOADER
    // mods can manually set their target engine versions if needed
    if (modSettings.versionOverride) {
        engine.version = modSettings.versionOverride;
        return;
    }

    // check if we have any mods with gameconfigs
    int32 m = 0;
    for (m = 0; m < modList.size(); ++m) {
        if (!modList[m].active)
            break;
        SetActiveMod(m);

        FileInfo checkInfo;
        InitFileInfo(&checkInfo);
        if (LoadFile(&checkInfo, "Data/Game/GameConfig.bin", FMODE_RB)) {
            readDataPack = false;
            CloseFile(&checkInfo);
            m = 0; // found one, just sets this to 0 again
            break;
        }
    }

    if (m) // didn't find a gameconfig
        SetActiveMod(-1);
#endif

    FileInfo info;
    InitFileInfo(&info);
    if (!readDataPack) {
        if (LoadFile(&info, "Data/Game/GameConfig.bin", FMODE_RB)) {
#if RETRO_USE_MOD_LOADER
            SetActiveMod(-1);
#endif
            uint32 sig = ReadInt32(&info, false);

            // GameConfig has "CFG" signature, its RSDKv5 formatted
            if (sig == RSDK_SIGNATURE_CFG) {
                engine.version = 5;
            }
            else {
                // else, assume its RSDKv4 for now
                engine.version = 4;

                // Go back to the start of the file to check v3's "Data" string, that way we can tell if its v3 or v4
                Seek_Set(&info, 0);

                uint8 length = ReadInt8(&info);
                char buffer[0x40];
                ReadBytes(&info, buffer, length);

                // the "Data" thing is actually a string, but lets treat it as a "signature" for simplicity's sake shall we?
                length     = ReadInt8(&info);
                uint32 sig = ReadInt32(&info, false);
                if (sig == RSDK_SIGNATURE_DATA && length == 4)
                    engine.version = 3;
            }

            CloseFile(&info);
        }
    }
    else {
        info.externalFile = true;
        if (LoadFile(&info, dataPacks[dataPackCount - 1].name, FMODE_RB)) {
            uint32 sig = ReadInt32(&info, false);
            if (sig == RSDK_SIGNATURE_RSDK) {
                ReadInt8(&info); // 'v'
                uint8 version = ReadInt8(&info);

                switch (version) {
                    default: break;
                    case '3': engine.version = 3; break;
                    case '4': engine.version = 4; break;
                    case '5': engine.version = 5; break;
                }
            }
            else {
                // v3 has no 'RSDK' signature
                engine.version = 3;
            }

            CloseFile(&info);
        }
    }
}
#endif

bool32 RSDK::LoadDataPack(const char *filePath, size_t fileOffset, bool32 useBuffer)
{
    MEM_ZERO(dataPacks[dataPackCount]);
    useDataPack = false;
    FileInfo info;

    char dataPackPath[0x100];
#if RETRO_PLATFORM == RETRO_PS3
    sprintf_s(dataPackPath, sizeof(dataPackPath), "/dev_bdvd/PS3_GAME/USRDIR/%s", filePath);
#else
    sprintf_s(dataPackPath, sizeof(dataPackPath), "%s%s", SKU::userFileDir, filePath);
#endif

    InitFileInfo(&info);
    info.externalFile = true;
    if (LoadFile(&info, dataPackPath, FMODE_RB)) {
        uint32 sig = ReadInt32(&info, false);
        if (sig != RSDK_SIGNATURE_RSDK)
            return false;

        useDataPack = true;

        ReadInt8(&info); // 'v'
        ReadInt8(&info); // version

        strcpy(dataPacks[dataPackCount].name, dataPackPath);

        dataPacks[dataPackCount].fileCount = ReadInt16(&info);
        for (int32 f = 0; f < dataPacks[dataPackCount].fileCount; ++f) {
            uint8 b[4];
            for (int32 y = 0; y < 4; y++) {
                ReadBytes(&info, b, 4);
                dataFileList[f].hash[y] = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3] << 0);
            }

            dataFileList[f].offset = ReadInt32(&info, false);
            dataFileList[f].size   = ReadInt32(&info, false);

            dataFileList[f].encrypted = (dataFileList[f].size & 0x80000000) != 0;
            dataFileList[f].size &= 0x7FFFFFFF;
            dataFileList[f].useFileBuffer = useBuffer;
            dataFileList[f].packID        = dataPackCount;
        }

        dataPacks[dataPackCount].fileBuffer = NULL;
        if (useBuffer) {
            dataPacks[dataPackCount].fileBuffer = (uint8 *)malloc(info.fileSize);
            Seek_Set(&info, 0);
            ReadBytes(&info, dataPacks[dataPackCount].fileBuffer, info.fileSize);
        }

        dataFileListCount += dataPacks[dataPackCount].fileCount;
        dataPackCount++;

        CloseFile(&info);

        return true;
    }
    else {
        useDataPack = false;
        return false;
    }
}

#if !RETRO_USE_ORIGINAL_CODE && RETRO_REV0U
inline bool ends_with(std::string const &value, std::string const &ending)
{
    if (ending.size() > value.size())
        return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}
#endif

bool32 RSDK::OpenDataFile(FileInfo *info, const char *filename)
{
    char hashBuffer[0x400];
    StringLowerCase(hashBuffer, filename);
    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5_BUFFER(hashBuffer, hash);

    for (int32 f = 0; f < dataFileListCount; ++f) {
        RSDKFileInfo *file = &dataFileList[f];

        if (!HASH_MATCH_MD5(hash, file->hash))
            continue;

        info->usingFileBuffer = file->useFileBuffer;
        if (!file->useFileBuffer) {
            info->file = fOpen(dataPacks[file->packID].name, "rb");
            if (!info->file) {
                PrintLog(PRINT_NORMAL, "File not found (Unable to open datapack): %s", filename);
                return false;
            }

            fSeek(info->file, file->offset, SEEK_SET);
        }
        else {
            // a bit of a hack, but it is how it is in the original
            info->file = (FileIO *)&dataPacks[file->packID].fileBuffer[file->offset];

            uint8 *fileBuffer = (uint8 *)info->file;
            info->fileBuffer  = fileBuffer;
        }

        info->fileSize   = file->size;
        info->readPos    = 0;
        info->fileOffset = file->offset;
        info->encrypted  = file->encrypted;
        memset(info->encryptionKeyA, 0, 0x10 * sizeof(uint8));
        memset(info->encryptionKeyB, 0, 0x10 * sizeof(uint8));
        if (info->encrypted) {
            GenerateELoadKeys(info, filename, info->fileSize);
            info->eKeyNo      = (info->fileSize / 4) & 0x7F;
            info->eKeyPosA    = 0;
            info->eKeyPosB    = 8;
            info->eNybbleSwap = false;
        }

#if !RETRO_USE_ORIGINAL_CODE
        PrintLog(PRINT_NORMAL, "Loaded data file %s", filename);
#endif
        return true;
    }

#if !RETRO_USE_ORIGINAL_CODE
    PrintLog(PRINT_NORMAL, "Data file not found: %s", filename);
#else
    PrintLog(PRINT_NORMAL, "File not found: %s", filename);
#endif
    return false;
}

bool32 RSDK::LoadFile(FileInfo *info, const char *filename, uint8 fileMode)
{
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile] Attempting to load: '%s', Mode: %d, External: %d", filename, fileMode, info->externalFile);
    if (info->file)
        return false;

    char fullFilePath[0x100];
    strcpy(fullFilePath, filename);

#if RETRO_USE_MOD_LOADER
    char pathLower[0x100];
    memset(pathLower, 0, sizeof(pathLower));
    for (int32 c = 0; c < strlen(filename); ++c) pathLower[c] = tolower(filename[c]);

    bool32 addPath = false;
    int32 m        = modSettings.activeMod != -1 ? modSettings.activeMod : 0;
    for (; m < modList.size(); ++m) {
        if (modList[m].active) {
            std::map<std::string, std::string>::const_iterator iter = modList[m].fileMap.find(pathLower);
            if (iter != modList[m].fileMap.cend()) {
                if (std::find(modList[m].excludedFiles.begin(), modList[m].excludedFiles.end(), pathLower) == modList[m].excludedFiles.end()) {
                    strcpy(fullFilePath, iter->second.c_str());
                    info->externalFile = true;
                    break;
                }
                else {
                    PrintLog(PRINT_NORMAL, "[MOD] Excluded File: %s", filename);
                }
            }
        }
        if (modSettings.activeMod != -1) {
            PrintLog(PRINT_NORMAL, "[MOD] Failed to find file %s in active mod %s", filename, modList[m].id.c_str());
            // TODO return false? check original impl later
        }
    }

#if RETRO_REV0U
    if (modSettings.forceScripts && !info->externalFile) {
        if (std::string(fullFilePath).rfind("Data/Scripts/", 0) == 0 && ends_with(std::string(fullFilePath), "txt")) {
            // is a script, since those dont exist normally, load them from "scripts/"
            info->externalFile = true;
            addPath            = true;
            std::string fStr   = std::string(fullFilePath);
            fStr.erase(fStr.begin(), fStr.begin() + 5); // remove "Data/"
            StrCopy(fullFilePath, fStr.c_str());
        }
    }
#endif

#if RETRO_PLATFORM == RETRO_OSX || RETRO_PLATFORM == RETRO_ANDROID
    if (addPath) {
        char pathBuf[0x100];
        sprintf_s(pathBuf, sizeof(pathBuf), "%s%s", SKU::userFileDir, fullFilePath);
        sprintf_s(fullFilePath, sizeof(fullFilePath), "%s", pathBuf);
    }
#else
    (void)addPath;
#endif // ! RETRO_PLATFORM
#endif // ! RETRO_MOD_LOADER

#if !RETRO_USE_ORIGNAL_CODE
    // somewhat hacky that also pleases the mod gods
    if (!info->externalFile) {
        char pathBuf[0x100];
        sprintf_s(pathBuf, sizeof(pathBuf), "%s%s", SKU::userFileDir, fullFilePath);
        sprintf_s(fullFilePath, sizeof(fullFilePath), "%s", pathBuf);
    }
#endif

    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile] Checking packed data files for '%s'.", filename);
    if (!info->externalFile && fileMode == FMODE_RB && useDataPack) {
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile]   Attempting to load '%s' from Data.rsdk.", filename);
        return OpenDataFile(info, filename);
    }

    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile] Checking external file for '%s'. externalFile flag is: %d", filename, info->externalFile);
    // This condition is for files not from DATA.RSDK and for file I/O
    if (fileMode == FMODE_RB || fileMode == FMODE_WB || fileMode == FMODE_RB_PLUS) { // Check if mode is one we handle for fOpen
        if ((info->externalFile || !useDataPack) && (fileMode == FMODE_RB || fileMode == FMODE_RB_PLUS)) {
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile]   Attempting fOpen on external file: '%s' with mode '%s'", fullFilePath, openModes[fileMode - 1]);
            info->file = fOpen(fullFilePath, openModes[fileMode - 1]);
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile]     fOpen result: %p (NULL means failure) for '%s'", info->file, fullFilePath);

            // If original fails, prepare for variations
            if (!info->file) {
                RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile]     Initial fOpen failed for '%s'. Trying case variations.", fullFilePath);
                char variedPath[0x100];
                char filenameBaseCopy[0x100];
                char currentVariation[0x100];
                
                const char *originalFilenamePartPtr = strrchr(fullFilePath, '/');
                const char *winOriginalFilenamePartPtr = strrchr(fullFilePath, '\\');
                if (winOriginalFilenamePartPtr > originalFilenamePartPtr) originalFilenamePartPtr = winOriginalFilenamePartPtr;

                size_t dirPathLen = 0;
                if (originalFilenamePartPtr) { 
                    dirPathLen = (originalFilenamePartPtr + 1) - fullFilePath;
                    strncpy(variedPath, fullFilePath, dirPathLen); 
                    variedPath[dirPathLen] = '\0'; 
                    strcpy(filenameBaseCopy, originalFilenamePartPtr + 1); 
                } else { 
                    strcpy(filenameBaseCopy, fullFilePath);
                    variedPath[0] = '\0'; 
                }

                if (strlen(filenameBaseCopy) > 0) {

                    // Attempt 2: All-Lowercase Filename
                    strcpy(currentVariation, filenameBaseCopy);
                    StrToLower(currentVariation);
                    strcpy(variedPath + dirPathLen, currentVariation); 
                    info->file = fOpen(variedPath, openModes[fileMode - 1]);

                    // Attempt 3: All-Uppercase Filename
                    if (!info->file) {
                        strcpy(currentVariation, filenameBaseCopy); 
                        StrToUpper(currentVariation);
                        strcpy(variedPath + dirPathLen, currentVariation);
                        info->file = fOpen(variedPath, openModes[fileMode - 1]);
                    }

                    // Attempt 4: Title Case Filename
                    if (!info->file) {
                        strcpy(currentVariation, filenameBaseCopy); 
                        StrToTitleCase(currentVariation);
                        strcpy(variedPath + dirPathLen, currentVariation);
                        info->file = fOpen(variedPath, openModes[fileMode - 1]);
                    }

                    // Attempt 5: <Name><Number><Letter>.ext Heuristic
                    if (!info->file) {
                        strcpy(currentVariation, filenameBaseCopy); // Use original-case filename part

                        int lenFN = strlen(currentVariation);
                        const char *dotFN = strrchr(currentVariation, '.');

                        if (dotFN && dotFN != currentVariation && (dotFN - currentVariation) >= 3) { 
                            int dotPosFN = dotFN - currentVariation;
                            int letterPosFN = dotPosFN - 1; 
                            int digitPosFN = letterPosFN - 1;

                            if (isalpha((unsigned char)currentVariation[letterPosFN]) && isdigit((unsigned char)currentVariation[digitPosFN])) {
                                char originalLetterFN = currentVariation[letterPosFN];
                                char flippedLetterFN = 0;
                                if (islower(originalLetterFN)) flippedLetterFN = toupper(originalLetterFN);
                                else if (isupper(originalLetterFN)) flippedLetterFN = tolower(originalLetterFN);

                                if (flippedLetterFN) {
                                    currentVariation[letterPosFN] = flippedLetterFN;
                                    strcpy(variedPath + dirPathLen, currentVariation); 
                                    info->file = fOpen(variedPath, openModes[fileMode - 1]);
                                }
                            }
                        }
                    } // End of Attempt 5

                    // Attempt 6: NEW RattleKiller.bin Heuristic
                    if (!info->file) {
                        char lowerFilenameCompare[0x100]; 
                        strcpy(lowerFilenameCompare, filenameBaseCopy); 
                        StrToLower(lowerFilenameCompare); 

                        if (strcmp(lowerFilenameCompare, "rattlekiller.bin") == 0) {
                            strcpy(variedPath + dirPathLen, "RattleKiller.bin"); 
                            info->file = fOpen(variedPath, openModes[fileMode - 1]);
                        }
                    } // End of Attempt 6

                    // Attempt 7: Specific Heuristic for Scene<N><V>.bin (was Attempt 6)
                    if (!info->file) {
                        char lowerOriginalFilenameForPattern[0x100]; 
                        strcpy(lowerOriginalFilenameForPattern, filenameBaseCopy); 
                        StrToLower(lowerOriginalFilenameForPattern); 

                        int fnLen = strlen(lowerOriginalFilenameForPattern);
                        if (fnLen == 11 && strncmp(lowerOriginalFilenameForPattern, "scene", 5) == 0 && isdigit(lowerOriginalFilenameForPattern[5]) && strcmp(lowerOriginalFilenameForPattern + 7, ".bin") == 0) {
                            char sceneHeuristicPath[0x100];
                            strncpy(sceneHeuristicPath, fullFilePath, dirPathLen); 
                            sceneHeuristicPath[dirPathLen] = '\0'; 

                            char sceneHeuristicFilename[0x100];
                            strcpy(sceneHeuristicFilename, "Scene"); 
                            sceneHeuristicFilename[5] = filenameBaseCopy[5]; 
                            
                            char originalVariantChar = filenameBaseCopy[6]; 
                            char flippedVariantChar = 0;

                            if (islower(originalVariantChar)) flippedVariantChar = toupper(originalVariantChar);
                            else if (isupper(originalVariantChar)) flippedVariantChar = tolower(originalVariantChar);
                            
                            if (flippedVariantChar) {
                                sceneHeuristicFilename[6] = flippedVariantChar;
                                strcpy(sceneHeuristicFilename + 7, filenameBaseCopy + 7); 
                                strcat(sceneHeuristicPath, sceneHeuristicFilename); 
                                info->file = fOpen(sceneHeuristicPath, openModes[fileMode - 1]);
                            }
                        }
                    }
                } 
            } 
        } else if (fileMode == FMODE_WB) { // Write mode: use original path, no case variation
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile]   Attempting fOpen (write mode) on external file: '%s' with mode '%s'", fullFilePath, openModes[fileMode - 1]);
            info->file = fOpen(fullFilePath, openModes[fileMode - 1]);
            RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile]     fOpen result: %p (NULL means failure) for '%s'", info->file, fullFilePath);
        } else { 
             RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile]   Attempting fOpen (fallback) on external file: '%s' with mode '%s'", fullFilePath, openModes[fileMode - 1]);
             info->file = fOpen(fullFilePath, openModes[fileMode - 1]);
             RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile]     fOpen result: %p (NULL means failure) for '%s'", info->file, fullFilePath);
        }
    }
    // The original "if (!info->file)" check later in the function will handle actual "File not found" if all attempts fail.

    if (!info->file) {
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile]   All fOpen attempts FAILED for external file: '%s'", fullFilePath);
#if !RETRO_USE_ORIGINAL_CODE
        PrintLog(PRINT_NORMAL, "File not found: %s", fullFilePath);
#endif
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile] Exiting. File: '%s', Success: %d, fileSize: %d, readPos: %d", filename, 0, info->fileSize, info->readPos);
        return false;
    }

    info->readPos  = 0;
    info->fileSize = 0;

    if (fileMode != FMODE_WB) {
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile]     Attempting fSeek to end for size for '%s'", fullFilePath);
        fSeek(info->file, 0, SEEK_END);
        info->fileSize = (int32)fTell(info->file);
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile]       fTell (fileSize): %d for '%s'", info->fileSize, fullFilePath);
        RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile]     Attempting fSeek to beginning for '%s'", fullFilePath);
        fSeek(info->file, 0, SEEK_SET);
    }
#if !RETRO_USE_ORIGINAL_CODE
    PrintLog(PRINT_NORMAL, "Loaded file %s", fullFilePath);
#endif
    RSDK::PrintLog(RSDK::PRINT_NORMAL, "[LoadFile] Exiting. File: '%s', Success: %d, fileSize: %d, readPos: %d", filename, (info->fileSize > 0 || fileMode == FMODE_WB), info->fileSize, info->readPos);
    return true;
}

void RSDK::GenerateELoadKeys(FileInfo *info, const char *key1, int32 key2)
{
    // This function splits hashes into bytes by casting their integers to byte arrays,
    // which only works as intended on little-endian CPUs.
#if !RETRO_USE_ORIGINAL_CODE
    RETRO_HASH_MD5(hash);
#else
    uint8 hash[0x10];
#endif
    char hashBuffer[0x400];

    // KeyA
    StringUpperCase(hashBuffer, key1);
#if !RETRO_USE_ORIGINAL_CODE
    GEN_HASH_MD5_BUFFER(hashBuffer, hash);

    for (int32 i = 0; i < 4; ++i)
        for (int32 j = 0; j < 4; ++j) info->encryptionKeyA[i * 4 + j] = (hash[i] >> (8 * (j ^ 3))) & 0xFF;
#else
    GEN_HASH_MD5_BUFFER(hashBuffer, (uint32 *)hash);

    for (int32 y = 0; y < 0x10; y += 4) {
        info->encryptionKeyA[y + 3] = hash[y + 0];
        info->encryptionKeyA[y + 2] = hash[y + 1];
        info->encryptionKeyA[y + 1] = hash[y + 2];
        info->encryptionKeyA[y + 0] = hash[y + 3];
    }
#endif

    // KeyB
    sprintf_s(hashBuffer, sizeof(hashBuffer), "%d", key2);
#if !RETRO_USE_ORIGINAL_CODE
    GEN_HASH_MD5_BUFFER(hashBuffer, hash);

    for (int32 i = 0; i < 4; ++i)
        for (int32 j = 0; j < 4; ++j) info->encryptionKeyB[i * 4 + j] = (hash[i] >> (8 * (j ^ 3))) & 0xFF;
#else
    GEN_HASH_MD5_BUFFER(hashBuffer, (uint32 *)hash);

    for (int32 y = 0; y < 0x10; y += 4) {
        info->encryptionKeyB[y + 3] = hash[y + 0];
        info->encryptionKeyB[y + 2] = hash[y + 1];
        info->encryptionKeyB[y + 1] = hash[y + 2];
        info->encryptionKeyB[y + 0] = hash[y + 3];
    }
#endif
}

void RSDK::DecryptBytes(FileInfo *info, void *buffer, size_t size)
{
    if (size) {
        uint8 *data = (uint8 *)buffer;
        while (size > 0) {
            *data ^= info->eKeyNo ^ info->encryptionKeyB[info->eKeyPosB];
            if (info->eNybbleSwap)
                *data = ((*data << 4) + (*data >> 4)) & 0xFF;
            *data ^= info->encryptionKeyA[info->eKeyPosA];

            info->eKeyPosA++;
            info->eKeyPosB++;

            if (info->eKeyPosA <= 15) {
                if (info->eKeyPosB > 12) {
                    info->eKeyPosB = 0;
                    info->eNybbleSwap ^= 1;
                }
            }
            else if (info->eKeyPosB <= 8) {
                info->eKeyPosA = 0;
                info->eNybbleSwap ^= 1;
            }
            else {
                info->eKeyNo += 2;
                info->eKeyNo &= 0x7F;

                if (info->eNybbleSwap) {
                    info->eNybbleSwap = false;

                    info->eKeyPosA = info->eKeyNo % 7;
                    info->eKeyPosB = (info->eKeyNo % 12) + 2;
                }
                else {
                    info->eNybbleSwap = true;

                    info->eKeyPosA = (info->eKeyNo % 12) + 3;
                    info->eKeyPosB = info->eKeyNo % 7;
                }
            }

            ++data;
            --size;
        }
    }
}
void RSDK::SkipBytes(FileInfo *info, int32 size)
{
    if (size) {
        while (size > 0) {
            info->eKeyPosA++;
            info->eKeyPosB++;

            if (info->eKeyPosA <= 15) {
                if (info->eKeyPosB > 12) {
                    info->eKeyPosB = 0;
                    info->eNybbleSwap ^= 1;
                }
            }
            else if (info->eKeyPosB <= 8) {
                info->eKeyPosA = 0;
                info->eNybbleSwap ^= 1;
            }
            else {
                info->eKeyNo += 2;
                info->eKeyNo &= 0x7F;

                if (info->eNybbleSwap) {
                    info->eNybbleSwap = false;

                    info->eKeyPosA = info->eKeyNo % 7;
                    info->eKeyPosB = (info->eKeyNo % 12) + 2;
                }
                else {
                    info->eNybbleSwap = true;

                    info->eKeyPosA = (info->eKeyNo % 12) + 3;
                    info->eKeyPosB = info->eKeyNo % 7;
                }
            }

            --size;
        }
    }
}

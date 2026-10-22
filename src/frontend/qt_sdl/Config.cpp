/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <iostream>
#include <fstream>
#include <mutex>
#include <filesystem>
#include <regex>
#include "toml/toml.hpp"

#include "Platform.h"
#include "Config.h"
#include "ScreenLayout.h"
#include "main.h"

using namespace std::string_literals;


namespace Config
{
using namespace melonDS;


const char* kConfigFile = "melonRA.toml";
// config of a plain melonDS install (or an older melonRA build) in the same folder,
// imported the first time melonRA starts there
const char* kMelonDSConfigFile = "melonDS.toml";

const char* kLegacyConfigFile = "melonDS.ini";
const char* kLegacyUniqueConfigFile = "melonDS.%d.ini";

toml::value RootTable;

DefaultList<int> DefaultInts =
{
    {"Instance*.Keyboard", -1},
    {"Instance*.Joystick", -1},
    {"Instance*.Window*.Width", 256},
    {"Instance*.Window*.Height", 384},
    {"Screen.VSyncInterval", 1},
    {"3D.Renderer", renderer3D_Software},
    {"3D.GL.ScaleFactor", 1},
#ifdef JIT_ENABLED
    {"JIT.MaxBlockSize", 32},
#endif
    {"Instance*.Firmware.Language", 1},
    {"Instance*.Firmware.BirthdayMonth", 1},
    {"Instance*.Firmware.BirthdayDay", 1},
    {"MP.AudioMode", 1},
    {"MP.RecvTimeout", 25},
    {"Instance*.Audio.Volume", 256},
    {"Mic.InputType", 1},
    {"Mouse.HideSeconds", 5},
    {"Instance*.DSi.Battery.Level", 0xF},
#ifdef GDBSTUB_ENABLED
    {"Instance*.Gdb.ARM7.Port", 3334},
    {"Instance*.Gdb.ARM9.Port", 3333},
#endif
    {"LAN.HostNumPlayers", 16},
};

RangeList IntRanges =
{
    {"Emu.ConsoleType", {0, 1}},
    {"3D.Renderer", {0, renderer3D_Max-1}},
    {"Screen.VSyncInterval", {1, 20}},
    {"3D.GL.ScaleFactor", {1, 16}},
    {"Audio.Interpolation", {0, 4}},
    {"Instance*.Audio.Volume", {0, 256}},
    {"Mic.InputType", {0, micInputType_MAX-1}},
    {"Instance*.Window*.ScreenRotation", {0, screenRot_MAX-1}},
    {"Instance*.Window*.ScreenGap", {0, 500}},
    {"Instance*.Window*.ScreenLayout", {0, screenLayout_MAX-1}},
    {"Instance*.Window*.ScreenSizing", {0, screenSizing_MAX-1}},
    {"Instance*.Window*.ScreenAspectTop", {0, AspectRatiosNum-1}},
    {"Instance*.Window*.ScreenAspectBot", {0, AspectRatiosNum-1}},
    {"MP.AudioMode", {0, 2}},
    {"LAN.HostNumPlayers", {2, 16}},
};

DefaultList<bool> DefaultBools =
{
    {"Screen.Filter", true},
    {"3D.Soft.Threaded", true},
    {"3D.GL.HiresCoordinates", true},
    {"LimitFPS", true},
    {"Instance*.Window*.ShowOSD", true},
    {"Emu.DirectBoot", true},
    {"Instance*.DS.Battery.LevelOkay", true},
    {"Instance*.DSi.Battery.Charging", true},
#ifdef JIT_ENABLED
    {"JIT.BranchOptimisations", true},
    {"JIT.LiteralOptimisations", true},
#ifndef __APPLE__
    {"JIT.FastMemory", true},
#endif
#endif
    {"DSi.DSP.HLE", true},
    {"Instance*.RTC.SyncToHost", true},

    {"RetroAchievements.Enabled", true},
    {"RetroAchievements.Hardcore", true},
    {"RetroAchievements.Notifications", true},
    {"RetroAchievements.LeaderboardNotifications", true},
    {"RetroAchievements.ChallengeIndicators", true},
    {"RetroAchievements.ProgressIndicators", true},
    {"RetroAchievements.LeaderboardTrackers", true},
    {"RetroAchievements.Sounds", true},
    {"RetroAchievements.ShowAccountOnStart", true},
    {"RetroAchievements.OfflineSessions", true},
    {"GameList.Enabled", true},
    {"GameList.SearchSubfolders", true},
    {"GameList.RegionNames", true},
    {"GameList.CoversGameTDB", true},
    {"GameList.CoversLibretro", true},
    {"RetroAchievements.NotificationsBold", true},
    {"RetroAchievements.LeaderboardNotificationsBold", true},
    {"RetroAchievements.ChallengeIndicatorsBold", true},
    {"RetroAchievements.ProgressIndicatorsBold", true},
    {"RetroAchievements.LeaderboardTrackersBold", true},
};

DefaultList<std::string> DefaultStrings =
{
    {"DLDI.ImagePath",                  "dldi.bin"},
    {"DSi.SD.ImagePath",                "dsisd.bin"},
    {"Instance*.Firmware.Username",     "melonRA"}
};

DefaultList<double> DefaultDoubles =
{
    {"TargetFPS", 60.0},
    {"FastForwardFPS", 1000.0},
    {"SlowmoFPS", 30.0},
};

LegacyEntry LegacyFile[] =
{
    {"Key_A",      0, "Keyboard.A", true},
    {"Key_B",      0, "Keyboard.B", true},
    {"Key_Select", 0, "Keyboard.Select", true},
    {"Key_Start",  0, "Keyboard.Start", true},
    {"Key_Right",  0, "Keyboard.Right", true},
    {"Key_Left",   0, "Keyboard.Left", true},
    {"Key_Up",     0, "Keyboard.Up", true},
    {"Key_Down",   0, "Keyboard.Down", true},
    {"Key_R",      0, "Keyboard.R", true},
    {"Key_L",      0, "Keyboard.L", true},
    {"Key_X",      0, "Keyboard.X", true},
    {"Key_Y",      0, "Keyboard.Y", true},

    {"Joy_A",      0, "Joystick.A", true},
    {"Joy_B",      0, "Joystick.B", true},
    {"Joy_Select", 0, "Joystick.Select", true},
    {"Joy_Start",  0, "Joystick.Start", true},
    {"Joy_Right",  0, "Joystick.Right", true},
    {"Joy_Left",   0, "Joystick.Left", true},
    {"Joy_Up",     0, "Joystick.Up", true},
    {"Joy_Down",   0, "Joystick.Down", true},
    {"Joy_R",      0, "Joystick.R", true},
    {"Joy_L",      0, "Joystick.L", true},
    {"Joy_X",      0, "Joystick.X", true},
    {"Joy_Y",      0, "Joystick.Y", true},

    {"HKKey_Lid",                 0, "Keyboard.HK_Lid", true},
    {"HKKey_Mic",                 0, "Keyboard.HK_Mic", true},
    {"HKKey_Pause",               0, "Keyboard.HK_Pause", true},
    {"HKKey_Reset",               0, "Keyboard.HK_Reset", true},
    {"HKKey_FastForward",         0, "Keyboard.HK_FastForward", true},
    {"HKKey_FastForwardToggle",   0, "Keyboard.HK_FrameLimitToggle", true},
    {"HKKey_FullscreenToggle",    0, "Keyboard.HK_FullscreenToggle", true},
    {"HKKey_SwapScreens",         0, "Keyboard.HK_SwapScreens", true},
    {"HKKey_SwapScreenEmphasis",  0, "Keyboard.HK_SwapScreenEmphasis", true},
    {"HKKey_SolarSensorDecrease", 0, "Keyboard.HK_SolarSensorDecrease", true},
    {"HKKey_SolarSensorIncrease", 0, "Keyboard.HK_SolarSensorIncrease", true},
    {"HKKey_FrameStep",           0, "Keyboard.HK_FrameStep", true},
    {"HKKey_PowerButton",         0, "Keyboard.HK_PowerButton", true},
    {"HKKey_VolumeUp",            0, "Keyboard.HK_VolumeUp", true},
    {"HKKey_VolumeDown",          0, "Keyboard.HK_VolumeDown", true},
    {"HKKey_GuitarGripGreen",     0, "Keyboard.HK_GuitarGripGreen", true},
    {"HKKey_GuitarGripRed",       0, "Keyboard.HK_GuitarGripRed", true},
    {"HKKey_GuitarGripYellow",    0, "Keyboard.HK_GuitarGripYellow", true},
    {"HKKey_GuitarGripBlue",      0, "Keyboard.HK_GuitarGripBlue", true},

    {"HKJoy_Lid",                 0, "Joystick.HK_Lid", true},
    {"HKJoy_Mic",                 0, "Joystick.HK_Mic", true},
    {"HKJoy_Pause",               0, "Joystick.HK_Pause", true},
    {"HKJoy_Reset",               0, "Joystick.HK_Reset", true},
    {"HKJoy_FastForward",         0, "Joystick.HK_FastForward", true},
    {"HKJoy_FastForwardToggle",   0, "Joystick.HK_FrameLimitToggle", true},
    {"HKJoy_FullscreenToggle",    0, "Joystick.HK_FullscreenToggle", true},
    {"HKJoy_SwapScreens",         0, "Joystick.HK_SwapScreens", true},
    {"HKJoy_SwapScreenEmphasis",  0, "Joystick.HK_SwapScreenEmphasis", true},
    {"HKJoy_SolarSensorDecrease", 0, "Joystick.HK_SolarSensorDecrease", true},
    {"HKJoy_SolarSensorIncrease", 0, "Joystick.HK_SolarSensorIncrease", true},
    {"HKJoy_FrameStep",           0, "Joystick.HK_FrameStep", true},
    {"HKJoy_PowerButton",         0, "Joystick.HK_PowerButton", true},
    {"HKJoy_VolumeUp",            0, "Joystick.HK_VolumeUp", true},
    {"HKJoy_VolumeDown",          0, "Joystick.HK_VolumeDown", true},
    {"HKJoy_GuitarGripGreen",     0, "Joystick.HK_GuitarGripGreen", true},
    {"HKJoy_GuitarGripRed",       0, "Joystick.HK_GuitarGripRed", true},
    {"HKJoy_GuitarGripYellow",    0, "Joystick.HK_GuitarGripYellow", true},
    {"HKJoy_GuitarGripBlue",      0, "Joystick.HK_GuitarGripBlue", true},

    {"JoystickID", 0, "JoystickID", true},

    {"ScreenRotation", 0, "Window0.ScreenRotation", true},
    {"ScreenGap",      0, "Window0.ScreenGap", true},
    {"ScreenLayout",   0, "Window0.ScreenLayout", true},
    {"ScreenSwap",     1, "Window0.ScreenSwap", true},
    {"ScreenSizing",   0, "Window0.ScreenSizing", true},
    {"IntegerScaling", 1, "Window0.IntegerScaling", true},
    {"ScreenAspectTop",0, "Window0.ScreenAspectTop", true},
    {"ScreenAspectBot",0, "Window0.ScreenAspectBot", true},

    {"ScreenFilter",        1, "Screen.Filter", false},
    {"ScreenUseGL",         1, "Screen.UseGL", false},
    {"ScreenVSync",         1, "Screen.VSync", false},
    {"ScreenVSyncInterval", 0, "Screen.VSyncInterval", false},

    {"3DRenderer", 0, "3D.Renderer", false},
    {"Threaded3D", 1, "3D.Soft.Threaded", false},

    {"GL_ScaleFactor", 0, "3D.GL.ScaleFactor", false},
    {"GL_BetterPolygons", 1, "3D.GL.BetterPolygons", false},
    {"GL_HiresCoordinates", 1, "3D.GL.HiresCoordinates", false},

    {"LimitFPS", 1, "LimitFPS", false},
    {"MaxFPS", 0, "MaxFPS", false},
    {"AudioSync", 1, "AudioSync", false},
    {"ShowOSD", 1, "Window0.ShowOSD", true},

    {"ConsoleType", 0, "Emu.ConsoleType", false},
    {"DirectBoot", 1, "Emu.DirectBoot", false},

#ifdef JIT_ENABLED
    {"JIT_Enable", 1, "JIT.Enable", false},
    {"JIT_MaxBlockSize", 0, "JIT.MaxBlockSize", false},
    {"JIT_BranchOptimisations", 1, "JIT.BranchOptimisations", false},
    {"JIT_LiteralOptimisations", 1, "JIT.LiteralOptimisations", false},
    {"JIT_FastMemory", 1, "JIT.FastMemory", false},
#endif

    {"ExternalBIOSEnable", 1, "Emu.ExternalBIOSEnable", false},

    {"BIOS9Path", 2, "DS.BIOS9Path", false},
    {"BIOS7Path", 2, "DS.BIOS7Path", false},
    {"FirmwarePath", 2, "DS.FirmwarePath", false},

    {"DSiExternalBIOSEnable", 1, "DSi.ExternalBIOSEnable", false},

    {"DSiBIOS9Path", 2, "DSi.BIOS9Path", false},
    {"DSiBIOS7Path", 2, "DSi.BIOS7Path", false},
    {"DSiFirmwarePath", 2, "DSi.FirmwarePath", false},
    {"DSiNANDPath", 2, "DSi.NANDPath", false},

    {"DLDIEnable", 1, "DLDI.Enable", false},
    {"DLDISDPath", 2, "DLDI.ImagePath", false},
    {"DLDISize", 0, "DLDI.ImageSize", false},
    {"DLDIReadOnly", 1, "DLDI.ReadOnly", false},
    {"DLDIFolderSync", 1, "DLDI.FolderSync", false},
    {"DLDIFolderPath", 2, "DLDI.FolderPath", false},

    {"DSiSDEnable", 1, "DSi.SD.Enable", false},
    {"DSiSDPath", 2, "DSi.SD.ImagePath", false},
    {"DSiSDSize", 0, "DSi.SD.ImageSize", false},
    {"DSiSDReadOnly", 1, "DSi.SD.ReadOnly", false},
    {"DSiSDFolderSync", 1, "DSi.SD.FolderSync", false},
    {"DSiSDFolderPath", 2, "DSi.SD.FolderPath", false},

    {"FirmwareOverrideSettings", 1, "Firmware.OverrideSettings", true},
    {"FirmwareUsername", 2, "Firmware.Username", true},
    {"FirmwareLanguage", 0, "Firmware.Language", true},
    {"FirmwareBirthdayMonth", 0, "Firmware.BirthdayMonth", true},
    {"FirmwareBirthdayDay", 0, "Firmware.BirthdayDay", true},
    {"FirmwareFavouriteColour", 0, "Firmware.FavouriteColour", true},
    {"FirmwareMessage", 2, "Firmware.Message", true},
    {"FirmwareMAC", 2, "Firmware.MAC", true},

    {"MPAudioMode", 0, "MP.AudioMode", false},
    {"MPRecvTimeout", 0, "MP.RecvTimeout", false},

    {"LANDevice", 2, "LAN.Device", false},
    {"DirectLAN", 1, "LAN.DirectMode", false},

    {"AudioInterp", 0, "Audio.Interpolation", false},
    {"AudioBitDepth", 0, "Audio.BitDepth", false},
    {"AudioVolume", 0, "Audio.Volume", true},
    {"DSiVolumeSync", 1, "Audio.DSiVolumeSync", true},
    {"MicInputType", 0, "Mic.InputType", false},
    {"MicDevice", 2, "Mic.Device", false},
    {"MicWavPath", 2, "Mic.WavPath", false},

    {"LastROMFolder", 2, "LastROMFolder", false},
    {"LastBIOSFolder", 2, "LastBIOSFolder", false},

    {"RecentROM_0", 4, "RecentROM[0]", false},
    {"RecentROM_1", 4, "RecentROM[1]", false},
    {"RecentROM_2", 4, "RecentROM[2]", false},
    {"RecentROM_3", 4, "RecentROM[3]", false},
    {"RecentROM_4", 4, "RecentROM[4]", false},
    {"RecentROM_5", 4, "RecentROM[5]", false},
    {"RecentROM_6", 4, "RecentROM[6]", false},
    {"RecentROM_7", 4, "RecentROM[7]", false},
    {"RecentROM_8", 4, "RecentROM[8]", false},
    {"RecentROM_9", 4, "RecentROM[9]", false},

    {"SaveFilePath", 2, "SaveFilePath", true},
    {"SavestatePath", 2, "SavestatePath", true},
    {"CheatFilePath", 2, "CheatFilePath", true},

    {"EnableCheats", 1, "EnableCheats", true},

    {"MouseHide",        1, "Mouse.Hide", false},
    {"MouseHideSeconds", 0, "Mouse.HideSeconds", false},
    {"PauseLostFocus",   1, "PauseLostFocus", false},
    {"MuteFastForward",   1, "MuteFastForward", false},
    {"UITheme",          2, "UITheme", false},

    {"RTCOffset",       3, "RTC.Offset", true},

    {"DSBatteryLevelOkay",   1, "DS.Battery.LevelOkay", true},
    {"DSiBatteryLevel",    0, "DSi.Battery.Level", true},
    {"DSiBatteryCharging", 1, "DSi.Battery.Charging", true},

#ifdef GDBSTUB_ENABLED
    {"GdbEnabled", 1, "Gdb.Enabled", false},
    {"GdbPortARM7", 0, "Gdb.ARM7.Port", true},
    {"GdbPortARM9", 0, "Gdb.ARM9.Port", true},
    {"GdbARM7BreakOnStartup", 1, "Gdb.ARM7.BreakOnStartup", true},
    {"GdbARM9BreakOnStartup", 1, "Gdb.ARM9.BreakOnStartup", true},
#endif

    {"Camera0_InputType", 0, "DSi.Camera0.InputType", false},
    {"Camera0_ImagePath", 2, "DSi.Camera0.ImagePath", false},
    {"Camera0_CamDeviceName", 2, "DSi.Camera0.DeviceName", false},
    {"Camera0_XFlip", 1, "DSi.Camera0.XFlip", false},
    {"Camera1_InputType", 0, "DSi.Camera1.InputType", false},
    {"Camera1_ImagePath", 2, "DSi.Camera1.ImagePath", false},
    {"Camera1_CamDeviceName", 2, "DSi.Camera1.DeviceName", false},
    {"Camera1_XFlip", 1, "DSi.Camera1.XFlip", false},

    {"", -1, "", false}
};


static std::string GetDefaultKey(std::string path)
{
    std::string tables[] = {"Instance", "Window", "Camera"};

    std::string ret = "";
    int plen = path.length();
    for (int i = 0; i < plen;)
    {
        bool found = false;

        for (auto& tbl : tables)
        {
            int tlen = tbl.length();
            if ((plen-i) <= tlen) continue;
            if (path.substr(i, tlen) != tbl) continue;
            if (path[i+tlen] < '0' || path[i+tlen] > '9') continue;

            ret += tbl + "*";
            i = path.find('.', i+tlen);
            if (i == std::string::npos) return ret;

            found = true;
            break;
        }

        if (!found)
        {
            ret += path[i];
            i++;
        }
    }

    return ret;
}


Array::Array(toml::value& data) : Data(data)
{
}

size_t Array::Size()
{
    return Data.size();
}

void Array::Clear()
{
    toml::array newarray;
    Data = newarray;
}

Array Array::GetArray(const int id)
{
    while (Data.size() < id+1)
        Data.push_back(toml::array());

    toml::value& arr = Data[id];
    if (!arr.is_array())
        arr = toml::array();

    return Array(arr);
}

int Array::GetInt(const int id)
{
    while (Data.size() < id+1)
        Data.push_back(0);

    toml::value& tval = Data[id];
    if (!tval.is_integer())
        tval = 0;

    return (int)tval.as_integer();
}

int64_t Array::GetInt64(const int id)
{
    while (Data.size() < id+1)
        Data.push_back(0);

    toml::value& tval = Data[id];
    if (!tval.is_integer())
        tval = 0;

    return tval.as_integer();
}

bool Array::GetBool(const int id)
{
    while (Data.size() < id+1)
        Data.push_back(false);

    toml::value& tval = Data[id];
    if (!tval.is_boolean())
        tval = false;

    return tval.as_boolean();
}

std::string Array::GetString(const int id)
{
    while (Data.size() < id+1)
        Data.push_back("");

    toml::value& tval = Data[id];
    if (!tval.is_string())
        tval = "";

    return tval.as_string();
}

double Array::GetDouble(const int id)
{
    while (Data.size() < id+1)
        Data.push_back(0.0);

    toml::value& tval = Data[id];
    if (!tval.is_floating())
        tval = 0.0;

    return tval.as_floating();
}

void Array::SetInt(const int id, int val)
{
    while (Data.size() < id+1)
        Data.push_back(0);

    toml::value& tval = Data[id];
    tval = val;
}

void Array::SetInt64(const int id, int64_t val)
{
    while (Data.size() < id+1)
        Data.push_back(0);

    toml::value& tval = Data[id];
    tval = val;
}

void Array::SetBool(const int id, bool val)
{
    while (Data.size() < id+1)
        Data.push_back(false);

    toml::value& tval = Data[id];
    tval = val;
}

void Array::SetString(const int id, const std::string& val)
{
    while (Data.size() < id+1)
        Data.push_back("");

    toml::value& tval = Data[id];
    tval = val;
}

void Array::SetDouble(const int id, double val)
{
    while (Data.size() < id+1)
        Data.push_back(0.0);

    toml::value& tval = Data[id];
    tval = val;
}


/*Table::Table()// : Data(toml::value())
{
    Data = toml::value();
    PathPrefix = "";
}*/

// --- per-game settings ---

static std::mutex activeGameLock;
static std::string activeGame;
static thread_local int usualDepth = 0;

UsualSettings::UsualSettings() { usualDepth++; }
UsualSettings::~UsualSettings() { usualDepth--; }

std::string ActiveGame()
{
    std::lock_guard<std::mutex> guard(activeGameLock);
    return activeGame;
}

void SetActiveGame(const std::string& id)
{
    std::lock_guard<std::mutex> guard(activeGameLock);
    activeGame = id;
}

std::string GameSettingsId(const std::string& gameCode, const std::string& fileName)
{
    bool valid = gameCode.size() == 4 && gameCode != "####";
    for (char c : gameCode)
        if (!isalnum((unsigned char)c)) valid = false;
    if (valid) return gameCode;

    // homebrew has no real game code: go by the file name
    std::string name = fileName;
    for (char& c : name) c = (char)tolower((unsigned char)c);
    return "file:" + name;
}

// the game's own table, or null (no lookups that would add entries)
static toml::value* gameTable(const std::string& id)
{
    if (id.empty() || !RootTable.is_table() || !RootTable.contains("GameSettings")) return nullptr;
    toml::value& all = RootTable.at("GameSettings");
    if (!all.is_table() || !all.contains(id)) return nullptr;
    toml::value& game = all.at(id);
    return game.is_table() ? &game : nullptr;
}

// only the first instance runs games with their own settings
static bool appliesTo(const std::string& fullPath)
{
    return fullPath.compare(0, 8, "Instance") != 0 || fullPath.compare(0, 10, "Instance0.") == 0;
}

// the running game's value for this setting, if it has one
static toml::value* gameValue(const std::string& fullPath)
{
    if (usualDepth > 0 || !appliesTo(fullPath)) return nullptr;
    toml::value* game = gameTable(ActiveGame());
    if (!game || !game->contains(fullPath)) return nullptr;
    return &game->at(fullPath);
}

bool HasGameSettings(const std::string& id)
{
    toml::value* game = gameTable(id);
    return game && !game->as_table().empty();
}

QVariantMap GetGameSettings(const std::string& id)
{
    QVariantMap out;
    toml::value* game = gameTable(id);
    if (!game) return out;
    for (const auto& [key, val] : game->as_table())
    {
        const QString k = QString::fromStdString(key);
        if (val.is_boolean()) out[k] = val.as_boolean();
        else if (val.is_integer()) out[k] = (qlonglong)val.as_integer();
        else if (val.is_string()) out[k] = QString::fromStdString(val.as_string());
    }
    return out;
}

void SetGameSettings(const std::string& id, const QVariantMap& values)
{
    if (id.empty()) return;
    if (!RootTable.is_table()) RootTable = toml::table();

    if (values.isEmpty())
    {
        if (RootTable.contains("GameSettings") && RootTable.at("GameSettings").is_table())
            RootTable.at("GameSettings").as_table().erase(id);
        return;
    }

    toml::table game;
    for (auto it = values.begin(); it != values.end(); ++it)
    {
        const std::string key = it.key().toStdString();
        const QVariant& v = it.value();
        if (v.typeId() == QMetaType::Bool) game[key] = v.toBool();
        else if (v.typeId() == QMetaType::QString) game[key] = v.toString().toStdString();
        else game[key] = (int64_t)v.toLongLong();
    }
    toml::value& all = RootTable["GameSettings"];
    if (!all.is_table()) all = toml::table();
    all.as_table()[id] = game;
}

Table::Table(toml::value& data, const std::string& path) : Data(data)
{
    if (path.empty())
        PathPrefix = "";
    else
        PathPrefix = path + ".";
}

Table& Table::operator=(const Table& b)
{
    Data = b.Data;
    PathPrefix = b.PathPrefix;

    return *this;
}

Array Table::GetArray(const std::string& path)
{
    toml::value& arr = ResolvePath(path);
    if (!arr.is_array())
        arr = toml::array();

    return Array(arr);
}

Table Table::GetTable(const std::string& path, const std::string& defpath)
{
    toml::value& tbl = ResolvePath(path);
    if (!tbl.is_table())
    {
        toml::value defval = toml::table();
        if (!defpath.empty())
            defval = ResolvePath(defpath);

        tbl = defval;
    }

    return Table(tbl, PathPrefix + path);
}

int Table::GetInt(const std::string& path)
{
    int ret;
    if (toml::value* game = gameValue(PathPrefix + path); game && game->is_integer())
        ret = (int)game->as_integer();
    else
    {
        toml::value& tval = ResolvePath(path);
        if (!tval.is_integer())
            tval = FindDefault(path, 0, DefaultInts);
        ret = (int)tval.as_integer();
    }

    std::string rngkey = GetDefaultKey(PathPrefix+path);
    if (IntRanges.count(rngkey) != 0)
    {
        auto& range = IntRanges[rngkey];
        ret = std::clamp(ret, std::get<0>(range), std::get<1>(range));
    }

    return ret;
}

int64_t Table::GetInt64(const std::string& path)
{
    toml::value& tval = ResolvePath(path);
    if (!tval.is_integer())
        tval = 0;

    return tval.as_integer();
}

bool Table::GetBool(const std::string& path)
{
    if (toml::value* game = gameValue(PathPrefix + path); game && game->is_boolean())
        return game->as_boolean();

    toml::value& tval = ResolvePath(path);
    if (!tval.is_boolean())
        tval = FindDefault(path, false, DefaultBools);

    return tval.as_boolean();
}

std::string Table::GetString(const std::string& path)
{
    if (toml::value* game = gameValue(PathPrefix + path); game && game->is_string())
        return game->as_string();

    toml::value& tval = ResolvePath(path);
    if (!tval.is_string())
        tval = FindDefault(path, ""s, DefaultStrings);

    return tval.as_string();
}

double Table::GetDouble(const std::string& path)
{
    toml::value& tval = ResolvePath(path);
    if (!tval.is_floating())
        tval = FindDefault(path, 0.0, DefaultDoubles);

    return tval.as_floating();
}

void Table::SetInt(const std::string& path, int val)
{
    std::string rngkey = GetDefaultKey(PathPrefix+path);
    if (IntRanges.count(rngkey) != 0)
    {
        auto& range = IntRanges[rngkey];
        val = std::clamp(val, std::get<0>(range), std::get<1>(range));
    }

    // a setting the running game has its own value for: change the game's
    if (toml::value* game = gameValue(PathPrefix + path)) { *game = val; return; }

    toml::value& tval = ResolvePath(path);
    tval = val;
}

void Table::SetInt64(const std::string& path, int64_t val)
{
    toml::value& tval = ResolvePath(path);
    tval = val;
}

void Table::SetBool(const std::string& path, bool val)
{
    if (toml::value* game = gameValue(PathPrefix + path)) { *game = val; return; }

    toml::value& tval = ResolvePath(path);
    tval = val;
}

void Table::SetString(const std::string& path, const std::string& val)
{
    if (toml::value* game = gameValue(PathPrefix + path)) { *game = val; return; }

    toml::value& tval = ResolvePath(path);
    tval = val;
}

void Table::SetDouble(const std::string& path, double val)
{
    toml::value& tval = ResolvePath(path);
    toml::floating_format_info info = {.prec=10};
    tval = toml::value(val, info);
}

toml::value& Table::ResolvePath(const std::string& path)
{
    toml::value* ret = &Data;
    std::string tmp = path;

    size_t sep;
    while ((sep = tmp.find('.')) != std::string::npos)
    {
        ret = &(*ret)[tmp.substr(0, sep)];
        tmp = tmp.substr(sep+1);
    }

    return (*ret)[tmp];
}

template<typename T> T Table::FindDefault(const std::string& path, T def, DefaultList<T> list)
{
    std::string defkey = GetDefaultKey(PathPrefix+path);

    T ret = def;
    while (list.count(defkey) == 0)
    {
        if (defkey.empty()) break;
        size_t sep = defkey.rfind('.');
        if (sep == std::string::npos) break;
        defkey = defkey.substr(0, sep);
    }
    if (list.count(defkey) != 0)
        ret = list[defkey];

    return ret;
}


bool LoadLegacyFile(int inst)
{
    Platform::FileHandle* f;
    if (inst > 0)
    {
        char name[100] = {0};
        snprintf(name, 99, kLegacyUniqueConfigFile, inst+1);
        f = Platform::OpenLocalFile(name, Platform::FileMode::ReadText);
    }
    else
    {
        f = Platform::OpenLocalFile(kLegacyConfigFile, Platform::FileMode::ReadText);
    }

    if (!f) return true;

    toml::value* root;// = GetLocalTable(inst);
    if (inst == -1)
        root = &RootTable;
    else
        root = &RootTable["Instance" + std::to_string(inst)];

    char linebuf[1024];
    char entryname[32];
    char entryval[1024];
    while (!Platform::IsEndOfFile(f))
    {
        if (!Platform::FileReadLine(linebuf, 1024, f))
            break;

        int ret = sscanf(linebuf, "%31[A-Za-z_0-9]=%[^\t\r\n]", entryname, entryval);
        entryname[31] = '\0';
        if (ret < 2) continue;

        for (LegacyEntry* entry = &LegacyFile[0]; entry->Type != -1; entry++)
        {
            if (!strncmp(entry->Name, entryname, 32))
            {
                if (!(entry->InstanceUnique ^ (inst == -1)))
                    break;

                std::string path = entry->TOMLPath;
                toml::value* table = root;
                size_t sep;
                while ((sep = path.find('.')) != std::string::npos)
                {
                    table = &(*table)[path.substr(0, sep)];
                    path = path.substr(sep+1);
                }

                int arrayid = -1;
                if (path[path.size()-1] == ']')
                {
                    size_t tmp = path.rfind('[');
                    arrayid = std::stoi(path.substr(tmp+1, path.size()-tmp-2));
                    path = path.substr(0, tmp);
                }

                toml::value& val = (*table)[path];

                switch (entry->Type)
                {
                    case 0:
                        val = strtol(entryval, nullptr, 10);
                        break;

                    case 1:
                        val = !!strtol(entryval, nullptr, 10);
                        break;

                    case 2:
                        val = entryval;
                        break;

                    case 3:
                        val = strtoll(entryval, nullptr, 10);
                        break;

                    case 4:
                        if (!val.is_array()) val = toml::array();
                        while (val.size() < arrayid+1)
                            val.push_back("");
                        val[arrayid] = entryval;
                        break;
                }

                break;
            }
        }
    }

    CloseFile(f);
    return true;
}

bool LoadLegacy()
{
    for (int i = -1; i < 16; i++)
        LoadLegacyFile(i);

    return true;
}

bool Load()
{
    auto cfgpath = Platform::GetLocalFilePath(kConfigFile);

    if (!Platform::CheckFileWritable(cfgpath))
        return false;

    RootTable = toml::value();

    if (!Platform::FileExists(cfgpath))
    {
        // carry over settings from melonDS.toml; they are written to
        // melonRA.toml on the next save, and the old file is left untouched
        auto oldpath = Platform::GetLocalFilePath(kMelonDSConfigFile);
        if (!Platform::FileExists(oldpath))
            return LoadLegacy();
        cfgpath = oldpath;
    }

    try
    {
        RootTable = toml::parse(std::filesystem::u8path(cfgpath));
    }
    catch (toml::syntax_error& err)
    {
        //RootTable = toml::table();
    }

    return true;
}

// set once a backup has been restored, until melonRA restarts
static bool savingDisabled = false;

void Save()
{
    if (savingDisabled) return;
    auto cfgpath = Platform::GetLocalFilePath(kConfigFile);
    if (!Platform::CheckFileWritable(cfgpath))
        return;

    std::ofstream file;
    file.open(std::filesystem::u8path(cfgpath), std::ofstream::out | std::ofstream::trunc);
    file << RootTable;
    file.close();
}


// --- backups ---

static const char* kBackupMarker = "melonRABackup";

// settings that point at files or folders on this computer
static bool isPathKey(const std::string& key)
{
    auto endsWith = [&](const char* s)
    {
        size_t n = strlen(s);
        return key.size() >= n && key.compare(key.size() - n, n, s) == 0;
    };
    return endsWith("Path") || endsWith("Folder") || endsWith("Folders") || key == "RecentROM";
}

static void dropPaths(toml::value& v)
{
    if (!v.is_table()) return;
    auto& tbl = v.as_table();
    for (auto it = tbl.begin(); it != tbl.end();)
    {
        if (it->second.is_table())
        {
            dropPaths(it->second);
            ++it;
        }
        else if (isPathKey(it->first) && (it->second.is_string() || it->second.is_array()))
            it = tbl.erase(it);
        else
            ++it;
    }
}

static void eraseKey(toml::value& root, const char* table, const char* key)
{
    if (!root.is_table() || !root.contains(table)) return;
    toml::value& t = root.at(table);
    if (t.is_table()) t.as_table().erase(key);
}

// `from`'s values over `into`, table by table
static void mergeInto(toml::value& into, const toml::value& from)
{
    if (!into.is_table() || !from.is_table())
    {
        into = from;
        return;
    }
    for (const auto& [key, val] : from.as_table())
    {
        auto& tbl = into.as_table();
        auto it = tbl.find(key);
        if (it != tbl.end() && it->second.is_table() && val.is_table())
            mergeInto(it->second, val);
        else
            tbl[key] = val;
    }
}

bool ExportBackup(const std::string& path, bool includePaths, bool includeLogin,
                  const std::string& header, std::string& error)
{
    toml::value data = RootTable;
    if (!data.is_table()) data = toml::table();

    // unique to this installation: it signs the saved unlocks kept here
    eraseKey(data, "RetroAchievements", "QueueKey");
    if (!includeLogin)
    {
        eraseKey(data, "RetroAchievements", "Username");
        eraseKey(data, "RetroAchievements", "Token");
    }
    if (!includePaths) dropPaths(data);

    data.as_table()[kBackupMarker] = toml::table{
        {"Paths", includePaths},
        {"Login", includeLogin},
    };

    std::ofstream file(std::filesystem::u8path(path), std::ofstream::out | std::ofstream::trunc);
    if (!file)
    {
        error = "The file couldn't be created.";
        return false;
    }
    file << header << "\n" << data;
    file.close();
    if (!file)
    {
        error = "The file couldn't be written.";
        return false;
    }
    return true;
}

bool RestoreBackup(const std::string& path, std::string& error)
{
    toml::value data;
    try
    {
        data = toml::parse(std::filesystem::u8path(path));
    }
    catch (std::exception&)
    {
        error = "The file isn't a settings backup, or it's damaged.";
        return false;
    }
    // a backup, or a plain melonRA.toml / melonDS.toml copied by hand
    if (!data.is_table() || (!data.contains(kBackupMarker) && !data.contains("Instance0")))
    {
        error = "The file doesn't contain melonRA settings.";
        return false;
    }
    data.as_table().erase(kBackupMarker);
    eraseKey(data, "RetroAchievements", "QueueKey");

    toml::value merged = RootTable;
    if (!merged.is_table()) merged = toml::table();
    mergeInto(merged, data);

    auto cfgpath = Platform::GetLocalFilePath(kConfigFile);
    if (!Platform::CheckFileWritable(cfgpath))
    {
        error = "melonRA can't write its settings file.";
        return false;
    }

    // keep the current settings, just in case
    std::error_code ec;
    std::filesystem::copy_file(std::filesystem::u8path(cfgpath), std::filesystem::u8path(cfgpath + ".bak"),
                               std::filesystem::copy_options::overwrite_existing, ec);

    std::ofstream file(std::filesystem::u8path(cfgpath), std::ofstream::out | std::ofstream::trunc);
    file << merged;
    file.close();
    if (!file)
    {
        error = "The settings file couldn't be written.";
        return false;
    }

    savingDisabled = true;
    return true;
}

Table GetLocalTable(int instance)
{
    if (instance == -1)
        return Table(RootTable, "");

    std::string key = "Instance" + std::to_string(instance);
    toml::value& tbl = RootTable[key];
    if (tbl.is_empty())
        RootTable[key] = RootTable["Instance0"];

    return Table(tbl, key);
}

}

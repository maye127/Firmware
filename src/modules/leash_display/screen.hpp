#pragma once

#include <uORB/topics/leash_display.h>

class Screen
{
public:
    static void init();
    static void showLogo();
    static void showTest();
    static void showMain(int mode, const char *presetName, int leashBattery, int airdogBattery,
                         int airdogMode, int followMode, int landMode,
                         int leashGPS, int airdogGPS);
    static void showMenu(int buttons, int type, int value, const char *presetName, int activity, const char *text);
    static void showInfo(int info, int error, int leashBattery);
    static void showList(LeashDisplay_Lines lines, int lineCount);
};


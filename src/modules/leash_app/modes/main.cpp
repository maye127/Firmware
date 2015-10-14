#include "main.h"

#include <uORB/topics/bt_state.h>
#include <uORB/topics/kbd_handler.h>
#include <uORB/topics/leash_display.h>
#include <uORB/topics/vehicle_status.h>

#include "connect.h"
#include "menu.h"
#include "../displayhelper.h"
#include "../uorb_functions.h"

#include <stdio.h>

static const hrt_abstime  blink_time = 1.5e6; //1.5 second
static const hrt_abstime  between_blinks = 1.5e6; //1.5 seconds

namespace modes
{

Main::Main() :
    leashGPS(NO_GPS),
    airdogGPS(NO_GPS)
{
    checkGPS();
    DataManager *dm = DataManager::instance();
    dm->activityManager.get_display_name(currentActivity, sizeof(currentActivity)/sizeof(char));

    displayInfo.mode = MAINSCREEN_INFO;
    displayInfo.airdog_mode = AIRDOGMODE_NONE;
    displayInfo.follow_mode = dm->activityManager.getFollowValue();
    displayInfo.land_mode =   dm->activityManager.getLandValue();

    local_timer = 0;
    baseCondition.main = GROUNDED;
    baseCondition.sub = NONE;

    DisplayHelper::showMain(MAINSCREEN_INFO, currentActivity
                                ,displayInfo.airdog_mode
                                ,displayInfo.follow_mode
                                ,displayInfo.land_mode
                                ,leashGPS, airdogGPS);
}

int Main::getTimeout()
{
    return Error::getTimeout();
}

void Main::listenForEvents(bool awaitMask[])
{
    Error::listenForEvents(awaitMask);

    awaitMask[FD_AirdogStatus] = 1;
    awaitMask[FD_BLRHandler] = 1;
    awaitMask[FD_DroneLocalPos] = 1;
    awaitMask[FD_KbdHandler] = 1;
    awaitMask[FD_LeashGlobalPos] = 1; //For come to me command
    awaitMask[FD_LeashRowGPS] = 1;
    awaitMask[FD_LocalPos] = 1;
}

Base* Main::processGround(int orbId)
{
    Base *nextMode = nullptr;
    DataManager *dm = DataManager::instance();

    makeAction();
    if (orbId == FD_KbdHandler && !ignoreKeyEvent)
    {
        if (key_pressed(BTN_MODE))
        {
            nextMode = new Menu();
        }
        else if (key_pressed(BTN_PLAY))
        {
            baseCondition.sub = CONFIRM_TAKEOFF;
            nextMode = makeAction();
        }
        else
        {
            baseCondition.sub = HELP;
            nextMode = makeAction();
        }
    }
    else if (orbId == FD_AirdogStatus)
    {
        if (dm->airdog_status.state_aird > AIRD_STATE_LANDED)
        {
            DOG_PRINT("[leash_app]{main} Detected flying drone while \
                    in GROUNDED main state. Leash restart?\n");
            baseCondition.main = IN_FLINGHT;
            baseCondition.sub = NONE;
            nextMode = makeAction();
        }
    }
    return nextMode;
}

Base* Main::processTakeoff(int orbId)
{
    Base *nextMode = nullptr;
    DataManager *dm = DataManager::instance();

    if (local_timer != 0)
    {
        if (local_timer + command_responce_time < hrt_absolute_time() )
        {
            if(dm->airdog_status.state_aird < AIRD_STATE_PREFLIGHT_MOTOR_CHECK)
            {
                baseCondition.main = GROUNDED;
                baseCondition.sub = TAKEOFF_FAILED;
            }
            else
            {
                baseCondition.main = IN_FLINGHT;
                baseCondition.sub = TAKING_OFF;
            }
            local_timer = 0;
        }
    }
    if (orbId == FD_KbdHandler && !ignoreKeyEvent)
    {
        if (key_pressed(BTN_MODE))
        {
            if (baseCondition.sub != TAKING_OFF)
            baseCondition.sub = NONE;
        }
        else if (key_pressed(BTN_OK))
        {
            if (baseCondition.sub == CONFIRM_TAKEOFF)
            {
                if (dm->airdog_status.state_aird < AIRD_STATE_PREFLIGHT_MOTOR_CHECK)
                {
                    baseCondition.sub = TAKEOFF_CONFIRMED;
                }
                else
                {
                    DisplayHelper::showInfo(INFO_TAKEOFF_FAILED, 0);
                    baseCondition.sub = TAKEOFF_FAILED;
                }
            }
            else if (baseCondition.sub == TAKEOFF_FAILED)
            {
                baseCondition.sub = NONE;
            }
        }
    }
    else if (orbId == FD_AirdogStatus)
    {
        if (dm->airdog_status.state_aird == AIRD_STATE_TAKING_OFF)
        {
            baseCondition.main = IN_FLINGHT;
            baseCondition.sub = TAKING_OFF;
        }
        else if (dm->airdog_status.state_aird == AIRD_STATE_IN_AIR)
        {
            local_timer = 0;
            baseCondition.main = IN_FLINGHT;
            baseCondition.sub = NONE;
        }
    }
    nextMode = makeAction();
    return nextMode;
}

Base* Main::processHelp(int orbId)
{
    Base *nextMode = nullptr;
    if (orbId == FD_KbdHandler && !ignoreKeyEvent)
    {
        if (key_pressed(BTN_MODE))
        {
            baseCondition.sub = NONE;
        }
        else if (key_pressed(BTN_PLAY))
        {
            baseCondition.sub = CONFIRM_TAKEOFF;
        }
    }
    nextMode = makeAction();
    return nextMode;
}

Base* Main::doEvent(int orbId)
{
    Base *nextMode = nullptr;
    DataManager *dm = DataManager::instance();

    ignoreKeyEvent = isErrorShowed;

    Error::doEvent(orbId);

    if (ignoreKeyEvent)
    {
        makeAction();
    }

    /* -- check gps state -- */
    if (orbId == FD_DroneLocalPos || orbId == FD_LocalPos)
    {
        checkGPS();
    }
    /* -- disconnected -- */
    if (dm->bt_handler.global_state == CONNECTING)
    {
        nextMode = new ModeConnect(ModeConnect::State::DISCONNECTED);
    }
    else
    {
    /* -- grounded -- */
    if (baseCondition.main == GROUNDED)
    {
        switch (baseCondition.sub)
        {
            case NONE:
                nextMode = processGround(orbId);
                break;
            case HELP:
                nextMode = processHelp(orbId);
                break;
            case TAKEOFF_FAILED:
            case TAKEOFF_CONFIRMED:
            case CONFIRM_TAKEOFF:
                nextMode = processTakeoff(orbId);
                break;
            default:
                DOG_PRINT("[leash_app]{main} unexpected sub condition, got %d\n", baseCondition.sub);
                break;
        }
    }
    /* -- in air -- */
    else
    {
        switch(baseCondition.sub)
        {
            case TAKEOFF_CONFIRMED:
            case TAKING_OFF:
                nextMode = processTakeoff(orbId);
                break;
            case NONE:
            case PLAY:
            case PAUSE:
                nextMode = processFlight(orbId);
                break;
            case LANDING:
            case RTL:
                nextMode = processLandRTL(orbId);
                break;
            default:
                DOG_PRINT("[leash_app]{main} unexpected sub condition, got %d\n", baseCondition.sub);
                break;
        }
    }
    }

    // Check if we are in service screen
    Base* service = checkServiceScreen(orbId);
    if (service)
        nextMode = service;

    return nextMode;
}

void Main::checkGPS()
{
    DataManager *dm = DataManager::instance();
    float leash_eph = dm->localPos.eph;
    float airdog_eph = dm->droneLocalPos.eph;

    if (leash_eph == 0.0f)
        leashGPS = NO_GPS;
    else if (leash_eph < 1.5f)
        leashGPS = EXCELENT_GPS;
    else if (leash_eph < 2.5f)
        leashGPS = GOOD_GPS;
    else if (leash_eph < 3.2f)
        leashGPS = FAIR_GPS;
    else
        leashGPS = BAD_GPS;

    if (airdog_eph == 0.0f)
        airdogGPS = NO_GPS;
    else if (airdog_eph < 1.5f)
        airdogGPS = EXCELENT_GPS;
    else if (airdog_eph < 2.5f)
        airdogGPS = GOOD_GPS;
    else if (airdog_eph < 3.2f)
        airdogGPS = FAIR_GPS;
    else
        airdogGPS = BAD_GPS;
}

bool Main::onError(int errorCode)
{
    baseCondition.sub = NONE;
    return false;
}

Base* Main::makeAction()
{
    static hrt_abstime change_time = 0;
    static bool blink_ready = false;
    Base *nextMode = nullptr;
    DataManager *dm = DataManager::instance();

    dm->activityManager.get_display_name(currentActivity, sizeof(currentActivity));

    // On ground
    if (baseCondition.main == GROUNDED && !isErrorShowed)
    {
        switch (baseCondition.sub)
        {
            case NONE:
                if (change_time == 0)
                {
                    change_time = hrt_absolute_time();
                }

                if (blink_ready)
                {
                    if (hrt_absolute_time() - change_time > blink_time) 
                    {
                        blink_ready = false;
                        change_time = 0;
                    }
                    DisplayHelper::showMain(MAINSCREEN_INFO, "READY..."
                                                ,AIRDOGMODE_NONE
                                                ,displayInfo.follow_mode
                                                ,displayInfo.land_mode
                                                ,leashGPS, airdogGPS);
                }
                else
                {
                    if (hrt_absolute_time() - change_time > between_blinks) 
                    {
                        blink_ready = true;
                        change_time = 0;
                    }
                    DisplayHelper::showMain(MAINSCREEN_INFO, currentActivity
                                                ,AIRDOGMODE_NONE
                                                ,displayInfo.follow_mode
                                                ,displayInfo.land_mode
                                                ,leashGPS, airdogGPS);
                }
                break;
            case HELP:
                DisplayHelper::showMain(MAINSCREEN_READY_TO_TAKEOFF, currentActivity,
                                        AIRDOGMODE_NONE, FOLLOW_PATH, LAND_SPOT,
                                        leashGPS, airdogGPS);
                DisplayHelper::showMain(MAINSCREEN_READY_TO_TAKEOFF, currentActivity
                                            ,AIRDOGMODE_NONE
                                            ,displayInfo.follow_mode
                                            ,displayInfo.land_mode
                                            ,leashGPS, airdogGPS);
                break;
            case CONFIRM_TAKEOFF:
                DOG_PRINT("[leash_app]{main menu} confirm airdog screen\n");
                DisplayHelper::showMain(MAINSCREEN_CONFIRM_TAKEOFF, currentActivity, 0, 0, 0,
                                        leashGPS, airdogGPS);
                break;
            case TAKEOFF_CONFIRMED:
                DOG_PRINT("[leash_app]{main menu} takeoff confirm\n");
                DisplayHelper::showMain(MAINSCREEN_TAKING_OFF, currentActivity, 0, 0, 0,
                                        leashGPS, airdogGPS);
                if (local_timer == 0)
                {
                    send_arm_command(dm->airdog_status);
                    local_timer = hrt_absolute_time();
                }
                break;
            case TAKING_OFF:
                DisplayHelper::showMain(MAINSCREEN_TAKING_OFF, currentActivity, 0, 0, 0,
                                        leashGPS, airdogGPS);
                break;
            case TAKEOFF_FAILED:
                DisplayHelper::showInfo(INFO_TAKEOFF_FAILED, 0);
                break;
            default:
                DOG_PRINT("[leash_app]{main} unexpected sub condition, got %d\n", baseCondition.sub);
                break;
        }
    }
    // In flight
    else if (!isErrorShowed)
    {
        switch (baseCondition.sub)
        {
            case NONE:
            case PAUSE:
            case PLAY:
                DisplayHelper::showMain(displayInfo.mode, currentActivity
                                ,displayInfo.airdog_mode
                                ,displayInfo.follow_mode
                                ,displayInfo.land_mode,
                                leashGPS, airdogGPS);
                break;
            case TAKING_OFF:
                DisplayHelper::showMain(MAINSCREEN_TAKING_OFF, currentActivity, 0, 0, 0,
                                        leashGPS, airdogGPS);
                break;
            case LANDING:
                DisplayHelper::showMain(MAINSCREEN_LANDING, currentActivity, 0, 0, 0,
                                        leashGPS, airdogGPS);
                break;
            case RTL:
                DisplayHelper::showMain(MAINSCREEN_GOING_HOME, currentActivity, 0, 0, 0,
                                        leashGPS, airdogGPS);
                break;
            default:
                DOG_PRINT("[leash_app]{main} unexpected sub condition, got %d\n", baseCondition.sub);
                break;
        }
    }
    return nextMode;
}

Base* Main::processFlight(int orbId)
{
    Base *nextMode = nullptr;
    DataManager *dm = DataManager::instance();

    /* Now all keyboard actions are processed in 'kbd_handler' in 'leash' module
     * moving them to this module should be considered as TODO[Max]
     * for now we are only monitoring airdog states to show corresponding info on 'leash_display'
     */
    
    if (orbId == FD_AirdogStatus || baseCondition.sub == NONE)
    {
        // process main state
        if (dm->airdog_status.state_main == MAIN_STATE_LOITER)
        {
            displayInfo.airdog_mode = AIRDOGMODE_PAUSE;
            baseCondition.sub = PAUSE;
        }
        else if (dm->airdog_status.state_main == MAIN_STATE_RTL)
        {
            displayInfo.airdog_mode = AIRDOGMODE_PAUSE;
            baseCondition.sub = RTL;
        }
        else
        {
            switch(dm->airdog_status.state_main)
            {
                case MAIN_STATE_ABS_FOLLOW:
                case MAIN_STATE_CABLE_PARK:
                case MAIN_STATE_AUTO_PATH_FOLLOW:
                case MAIN_STATE_CIRCLE_AROUND:
                case MAIN_STATE_KITE_LITE:
                case MAIN_STATE_FRONT_FOLLOW:
                    displayInfo.airdog_mode = AIRDOGMODE_PLAY;
                    baseCondition.sub = PLAY;
                    break;
            }
        }
        // process airdog state
        if (dm->airdog_status.state_aird == AIRD_STATE_LANDING)
        {
            displayInfo.airdog_mode = AIRDOGMODE_PAUSE;
            baseCondition.sub = LANDING;
        }
    }
    else if (orbId == FD_KbdHandler)
    {
        DOG_PRINT("we are here!!!!!!!!!\n");
        if ( key_ShortPressed(BTN_MODE))
        {
            if (baseCondition.main == IN_FLINGHT)
            {
                baseCondition.main = MANUAL_FLIGHT;
                displayInfo.mode = MAINSCREEN_INFO_SUB;
            }
            else if (baseCondition.main == MANUAL_FLIGHT)
            {
                baseCondition.main = IN_FLINGHT;
                displayInfo.mode = MAINSCREEN_INFO;
            }
        }
        else
        {
            decide_command(baseCondition.main);
        }
    }
    nextMode = makeAction();
    return nextMode;
}

Base* Main::processLandRTL(int orbId)
{
    Base *nextMode = nullptr;
    DataManager *dm = DataManager::instance();

    if (orbId == FD_AirdogStatus)
    {
        if (dm->airdog_status.state_aird == AIRD_STATE_LANDING)
        {
            displayInfo.airdog_mode = AIRDOGMODE_PAUSE;
            baseCondition.sub = LANDING;
        }
        else if (dm->airdog_status.state_aird < AIRD_STATE_PREFLIGHT_MOTOR_CHECK)
        {
            baseCondition.main = GROUNDED;
            baseCondition.sub = NONE;
        }
        else if (dm->airdog_status.state_main == MAIN_STATE_RTL)
        {
            displayInfo.airdog_mode = AIRDOGMODE_PAUSE;
            baseCondition.sub = RTL;
        }
        else if (dm->airdog_status.state_aird == AIRD_STATE_IN_AIR)
        {
            displayInfo.airdog_mode = AIRDOGMODE_PAUSE;
            baseCondition.sub = PAUSE;
        }
    }
    else if (orbId == FD_KbdHandler)
    {
        decide_command(baseCondition.main);
    }
    nextMode = makeAction();
    return nextMode;
}

void Main::decide_command(MainStates mainState)
{
    DataManager *dm = DataManager::instance();

    if (key_ShortPressed(BTN_LEFT)
        || key_RepeatedPressed(BTN_LEFT))
        sendAirDogCommnad(VEHICLE_CMD_NAV_REMOTE_CMD, REMOTE_CMD_LEFT);

    else if (key_ShortPressed(BTN_RIGHT)
            || key_RepeatedPressed(BTN_RIGHT))
        sendAirDogCommnad(VEHICLE_CMD_NAV_REMOTE_CMD, REMOTE_CMD_RIGHT);

    else if (key_ShortPressed(BTN_PLAY))
        sendAirDogCommnad(VEHICLE_CMD_NAV_REMOTE_CMD, REMOTE_CMD_PLAY_PAUSE);

    else if (key_ShortPressed(BTN_TO_ME))
        sendAirDogCommnad(VEHICLE_CMD_NAV_REMOTE_CMD
                , REMOTE_CMD_COME_TO_ME
                ,0
                ,0
                ,0
                , dm->leashGlobalPos.lat
                , dm->leashGlobalPos.lon);

    else if (key_ShortPressed(BTN_TO_H))
        sendAirDogCommnad(VEHICLE_CMD_NAV_REMOTE_CMD, REMOTE_CMD_LAND_DISARM);

    else if (key_LongPressed(BTN_TO_H))
        send_rtl_command(dm->airdog_status);

    else if (key_ShortPressed(BTN_UP)
            || key_RepeatedPressed(BTN_UP))
    {
        switch(mainState)
        {
            case IN_FLINGHT:
                sendAirDogCommnad(VEHICLE_CMD_NAV_REMOTE_CMD, REMOTE_CMD_UP);
                break;
            case MANUAL_FLIGHT:
                sendAirDogCommnad(VEHICLE_CMD_NAV_REMOTE_CMD, REMOTE_CMD_FURTHER);
                break;
            default:
                DOG_PRINT("[modes]{decide_command} not supported mainState: %d\n", mainState);
                break;
        }
    }
    else if (key_ShortPressed(BTN_DOWN)
            || key_RepeatedPressed(BTN_DOWN))
    {
        switch(mainState)
        {
            case IN_FLINGHT:
                sendAirDogCommnad(VEHICLE_CMD_NAV_REMOTE_CMD, REMOTE_CMD_DOWN);
                break;
            case MANUAL_FLIGHT:
                sendAirDogCommnad(VEHICLE_CMD_NAV_REMOTE_CMD, REMOTE_CMD_CLOSER);
                break;
            default:
                DOG_PRINT("[modes]{decide_command} not supported mainState: %d\n", mainState);
                break;
        }
    }
}

} //end of namespace modes

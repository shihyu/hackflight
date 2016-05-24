/*
   V-REP simulator plugin code for Hackflight

   Copyright (C) Simon D. Levy 2016

   This file is part of Hackflight.

   Hackflight is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   Hackflight is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with Hackflight.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>
#include <unistd.h>

#include "v_repExtHackflight.hpp"
#include "scriptFunctionData.h"
#include "v_repLib.h"

#include "../firmware/pwm.hpp"
#include "../firmware/board.hpp"

// From firmware
extern void setup(void);
extern void loop(void);

#define JOY_DEV "/dev/input/js0"

#define CONCAT(x,y,z) x y z
#define strConCat(x,y,z)	CONCAT(x,y,z)

#define PLUGIN_NAME "Hackflight"

static LIBRARY vrepLib;

class LED {

    private:

        int handle;
        float color[3];
        bool on;
        float black[3];

    public:

        LED(void) {}

        LED(int handle, int r, int g, int b) {
            this->handle = handle;
            this->color[0] = r;
            this->color[1] = g;
            this->color[2] = b;
            this->on = false;
            this->black[0] = 0;
            this->black[1] = 0;
            this->black[2] = 0;
        }

        void turnOn(void) {
            simSetShapeColor(this->handle, NULL, 0, this->color);
            this->on = true;
        }

        void turnOff(void) {
            simSetShapeColor(this->handle, NULL, 0, this->black);
            this->on = false;
        }

        void toggle(void) {
            this->on = !this->on;
            simSetShapeColor(this->handle, NULL, 0, this->on ? this->color : this->black);
        }
};


struct sQuadcopter
{
    int handle;
    int prop1handle;
    int prop2handle;
    int prop3handle;
    int prop4handle;
    LED redLED;
    LED greenLED;
    float duration;
    char* waitUntilZero;
};

static sQuadcopter quadcopter;
static int joyfd;
static int pwm[8];
static struct timespec start_time;


// simExtHackflight_create -------------------------------------------------------------

#define LUA_CREATE_COMMAND "simExtHackflight_create"

// Five handles: quadcopter + four propellers
static const int inArgs_CREATE[]={
    7,
    sim_script_arg_int32,0,
    sim_script_arg_int32,0,
    sim_script_arg_int32,0,
    sim_script_arg_int32,0,
    sim_script_arg_int32,0,
    sim_script_arg_int32,0,
    sim_script_arg_int32,0,
};

void LUA_CREATE_CALLBACK(SScriptCallBack* cb)
{
    CScriptFunctionData D;
    int handle=-1;
    if (D.readDataFromStack(cb->stackID,inArgs_CREATE,inArgs_CREATE[0],LUA_CREATE_COMMAND)) {
        std::vector<CScriptFunctionDataItem>* inData=D.getInDataPtr();

        quadcopter.handle = inData->at(0).int32Data[0];

        quadcopter.greenLED = LED(inData->at(1).int32Data[0], 0, 255, 0);
        quadcopter.redLED   = LED(inData->at(2).int32Data[0], 255, 0, 0);

        quadcopter.prop1handle = inData->at(3).int32Data[0];
        quadcopter.prop2handle = inData->at(4).int32Data[0];
        quadcopter.prop3handle = inData->at(5).int32Data[0];
        quadcopter.prop4handle = inData->at(6).int32Data[0];

        quadcopter.waitUntilZero=NULL;
        quadcopter.duration=0.0f;
    }
    D.pushOutData(CScriptFunctionDataItem(handle));
    D.writeDataToStack(cb->stackID);
}

// simExtHackflight_destroy --------------------------------------------------------------------

#define LUA_DESTROY_COMMAND "simExtHackflight_destroy"

void LUA_DESTROY_CALLBACK(SScriptCallBack* cb)
{
    CScriptFunctionData D;
    D.pushOutData(CScriptFunctionDataItem(true)); // success
    D.writeDataToStack(cb->stackID);
}


// simExtHackflight_start ------------------------------------------------------------------------

#define LUA_START_COMMAND "simExtHackflight_start"

void LUA_START_CALLBACK(SScriptCallBack* cb)
{
    CScriptFunctionData D;
    cb->waitUntilZero=1; // the effect of this is that when we leave the callback, the Lua script gets control
	   				     // back only when this value turns zero. This allows for "blocking" functions.
    D.pushOutData(CScriptFunctionDataItem(true)); // success
    D.writeDataToStack(cb->stackID);

    // Close joystick if open
    if (joyfd > 0)
        close(joyfd);

    // Initialize joystick
    joyfd = open( JOY_DEV , O_RDONLY);
    if(joyfd > 0) 
        fcntl(joyfd, F_SETFL, O_NONBLOCK);

    setup();
}

// simExtHackflight_stop --------------------------------------------------------------------------------

#define LUA_STOP_COMMAND "simExtHackflight_stop"

void LUA_STOP_CALLBACK(SScriptCallBack* cb)
{
    CScriptFunctionData D;
    D.pushOutData(CScriptFunctionDataItem(true)); // success
    D.writeDataToStack(cb->stackID);
}


VREP_DLLEXPORT unsigned char v_repStart(void* reservedPointer,int reservedInt)
{ // This is called just once, at the start of V-REP.
    // Dynamically load and bind V-REP functions:
    char curDirAndFile[1024];
    getcwd(curDirAndFile, sizeof(curDirAndFile));

    std::string currentDirAndPath(curDirAndFile);
    std::string temp(currentDirAndPath);

    temp+="/libv_rep.so";

    vrepLib=loadVrepLibrary(temp.c_str());
    if (vrepLib==NULL)
    {
        std::cout << "Error, could not find or correctly load v_rep.dll. Cannot start 'Hackflight' plugin.\n";
        return(0); // Means error, V-REP will unload this plugin
    }
    if (getVrepProcAddresses(vrepLib)==0)
    {
        std::cout << "Error, could not find all required functions in v_rep.dll. Cannot start 'Hackflight' plugin.\n";
        unloadVrepLibrary(vrepLib);
        return(0); // Means error, V-REP will unload this plugin
    }

    // Check the V-REP version:
    int vrepVer;
    simGetIntegerParameter(sim_intparam_program_version,&vrepVer);
    if (vrepVer<30200) // if V-REP version is smaller than 3.02.00
    {
        std::cout << "Sorry, V-REP 3.2.0 or higher is required. Cannot start 'Hackflight' plugin.\n";
        unloadVrepLibrary(vrepLib);
        return(0); // Means error, V-REP will unload this plugin
    }

    // Register 4 new Lua commands:
    simRegisterScriptCallbackFunction(strConCat(LUA_CREATE_COMMAND,"@",PLUGIN_NAME), NULL, LUA_CREATE_CALLBACK);
    simRegisterScriptCallbackFunction(strConCat(LUA_DESTROY_COMMAND,"@",PLUGIN_NAME), NULL, LUA_DESTROY_CALLBACK);
    simRegisterScriptCallbackFunction(strConCat(LUA_START_COMMAND,"@",PLUGIN_NAME), NULL, LUA_START_CALLBACK);
    simRegisterScriptCallbackFunction(strConCat(LUA_STOP_COMMAND,"@",PLUGIN_NAME), NULL, LUA_STOP_CALLBACK);

    return(8); // return the version number of this plugin (can be queried with simGetModuleName)
}

VREP_DLLEXPORT void v_repEnd()
{ // This is called just once, at the end of V-REP
    unloadVrepLibrary(vrepLib); // release the library
}

VREP_DLLEXPORT void* v_repMessage(int message,int* auxiliaryData,void* customData,int* replyData)
{   
    // This is called quite often. Just watch out for messages/events you want to handle
    // This function should not generate any error messages:

    int errorModeSaved;
    simGetIntegerParameter(sim_intparam_error_report_mode,&errorModeSaved);
    simSetIntegerParameter(sim_intparam_error_report_mode,sim_api_errormessage_ignore);

    void* retVal=NULL;

    float force = 1;
    float torque = 0;
    simAddForceAndTorque(quadcopter.prop1handle, &force, &torque);
    simAddForceAndTorque(quadcopter.prop2handle, &force, &torque);
    simAddForceAndTorque(quadcopter.prop3handle, &force, &torque);
    simAddForceAndTorque(quadcopter.prop4handle, &force, &torque);

    // Read joystick
    if (joyfd > 0) {

        struct js_event js;
        read(joyfd, &js, sizeof(struct js_event));

        if (js.type & ~JS_EVENT_INIT) {
            int fakechan = 0;
            switch (js.number) {
                case 0:
                    fakechan = 3;
                    break;
                case 1:
                    fakechan = 1;
                    break;
                case 2:
                    fakechan = 2;
                    break;
                case 3:
                    fakechan = 4;
                    break;
                case 5:
                    fakechan = 5;
                    break;
            }

            if (fakechan > 0)
                pwm[fakechan-1] = 
                        CONFIG_PWM_MIN + (int)((js.value + 32767)/65534. * (CONFIG_PWM_MAX-CONFIG_PWM_MIN));
        }
    }


    if (message==sim_message_eventcallback_modulehandle)
    {
        // is the command also meant for Hackflight?
        if ( (customData==NULL)||(std::string("Hackflight").compare((char*)customData)==0) ) 
        {
            float dt=simGetSimulationTimeStep();

            if (quadcopter.duration>0.0f)
            { // movement mode

                quadcopter.duration-=dt;
            }

            else
            { // stopped mode
                if (quadcopter.waitUntilZero!=NULL)
                {
                    quadcopter.waitUntilZero[0]=0;
                    quadcopter.waitUntilZero=NULL;
                }
            }
        }
    }

    loop();

    simSetIntegerParameter(sim_intparam_error_report_mode,errorModeSaved); // restore previous settings

    return(retVal);
}

// Board implementation ===============================================================


void Board::imuInit(uint16_t & acc1G, float & gyroScale)
{
    // XXX use MPU6050 settings for now
    acc1G = 4096;
    gyroScale = (4.0f / 16.4f) * (M_PI / 180.0f) * 0.000001f;
}

void Board::imuRead(int16_t accADC[3], int16_t gyroADC[3])
{
    /*
    result,force=simReadForceSensor(sensor)

    if (result>0) then

        accel={force[1]/mass,force[2]/mass,force[3]/mass}
    */
}

void Board::init(uint32_t & imuLooptimeUsec)
{
    // Initialize nanosecond timer
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_time);

    // Set initial fake PWM values to midpoints
    for (int k=0; k<CONFIG_RC_CHANS; ++k)  {
        pwm[k] = (CONFIG_PWM_MIN + CONFIG_PWM_MAX) / 2;
    }

    // Special treatment for throttle and switch PWM: start them at the bottom
    // of the range.  As soon as they are moved, their actual values will
    // be returned by Board::readPWM().
    pwm[2] = CONFIG_PWM_MIN;
    pwm[4] = CONFIG_PWM_MIN;

    // Minimal V-REP simulation period
    imuLooptimeUsec = 10000;
}

void Board::delayMilliseconds(uint32_t msec)
{
    uint32_t startMicros = this->getMicros();

    while ((this->getMicros() - startMicros)/1000 > msec)
        ;
}

uint32_t Board::getMicros()
{
    struct timespec end_time;
    
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_time);

    return 1000000 * (end_time.tv_sec - start_time.tv_sec) + 
        (end_time.tv_nsec - start_time.tv_nsec) / 1000;
}

void Board::ledGreenOff(void)
{
    quadcopter.greenLED.turnOff();
}

void Board::ledGreenOn(void)
{
    quadcopter.greenLED.turnOn();
}

void Board::ledGreenToggle(void)
{
    quadcopter.greenLED.toggle();
}

void Board::ledRedOff(void)
{
    quadcopter.redLED.turnOff();
}

void Board::ledRedOn(void)
{
    quadcopter.redLED.turnOn();
}

void Board::ledRedToggle(void)
{
    quadcopter.redLED.toggle();
}

uint16_t Board::readPWM(uint8_t chan)
{
    return pwm[chan];
}

void Board::writeMotor(uint8_t index, uint16_t value)
{
}

// Unimplemented --------------------------------------------

void Board::checkReboot(bool pendReboot) { }
void Board::serialWriteByte(uint8_t c) { }
uint8_t Board::serialReadByte(void) { return 0; }
uint8_t Board::serialAvailableBytes(void) { return 0; }
void Board::reboot(void) { }

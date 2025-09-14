/*
 * Flight Simulator Instrument Data Link
 * Copyright (c) 2024 Scott Vincent
 */

#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mmsystem.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
// Using SIMCONNECT_CLIENTDATA_MAX_SIZE instead of redefining MaxDataSize
#define SOCKET int
#define SOCKADDR struct sockaddr
#define SOCKET_ERROR -1
#define closesocket close
#define WSAGetLastError() errno
#define INVALID_SOCKET -1
typedef struct sockaddr_in SOCKADDR_IN;
#define SND_FILENAME 0x00020000L
#define SND_ASYNC 0x0001
#define PlaySound(a,b,c) (0)
#define MAKEWORD(a,b) ((a) | ((b) << 8))
typedef struct WSAData { unsigned short wVersion; char szDescription[256]; } WSADATA;
#define WSAStartup(a,b) (0)
#define WSACleanup() (0)
#define _strnicmp strncasecmp
#define _stricmp strcasecmp
#define __cdecl
#define Sleep(x) usleep((x)*1000)
typedef long LONG_PTR;
typedef long HRESULT;
typedef void* HWND;
typedef struct { int left, top, right, bottom; } RECT;
typedef struct { size_t cbSize; RECT rcWindow; } WINDOWINFO;
#endif
#include <stdio.h>
#include <thread>
#include <cstring>
#include "simvarDefs.h"
#include "LVars-A310.h"
#include "LVars-Fbw.h"
#include "LVars-Kodiak100.h"
#include "jetbridge.h"
#include "vjoy.h"
#include "SimConnect.h"
#include "settings.h"

 // Data will be served on this port - loaded from settings
int InstrumentListenPort = 52021;  // Port for instrument panel requests
int InstrumentResponsePort = 52020;  // Port for instrument panel responses
int SimDataPort = 52022;  // Port for simulator data

// Settings
AppSettings appSettings;

// Change the next line to false if you always want to send
// full data across the network rather than deltas.
const bool UseDeltas = true;
//const int MaxDataSize = 8192;
const int MaxDataSize = SIMCONNECT_CLIENTDATA_MAX_SIZE;

// Uncomment the next line to show network data usage.
// This should be a lot lower when using deltas.
//#define SHOW_NETWORK_USAGE

#ifdef SHOW_NETWORK_USAGE
ULONGLONG networkStart = 0;
long networkIn;
long networkOut;
#endif

// Comment the following line out if you don't have any Raspberry Pi Pico USB devices
#define PICO_USB

#ifdef PICO_USB
#include "game-controllers.h"

void picoInit();
void picoRefresh();
void switchboxRefresh();
void g1000Refresh();
void g1000Encoder(int, int, int);
void g1000EncoderPush(int);
void g1000SoftkeyPush(int);
void g1000ButtonPush(int);

bool picoInitialised = false;
int switchboxId = -1;
int g1000Id = -1;
int alphaId = -1;
extern Joystick joystick[MaxJoysticks];
int g1000Axis[3];
int g1000Button[20];
int ignitionOff = 0;

bool modeChange = false;
time_t clrPress = 0;
HWND pfdWin = NULL;
HWND mfdWin = NULL;
WINDOWINFO pfdInfo;
WINDOWINFO mfdInfo;
bool g1000IsPrimary = false;
#endif

enum FLIGHT_PHASE {
    GROUND,
    TAKEOFF,
    CLIMB,
    CRUISE,
    DESCENT,
    APPROACH,
    GO_AROUND
};

bool quit = false;
bool initiatedPushback = false;
bool completedTakeOff = false;
bool hasFlown = false;
int onStandState = 0;
double skytrackState = 0;
bool isA310 = false;
bool isFbw = false;
bool isA320 = false;
bool isA380 = false;
bool is747 = false;
bool isK100 = false;
bool isPA28 = false;
bool isAirliner = false;
bool isNewAircraft = false;
char prevAircraft[32] = "\0";
double lastHeading = 0;
int seatBeltsReplicateDelay = 0;
int fixedPushback = -1;
LVars_A310 a310Vars;
LVars_FBW fbwVars;
HANDLE hSimConnect = NULL;
extern const char* versionString;
extern const char* SimVarDefs[][2];
extern WriteEvent WriteEvents[];

SimVars simVars;
double *varsStart;
int varsSize;

// Some panels request less data to save bandwidth
long writeDataSize = sizeof(WriteData);
long instrumentsDataSize = sizeof(SimVars);
long autopilotDataSize = (long)((LONG_PTR)(&simVars.autothrottleActive) + (long)sizeof(double) - (LONG_PTR)&simVars);
long radioDataSize = (long)((LONG_PTR)(&simVars.transponderCode) + (long)sizeof(double) - (LONG_PTR)&simVars);
long lightsDataSize = (long)((LONG_PTR)(&simVars.apuPercentRpm) + (long)sizeof(double) - (LONG_PTR)&simVars);

char* deltaData;
long deltaSize;
char* prevInstrumentsData;
char* prevAutopilotData;
char* prevRadioData;
char* prevLightsData;

int active = -1;
int bytes;
bool autopilotPanelConnected = false;
bool radioPanelConnected = false;
bool lightsPanelConnected = false;
SOCKET instrumentListenSockfd;  // Socket for receiving instrument panel requests
SOCKET instrumentResponseSockfd;  // Socket for sending responses to instrument panel
sockaddr_in senderAddr;
int addrSize = sizeof(senderAddr);
Request request;
SOCKET posSockfd;
sockaddr_in posSendAddr;
PosData posData;
int posDataSize = sizeof(PosData);
int posSkip = 0;
const int deltaDoubleSize = sizeof(DeltaDouble);
const int deltaStringSize = sizeof(DeltaString);

// Server thread
void server();
std::thread* serverThread = nullptr;

enum DEFINITION_ID {
    DEF_READ_ALL
};

enum REQUEST_ID {
    REQ_ID
};

#ifdef jetbridgeFallback
void pollJetbridge()
{
    // Use low frequency for jetbridge as vars not critical
    int loopMillis = 100;

    while (!quit)
    {
        if (simVars.connected && isA310) {
            readA310Jetbridge();
            Sleep(loopMillis);
        }
        else if (simVars.connected && isFbw) {
            readFbwJetbridge();
            Sleep(loopMillis);
        }
        else {
            Sleep(500);
        }
    }
}
#endif

void printSimConnectData(SIMCONNECT_RECV* pData, DWORD cbData) {
    printf("\n=== SimConnect Data Received ===\n");

    // Print pData as hexcode
    printf("Buffer: ");
    for (int i = 0; i < cbData; i++) {
        printf("%02x ", ((char*)pData)[i]);
    }
    printf("\n");


    // Print the size of the pData structure and its components
    printf("Size of SIMCONNECT_RECV structure: %lu bytes\n", sizeof(SIMCONNECT_RECV));
    printf("Size of dwSize member: %lu bytes\n", sizeof(pData->dwSize));
    printf("Size of dwVersion member: %lu bytes\n", sizeof(pData->dwVersion));
    printf("Size of dwID member: %lu bytes\n", sizeof(pData->dwID));

    printf("Base Structure:\n");
    printf("  Size: %lu bytes\n", pData->dwSize);
    printf("  Version: %lu\n", pData->dwVersion);
    printf("  ID: %lu (Type: %s)\n", pData->dwID,
        pData->dwID == SIMCONNECT_RECV_ID_NULL ? "NULL" :
        pData->dwID == SIMCONNECT_RECV_ID_EVENT ? "EVENT" :
        pData->dwID == SIMCONNECT_RECV_ID_SIMOBJECT_DATA ? "SIMOBJECT_DATA" :
        pData->dwID == SIMCONNECT_RECV_ID_CLIENT_DATA ? "CLIENT_DATA" :
        pData->dwID == SIMCONNECT_RECV_ID_QUIT ? "QUIT" : "UNKNOWN");
    printf("  Total Data Size: %lu bytes\n", cbData);
    
    if (pData->dwID == SIMCONNECT_RECV_ID_EVENT) {
        auto evt = static_cast<SIMCONNECT_RECV_EVENT*>(pData);
        printf("Event Data:\n");
        printf("  Group ID: %lu\n", evt->uGroupID);
        printf("  Event ID: %lu\n", evt->uEventID);
        printf("  Data: %lu\n", evt->dwData);
    } else if (pData->dwID == SIMCONNECT_RECV_ID_SIMOBJECT_DATA) {
        auto objData = static_cast<SIMCONNECT_RECV_SIMOBJECT_DATA*>(pData);
        printf("SimObject Data:\n");
        printf("  Request ID: %lu\n", objData->dwRequestID);
        printf("  Object ID: %lu\n", objData->dwObjectID);
        printf("  Define ID: %lu\n", objData->dwDefineID);
        printf("  Flags: %lu\n", objData->dwFlags);
        printf("  Entry: %lu of %lu\n", objData->dwentrynumber, objData->dwoutof);
        printf("  Define Count: %lu\n", objData->dwDefineCount);
    }
    printf("===========================\n\n");
    fflush(stdout);
}

void CALLBACK MyDispatchProc(SIMCONNECT_RECV* pData, DWORD cbData, void* pContext)
{
    //printf("MyDispatchProc called\n");
    //fflush(stdout);
    //printSimConnectData(pData, cbData);
    
    static int displayDelay = 0;

    //printf("Switch on pData->dwID = %lu\n", pData->dwID);
    //fflush(stdout);

    switch (pData->dwID)
    {
    case SIMCONNECT_RECV_ID_EVENT:
    {
        SIMCONNECT_RECV_EVENT* evt = (SIMCONNECT_RECV_EVENT*)pData;

        //printf("Switch on evt->uEventID = %lu\n", evt->uEventID);
        //fflush(stdout);

        switch (evt->uEventID)
        {
        case SIM_START:
        {
            printf("SimConnect Start event\n");
            fflush(stdout);
            break;
        }

        case SIM_STOP:
        {
            printf("SimConnect Stop event\n");
            fflush(stdout);
            break;
        }

        default:
        {
            printf("SimConnect unknown event id: %lu\n", evt->uEventID);
            fflush(stdout);
            break;
        }
        }

        break;
    }

    case SIMCONNECT_RECV_ID_SIMOBJECT_DATA:
    {
        auto pObjData = static_cast<SIMCONNECT_RECV_SIMOBJECT_DATA*>(pData);

        printf("Switch on pObjData->dwRequestID = %lu\n", pObjData->dwRequestID);
        fflush(stdout);

        switch (pObjData->dwRequestID)
        {
        case REQ_ID:
        {
            printf("Debug - Size calculation:\n");
            printf("  pObjData->dwSize: %lu\n", pObjData->dwSize);
            printf("  &pObjData->dwData: %p\n", &pObjData->dwData);
            printf("  pData: %p\n", pData);
            printf("  Offset: %ld\n", (long)(&pObjData->dwData) - (long)pData);
            printf("  varsSize: %d\n", varsSize);
            
            int dataSize = pObjData->dwSize - ((long)(&pObjData->dwData) - (long)pData);
            printf("  dataSize = %d\n", dataSize);
            
            if (dataSize != varsSize) {
                printf("Error: SimConnect expected %d bytes but received %d bytes\n", varsSize, dataSize);
                fflush(stdout);
            }
            else {
                memcpy(varsStart, &pObjData->dwData, varsSize);
            }

            // Populate internal variables
            simVars.skytrackState = skytrackState;
            isA310 = false;
            isFbw = false;
            isA320 = false;
            isA380 = false;
            is747 = false;
            isK100 = false;
            isPA28 = false;
            isAirliner = false;
            isNewAircraft = false;

            if (strcmp(simVars.aircraft, prevAircraft) != 0) {
                strcpy(prevAircraft, simVars.aircraft);
                isNewAircraft = true;
            }

            char* pos = strchr(simVars.aircraft, '3');
            if (pos && *(pos - 1) == 'A') {
                if (*(pos + 1) == '1') {
                    isA310 = true;
                    isAirliner = true;
                }
                else if (*(pos + 1) == '2') {
                    isFbw = true;
                    isA320 = true;
                    isAirliner = true;
                }
                else if (*(pos + 1) == '8') {
                    isFbw = true;
                    isA380 = true;
                    isAirliner = true;
                }
            }

            if (strncmp(simVars.aircraft, "Salty", 5) == 0 || strncmp(simVars.aircraft, "Boeing 747-8", 12) == 0) {
                is747 = true;
                isAirliner = true;
            }
            else if (strncmp(simVars.aircraft, "Kodiak 100", 10) == 0) {
                isK100 = true;
            }
            else if (strncmp(simVars.aircraft, "Just Flight PA28", 16) == 0) {
                isPA28 = true;
            }
            else if (strncmp(simVars.aircraft, "Airbus", 6) == 0 || strncmp(simVars.aircraft, "Boeing", 6) == 0) {
                isAirliner = true;
            }

            if (abs(simVars.hiHeading - lastHeading) > 10) {
                // Fix gyro if aircraft heading changes abruptly
                //SimConnect_TransmitClientEvent(hSimConnect, 0, KEY_HEADING_GYRO_SET, 1, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY);
                writeJetbridgeVar(KEY_HEADING_GYRO_SET, 1);
            }
            lastHeading = simVars.hiHeading;

            if (simVars.connected && isA310) {
                // Map A310 vars to real vars
                simVars.apuStartSwitch = a310Vars.apuStart;
                if (a310Vars.apuStartAvail) {
                    simVars.apuPercentRpm = 100;
                }
                else {
                    simVars.apuPercentRpm = 0;
                }

                if (seatBeltsReplicateDelay > 0) {
                    seatBeltsReplicateDelay--;
                }
                else if (simVars.seatBeltsSwitch != a310Vars.seatbeltsSwitch) {
                    // Replicate lvar value back to standard SDK variable to make PACX work correctly
                    //SimConnect_TransmitClientEvent(hSimConnect, 0, KEY_CABIN_SEATBELTS_ALERT_SWITCH_TOGGLE, 1, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY);
                    writeJetbridgeVar(KEY_CABIN_SEATBELTS_ALERT_SWITCH_TOGGLE, 1);
                    seatBeltsReplicateDelay = 10;
                }
                simVars.seatBeltsSwitch = a310Vars.seatbeltsSwitch;
                simVars.jbPitchTrim = a310Vars.pitchTrim1 + a310Vars.pitchTrim2;
                simVars.autopilotAirspeed = a310Vars.autopilotAirspeed;
                simVars.autopilotMach = a310Vars.autopilotAirspeed;
                simVars.autopilotHeading = a310Vars.autopilotHeading;
                simVars.autopilotAltitude = a310Vars.autopilotAltitude;
                simVars.autopilotVerticalSpeed = a310Vars.autopilotVerticalSpeed;
                simVars.tfAutoBrake = simVars.jbAutobrake + 1;
                simVars.flightDirectorActive = a310Vars.flightDirector;
                simVars.autopilotEngaged = a310Vars.autopilot;
                simVars.autothrottleActive = a310Vars.autothrottle;
                simVars.autopilotApproachHold = a310Vars.localiser;
                simVars.autopilotGlideslopeHold = a310Vars.approach;
                if (a310Vars.profile) {
                    simVars.jbManagedSpeed = 1;
                }
                else {
                    simVars.jbManagedSpeed = 0;
                }
                if (a310Vars.altHold || a310Vars.levelChange || a310Vars.profile) {
                    simVars.jbManagedAltitude = 0;  // For A310, managedAltitude == Selected VS
                }
                else {
                    simVars.jbManagedAltitude = 1;  // For A310, managedAltitude == Selected VS
                }
                if (a310Vars.gearHandle == 0 && simVars.gearLeftPos == 0 && simVars.gearCentrePos == 0 && simVars.gearRightPos == 0) {
                    // After gear up set handle to neutral position
                    writeJetbridgeVar(A310_GEAR_HANDLE, 1);
                }
                simVars.nav1Freq = a310Vars.ilsFrequency / 100;
                simVars.nav1Standby = a310Vars.ilsFrequency / 100;
                simVars.vor1Obs = a310Vars.ilsCourse;
            }
            else if (simVars.connected && isFbw) {
                // Map FBW vars to real vars
                simVars.apuStartSwitch = fbwVars.apuStart;
                if (fbwVars.apuStartAvail) {
                    simVars.apuPercentRpm = 100;
                }
                else {
                    simVars.apuPercentRpm = 0;
                }
                simVars.tfFlapsIndex = fbwVars.flapsIndex;
                simVars.parkingBrakeOn = fbwVars.parkBrakePos;
                simVars.tfSpoilersPosition = fbwVars.spoilersHandlePos;
                simVars.brakeLeftPedal = fbwVars.leftBrakePedal;
                simVars.brakeRightPedal = fbwVars.rightBrakePedal;
                simVars.rudderPosition = fbwVars.rudderPedalPos / 100.0;
                simVars.autopilotEngaged = (fbwVars.autopilot1 == 0 && fbwVars.autopilot2 == 0) ? 0 : 1;
                if (fbwVars.autothrust == 0) {
                    simVars.autothrottleActive = 0;
                }
                else {
                    simVars.autothrottleActive = 1;
                }
                simVars.transponderState = fbwVars.xpndrMode;
                simVars.autopilotHeading = fbwVars.autopilotHeading;
                simVars.autopilotAltitude = simVars.autopilotAltitude3;
                simVars.autopilotVerticalSpeed = fbwVars.autopilotVerticalSpeed;
                if (simVars.jbVerticalMode == 14) {
                    // V/S mode engaged
                    simVars.autopilotVerticalHold = 1;
                }
                else if (simVars.jbVerticalMode == 15) {
                    // FPA mode engaged
                    simVars.autopilotVerticalHold = -1;
                    simVars.autopilotVerticalSpeed = fbwVars.autopilotFpa;
                }
                else {
                    simVars.autopilotVerticalHold = 0;
                }
                simVars.autopilotApproachHold = simVars.jbLocMode;
                simVars.autopilotGlideslopeHold = simVars.jbApprMode;
                simVars.tfAutoBrake = simVars.jbAutobrake + 1;
                simVars.exhaustGasTemp1 = fbwVars.engineEgt1;
                simVars.exhaustGasTemp2 = fbwVars.engineEgt2;
                simVars.engineFuelFlow1 = fbwVars.engineFuelFlow1;
                simVars.engineFuelFlow2 = fbwVars.engineFuelFlow2;
            }
            else if (is747) {
                // Map Salty 747 vars to real vars
                simVars.autopilotAltitude = simVars.autopilotAltitude3;

                // Slot index 1 = Selected, 2 = Managed
                simVars.autopilotHeadingLock = simVars.autopilotHeadingSlotIndex == 1;
                simVars.autopilotVerticalHold = simVars.autopilotVsSlotIndex == 1;

                // B747 Bug - Fix initial autopilot altitude
                if (simVars.altAboveGround < 50 && simVars.autopilotAltitude > 49900) {
                    // Set autopilot altitude to 5000
                    //SimConnect_TransmitClientEvent(hSimConnect, 0, KEY_AP_ALT_VAR_SET_ENGLISH, 5000, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY);
                    writeJetbridgeVar(KEY_AP_ALT_VAR_SET_ENGLISH, 5000);
                }

                // B747 Bug - Fix master battery
                if (simVars.batteryLoad > 1.2) {
                    if (simVars.elecBat1 == 0) {
                        simVars.elecBat1 = 1;
                        simVars.elecBat2 = 1;
                        printf("Batteries on, load: %f\n", simVars.batteryLoad);
                        fflush(stdout);
                    }
                }
                else if (simVars.batteryLoad > 0) {
                    // Ignore fake load 0.0 setting
                    if (simVars.elecBat1 == 1) {
                        simVars.elecBat1 = 0;
                        simVars.elecBat2 = 0;
                        printf("Batteries off, load: %f\n", simVars.batteryLoad);
                        fflush(stdout);
                    }
                }
            }
            else {
                simVars.elecBat1 = simVars.dcVolts > 0;
            }

            if (simVars.connected && isAirliner && simVars.engineFuelFlow1 > 0 && simVars.suctionPressure < 0.001) {
                // Stop Annunciator reporting a vacuum fault on airliners
                simVars.suctionPressure = 5;
            }

            if (simVars.altAboveGround > 50) {
                hasFlown = true;
                simVars.landingRate = -999;

                if (initiatedPushback) {
                    // Reset ground state
                    initiatedPushback = false;
                    onStandState = 0;
                }

                if (!completedTakeOff && simVars.altAltitude > 10000) {
                    completedTakeOff = true;
                }
            }
            else if (completedTakeOff && simVars.elecBat1 == 0) {
                printf("Reset flight (Battery off)\n");
                fflush(stdout);
                completedTakeOff = false;
            }
#ifdef PICO_USB
            // Populate simvars for Pico USB devices
            picoRefresh();

            if (isNewAircraft) {
                simVars.sbMode = 0;     // Default to autopilot on switchbox
            }
#endif

            if (fixedPushback != -1) {
                // Pushback goes wrong sometimes (pushbackState == 4)
                fixedPushback++;
                if (fixedPushback == 20) {
                    if (simVars.pushbackState < 4) {
                        fixedPushback = -1;
                    }
                    else {
                        printf("Extra start pushback\n");
                        //SimConnect_TransmitClientEvent(hSimConnect, 0, KEY_TOGGLE_PUSHBACK, 0, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY);
                        writeJetbridgeVar(KEY_TOGGLE_PUSHBACK, 0);
                    }
                }
                else if (fixedPushback == 40) {
                    fixedPushback = -1;
                    if (simVars.pushbackState < 3) {
                        printf("Extra stop pushback\n");
                        //SimConnect_TransmitClientEvent(hSimConnect, 0, KEY_TOGGLE_PUSHBACK, 0, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY);
                        writeJetbridgeVar(KEY_TOGGLE_PUSHBACK, 0);
                    }
                }
            }

            if (!simVars.connected || simVars.elecBat1 == 0) {
                hasFlown = false;
                simVars.landingRate = -999;
            }

            // Record landing rate. TouchdownVs isn't accurate so use actual VS instead.
            if (hasFlown && simVars.onGround && simVars.landingRate == -999) {
                simVars.landingRate = abs(simVars.vsiVerticalSpeed);
                if (simVars.landingRate != 0) {
                    printf("Landing Rate: %d FPM\n", (int)((simVars.landingRate * 60) + 0.5));
                }
            }

            //// For testing only - Leave commented out
            //if (displayDelay > 0) {
            //    displayDelay--;
            //}
            //else {
            //    //printf("Aircraft: %s   Cruise Speed: %f\n", simVars.aircraft, simVars.cruiseSpeed);
            //    displayDelay = 60;
            //}

            break;
        }
        default:
        {
            printf("SimConnect unknown request id: %lu\n", pObjData->dwRequestID);
            fflush(stdout);
            break;
        }
        }

        break;
    }

#ifdef jetbridgeFallback
    case SIMCONNECT_RECV_ID_CLIENT_DATA: {
        auto pClientData = static_cast<SIMCONNECT_RECV_CLIENT_DATA*>(pData);

        if (pClientData->dwRequestID == jetbridge::kDownlinkRequest) {
            auto packet = static_cast<jetbridge::Packet*>((jetbridge::Packet*)&pClientData->dwData);
            if (isA310) {
                updateA310FromJetbridge(packet->data);
            }
            else if (isFbw) {
                updateFbwFromJetbridge(packet->data);
            }
        }
        break;
    }
#endif

    case SIMCONNECT_RECV_ID_QUIT:
    {
        // Comment out next line to stay running when FS2020 quits
        //quit = true;
        printf("SimConnect Quit\n");
        fflush(stdout);
        break;
    }
    }
}

void addReadDefs()
{
    varsStart = (double*)&simVars + 1;
    varsSize = 0;
    bool foundInternal = false;
    
    //printf("Debug - varsSize calculation:\n");
    //printf("  simVars address: %p\n", &simVars);
    //printf("  varsStart: %p\n", varsStart);
    //printf("  Offset: %ld\n", (long)varsStart - (long)&simVars);

    for (int i = 0;; i++) {
        //printf("varsSize: %d\n", varsSize);
        //printf("Processing variable %s\n", SimVarDefs[i][0]);
        if (SimVarDefs[i][0] == NULL) {
            break;
        }

        if (_stricmp(SimVarDefs[i][1], "internal") == 0) {
            foundInternal = true;
        }
        else if (foundInternal) {
            printf("ERROR: Internal variables must come last. Cannot add: %s\n", SimVarDefs[i][0]);
        }
        else if (_strnicmp(SimVarDefs[i][1], "string", 6) == 0) {
            // Add string
            SIMCONNECT_DATATYPE dataType;
            int dataLen;

            if (strcmp(SimVarDefs[i][1], "string32") == 0) {
                dataType = SIMCONNECT_DATATYPE_STRING32;
                dataLen = 32;
                //printf("  Found string32 variable: %s\n", SimVarDefs[i][0]);
                //printf("  Current varsSize: %d\n", varsSize);
                //printf("  Adding %d bytes\n", dataLen);
            }
            else {
                printf("Unsupported string type: %s\n", SimVarDefs[i][1]);
                dataType = SIMCONNECT_DATATYPE_STRING32;
                dataLen = 32;
            }

#ifdef _WIN32
            // Handle string data type
            if (SimConnect_AddToDataDefinition(hSimConnect, DEF_READ_ALL, SimVarDefs[i][0], NULL, dataType) != 0) {
                printf("Data def failed: %s (string)\n", SimVarDefs[i][0]);
            }
            else {
                varsSize += dataLen;
            }
#else
            // Non-Windows: Just accumulate size for string data
            //printf("  Adding string data for: %s\n", SimVarDefs[i][0]);
            //printf("  Current varsSize: %d\n", varsSize);
            //printf("  Adding %d bytes for string\n", dataLen);
            varsSize += dataLen;
            //printf("  New varsSize: %d\n", varsSize);
#endif
        }
        else if (_stricmp(SimVarDefs[i][1], "jetbridge") == 0) {
            // SimConnect variables start after all Jetbridge variables
            //printf("  Found jetbridge variable: %s\n", SimVarDefs[i][0]);
            //printf("  Current varsStart: %p\n", varsStart);
            varsStart++; // Increment pointer to next variable
            //printf("  New varsStart: %p\n", varsStart);
        }
        else {
            // Handle double (float64) data type
#ifdef _WIN32
            // Windows: Add to SimConnect definition
            if (SimConnect_AddToDataDefinition(hSimConnect, (DWORD)DEF_READ_ALL, SimVarDefs[i][0], SimVarDefs[i][1]) != 0) {
                printf("Data def failed: %s, %s\n", SimVarDefs[i][0], SimVarDefs[i][1]);
            }
            else {
                varsSize += sizeof(double);
            }
#else
            // Non-Windows: Just accumulate size for double data
            //printf("  Adding double data for: %s\n", SimVarDefs[i][0]);
            //printf("  Current varsSize: %d\n", varsSize);
            //printf("  Adding %lu bytes for double\n", sizeof(double));
            varsSize += sizeof(double);
            //printf("  New varsSize: %d\n", varsSize);
#endif
        }
    }
}

void mapEvents()
{
    for (int i = 0;; i++) {
        if (WriteEvents[i].name == NULL) {
            break;
        }

#ifdef _WIN32
        if (SimConnect_MapClientEventToSimEvent(hSimConnect, WriteEvents[i].id, WriteEvents[i].name) != 0) {
            printf("Map event failed: %s\n", WriteEvents[i].name);
        }
#endif
    }
}

void loadAppSettings()
{
    // Load settings from JSON file
    std::string settingsPath = "./settings/data_link-settings.json";
    if (SettingsManager::loadSettings(settingsPath, appSettings)) {
        printf("Settings loaded from: %s\n", settingsPath.c_str());
    } else {
        printf("Using default settings\n");
    }
    
    // Apply settings to global variables
    InstrumentListenPort = appSettings.instrumentPanel.listenPort;
    InstrumentResponsePort = appSettings.instrumentPanel.responsePort;
    SimDataPort = appSettings.simulatorData.port;
    
    printf("Configuration:\n");
    printf("  Instrument Panel Host: %s\n", appSettings.instrumentPanel.host.c_str());
    printf("  Instrument Listen Port: %d\n", InstrumentListenPort);
    printf("  Instrument Response Port: %d\n", InstrumentResponsePort);
    printf("  Simulator Data Host: %s:%d\n", appSettings.simulatorData.host.c_str(), appSettings.simulatorData.port);
    printf("  Data Link Host: %s:%d\n", appSettings.dataLink.host.c_str(), appSettings.dataLink.port);
    fflush(stdout);
}

void init()
{
    loadAppSettings();
    addReadDefs();
    mapEvents();

#ifdef _WIN32
    // Start requesting data
    if (SimConnect_RequestDataOnSimObject(hSimConnect, REQ_ID, DEF_READ_ALL, SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD_VISUAL_FRAME, 0, 0, 0, 0) != 0) {
        printf("Failed to start requesting data\n");
    }
#endif

#ifdef jetbridgeFallback
    jetbridgeInit(hSimConnect);
#endif
}

void cleanUp()
{
    if (hSimConnect) {
#ifdef _WIN32
        if (SimConnect_RequestDataOnSimObject(hSimConnect, REQ_ID, DEF_READ_ALL, SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD_NEVER, 0, 0, 0, 0) != 0) {
            printf("Failed to stop requesting data\n");
        }

        printf("Disconnecting from MS FS2020\n");
        SimConnect_Close(hSimConnect);
#endif
    }

    if (!quit) {
        printf("Stopping server\n");
        quit = true;
    }

    // Wait for server to quit
    if (serverThread) {
        serverThread->join();
        delete serverThread;
        serverThread = nullptr;
    }

    WSACleanup();
    printf("Finished\n");
}

void printEnumValues() {
    printf("\n=== SimConnect Enum Values ===\n");
    printf("\nReceive ID Values (SIMCONNECT_RECV_ID):\n");
    printf("SIMCONNECT_RECV_ID_NULL = %d\n", SIMCONNECT_RECV_ID_NULL);
    printf("SIMCONNECT_RECV_ID_EVENT = %d\n", SIMCONNECT_RECV_ID_EVENT);
    printf("SIMCONNECT_RECV_ID_SIMOBJECT_DATA = %d\n", SIMCONNECT_RECV_ID_SIMOBJECT_DATA);
    printf("SIMCONNECT_RECV_ID_CLIENT_DATA = %d\n", SIMCONNECT_RECV_ID_CLIENT_DATA);
    printf("SIMCONNECT_RECV_ID_QUIT = %d\n", SIMCONNECT_RECV_ID_QUIT);

    printf("\nEvent Values (used in uEventID):\n");
    printf("SIM_START = %d\n", SIM_START);
    printf("SIM_STOP = %d\n", SIM_STOP);

    printf("\nRequest Values:\n");
    printf("REQ_ID = %d\n", REQ_ID);
    printf("DEF_READ_ALL = %d\n", DEF_READ_ALL);

    printf("\nJetbridge Values:\n");
    printf("jetbridge::kDownlinkRequest = %d\n", jetbridge::kDownlinkRequest);
    printf("===========================\n\n");
    fflush(stdout);
}

#ifdef _WIN32
int __cdecl _tmain(int argc, _TCHAR* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    printf("Instrument Data Link %s Copyright (c) 2024 Scott Vincent\n", versionString);
    printf("Instruments Data Size: %ld bytes\n", instrumentsDataSize);
    fflush(stdout);
    
    printEnumValues();
    
    // Initialize settings and variables first
    init();
    
    // Create and start server thread
    serverThread = new std::thread(server);
    
    printEnumValues();

#ifdef _WIN32
    printf("Searching for local MS FS2020...\n");
#else
    printf("Waiting for sim data...\n");
#endif
    simVars.connected = 0;

#ifdef jetbridgeFallback
    std::thread jetbridgeThread(pollJetbridge);
#endif
    // HRESULT result; // TODO: implement correctly for windows
    HRESULT result = 0;
    int loopMillis = 10;
    int retryDelay = 0;

#ifdef _WIN32
    // Windows implementation using SimConnect
    while (!quit) {
        if (simVars.connected) {
            result = SimConnect_CallDispatch(hSimConnect, MyDispatchProc, NULL);
            if (result != 0) {
                printf("Disconnected from MS FS2020\n");
                simVars.connected = 0;
                printf("Searching for local MS FS2020...\n");
            }
        }
        else if (retryDelay > 0) {
            retryDelay--;
        }
        else {
            result = SimConnect_Open(&hSimConnect, "Instrument Data Link", NULL, 0, 0, 0);
            if (result == 0) {
                printf("Connected to MS FS2020\n");
                // init();
                simVars.connected = 1;
            }
            else {
                retryDelay = 200;
            }
        }
        Sleep(loopMillis);
    }
#else
    // Linux implementation using UDP
    int simDataSockfd;
    struct sockaddr_in simDataAddr;
    char simDataBuffer[MaxDataSize];

    // Create UDP socket for simulator data
    if ((simDataSockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        printf("SimData socket creation failed\n");
        fflush(stdout);
        return 1;
    }

    // Set socket to non-blocking mode
    int flags = fcntl(simDataSockfd, F_GETFL, 0);
    if (flags < 0 || fcntl(simDataSockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        printf("Failed to set sim data socket to non-blocking mode\n");
        fflush(stdout);
        close(simDataSockfd);
        return 1;
    }

    // Configure sim data socket
    memset(&simDataAddr, 0, sizeof(simDataAddr));
    simDataAddr.sin_family = AF_INET;
    in_addr_t addr = inet_addr(appSettings.simulatorData.host.c_str());
    if (addr == INADDR_NONE) {
        printf("Invalid simulator data host address: %s\n", appSettings.simulatorData.host.c_str());
        fflush(stdout);
        close(simDataSockfd);
        return 1;
    }
    simDataAddr.sin_addr.s_addr = addr;
    simDataAddr.sin_port = htons(SimDataPort);

    // Set socket reuse option
    int reuse = 1;
    if (setsockopt(simDataSockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
        printf("Failed to set SO_REUSEADDR on sim data socket\n");
        fflush(stdout);
        close(simDataSockfd);
        return 1;
    }

    // Bind sim data socket
    if (bind(simDataSockfd, (const struct sockaddr *)&simDataAddr, sizeof(simDataAddr)) < 0) {
        printf("SimData socket bind failed: %s\n", strerror(errno));
        fflush(stdout);
        close(simDataSockfd);
        return 1;
    }

    printf("SimData socket listening on port %d...\n", SimDataPort);
    fflush(stdout);

    const int MAX_FAILURES = 10;  // Number of consecutive failures before declaring connection lost
    int failureCount = 0;

    while (!quit) {
        if (simVars.connected) {
            // Try to receive simulator data
            socklen_t len = sizeof(simDataAddr);
            int n = recvfrom(simDataSockfd, simDataBuffer, MaxDataSize, 0, 
                            (struct sockaddr *)&simDataAddr, &len);

            if (n > 0) {
                printf("Received %d bytes of sim data\n", n);
                fflush(stdout);
                // Process received data
                SIMCONNECT_RECV* pData = (SIMCONNECT_RECV*)simDataBuffer;
                MyDispatchProc(pData, n, NULL);
                failureCount = 0;  // Reset failure counter on successful receive
            }
            else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                failureCount++;
                if (failureCount >= MAX_FAILURES) {
                    printf("Connection lost after %d consecutive failures\n", MAX_FAILURES);
                    fflush(stdout);
                    simVars.connected = 0;
                    printf("Waiting for simulator data...\n");
                    fflush(stdout);
                    failureCount = 0;  // Reset for next connection
                }
            }
        }
        else if (retryDelay > 0) {
            retryDelay--;
        }
        else {
            // Try to receive initial data to establish connection
            socklen_t len = sizeof(simDataAddr);
            int n = recvfrom(simDataSockfd, simDataBuffer, MaxDataSize, 0, 
                            (struct sockaddr *)&simDataAddr, &len);

            if (n > 0) {
                printf("Connected to simulator\n");
                // Process the received packet
                SIMCONNECT_RECV* pData = (SIMCONNECT_RECV*)simDataBuffer;
                MyDispatchProc(pData, n, NULL);
                init();
                simVars.connected = 1;
            }
            else {
                retryDelay = 200;
            }
        }

        usleep(loopMillis * 1000);  // Convert to microseconds
    }

    // Cleanup
    close(simDataSockfd);
#endif

#ifdef jetbridgeFallback
    // Wait for thread to exit
    jetbridgeThread.join();
#endif

    cleanUp();
    return 0;
}

void addDeltaDouble(long offset, double newVal)
{
    DeltaDouble deltaDouble;
    deltaDouble.offset = offset;
    deltaDouble.data = newVal;

    memcpy(deltaData + deltaSize, &deltaDouble, deltaDoubleSize);
    deltaSize += deltaDoubleSize;
}

void addDeltaString(long offset, char *newVal)
{
    DeltaString deltaString;
    deltaString.offset = 0x10000 | offset;  // Set high bit so we know it is a string
    strncpy(deltaString.data, newVal, 32);  // Only support string32

    memcpy(deltaData + deltaSize, &deltaString, deltaStringSize);
    deltaSize += deltaStringSize;
}

/// <summary>
/// Send the full set of data if this a new connection or we
/// don't want to use deltas.
/// </summary>
void sendFull(char* prevSimVars, long dataSize)
{
    bytes = sendto(instrumentResponseSockfd, (char*)&simVars, dataSize, 0, (SOCKADDR*)&senderAddr, addrSize);
#ifdef SHOW_NETWORK_USAGE
    networkOut += bytes;
#endif

    // Update prev data
    memcpy(prevSimVars, &simVars, dataSize);
}

/// <summary>
/// If this a new connection send all the data otherwise only send
/// the delta, i.e. data that has changed since we last sent it.
/// This should reduce network bandwidth usage hugely.
/// </summary>
void sendDelta(char* prevSimVars, long dataSize)
{
    // Initialise delta data
    deltaSize = 0;

    // Always send 'connected' var
    addDeltaDouble(0, simVars.connected);
    long offset = sizeof(double);

    // Add all vars that have changed to delta data
    for (int i = 0;; i++) {
        if (SimVarDefs[i][0] == NULL) {
            break;
        }

        char* oldVarPtr = prevSimVars + offset;
        char* newVarPtr = (char*)&simVars + offset;

        if (_strnicmp(SimVarDefs[i][1], "string", 6) == 0) {
            // Has string changed?
            if (strncmp(oldVarPtr, newVarPtr, 32) != 0) {
                addDeltaString(offset, newVarPtr);
                memcpy(oldVarPtr, newVarPtr, 32);
            }

            offset += 32;
        }
        else {
            // Has double changed?
            double* oldVar = (double*)oldVarPtr;
            double* newVar = (double*)newVarPtr;
            if (*oldVar != *newVar) {
                addDeltaDouble(offset, *newVar);
                memcpy(oldVarPtr, newVarPtr, sizeof(double));
            }

            offset += 8;
        }

        // Next variable
        if (offset >= dataSize) {
            break;
        }
    }

    if (deltaSize < dataSize) {
        // Send delta data
        bytes = sendto(instrumentResponseSockfd, (char*)deltaData, deltaSize, 0, (SOCKADDR*)&senderAddr, addrSize);
    }
    else {
        // Send full data
        bytes = sendto(instrumentResponseSockfd, (char*)&simVars, dataSize, 0, (SOCKADDR*)&senderAddr, addrSize);
    }
#ifdef SHOW_NETWORK_USAGE
    networkOut += bytes;
#endif
}

/// <summary>
/// If an event button is pressed return either EVENT_NONE or the event (sound)
/// that should be played depending on current aircraft state.
/// </summary>
EVENT_ID getCustomEvent(int eventNum)
{
    EVENT_ID event = EVENT_NONE;
    bool isClimbing = simVars.vsiVerticalSpeed > 3;
    bool isDescending = simVars.vsiVerticalSpeed < -3;

    FLIGHT_PHASE phase = GROUND;
    if (simVars.altAboveGround > 50) {
        if (simVars.altAltitude < 10000) {
            if (!completedTakeOff) {
                phase = TAKEOFF;
            }
            else if (isClimbing) {
                phase = GO_AROUND;
            }
            else {
                phase = APPROACH;
            }
        }
        else if (isClimbing) {
            phase = CLIMB;
        }
        else if (isDescending) {
            phase = DESCENT;
        }
        else {
            phase = CRUISE;
        }
    }

    switch (eventNum) {
    case 1:
        // Event button 1 pressed
        switch (phase) {
            case GROUND:
                if (completedTakeOff) {
                    // Landed
                    if (simVars.parkingBrakeOn) {
                        // Arrived at stand
                        return EVENT_DOORS_FOR_DISEMBARK;
                    }
                    else {
                        // Taxi in
                        return EVENT_DOORS_TO_MANUAL;
                    }
                }
                else if (simVars.pushbackState < 3) {
                    // Pushing back
                    return EVENT_DOORS_TO_AUTO;
                }
                else if (initiatedPushback) {
                    // Completed pushback
                    return EVENT_SEATS_FOR_TAKEOFF;
                }
                else if (simVars.parkingBrakeOn) {
                    // Still on stand
                    onStandState++;
                    switch (onStandState) {
                    case 1:
                        return EVENT_DOORS_FOR_BOARDING;
                    case 2:
                        return EVENT_WELCOME_ON_BOARD;
                    case 3:
                        return EVENT_BOARDING_COMPLETE;
                    }
                }
                return EVENT_NONE;
            case TAKEOFF:
                return EVENT_NONE;
            case CLIMB:
                return EVENT_START_SERVING;
            case CRUISE:
                return EVENT_START_SERVING;
            case DESCENT:
                return EVENT_NONE;
            case APPROACH:
                if (simVars.altAboveGround > 4000) {
                    return EVENT_LANDING_PREPARE_CABIN;
                }
                else {
                    return EVENT_SEATS_FOR_LANDING;
                }
            case GO_AROUND:
                return EVENT_NONE;
        }

    case 2:
        // Event button 2 pressed
        switch (phase) {
        case GROUND:
            if (completedTakeOff) {
                // Landed
                if (simVars.parkingBrakeOn) {
                    // Arrived at stand
                    printf("Reset flight (Captain goodbye)\n");
                    fflush(stdout);
                    completedTakeOff = false;
                    return EVENT_DISEMBARK;
                }
                else {
                    // Taxi in
                    return EVENT_REMAIN_SEATED;
                }
            }
            else {
                return simVars.pushbackState < 3 ? EVENT_PUSHBACK_STOP : EVENT_PUSHBACK_START;
            }
        case TAKEOFF:
            // If not reached 10000ft but descending and button 2 pressed just assume short flight
            if (!completedTakeOff && isDescending) {
                completedTakeOff = true;
                return EVENT_FINAL_DESCENT;
            }
            return EVENT_NONE;
        case CLIMB:
            if (simVars.seatBeltsSwitch == 1) {
                return EVENT_TURBULENCE;
            }
            else {
                return EVENT_NONE;
            }
        case CRUISE:
            if (simVars.seatBeltsSwitch == 1) {
                return EVENT_TURBULENCE;
            }
            else {
                return EVENT_REACHED_CRUISE;
            }
        case DESCENT:
            if (simVars.seatBeltsSwitch == 1) {
                return EVENT_TURBULENCE;
            }
            else {
                return EVENT_REACHED_TOD;
            }
        case APPROACH:
            if (simVars.altAboveGround > 4000) {
                return EVENT_FINAL_DESCENT;
            }
        case GO_AROUND:
            return EVENT_GO_AROUND;
        }
    }

    return EVENT_NONE;
}

void processRequest(int bytes)
{
    //// For testing only - Leave commented out
    //if (request.requestedSize == writeDataSize) {
    //    printf("Received %d bytes from %s - Write Request event: %s\n", bytes, inet_ntoa(senderAddr.sin_addr), WriteEvents[request.writeData.eventId].name);
    //}
    //else {
    //    // To  test you can send from client with this command: echo - e '\x1\x0\x0\x0' | ncat -u 192.168.1.80 52020
    //    printf("Received %d bytes from %s - Requesting %d bytes\n", bytes, inet_ntoa(senderAddr.sin_addr), request.requestedSize);
    //}

    if (request.requestedSize == writeDataSize) {
         // This is a write
        if (request.writeData.eventId == KEY_ENG_CRANK) {
            if (isA310) {
                // 1 = Start A, 3 = Off
                int value = 3;
                if (request.writeData.value == 1) {
                    value = 1;
                }
                writeJetbridgeVar(A310_ENG_IGNITION, value);
            }
            return;
        }

        if (!simVars.connected) {
            return;
        }

        //// For testing only - Leave commented out
        //if (request.writeData.eventId == KEY_CABIN_SEATBELTS_ALERT_SWITCH_TOGGLE) {
        //    request.writeData.eventId = KEY_FLAPS_INCR;
        //    request.writeData.value = 0;
        //    printf("Intercepted event - Changed to: %d = %f\n", request.writeData.eventId, request.writeData.value);
        //    printf("Flaps: %f\n", simVars.tfFlapsIndex);
        //    writeJetbridgeVar("K:FLAPS HANDLE INDEX, number", simVars.tfFlapsIndex + 1.0f);
        //}
        //else {
        //    printf("Unintercepted event: %d (%d) = %f\n", request.writeData.eventId, KEY_CABIN_SEATBELTS_ALERT_SWITCH_TOGGLE, request.writeData.value);
        //}

        if (request.writeData.eventId >= VJOY_BUTTONS && request.writeData.eventId <= VJOY_BUTTONS_END) {
            // Override vJoy anti ice buttons for A310
            if (isA310) {
                if (request.writeData.eventId == VJOY_BUTTON_13) {
                    // Anti ice on
                    writeJetbridgeVar(A310_ENG1_ANTI_ICE, 1);
                    writeJetbridgeVar(A310_ENG2_ANTI_ICE, 1);
                    writeJetbridgeVar(A310_WING_ANTI_ICE, 1);
                    return;
                }
                else if (request.writeData.eventId == VJOY_BUTTON_12) {
                    // Anti ice off
                    writeJetbridgeVar(A310_ENG1_ANTI_ICE, 0);
                    writeJetbridgeVar(A310_ENG2_ANTI_ICE, 0);
                    writeJetbridgeVar(A310_WING_ANTI_ICE, 0);
                    return;
                }
            }

#ifdef vJoyFallback
            vJoyButtonPress(request.writeData.eventId);
#else
            printf("vJoy button event ignored - vJoyFallback is not enabled\n");
#endif
            return;
        }

#ifdef jetbridgeFallback
        if (isA310 && jetbridgeA310ButtonPress(request.writeData.eventId, request.writeData.value)) {
            return;
        }
        else if (isFbw && jetbridgeFbwButtonPress(request.writeData.eventId, request.writeData.value)) {
            return;
        }
        else if (isK100 && jetbridgeK100ButtonPress(request.writeData.eventId, request.writeData.value)) {
            return;
        }
        else if (isPA28 && jetbridgePA28ButtonPress(request.writeData.eventId, request.writeData.value)) {
            return;
        }
        else if (jetbridgeMiscButtonPress(request.writeData.eventId, request.writeData.value)) {
            return;
        }
#endif

        // Process custom events
        if (request.writeData.eventId == KEY_CHECK_EVENT) {
            int eventNum = (int)(request.writeData.value);
            // Ignore event 1 in GA aircraft (button used for Engine Primer instead)
            if (eventNum == 1 && !isAirliner) {
                return;
            }
            else if (isA310 && a310Vars.engineIgnition < 2) {
                // If engine ignition is on then event keys start engines instead
                if (eventNum == 1) {
                    writeJetbridgeVar(A310_ENG1_STARTER, 1);
                }
                else {
                    writeJetbridgeVar(A310_ENG2_STARTER, 1);
                }
                return;
            }
            EVENT_ID event = getCustomEvent(eventNum);
            sendto(instrumentResponseSockfd, (char*)&event, sizeof(int), 0, (SOCKADDR*)&senderAddr, addrSize);
            if (event == EVENT_PUSHBACK_START || event == EVENT_PUSHBACK_STOP) {
                // Don't return (need to trigger the pushback)
                request.writeData.eventId = KEY_TOGGLE_PUSHBACK;
                if (event == EVENT_PUSHBACK_START) {
                    initiatedPushback = true;
                    fixedPushback = -1;
                }
                else {
                    fixedPushback = 0;
                }
            }
            else {
                return;
            }
        }
        else if (request.writeData.eventId == KEY_SKYTRACK_STATE) {
            skytrackState = request.writeData.value;
            return;
        }

        if (request.writeData.eventId == EVENT_RESET_DRONE_FOV) {
            writeJetbridgeVar(DRONE_CAMERA_FOV, 50);
            return;
        }

        if (request.writeData.eventId == KEY_TOGGLE_RAMPTRUCK) {
            printf("Ramp truck requested\n");
        }

        //if (SimConnect_TransmitClientEvent(hSimConnect, 0, request.writeData.eventId, (DWORD)request.writeData.value, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY) != 0) {
        //    printf("Failed to transmit event: %d\n", request.writeData.eventId);
        //}
        writeJetbridgeVar(request.writeData.eventId, request.writeData.value);
    }
    else if (request.requestedSize == instrumentsDataSize) {
        // Send instrument data to the client that polled us
        if (active != 1 || request.wantFullData || !UseDeltas) {
            if (active != 1) {
                printf("Instrument panel connected from %s\n", inet_ntoa(senderAddr.sin_addr));
                active = 1;
            }
            sendFull(prevInstrumentsData, instrumentsDataSize);
        }
        else {
            sendDelta(prevInstrumentsData, instrumentsDataSize);
        }
    }
    else if (request.requestedSize == autopilotDataSize) {
        // Send autopilot data to the client that polled us
        if (!autopilotPanelConnected || request.wantFullData || !UseDeltas) {
            if (!autopilotPanelConnected) {
                printf("Autopilot panel connected from %s\n", inet_ntoa(senderAddr.sin_addr));
                autopilotPanelConnected = true;
            }
            sendFull(prevAutopilotData, autopilotDataSize);
        }
        else {
            sendDelta(prevAutopilotData, autopilotDataSize);
        }
    }
    else if (request.requestedSize == radioDataSize) {
        // Send radio data to the client that polled us
        if (!radioPanelConnected || request.wantFullData || !UseDeltas) {
            if (!radioPanelConnected) {
                printf("Radio panel connected from %s\n", inet_ntoa(senderAddr.sin_addr));
                radioPanelConnected = true;
            }
            sendFull(prevRadioData, radioDataSize);
        }
        else {
            sendDelta(prevRadioData, radioDataSize);
        }
    }
    else if (request.requestedSize == lightsDataSize) {
        // Send power/lights data to the client that polled us
        if (!lightsPanelConnected || request.wantFullData || !UseDeltas) {
            if (!lightsPanelConnected) {
                printf("Power/Lights panel connected from %s\n", inet_ntoa(senderAddr.sin_addr));
                lightsPanelConnected = true;
            }
            sendFull(prevLightsData, lightsDataSize);
        }
        else {
            sendDelta(prevLightsData, lightsDataSize);
        }
    }
    else {
        // Data size mismatch
        bytes = sendto(instrumentResponseSockfd, (char*)&instrumentsDataSize, 4, 0, (SOCKADDR*)&senderAddr, addrSize);
#ifdef SHOW_NETWORK_USAGE
        networkOut += bytes;
#endif
        printf("Client at %s requested %d bytes instead of %ld bytes\n",
            inet_ntoa(senderAddr.sin_addr), request.requestedSize, (long)instrumentsDataSize);
    }
}

void server()
{
    printf("Starting UDP server...\n");
    fflush(stdout);

#ifdef _WIN32
    WSADATA wsaData;
    int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (err != 0) {
        printf("Failed to initialise Windows Sockets: %d\n", err);
        fflush(stdout);
        exit(1);
    }
    printf("Windows Sockets initialized\n");
    fflush(stdout);
#endif

    // Create UDP sockets for instrument panel communication
#ifdef _WIN32
    if ((instrumentListenSockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET ||
        (instrumentResponseSockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
#else
    if ((instrumentListenSockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0 ||
        (instrumentResponseSockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
#endif
        printf("Server failed to create UDP sockets\n");
        exit(1);
    }

    int opt = 1;
    setsockopt(instrumentListenSockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    setsockopt(instrumentResponseSockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    // Bind instrument listening socket
    sockaddr_in instrumentListenAddr;
    instrumentListenAddr.sin_family = AF_INET;
    in_addr_t instr_addr = inet_addr(appSettings.instrumentPanel.host.c_str());
    if (instr_addr == INADDR_NONE) {
        printf("Invalid instrument panel host address: %s\n", appSettings.instrumentPanel.host.c_str());
        fflush(stdout);
        exit(1);
    }
    instrumentListenAddr.sin_addr.s_addr = instr_addr;
    instrumentListenAddr.sin_port = htons(InstrumentListenPort);

    // Bind instrument panel socket
    if (bind(instrumentListenSockfd, (sockaddr*)&instrumentListenAddr, sizeof(instrumentListenAddr)) < 0) {
#ifdef _WIN32
        printf("Server failed to bind to instrument port %d: %d\n", InstrumentListenPort, WSAGetLastError());
#else
        printf("Server failed to bind to instrument port %d: %s\n", InstrumentListenPort, strerror(errno));
#endif
        fflush(stdout);
        exit(1);
    }
    
    printf("Successfully bound to instrument port %d on host %s\n", InstrumentListenPort, appSettings.instrumentPanel.host.c_str());
    fflush(stdout);

    deltaData = (char*)malloc(MaxDataSize);
    prevInstrumentsData = (char*)malloc(MaxDataSize);
    prevAutopilotData = (char*)malloc(MaxDataSize);
    prevRadioData = (char*)malloc(MaxDataSize);
    prevLightsData = (char*)malloc(MaxDataSize);

    printf("Server listening for instruments on port %d (responses on host %s port %d)\n", 
           InstrumentListenPort, appSettings.instrumentPanel.host.c_str(), InstrumentResponsePort);
    fflush(stdout);

    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;

    while (!quit) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(instrumentListenSockfd, &fds);

        // Wait for instrument panel to poll (non-blocking, 0.5 second timeout)
        int sel = select(FD_SETSIZE, &fds, 0, 0, &timeout);
        if (sel > 0) {
            socklen_t socklen = addrSize;
            bytes = recvfrom(instrumentListenSockfd, (char*)&request, sizeof(request), 0, (SOCKADDR*)&senderAddr, &socklen);
            
            printf("Instrument request received:");
            for (int i = 0; i < bytes; i++) {
                printf(" %02x", ((unsigned char*)&request)[i]);
            }
            printf("\n");
            fflush(stdout);

            // Set response address and port
            inet_pton(AF_INET, appSettings.instrumentPanel.host.c_str(), &senderAddr.sin_addr);
            senderAddr.sin_port = htons(InstrumentResponsePort);
            addrSize = socklen;

#ifdef SHOW_NETWORK_USAGE
            networkIn += bytes;
#endif
            if (bytes > 3) {
                printf("Received %d bytes from %s\n", bytes, inet_ntoa(senderAddr.sin_addr));
                fflush(stdout);
                processRequest(bytes);
            }
            else {
                if (bytes == -1) {
#ifdef _WIN32
                    int error = WSAGetLastError();
                    if (error == 10040) {
                        printf("Received more than %ld bytes from %s (WSAError = %d)\n", sizeof(request), inet_ntoa(senderAddr.sin_addr), error);
                    }
                    else {
                        printf("Received from %s but WSAError = %d\n", inet_ntoa(senderAddr.sin_addr), error);
                    }
#else
                    printf("Receive error from %s: %s\n", inet_ntoa(senderAddr.sin_addr), strerror(errno));
#endif
                    fflush(stdout);
                }
                else {
                    printf("Received %d bytes from %s - Not a valid request\n", bytes, inet_ntoa(senderAddr.sin_addr));
                    fflush(stdout);
                }
                bytes = SOCKET_ERROR;
            }
        }
        else {
            bytes = SOCKET_ERROR;
        }

        if (bytes == SOCKET_ERROR && active != 0) {
            printf("Waiting for instrument panel to connect\n");
            fflush(stdout);
            active = 0;
        }

#ifdef SHOW_NETWORK_USAGE
#ifdef _WIN32
        ULONGLONG now = GetTickCount64();
#else
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ULONGLONG now = (ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL);
#endif
        double elapsedMillis = now - networkStart;
        if (elapsedMillis > 2000) {
            if (networkStart > 0) {
                double kbInPerSec = networkIn / elapsedMillis;
                double kbOutPerSec = networkOut / elapsedMillis;
                printf("Network Usage: In = %.2f KB/s, Out = %.2f KB/s\n", kbInPerSec, kbOutPerSec);
                fflush(stdout);
            }
            networkStart = now;
            networkIn = 0;
            networkOut = 0;
        }
#endif
    }

    free(deltaData);
    free(prevInstrumentsData);
    free(prevAutopilotData);
    free(prevRadioData);
    free(prevLightsData);

    closesocket(instrumentListenSockfd);
    closesocket(instrumentResponseSockfd);
    printf("Server stopped\n");
    fflush(stdout);
}

#ifdef PICO_USB
/// <summary>
/// Raspberry Pi Pico Switchbox (4 encoders + 4 buttons) appears as a USB joystick with 4 axes and 9 buttons.
/// Raspberry Pi Pico Garmin G1000 appears as another USB joystick with 3 axes and 21 buttons.
/// </summary>
void picoInit()
{
    // Initialise simvars
    simVars.sbMode = 0;     // Default to autopilot on switchbox
    for (int i = 0; i < 7; i++) {
        if (i < 4) {
            simVars.sbEncoder[i] = 0;
        }
        simVars.sbButton[i] = 0;
    }

    simVars.sbParkBrake = -1;

    for (int i = 0; i < 3; i++) {
        g1000Axis[i] = 0;
    }

    for (int i = 0; i < 20; i++) {
        g1000Button[i] = 0;
    }

    initJoysticks();

    for (int id = 0; id < 16; id++) {
        if (strcmp(joystick[id].name, "Pico Switchbox") == 0 && joystick[id].initialised) {
            switchboxId = id;
        }
        else if (strcmp(joystick[id].name, "Pico G1000") == 0 && joystick[id].initialised) {
            g1000Id = id;
        }
        else if (strcmp(joystick[id].name, "Alpha Flight Controls") == 0 && joystick[id].initialised) {
            alphaId = id;
        }
    }

    if (switchboxId >= 0) {
        printf("Found Pico Switchbox joystick id %d\n", switchboxId);
    }
    else {
        printf("No Pico Switchbox connected\n");
    }

    if (g1000Id >= 0) {
        printf("Found Pico G1000 joystick id %d\n", g1000Id);
    }
    else {
        printf("No Pico G1000 connected\n");
    }

    if (alphaId >= 0) {
        printf("Found Alpha Flight Controls joystick id %d\n", alphaId);
    }
    else {
        printf("No Alpha Flight Controls connected\n");
    }
}

void picoRefresh()
{
    if (!picoInitialised) {
        picoInit();
        picoInitialised = true;
    }

    if (switchboxId >= 0) {
        joyRefresh(switchboxId);

        if (joystick[switchboxId].zeroed) {
            switchboxRefresh();
        }
    }

    if (g1000Id >= 0) {
        joyRefresh(g1000Id);

        if (joystick[g1000Id].zeroed) {
            g1000Refresh();
        }
    }

    if (alphaId >= 0) {
        joyRefresh(alphaId);

        // Button 30 is held down when ignition switch is set to off. Convert this to a
        // brief press of vJoy button 1 so that our secondary ignition switch still works.
        if (ignitionOff != joystick[alphaId].button[30]) {
            ignitionOff = joystick[alphaId].button[30];
            if (ignitionOff == 2) {
#ifdef vJoyFallback
                vJoyButtonPress(VJOY_BUTTON_1);
#endif
            }
        }
    }
}

void switchboxRefresh()
{
    // Set simvars
    for (int i = 0; i < 4; i++) {
        simVars.sbEncoder[i] = joystick[switchboxId].axis[i];
    }

    for (int i = 0; i < 7; i++) {
        simVars.sbButton[i] = joystick[switchboxId].button[i];
    }

    // Mode button
    bool button7 = joystick[switchboxId].button[7] == 2;

    // Park Brake
    simVars.sbParkBrake = joystick[switchboxId].button[8] == 2;

    // Check for mode button press (plays a sound when switched)
    if (button7 && !modeChange) {
        modeChange = true;
        if (simVars.sbMode > 3) {
            simVars.sbMode = 1;
        }
        else {
            simVars.sbMode++;
        }

        char soundFile[50];
        switch (int(simVars.sbMode)) {
            case 2: strcpy(soundFile, "Sounds\\Switchbox Radio.wav"); break;
            case 3: strcpy(soundFile, "Sounds\\Switchbox Instruments.wav"); break;
            case 4: strcpy(soundFile, "Sounds\\Switchbox Navigation.wav"); break;
            default: strcpy(soundFile, "Sounds\\Switchbox Autopilot.wav"); break;
        }

        PlaySound(soundFile, NULL, SND_FILENAME | SND_ASYNC);
    }

    // Check for mode button release
    if (modeChange && !button7) {
        modeChange = false;
    }
}

void g1000Refresh()
{
    // Passes 0 if no change, > 0 if turned clockwise and < 0 if turned anti-clockwise
    g1000Encoder(joystick[g1000Id].axis[0] - g1000Axis[0], joystick[g1000Id].axis[1] - g1000Axis[1], joystick[g1000Id].axis[2] - g1000Axis[2]);
        
    g1000Axis[0] = joystick[g1000Id].axis[0];
    g1000Axis[1] = joystick[g1000Id].axis[1];
    g1000Axis[2] = joystick[g1000Id].axis[2];

    for (int i = 0; i < 20; i++) {
        // If button state hasn't changed check for CLR long press
        if (joystick[g1000Id].button[i] == g1000Button[i]) {
            if (i == 18 && joystick[g1000Id].button[i] == 2 && clrPress > 0) {
                time_t now;
                time(&now);
                if (now - clrPress > 1) {
                    writeJetbridgeHvar(WriteEvents[HVAR_G1000_PFD_CLR_LONG].name);
                    clrPress = 0;
                }
            }
            continue;
        }

        // Button state has changed
        g1000Button[i] = joystick[g1000Id].button[i];

        if (g1000Button[i] == 2) {
            // Button has been pressed
            if (i < 2) {
                // Encoder click x 2
                g1000EncoderPush(i);
            }
            else if (i < 14) {
                // Softkey x 12
                g1000SoftkeyPush(i - 2);
            }
            else {
                // Button x 6
                g1000ButtonPush(i - 14);

                // If CLR button save time pressed
                if (i == 18) {
                    time(&clrPress);
                }
            }
        }
        else {
            // Button has been released
            if (i == 18) {
                clrPress = 0;
            }
        }
    }
}

void g1000Encoder(int lowerDiff, int upperDiff, int zoomDiff)
{
    EVENT_ID eventId;

    if (lowerDiff != 0) {
        if (lowerDiff > 0) {
            if (g1000IsPrimary) eventId = HVAR_G1000_PFD_LOWER_INC; else eventId = HVAR_G1000_MFD_LOWER_INC;
        }
        else {
            if (g1000IsPrimary) eventId = HVAR_G1000_PFD_LOWER_DEC; else eventId = HVAR_G1000_MFD_LOWER_DEC;
        }
        writeJetbridgeHvar(WriteEvents[eventId].name);
    }

    if (upperDiff != 0) {
        if (upperDiff > 0) {
            if (g1000IsPrimary) eventId = HVAR_G1000_PFD_UPPER_INC; else eventId = HVAR_G1000_MFD_UPPER_INC;
        }
        else {
            if (g1000IsPrimary) eventId = HVAR_G1000_PFD_UPPER_DEC; else eventId = HVAR_G1000_MFD_UPPER_DEC;
        }
        writeJetbridgeHvar(WriteEvents[eventId].name);
    }

    if (zoomDiff != 0) {
        if (zoomDiff > 0) {
            eventId = HVAR_G1000_MFD_RANGE_DEC;
        }
        else {
            eventId = HVAR_G1000_MFD_RANGE_INC;
        }
        writeJetbridgeHvar(WriteEvents[eventId].name);
    }
}

void g1000SwapWindows()
{
#ifdef _WIN32
    for (HWND hwnd = GetTopWindow(NULL); hwnd != NULL; hwnd = GetNextWindow(hwnd, GW_HWNDNEXT))
    {
        if (!IsWindowVisible(hwnd))
            continue;

        char title[256];
        int len = GetWindowText(hwnd, title, 256);
        if (len == 0)
            continue;

        if (strcmp(title, "AS1000_PFD") == 0) {
            pfdInfo.cbSize = sizeof(WINDOWINFO);
            if (GetWindowInfo(hwnd, &pfdInfo)) {
                pfdWin = hwnd;
            }
        }
        else if (strcmp(title, "AS1000_MFD") == 0) {
            mfdInfo.cbSize = sizeof(WINDOWINFO);
            if (GetWindowInfo(hwnd, &mfdInfo)) {
                mfdWin = hwnd;
            }
        }
    }

    if (!pfdWin || !mfdWin)
        return 1;

    WINDOWINFO tempInfo;
    memcpy(&tempInfo, &pfdInfo, sizeof(WINDOWINFO));
    memcpy(&pfdInfo, &mfdInfo, sizeof(WINDOWINFO));
    memcpy(&mfdInfo, &tempInfo, sizeof(WINDOWINFO));

    UINT flags = SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER;
    SetWindowPos(pfdWin, NULL, pfdInfo.rcWindow.left, pfdInfo.rcWindow.top, pfdInfo.rcWindow.right - pfdInfo.rcWindow.left, pfdInfo.rcWindow.bottom - pfdInfo.rcWindow.top, flags);
    SetWindowPos(mfdWin, NULL, mfdInfo.rcWindow.left, mfdInfo.rcWindow.top, mfdInfo.rcWindow.right - mfdInfo.rcWindow.left, mfdInfo.rcWindow.bottom - mfdInfo.rcWindow.top, flags);

    g1000IsPrimary = pfdInfo.rcWindow.top > -100;
#endif
}

void g1000EncoderPush(int num)
{
    EVENT_ID eventId;
    if (num == 0) {
        if (g1000IsPrimary) eventId = HVAR_G1000_PFD_PUSH; else eventId = HVAR_G1000_MFD_PUSH;
        writeJetbridgeHvar(WriteEvents[eventId].name);
    }
    else {
        g1000SwapWindows();
    }
}

void g1000SoftkeyPush(int num)
{
    int eventNum;
    if (g1000IsPrimary) eventNum = HVAR_G1000_PFD_SOFTKEY_1 + num; else eventNum = HVAR_G1000_MFD_SOFTKEY_1 + num;
    writeJetbridgeHvar(WriteEvents[eventNum].name);
}

void g1000ButtonPush(int num)
{
    EVENT_ID eventId;
    if (g1000IsPrimary) {
        switch (num) {
        case 0: eventId = HVAR_G1000_PFD_DIRECTTO; break;
        case 1: eventId = HVAR_G1000_PFD_MENU; break;
        case 2: eventId = HVAR_G1000_PFD_FPL; break;
        case 3: eventId = HVAR_G1000_PFD_PROC; break;
        case 4: eventId = HVAR_G1000_PFD_CLR; break;
        default: eventId = HVAR_G1000_PFD_ENT; break;
        }
    }
    else {
        switch (num) {
        case 0: eventId = HVAR_G1000_MFD_DIRECTTO; break;
        case 1: eventId = HVAR_G1000_MFD_MENU; break;
        case 2: eventId = HVAR_G1000_MFD_FPL; break;
        case 3: eventId = HVAR_G1000_MFD_PROC; break;
        case 4: eventId = HVAR_G1000_MFD_CLR; break;
        default: eventId = HVAR_G1000_MFD_ENT; break;
        }
    }
    writeJetbridgeHvar(WriteEvents[eventId].name);
}
#endif // PICO_USB

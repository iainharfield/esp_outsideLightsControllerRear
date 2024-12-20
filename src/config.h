#include <WString.h>

#define WDCntlTimes     "/house/cntrl/outside-lights-rear/wd-control-times" // Times received from either UI or Python app
#define WECntlTimes     "/house/cntrl/outside-lights-rear/we-control-times" // Times received from either UI or MySQL via Python app
#define runtimeState    "/house/cntrl/outside-lights-rear/runtime-state"	 // published state: ON, OFF, and AUTO
#define WDUICmdState    "/house/cntrl/outside-lights-rear/wd-command"		 // UI Button press received
#define WEUICmdState    "/house/cntrl/outside-lights-rear/we-command"		 // UI Button press received
#define LightState      "/house/cntrl/outside-lights-rear/light-state"       // ON or OFF
#define RefreshID       "OLR"	

#define StartUpMessage  "/nStarting Outside Lights Rear Controller on "

String deviceName 	= "outside-lights-Rear";
String deviceType 	= "CNTRL";
String app_id 		= "OLR"; 	

const char *oh3CommandTrigger 	= "/house/cntrl/outside-lights-rear/pir-command"; // Event fron the PIR detector (PIRON or PIROFF) PIR to be IMPLEMENTED
const char *oh3StateManual 		= "/house/cntrl/outside-lights-rear/manual-state";	 // 	Status of the Manual control switch control MAN or AUTOr 
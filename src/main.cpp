
// #include <Arduino.h>
#include <ArduinoOTA.h>
#include <Ticker.h>
#include <AsyncMqttClient.h> 
#include <time.h>

#include "hh_defines.h"
#include "hh_utilities.h"
#include "hh_cntrl.h"

// Folling line added to stop compilation error suddenly occuring in 2024???
#include "ESPAsyncDNSServer.h"

#include <config.h>

#define ESP8266_DRD_USE_RTC true
#define ESP_DRD_USE_LITTLEFS false
#define ESP_DRD_USE_SPIFFS false
#define ESP_DRD_USE_EEPROM false
#define DOUBLERESETDETECTOR_DEBUG true
#include <ESP_DoubleResetDetector.h>

//***********************
// Template functions
//***********************
bool onMqttMessageAppExt(char *, char *, const AsyncMqttClientMessageProperties &, const size_t &, const size_t &, const size_t &);
bool onMqttMessageAppCntrlExt(char *, char *, const AsyncMqttClientMessageProperties &, const size_t &, const size_t &, const size_t &);
void appMQTTTopicSubscribe();
void telnet_extension_1(char);
void telnet_extension_2(char);
void telnet_extensionHelp(char);
void startTimesReceivedChecker();
void processCntrlTOD_Ext();

//*************************************
// defined in asyncConnect.cpp
//*************************************
extern void mqttTopicsubscribe(const char *topic, int qos);
extern void platform_setup(bool);
extern void handleTelnet();
extern void printTelnet(String);
extern AsyncMqttClient mqttClient;
extern void wifiSetupConfig(bool);
extern templateServices coreServices;
extern char ntptod[MAX_CFGSTR_LENGTH];
//extern bool mqttLog(const char* msg, byte recordType, bool mqtt, bool monitor);

//*************************************
// defined in cntrl.cpp
//*************************************
extern cntrlState *cntrlObjRef; // pointer to cntrlStateOLF
cntrlState controlState;		// Create and set defaults

///#define WDCntlTimes "/house/cntrl/outside-lights-front/wd-control-times" // Times received from either UI or Python app
///#define WECntlTimes "/house/cntrl/outside-lights-front/we-control-times" // Times received from either UI or MySQL via Python app
///#define runtimeState "/house/cntrl/outside-lights-front/runtime-state"	 // published state: ON, OFF, and AUTO
///#define WDUICmdState "/house/cntrl/outside-lights-front/wd-command"		 // UI Button press received
///#define WEUICmdState "/house/cntrl/outside-lights-front/we-command"		 // UI Button press received
///#define RefreshID "OLF"													 // the key send to Python app to refresh Cntroler state

#define DRD_TIMEOUT 3
#define DRD_ADDRESS 0

DoubleResetDetector *drd;

// defined in telnet.cpp
extern int reporting;
extern bool telnetReporting;

//
// Application specific
//

///String deviceName 	= "outside-lights-front";
///String deviceType 	= "CNTRL";
///String app_id 		= "OLF"; 	// configure

int relay_pin 		= D1;		// wemos D1. LIght on or off (Garden lights)
int relay_pin_pir 	= D2;		// wemos D2. LIght on or off (Garage Path)
int OLFManualStatus = D3; 		// Manual over ride.  If low then lights held on manually
int LIGHTSON 		= 1;
int LIGHTSOFF 		= 0;
int LIGHTSAUTO 		= 3; 		// Not using this at the moment

bool bManMode = false; 			// true = Manual, false = automatic

///const char *oh3CommandTrigger 	= "/house/cntrl/outside-lights-front/pir-command"; // Event fron the PIR detector (front porch: PIRON or PIROFF
///const char *oh3StateManual 		= "/house/cntrl/outside-lights-front/manual-state";	 // 	Status of the Manual control switch control MAN or AUTO

//************************
// Application specific
//************************
bool processCntrlMessageApp_Ext(char *, const char *, const char *, const char *);
void processAppTOD_Ext();

devConfig espDevice;

Ticker configurationTimesReceived;
bool timesReceived;

void setup()
{
	//***************************************************
	// Set-up Platform - hopefully dont change this
	//***************************************************
	bool configWiFi = false;
	Serial.begin(115200);
	while (!Serial)
		delay(300);

	espDevice.setup(deviceName, deviceType);
	//Serial.println("\nStarting Outside Lights Front Controller on ");
	Serial.println(StartUpMessage);
	Serial.println(ARDUINO_BOARD);

	drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);
	if (drd->detectDoubleReset())
	{
		configWiFi = true;
	}

	// this app is a contoller
	// configure the MQTT topics for the Controller
	controlState.setCntrlName((String)app_id);
	controlState.setRefreshID(RefreshID);
	controlState.setCntrlObjRef(controlState);

	// startCntrl();

	// Platform setup: Set up and manage: WiFi, MQTT and Telnet
	platform_setup(configWiFi);

	//***********************
	// Application setup
	//***********************

	pinMode(relay_pin, OUTPUT);
	pinMode(relay_pin_pir, OUTPUT);
	pinMode(OLFManualStatus, INPUT);
	digitalWrite(relay_pin, LIGHTSOFF);
	digitalWrite(relay_pin_pir, LIGHTSOFF);

	configurationTimesReceived.attach(30, startTimesReceivedChecker);
}

void loop()
{
	int pinVal = 0;
	char logString[MAX_LOGSTRING_LENGTH];
	drd->loop();

	// Go look for OTA request
	ArduinoOTA.handle();

	handleTelnet();

	pinVal = digitalRead(OLFManualStatus);
	if (pinVal == 0) // means manual switch ON and lights forced to stay on
	{
		bManMode = true;
		memset(logString, 0, sizeof logString);
		sprintf(logString, "%s,%s,%s,%s", ntptod, espDevice.getType().c_str(), espDevice.getName().c_str(), "Outside Lights Front manually held ON");
		mqttLog(logString, REPORT_WARN, true, true);

		app_WD_on(&controlState); // FIXTHIS WD or WE
		mqttClient.publish(oh3StateManual, 1, true, "MAN");
	}
	else
	{
		bManMode = false;
		// mqttClient.publish(oh3StateManual, 1, true, "AUTO"); //FIXTHIS  I thing we cant assume auto - need to get state prior
	}
}

//****************************************************************
// Process any application specific inbound MQTT messages
// Return False if none
// Return true if an MQTT message was handled here
//****************************************************************
bool onMqttMessageAppExt(char *topic, char *payload, const AsyncMqttClientMessageProperties &properties, const size_t &len, const size_t &index, const size_t &total)
{
	(void)payload;

	char mqtt_payload[len + 1];
	mqtt_payload[len] = '\0';
	strncpy(mqtt_payload, payload, len);

	mqttLog(mqtt_payload, REPORT_DEBUG, true, true);

	if (strcmp(topic, oh3CommandTrigger) == 0)
	{
		if (strcmp(mqtt_payload, "PIRON") == 0) // FIXTHIS: there are multiple PIR - tey are being treated the same
		{
			digitalWrite(relay_pin_pir, LIGHTSON);
			digitalWrite(relay_pin, LIGHTSON);
			return true;
		}

		/*
		if (strcmp(mqtt_payload, "PIROFF") == 0)
		{
			//  Switch off unless manually held on by switch
			if (bManMode != true)
			{ // FIXTHIS - Both off ?
				digitalWrite(relay_pin_pir, LIGHTSOFF);
				digitalWrite(relay_pin, LIGHTSOFF);
			}
			//}
			return true;
		}
		*/
	}
	return false;
}

void processAppTOD_Ext()
{
	mqttLog("OLR Application Processing TOD", REPORT_INFO, true, true);
}

bool processCntrlMessageApp_Ext(char *mqttMessage, const char *onMessage, const char *offMessage, const char *commandTopic)
{
	if (strcmp(mqttMessage, "SET") == 0)
	{
		mqttClient.publish(oh3StateManual, 1, true, "AUTO"); // This just sets the UI to show that MAN start is OFF
		return true;
	}
	return false;
}
//***************************************************
// Connected to MQTT Broker
// Subscribe to application specific topics
//***************************************************
void appMQTTTopicSubscribe()
{

	mqttTopicsubscribe(oh3CommandTrigger, 2);

	controlState.setWDCntrlTimesTopic(WDCntlTimes);
	controlState.setWDUIcommandStateTopic(WDUICmdState);
	controlState.setWDCntrlRunTimesStateTopic(runtimeState);

	controlState.setWECntrlTimesTopic(WECntlTimes);
	controlState.setWEUIcommandStateTopic(WEUICmdState);
	controlState.setWECntrlRunTimesStateTopic(runtimeState);
}

void app_WD_on(void *cid)
{
	String msg;
	cntrlState *obj = (cntrlState *)cid;

	if (coreServices.getWeekDayState() == 1)			// 1 means weekday : FIXTHIS : Why am I making this test is it necessary? 
	{
		digitalWrite(relay_pin, LIGHTSON);
		controlState.setOutputState(1);
		
		msg = obj->getCntrlName() + ", WD ON";
		mqttLog(msg.c_str(), REPORT_INFO, true, true);

		mqttClient.publish(LightState, 0, true, "ON");		// QoS = 0
	}
	else
	{
	    msg = obj->getCntrlName() + ", WD ON : Do nothing as it's the weekend.";
		mqttLog(msg.c_str(), REPORT_INFO, true, true);
	}
}

void app_WD_off(void *cid)
{
	String msg;
	cntrlState *obj = (cntrlState *)cid;

	if (coreServices.getWeekDayState() == 1)			// 1 means weekday : FIXTHIS : Why am I making this test is it necessary? 
	{
		digitalWrite(relay_pin, LIGHTSOFF);
		controlState.setOutputState(0);
		
		msg = obj->getCntrlName() + ", WD OFF";
		mqttLog(msg.c_str(), REPORT_INFO, true, true);

		mqttClient.publish(LightState, 0, true, "OFF");		// QoS = 0
	}
	else
	{
	    msg = obj->getCntrlName() + ", WD OFF : Do nothing as it's the weekend.";
		mqttLog(msg.c_str(), REPORT_INFO, true, true);
	}
}

void app_WE_on(void *cid)
{
	String msg;
	cntrlState *obj = (cntrlState *)cid;
	
	if (coreServices.getWeekDayState() == 0)			// 0 means weekend : FIXTHIS : Why am I making this test is it necessary? 
	{
		digitalWrite(relay_pin, LIGHTSON);
		controlState.setOutputState(1);
		String msg = obj->getCntrlName() + ", WE ON";
		mqttLog(msg.c_str(), REPORT_INFO, true, true);
		
		mqttClient.publish(LightState, 0, true, "ON");		// QoS = 0
	}
	else	
	{
	    msg = obj->getCntrlName() + ", WE ON : Do nothing as its a week day.";
		mqttLog(msg.c_str(), REPORT_INFO, true, true);
	}
}

void app_WE_off(void *cid)
{
	String msg;
	cntrlState *obj = (cntrlState *)cid;

	if (coreServices.getWeekDayState() == 0)			// 0 means weekend : FIXTHIS : Why am I making this test is it necessary? 
	{
		digitalWrite(relay_pin, LIGHTSOFF);
		controlState.setOutputState(0);

		String msg = obj->getCntrlName() + ", WE OFF";
		mqttLog(msg.c_str(), REPORT_INFO, true, true);

		mqttClient.publish(LightState, 0, true, "OFF");		// QoS = 0
	}
	else
	{
	    msg = obj->getCntrlName() + ", WE OFF : Do nothing as its a week day.";
		mqttLog(msg.c_str(), REPORT_INFO, true, true);
	}
}
void app_WD_auto(void *cid)
{
	String msg;
	cntrlState *obj = (cntrlState *)cid;	
	
	if (coreServices.getWeekDayState() == 1)			// 1 means weekday : : FIXTHIS : Why am I making this test is it necessary? 
	{
		msg = obj->getCntrlName() + ", WD AUTO";
		mqttLog(msg.c_str(), REPORT_INFO, true, true);
	}
	else
	{
	    msg = obj->getCntrlName() + ", WD AUTO : Do nothing as it's the weekend.";
		mqttLog(msg.c_str(), REPORT_INFO, true, true);
	}

}

void app_WE_auto(void *cid)
{
	String msg;
	cntrlState *obj = (cntrlState *)cid;

	if (coreServices.getWeekDayState() == 0)			// 0 means weekend : FIXTHIS : Why am I making this test is it necessary? 
	{
		msg = obj->getCntrlName() + ", WE AUTO";
		mqttLog(msg.c_str(), REPORT_INFO, true, true);
	}
	else
	{
	    msg = obj->getCntrlName() + ", WE AUTO : Do nothing as its a week day.";
		mqttLog(msg.c_str(), REPORT_INFO, true, true);
	}
}

void startTimesReceivedChecker()
{
	controlState.runTimeReceivedCheck();
}

void processCntrlTOD_Ext()
{
	controlState.processCntrlTOD_Ext();
}
void telnet_extension_1(char c)
{
	controlState.telnet_extension_1(c);
}
// Process any application specific telnet commannds
void telnet_extension_2(char c)
{
	if (bManMode == true)
	{
		printTelnet((String) "MANual mode:\tON");
	}
	else
	{
		printTelnet((String) "MANual mode:\tOFF");
	}
}

// Process any application specific telnet commannds
void telnet_extensionHelp(char c)
{
	printTelnet((String) "x\t\tSome description");
}

bool onMqttMessageAppCntrlExt(char *topic, char *payload, const AsyncMqttClientMessageProperties &properties, const size_t &len, const size_t &index, const size_t &total)
{
	return controlState.onMqttMessageCntrlExt(topic, payload, properties, len, index, total);
}

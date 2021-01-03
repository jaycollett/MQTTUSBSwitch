// *******************************************************************************
//
// Ultimate Foosball Table Firmware v1.0 (12/24/2020)
//
// Firmware for my custom built USB Switch which has MQTT support
//
// Written by Jay Collett (jay AT jaycollett.com)
// http://www.jaycollett.com
//
// Additional unmodified libraries used (non-Arduino):
// WifiManager (https://github.com/tzapu/WiFiManager)
//
// This code is licensed under the MIT license.
//
// *******************************************************************************
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>         //https://github.com/knolleary/pubsubclient

#define DEBUG

// Setup our debug printing
#ifdef DEBUG
#define debug(x)     Serial.print(x)
#define debugln(x)   Serial.println(x)
#else
#define debug(x)     // define empty, so macro does nothing
#define debugln(x)
#endif

const char    fmversion[]     = "v1.2";                         // firmware version
const char    mqtt_server[]   = "xxxxxxxxxx.xxx";               // server name for mqtt
const char    mqtt_username[] = "xxxxxxxxxxxxxxxxx";            // username for MQTT broker (USE ONE)
const char    mqtt_password[] = "xxxxxxxxxxxxxx";               // password for MQTT broker
const char    mqtt_clientid[] = "mqttUsbSwitch";                // client id for connections to MQTT broker
const String  mqtt_out_topic  = "mqttUSBSwitch/stats";          // the full topic for the publishing of data
const String  mqtt_in_topic   = "mqttUSBSwitch/cmd/switch";     // the full topic for the subscription
const String  fwVersionTopic  = mqtt_out_topic + "/firmware";
const String  swStatusTopic   = mqtt_out_topic + "/switch";

#define powerConnectPin        5  //GPIO5
#define powerDisconnectPin     12 //GPIO12
#define dataConnectPin         4  //GPIO4
#define dataDisconnectPin      13 //GPIO13
#define manualSwitchButtonPin  14 //GPIO14

// keep track of our switch status (defaults on boot to off, and we make sure the switch is turned off)
String switchStatus = "off";

// init our WiFi client object
WiFiClient espWiFiClient;

// init our mqtt client
PubSubClient client(espWiFiClient);


// ************************************************************************************************
//
//                                            Setup Method
//
// ************************************************************************************************
void setup() {
  // Setup the hw serial for the feather, used for debug...
#ifdef DEBUG
  Serial.begin(115200);
#endif

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //wifiManager.resetSettings(); //allows you to reset the stored config, would only use once then reflash with it
  // commented out

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  // set timeout for AP mode if can't connect to WiFi, if it times out, it'll reset the ESP, we set five minutes
  wifiManager.setConfigPortalTimeout(300);
  
  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect()) {
    debugln("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  // now we have a working network, configure MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttSubscriptionMessage);

  //define io pin use
  pinMode(BUILTIN_LED, OUTPUT);
  pinMode(powerConnectPin, OUTPUT);
  pinMode(powerDisconnectPin, OUTPUT);
  pinMode(dataConnectPin, OUTPUT);
  pinMode(dataDisconnectPin, OUTPUT);
  pinMode(manualSwitchButtonPin, INPUT);

  // set default status of pins on startup
  digitalWrite(BUILTIN_LED, HIGH); //high turns it off..
  digitalWrite(powerConnectPin, LOW);
  digitalWrite(powerDisconnectPin, LOW);
  digitalWrite(dataConnectPin, LOW);
  digitalWrite(dataDisconnectPin, LOW);

}

// ************************************************************************************************
//
//                                          Loop Method
//
// ************************************************************************************************
void loop() {

  if (!client.connected()) {
    reconnect();
  }

  // check to see if the manual switch button was pushed with software debounce
  if (digitalRead(manualSwitchButtonPin) == LOW) {
    debugln("Switch is low waiting, 100ms");
    delay(100);
    if (digitalRead(manualSwitchButtonPin) == LOW) {
      // switch the relays as the user requested
      debugln("Switching...");
      if (switchStatus.equalsIgnoreCase("on")) {
        debugln("we are on, switching off...");
        // turn off the switch
        disconnectUSB();
      } else {
        debugln("we are off, switching on...");
        // connect as we are currently disconnected
        connectUSB();
      }
    }
  }

  // required call to ensure all MQTT messages are processed (in or out)
  client.loop();
}



// !#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#
//
//                                    Supporting methods below
//
// !#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#



// **************************************************************************
// Method to deal with messages we get on topics we are subscribed to
// **************************************************************************
void mqttSubscriptionMessage(char* topic, byte* payload, unsigned int length) {
  debug("Message arrived [");
  debug(topic);
  debug("] ");
  String payloadString = "";

  for (int i = 0; i < length; i++) {
    debug((char)payload[i]);
    payloadString += (char)payload[i];
  }

  debugln();
  debug("Payload string is: [");
  debug(payloadString);
  debugln("]");

  // Switch on the LED if an 1 was received as first character
  if (payloadString.equalsIgnoreCase("on")) {
    // switch the usb lines to connect everything
    connectUSB();
  }
  else if (payloadString.equalsIgnoreCase("off")) {
    // switch the usb lines to disconnect everything
    disconnectUSB();
  }

  // finally, let's publish the current state of our system back to the MQTT broker
  publishMQTTData();
}

// **************************************************************************
// Method to handle publishing MQTT our data to server
// **************************************************************************
void publishMQTTData() {
  client.publish(fwVersionTopic.c_str(), fmversion);
  client.publish(swStatusTopic.c_str(), switchStatus.c_str());
}

// **************************************************************************
// Method to handle configuration mode of WiFiManager
// **************************************************************************
void configModeCallback (WiFiManager *myWiFiManager) {
  debugln("Entered config mode");
  debugln(WiFi.softAPIP());

  //if you used auto generated SSID, print it
  debugln(myWiFiManager->getConfigPortalSSID());
}

// **************************************************************************
// Method to handle the (re)connection of our client to the MQTT server
// **************************************************************************
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    debug("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(mqtt_clientid, mqtt_username, mqtt_password)) {
      debugln("connected");

      // Once connected, publish our data...
      publishMQTTData();

      // ... and resubscribe
      if (!client.subscribe(mqtt_in_topic.c_str())) {
        debugln("Failed to subscribe to topic");
      }
    } else {
      debug("failed, rc=");
      debug(client.state());
      debugln(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// **************************************************************************
// Method to handle the CONNECTION of the USB interface
// **************************************************************************
void connectUSB() {
  // first we must connect the power and gnd lines
  digitalWrite(powerConnectPin, HIGH);
  delay(80);
  digitalWrite(powerConnectPin, LOW);

  // now that the power lines are connected, we wait a fraction of a second
  delay(80);

  // now we can disconnect power and gnd
  digitalWrite(dataConnectPin, HIGH);
  delay(80);
  digitalWrite(dataConnectPin, LOW);

  // set our onboard LED status to on
  digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level

  // update our tracking var
  switchStatus = "on";

  // Once connected, publish our data...
  publishMQTTData();
}

// **************************************************************************
// Method to handle the DISCONNECTION of the USB interface
// **************************************************************************
void disconnectUSB() {
  // first we must remove the data lines
  digitalWrite(powerDisconnectPin, HIGH);
  delay(80);
  digitalWrite(powerDisconnectPin, LOW);

  // now that the data lines are disconnected, we wait a fraction of a second
  delay(80);

  // now we can disconnect power and gnd
  digitalWrite(powerDisconnectPin, HIGH);
  delay(80);
  digitalWrite(powerDisconnectPin, LOW);

  // update our tracking var
  switchStatus = "off";

  // turn off our onboard LED indicator
  digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH

  // Once connected, publish our data...
  publishMQTTData();
}

/***************************************************************************
 Code to use with the Salt sentry , see https://www.tindie.com/products/ErikLemcke/mqtt--wifi-doorbell-with-esp8266/
 Created by Erik Lemcke 05-01-2021

 This code can be used from the arduino library, you will need the following libraries:
 https://github.com/tzapu/WiFiManager     Included in this repository (src folder)
 https://github.com/bblanchon/ArduinoJson
 https://github.com/knolleary/pubsubclient
 https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266mDNS

 credits:

 https://tzapu.com/                   for the awesome WiFiManger library
 https://github.com/esp8266/Arduino   for the ESP8266 arduino core
 https://arduinojson.org/             for the ArduinoJson library
 https://pubsubclient.knolleary.net/  for the mqtt library
  
 ***************************************************************************/
#include <fs.h>                   //this needs to be first, or it all crashes and burns...
#import "index.h"

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "src/WiFiManager.h" 

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>
#include <base64.h>
#include <ESP8266HTTPClient.h>

#include <WiFiClientSecure.h>

#include <Wire.h>
#include "Adafruit_VL53L0X.h"

Adafruit_VL53L0X lox = Adafruit_VL53L0X();

int resetState = 0;

WiFiClient espClient;
HTTPClient http; 
PubSubClient client(espClient);

WiFiManager wifiManager;
ESP8266WebServer server(80);

bool firstLoop = true;
unsigned long previousMillis = 0;       

String currentFirmwareVersion = "0.1.0" ;

DNSServer dnsServer; //Needed for captive portal when device is already connected to a wifi network

//extra parameters
char mqtt_server[40];
char mqtt_port[6] ;
char mqtt_username[40];
char mqtt_password[40];
char mqtt_topic[40];
char mqtt_distance_topic[49];
char mqtt_status[60] = "unknown";

char dz_idx[5];
char oh_itemid[40];
char min_range[5];
char max_range[5];

float lastMeasure = 0;

//flag for saving data
bool shouldSaveConfig = false;
bool apstarted = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void saveSettings() {
  Serial.println("Handling webserver request savesettings");

  //store the updates values in the json config file
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = server.arg("mqtt_server");
    json["mqtt_port"] = server.arg("mqtt_port");
    json["mqtt_username"] = server.arg("mqtt_username");
    json["mqtt_password"] = server.arg("mqtt_password");
    json["mqtt_topic"] = server.arg("mqtt_topic");

    json["dz_idx"] = server.arg("dz_idx");
    json["oh_itemid"] = server.arg("oh_itemid");

    json["min_range"] = server.arg("min_range");
    json["max_range"] = server.arg("max_range");
   
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();

    //put updated parameters into memory so they become effective immediately
    server.arg("mqtt_server").toCharArray(mqtt_server,40);
    server.arg("mqtt_port").toCharArray(mqtt_port,40);
    server.arg("mqtt_username").toCharArray(mqtt_username,40);
    server.arg("mqtt_password").toCharArray(mqtt_password,40);
    server.arg("mqtt_topic").toCharArray(mqtt_topic,40);
    server.arg("dz_idx").toCharArray(dz_idx,40);
    server.arg("oh_itemid").toCharArray(oh_itemid,40);

    server.arg("min_range").toCharArray(min_range,40);
    server.arg("max_range").toCharArray(max_range,40);
   
    server.send(200, "text/html", "Settings have been saved. You will be redirected to the configuration page in 5 seconds <meta http-equiv=\"refresh\" content=\"5; url=/\" />");
    
    //mqtt settings might have changed, let's reconnect to the mqtt server if one is configured
    if (strlen(mqtt_topic) != 0){
      Serial.println("mqtt topic set, need to connect");
      reconnect();
    }
}


void setup() {
 
  Serial.begin(115200);
  Serial.println();
  Serial.println("Salt sentry");
  Serial.println("Firmware version: " + currentFirmwareVersion);

  pinMode(12, INPUT_PULLUP); // config switch
 
  //read configuration from FS json
  Serial.println("mounting file system");

  if (SPIFFS.begin()) {
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        if (json.success()) {
          Serial.println("parsed config file json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_topic, json["mqtt_topic"]);

          strcpy(dz_idx, json["dz_idx"]);
          strcpy(oh_itemid, json["oh_itemid"]);

          strcpy(min_range, json["min_range"]);
          strcpy(max_range, json["max_range"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    } else {
      Serial.println("config.json does not exist");
    }
  } else {
    Serial.println("failed to mount file system");
  }
  //end read

  WiFiManagerParameter custom_mqtt_server("server", "ip address", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_username("username", "username", mqtt_username, 40);
  WiFiManagerParameter custom_mqtt_password("password", "password", mqtt_password, 40);
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 40);

  WiFiManagerParameter custom_dz_idx("dzidx", "Domoticz idx", dz_idx, 5);
  WiFiManagerParameter custom_oh_itemid("ohitemid", "OpenHAB itemId", oh_itemid, 40);

  WiFiManagerParameter custom_min_range("min_range", "full distance in cm", min_range, 5);
  WiFiManagerParameter custom_max_range("max_range", "empty distance in cm", max_range, 5);

  WiFiManagerParameter custom_text("<p>Fill the folowing values with your home assistant / domoticz infromation. Username and password are optional</p>");
  wifiManager.addParameter(&custom_text);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);

  WiFiManagerParameter custom_text1("<p>Fill the folowing field with your MQTT topic for Home assistant</p>");
  wifiManager.addParameter(&custom_text1);
  
  wifiManager.addParameter(&custom_mqtt_topic);

  WiFiManagerParameter custom_text2("<p>Fill the folowing field with your IDX value for Domoticz</p>");
  wifiManager.addParameter(&custom_text2);
    
  wifiManager.addParameter(&custom_dz_idx);

  WiFiManagerParameter custom_text3("<p>Fill the folowing field with your itemId for OpenHAB</p>");
  wifiManager.addParameter(&custom_text3);
  wifiManager.addParameter(&custom_oh_itemid);

  WiFiManagerParameter custom_text4("<p>Fill the distances from the sensor to the salt for wich the Salt sentry should consider the salt full or empty</p>");
  wifiManager.addParameter(&custom_text4);
  wifiManager.addParameter(&custom_min_range);
  wifiManager.addParameter(&custom_max_range);

  Serial.println("WifiManager config done");

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //and goes into a blocking loop awaiting configuration      
  if (!wifiManager.autoConnect("saltSentry")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected to wifi network");

  //Define url's for webserver 
  server.on("/saveSettings", saveSettings);
  server.on("/", handleRoot);
  server.onNotFound([]() {
    handleRoot();
  });
  server.begin();

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());

  strcpy(dz_idx, custom_dz_idx.getValue());
  strcpy(oh_itemid, custom_oh_itemid.getValue());
  strcpy(min_range, custom_min_range.getValue());
  strcpy(max_range, custom_max_range.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_username"] = mqtt_username;
    json["mqtt_password"] = mqtt_password;
    json["mqtt_topic"] = mqtt_topic;
    json["dz_idx"] = dz_idx;
    json["oh_itemid"] = oh_itemid;
    json["min_range"] = min_range;
    json["max_range"] = max_range;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  //MQTT
  client.setServer(mqtt_server, atoi(mqtt_port));

  Serial.println("Salt sentry ip on " + WiFi.SSID() + ": " + WiFi.localIP().toString()); 

  //Initialize time of flight sensor
  Wire.begin(2,14);
  if (!lox.begin(false, &Wire)) {
    Serial.println(F("Failed to boot VL53L0X"));
    delay(100000);
  }  
  Serial.println("VL53L0X booted");
}


//MQTT reconnect function
void reconnect() {
  client.disconnect();
  client.setServer(mqtt_server, atoi(mqtt_port));
  Serial.print("Attempting MQTT connection to ");
  Serial.print(mqtt_server);
  Serial.print(" on port ");
  Serial.print(mqtt_port);
  Serial.print("...");
  
  if (client.connect("SaltSentry", mqtt_username, mqtt_password)) {
     Serial.println("connected");
     String("<div style=\"color:green;float:left\">connected</div>").toCharArray(mqtt_status,60);
   } else {
     Serial.print("failed, rc=");
     String("<div style=\"color:red;float:left\">connection failed</div>").toCharArray(mqtt_status,60);
     Serial.print(client.state());
     Serial.println(" try again in 5 seconds");

  

    unsigned long previousMillis1 = 0;

     //connection failed, Go into a (non-blocking) loop until we are connected 
     while (!client.connected()) {
        resetstate();
         server.handleClient();  //call to handle webserver connection needed, because the while loop will block the processor
         unsigned long currentMillis1 = millis();
         if(currentMillis1 - previousMillis1 >= 5000) {
         previousMillis1 = currentMillis1;
         Serial.print("Attempting MQTT connection...");
          if (client.connect("SaltSentry", mqtt_username, mqtt_password)) {
                Serial.println("connected");
                String("<div style=\"color:green;float:left\">connected</div>").toCharArray(mqtt_status,60);
             } else {
                     String("<div style=\"color:red;float:left\">connection failed</div>").toCharArray(mqtt_status,60);
                Serial.print("failed, rc=");
                Serial.print(client.state());
                Serial.println(" try again in 5 seconds");
             }
         }
      }
   }
}

long lastMsg = 0;

//Initialize a reset if pin 12 is low
void resetstate (){
   resetState = digitalRead(12);
   if (resetState == LOW){
    Serial.println("It seems someone wants to go for a reset...");
  
    int count = 0;

    //See if they still want to go for a factory reset
    while (count < 25 && digitalRead(12) == LOW) {
      delay(100);                                           
      count ++;
    }
  
    resetState = digitalRead(12);
    //See if they still want to go for it
    if (resetState == LOW){
       Serial.println("Let's do it");
       Serial.println("we should not get here :(");
       SPIFFS.format();
       wifiManager.resetSettings();
       delay(500);
       ESP.restart();  
    } else {
      Serial.println("They chickened out...");
      Serial.println("Starting AP that can be used to obtain IP");

      boolean result = false;
      while (result == false){
        result = WiFi.softAP("Salt sentry online" );  
      }
      
      if(result == true)
      {
        dnsServer.start(53, "*", WiFi.softAPIP());
          
        apstarted = true;
        previousMillis =  millis();
        Serial.println("AP IP address: " +  WiFi.softAPIP().toString());
      }
    }
  }
}

float calculatePercentage(float distanceCm, String minRange, String maxRange) {  
  float percentage;
  float rangeCm = distanceCm;
  float correctedRange = rangeCm - minRange.toFloat();

  if (correctedRange < 0) {
      percentage = 100;
  } else {
     percentage = (correctedRange / ((maxRange.toFloat()-minRange.toFloat()) / 100));
     if (percentage > 100) {
      percentage = 0;
     } else {
      percentage = 100 - percentage;
     }
  }

  // 1 decimal
  percentage = percentage * 10;
  return round(percentage)/10;
}


void loop() {

  //if a AP is started, kill it after 3 minutes
  if (apstarted == true){
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= 300000) {
      previousMillis = currentMillis;
      Serial.println("Stopping the AP, 3 minutes are past!");
      WiFi.softAPdisconnect(false);
      apstarted = false;    
    }
  }
 
  resetstate();
  
  if (strlen(mqtt_topic) != 0){
     //try to reconnect to mqtt server if connection is lost
    if (!client.connected()) { 
      reconnect();
    }
    client.loop();
  }
  
  server.handleClient();
  dnsServer.processNextRequest();
  
  resetState = digitalRead(12);

  //Go into a non-blocking loop to do a measurement every 5 minutes
  float percentage;
  unsigned long currentMillis = millis();
  
    if (currentMillis - previousMillis >= 300000 || firstLoop == true) {
      firstLoop = false;
      previousMillis = currentMillis;

      VL53L0X_RangingMeasurementData_t measure;
      lox.rangingTest(&measure, false);      

      // If we're measuring a slightly lower numer of mm than before, cummunicate the last measurment 
      float currentMeasure;
      if (measure.RangeStatus != 4) {
        Serial.println(measure.RangeMilliMeter);
        float measurement = measure.RangeMilliMeter;
        measurement = measurement / 10;

//        Serial.print("Vorige meting:");
//        Serial.println(lastMeasure);
//        
//        Serial.print("Gemeten:");
//        Serial.println(measurement);
//
//        Serial.print("Verschil met vorige meting:");
//        Serial.println(lastMeasure - measurement);
        
        if (measurement < lastMeasure && lastMeasure - measurement <= 2){
          
//           Serial.print("Verschil van 1 cm of minder, communiceren:");
//           Serial.println(lastMeasure);
          currentMeasure = lastMeasure;
        } else {
//          Serial.print("Geen verschil van 1 cm of minder, communiceren:");
//          Serial.println(measurement);
          currentMeasure = measurement;
          lastMeasure = measurement;
        }
        
        percentage = calculatePercentage(currentMeasure, min_range, max_range);
      } else {
        Serial.println("meaurment out of range, returning 100%");
        percentage = 100;
      }

      
      
      if (strlen(mqtt_topic) != 0){
        Serial.println("Sending MQTT message");
        sendMqttMessage(percentage, currentMeasure);
      }
    
     if (strlen(dz_idx) != 0){
      sendDomoticzMessage(percentage, currentMeasure);
     }
   
     // OpenHAB
     if (strlen(oh_itemid) != 0){
       sendOpenHabMessage(percentage, currentMeasure);
      }
  }
}

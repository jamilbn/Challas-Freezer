
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <Ticker.h>

#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <EEPROM.h>

const char* host = "Challas-Freezer"; //hotname for mDNS and SSID for SoftAP

const char* softAPPassword = "#######";

const char* ssid     = "#######";
const char* password = "#######";

const char* authUsername = "admin";
const char* authPassword = "#######";

const char* updatePath = "/firmware";


//ThingSpeak
String thingSpeakKey = "#######";
const char* thingSpeakURL = "api.thingspeak.com";


//ILI9341
#define TFT_DC D3
#define TFT_CS D4

// Data wire is plugged into port 5 on the Arduino
#define ONE_WIRE_BUS TX

/*Set Sensors Resolution of the DS18B20 as in Datasheet "The resolution of the temperature
  sensor is user-configurable to 9, 10, 11, or 12 bits, corresponding to increments of 0.5°C, 0.25°C, 0.125°C, and 0.0625°C"*/
#define SENSORS_RES 10

//Select temperature scale
#define TEMP_SCALE "C"

// Set Solid State Relays pins
#define TEMP1_PIN RX
#define TEMP2_PIN D1
#define TEMP3_PIN D2


// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);


ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

WiFiClientSecure httpsClient;

Ticker checkWiFi;


//Timer
long prevMillis = 0;
long interval = 1000;

long prevMillisTemp = 0;
long intervalTemp = 750;

long prevMillisMinute = 0;
long intervalMinute = 60000;

bool  F1 = 0;
bool  temp1Relay = 0;
float temp1 = 0;
float temp1DF = 0;
float temp1Max = 0;
float temp1Min = 0;
float temp1Read = 0;

bool  F2 = 0;
bool  temp2Relay = 0;
float temp2 = 0;
float temp2DF = 0;
float temp2Max = 0;
float temp2Min = 0;
float temp2Read = 0;

bool  F3 = 0;
bool  temp3Relay = 0;
float temp3 = 0;
float temp3DF = 0;
float temp3Max = 0;
float temp3Min = 0;
float temp3Read = 0;


void setup() {

  pinMode(TEMP1_PIN, OUTPUT);
  pinMode(TEMP2_PIN, OUTPUT);
  pinMode(TEMP3_PIN, OUTPUT);

  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.setResolution(SENSORS_RES);
  sensors.requestTemperatures();

  if (TEMP_SCALE == "C") {
    temp1Read = sensors.getTempCByIndex(0); temp1Max = temp1Min = temp1Read;
    temp2Read = sensors.getTempCByIndex(1); temp2Max = temp2Min = temp2Read;
    temp3Read = sensors.getTempCByIndex(2); temp3Max = temp3Min = temp3Read;
  }

  else if (TEMP_SCALE == "F") {
    temp1Read = sensors.getTempFByIndex(0); temp1Max = temp1Min = temp1Read;
    temp2Read = sensors.getTempFByIndex(1); temp2Max = temp2Min = temp2Read;
    temp3Read = sensors.getTempFByIndex(2); temp3Max = temp3Min = temp3Read;
  }


  tft.begin();
  tft.fillScreen(ILI9341_BLACK);

  drawScreen();


  //Load EEPROM Values
  eepromLoad();



  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);
  WiFi.softAP(host, softAPPassword);

  checkWiFi.attach(10, wifiCheck);

  MDNS.begin(host);
  MDNS.addService("http", "tcp", 80);

  httpUpdater.setup(&httpServer, updatePath, authUsername, authPassword);

  // replay to all requests with same HTML
  httpServer.onNotFound([]() {
    httpRoot();
  });



  //Format EEPROM data
  httpServer.on("/eepromclear", HTTP_GET, []() {
    eepromClear();
    httpServer.send(200, "text/plain", "EEPROM FORMATED");
  });


  //Get Variables values in JSON format
  httpServer.on("/get", HTTP_GET, []() {
    sendJSONVars();
  });


  // Use IPADDRESS/set?F1=1&temp1=-1 and other variables. Can set one or multiples.
  httpServer.on("/set", HTTP_GET, []() {
    setValues();
    eepromSave();
    httpRoot();
  });


  httpServer.begin();


}


void wifiCheck() {

  checkWiFi.detach();
  
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP); 
  }

}


void loop() {


  unsigned long currMillis = millis();
  
  if (currMillis - prevMillisTemp > intervalTemp ) {
    prevMillisTemp = currMillis;

    requestTemp();

  }

  
  if (currMillis - prevMillis > interval) {
    prevMillis = currMillis;

    controlTemp();
    drawScreen();

  }

  if (currMillis - prevMillisMinute > intervalMinute ) {
    prevMillisMinute = currMillis;

    thingSpeakUpdate();

  }

  httpServer.handleClient();
  delay(10);

}


void thingSpeakUpdate() {

  if (httpsClient.connect(thingSpeakURL, 443)) {
    String postVars = "api_key=";
    postVars += thingSpeakKey;
    postVars += "&amp;field1=" + String(temp1Read);
    postVars += "&amp;field2=" + String(temp2Read);
    postVars += "&amp;field3=" + String(temp3Read);
    postVars += "\r\n\r\n";

    httpsClient.print("POST /update HTTP/1.1\n");
    httpsClient.print("Host: api.thingspeak.com\n");
    httpsClient.print("Connection: close\n");
    httpsClient.print("X-THINGSPEAKAPIKEY: " + thingSpeakKey + "\n");
    httpsClient.print("Content-Type: application/x-www-form-urlencoded\n");
    httpsClient.print("Content-Length: ");
    httpsClient.print(postVars.length());
    httpsClient.print("\n\n");
    httpsClient.print(postVars);

  }

  httpsClient.stop();

}


void httpRoot() {

  String responseHTML = "<!DOCTYPE html><html><head><title>" + String(host) + "</title></head><body>"
                        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, maximum-scale=1\">"
                        "<h1> <a href = \"/\">" + String(host) + "</a></h1>"
                        "<b>Freezer 1</b> is:  ";

  if (F1 == 1) {
    responseHTML += "<a href=\"/set?F1=0\">ON</a><br/>";
  }
  else {
    responseHTML += "<a href=\"/set?F1=1\">OFF</a><br/>";
  }

  responseHTML += "Temperature: " + String(temp1Read) + " " + String(TEMP_SCALE) + "<br/>"
                  "Max: " + String(temp1Max) + " " + String(TEMP_SCALE) + "<br/>"
                  "Min: " + String(temp1Min) + " " + String(TEMP_SCALE) + "<br/>"
                  "<form action=\"/set\" method=\"get\">"
                  "Set Temperature: <input type=\"number\" step=\"0.5\" name=\"temp1\" size=\"5\" value=\"" + String(temp1) + "\"><input type=\"submit\" value=\"Set\">"
                  "</form>"
                  "<form action=\"/set\" method=\"get\">"
                  "Differential Temperature: <input type=\"number\" step=\"0.5\" name=\"temp1DF\" size=\"5\" value=\"" + String(temp1DF) + "\"><input type=\"submit\" value=\"Set\">"
                  "</form><br/>";

  responseHTML += "<b>Freezer 2</b> is:  ";

  if (F2 == 1) {
    responseHTML += "<a href=\"/set?F2=0\">ON</a><br/>";
  }
  else {
    responseHTML += "<a href=\"/set?F2=1\">OFF</a><br/>";
  }

  responseHTML += "Temperature: " + String(temp2Read) + " " + String(TEMP_SCALE) + "<br/>"
                  "Max: " + String(temp2Max) + " " + String(TEMP_SCALE) + "<br/>"
                  "Min: " + String(temp2Min) + " " + String(TEMP_SCALE) + "<br/>"
                  "<form action=\"/set\" method=\"get\">"
                  "Set Temperature: <input type=\"number\" step=\"0.5\" name=\"temp2\" size=\"7\" value=\"" + String(temp2) + "\"><input type=\"submit\" value=\"Set\">"
                  "</form>"
                  "<form action=\"/set\" method=\"get\">"
                  "Differential Temperature: <input type=\"number\" step=\"0.5\" name=\"temp2DF\" size=\"7\" value=\"" + String(temp2DF) + "\"><input type=\"submit\" value=\"Set\">"
                  "</form><br/>";

  responseHTML += "<b>Freezer 3</b> is:  ";

  if (F3 == 1) {
    responseHTML += "<a href=\"/set?F3=0\">ON</a><br/>";
  }
  else {
    responseHTML += "<a href=\"/set?F3=1\">OFF</a><br/>";
  }

  responseHTML += "Temperature: " + String(temp3Read) + " " + String(TEMP_SCALE) + "<br/>"
                  "Max: " + String(temp3Max) + " " + String(TEMP_SCALE) + "<br/>"
                  "Min: " + String(temp3Min) + " " + String(TEMP_SCALE) + "<br/>"
                  "<form action=\"/set\" method=\"get\">"
                  "Set Temperature: <input type=\"number\" step=\"0.5\" name=\"temp3\" size=\"7\" value=\"" + String(temp3) + "\"><input type=\"submit\" value=\"Set\">"
                  "</form>"
                  "<form action=\"/set\" method=\"get\">"
                  "Differential Temperature: <input type=\"number\" step=\"0.5\" name=\"temp3DF\" size=\"7\" value=\"" + String(temp3DF) + "\"><input type=\"submit\" value=\"Set\">"
                  "</form><br/>";

  responseHTML += "</body></html>";

  httpServer.send(200, "text/html", responseHTML);

}


void setValues() {

  String state = httpServer.arg("F1");

  if (state.length() > 0 && (state == "0" || state == "1")) {
    F1 = state.toInt();
  }
  state = httpServer.arg("temp1");
  if (state.length() > 0) {
    temp1 = state.toFloat();
  }
  state = httpServer.arg("temp1DF");
  if (state.length() > 0) {
    temp1DF = state.toFloat();
  }

  state = httpServer.arg("F2");
  if (state.length() > 0 && (state == "0" || state == "1")) {
    F2 = state.toInt();
  }
  state = httpServer.arg("temp2");
  if (state.length() > 0) {
    temp2 = state.toFloat();
  }
  state = httpServer.arg("temp2DF");
  if (state.length() > 0) {
    temp2DF = state.toFloat();
  }

  state = httpServer.arg("F3");
  if (state.length() > 0 && (state == "0" || state == "1")) {
    F3 = state.toInt();
  }
  state = httpServer.arg("temp3");
  if (state.length() > 0) {
    temp3 = state.toFloat();
  }
  state = httpServer.arg("temp3DF");
  if (state.length() > 0) {
    temp3DF = state.toFloat();
  }


}

void sendJSONVars() {

  String json = "[{";
  json += "\"F1\":\"" + String(F1) + "\"";
  json += ", \"temp1\":\"" + String(temp1) + "\"";
  json += ", \"temp1DF\":\"" + String(temp1DF) + "\"";
  json += ", \"temp1Read\":\"" + String(temp1Read) + "\"";
  json += ", \"temp1Max\":\"" + String(temp1Max) + "\"";
  json += ", \"temp1Min\":\"" + String(temp1Min) + "\"";
  json += ", \"temp1Relay\":\"" + String(temp1Relay) + "\"";

  json += ", \"F2\":\"" + String(F2) + "\"";
  json += ", \"temp2\":\"" + String(temp2) + "\"";
  json += ", \"temp2DF\":\"" + String(temp2DF) + "\"";
  json += ", \"temp2Read\":\"" + String(temp2Read) + "\"";
  json += ", \"temp2Max\":\"" + String(temp2Max) + "\"";
  json += ", \"temp2Min\":\"" + String(temp2Min) + "\"";
  json += ", \"temp2Relay\":\"" + String(temp2Relay) + "\"";

  json += ", \"F3\":\"" + String(F3) + "\"";
  json += ", \"temp3\":\"" + String(temp3) + "\"";
  json += ", \"temp3DF\":\"" + String(temp3DF) + "\"";
  json += ", \"temp3Read\":\"" + String(temp3Read) + "\"";
  json += ", \"temp3Max\":\"" + String(temp3Max) + "\"";
  json += ", \"temp3Min\":\"" + String(temp3Min) + "\"";
  json += ", \"temp3Relay\":\"" + String(temp3Relay) + "\"";

  json += "}]";
  httpServer.send(200, "text/json", json);
  json = String();

}


void requestTemp() {

  sensors.requestTemperatures();

  if (TEMP_SCALE == "C") {
    temp1Read = sensors.getTempCByIndex(0);
    temp2Read = sensors.getTempCByIndex(1);
    temp3Read = sensors.getTempCByIndex(2);
  }
  else if (TEMP_SCALE == "F") {
    temp1Read = sensors.getTempFByIndex(0);
    temp2Read = sensors.getTempFByIndex(1);
    temp3Read = sensors.getTempFByIndex(2);
  }

  temp1Max = _max(temp1Read , temp1Max);
  temp1Min = _min(temp1Read , temp1Min);
  temp2Max = _max(temp2Read , temp2Max);
  temp2Min = _min(temp2Read , temp2Min);
  temp3Max = _max(temp3Read , temp3Max);
  temp3Min = _min(temp3Read , temp3Min);

}


void controlTemp() {

  if (F1 == 0) {
    digitalWrite(TEMP1_PIN, LOW);
    temp1Relay = 0;
  }
  if (F2 == 0) {
    digitalWrite(TEMP2_PIN, LOW);
    temp2Relay = 0;
  }
  if (F3 == 0) {
    digitalWrite(TEMP3_PIN, LOW);
    temp3Relay = 0;
  }

  if (F1 == 1) {
    if (temp1Read > temp1 + temp1DF || temp1Read < temp1) {
      if (temp1Read > temp1 + temp1DF ) {
        digitalWrite(TEMP1_PIN, HIGH);
        temp1Relay = 1;
      }
      else {
        digitalWrite(TEMP1_PIN, LOW);
        temp1Relay = 0;
      }

    }
  }


  if (F2 == 1) {
    if (temp2Read > temp2 + temp2DF || temp2Read < temp2)  {
      if (temp2Read > temp2 + temp2DF) {
        digitalWrite(TEMP2_PIN, HIGH);
        temp2Relay = 1;
      }
      else {
        digitalWrite(TEMP2_PIN, LOW);
        temp2Relay = 0;
      }

    }
  }

  if (F3 == 1) {
    if (temp3Read > temp3 + temp3DF || temp3Read < temp3) {
      if (temp3Read > temp3 + temp3DF ) {
        digitalWrite(TEMP3_PIN, HIGH);
        temp3Relay = 1;
      }
      else {
        digitalWrite(TEMP3_PIN, LOW);
        temp3Relay = 0;
      }

    }
  }
}


void eepromClear() {
  EEPROM.begin(512);
  for (int i = 0; i < 512; i++)
    EEPROM.write(i, 0);
  EEPROM.end();
}


void eepromSave() {

  EEPROM.begin(512);

  EEPROM.put(1, F1);
  EEPROM.put(2, F2);
  EEPROM.put(3, F3);

  EEPROM.put(4, temp1);
  EEPROM.put(9, temp2);
  EEPROM.put(13, temp3);

  EEPROM.put(17, temp1DF);
  EEPROM.put(21, temp2DF);
  EEPROM.put(25, temp3DF);

  EEPROM.end();

}


void eepromLoad() {

  EEPROM.begin(512);

  EEPROM.get(1, F1);
  EEPROM.get(2, F2);
  EEPROM.get(3, F3);
  EEPROM.get(4, temp1);
  EEPROM.get(9, temp2);
  EEPROM.get(13, temp3);
  EEPROM.get(17, temp1DF);
  EEPROM.get(21, temp2DF);
  EEPROM.get(25, temp3DF);

  EEPROM.end();

}


void drawScreen() {

  tft.drawRect(0, 0, 240, 105, ILI9341_WHITE);
  tft.drawRect(0, 107, 240, 105, ILI9341_WHITE);
  tft.drawRect(0, 213, 240, 105, ILI9341_WHITE);


  tft.drawCircle(30, 40, 12, ILI9341_WHITE);
  tft.drawCircle(30, 148, 12, ILI9341_WHITE);
  tft.drawCircle(30, 256, 12, ILI9341_WHITE);

  if (temp1Relay == 1) {
    tft.fillCircle(30, 40, 10, ILI9341_GREEN);
  }
  else if (F1 == 1 && temp1Relay == 0) {
    tft.fillCircle(30, 40, 10, ILI9341_YELLOW);
  }
  else {
    tft.fillCircle(30, 40, 10, ILI9341_BLACK);
  }

  if (temp2Relay == 1) {
    tft.fillCircle(30, 148, 10, ILI9341_GREEN);
  }
  else if (F2 == 1 && temp2Relay == 0) {
    tft.fillCircle(30, 148, 10, ILI9341_YELLOW);
  }
  else {
    tft.fillCircle(30, 148, 10, ILI9341_BLACK);
  }

  if (temp3Relay == 1) {
    tft.fillCircle(30, 256, 10, ILI9341_GREEN);
  }
  else if (F3 == 1 && temp2Relay == 0) {
    tft.fillCircle(30, 256, 10, ILI9341_YELLOW);
  }
  else {
    tft.fillCircle(30, 256, 10, ILI9341_BLACK);
  }


  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(30, 4);
  tft.print("Freezer 1");
  tft.setCursor(30, 111);
  tft.print("Freezer 2");
  tft.setCursor(30, 217);
  tft.print("Freezer 3");

  tft.setTextSize(4);
  tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  tft.setCursor(60, 27);
  tft.print(temp1Read, 2);
  tft.setCursor(60, 134);
  tft.print(temp2Read, 2);
  tft.setCursor(60, 244);
  tft.print(temp3Read, 2);

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(160, 4);
  tft.print(temp1, 2);
  tft.setCursor(160, 111);
  tft.print(temp2, 2);
  tft.setCursor(160, 217);
  tft.print(temp3, 2);

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(160, 75);
  tft.print(temp1DF, 2);
  tft.setCursor(160, 182);
  tft.print(temp2DF, 2);
  tft.setCursor(160, 288);
  tft.print(temp3DF, 2);

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
  tft.setCursor(15, 65);
  tft.print("Max:");
  tft.print(temp1Max, 2);
  tft.setCursor(15, 172);
  tft.print("Max:");
  tft.print(temp2Max, 2);
  tft.setCursor(15, 278);
  tft.print("Max:");
  tft.print(temp3Max, 2);
  tft.setTextColor(ILI9341_BLUE, ILI9341_BLACK);
  tft.setCursor(15, 83);
  tft.print("Min:");
  tft.print(temp1Min, 2);
  tft.setCursor(15, 190);
  tft.print("Min:");
  tft.print(temp2Min, 2);
  tft.setCursor(15, 296);
  tft.print("Min:");
  tft.print(temp3Min, 2);

}



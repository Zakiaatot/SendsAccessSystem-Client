// 2022.9.20 by Sends Devops Zhang Zhifei  @zakiaatot
#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFiMulti.h>
#include <ArduinoWebsockets.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// pin define
#define SS_PIN 0
#define RST_PIN 4
#define opendoor 5

MFRC522 rfid(SS_PIN, RST_PIN);
ESP8266WiFiMulti WifiMulti;
using namespace websockets;
WebsocketsClient WebClient;

const char *api = "XXX"; //"10.12.65.228"
const uint16_t port = 80;       //9002
const String verifypath = "/verify";
const String websocketpath = "/SendsAccessClient";
const String websocketuid = "XXX";
const String websockettoken = "XXX";

const int dooropentime = 5000;
unsigned long starttime = 0;
boolean isdooropen = false;

void setup()
{
  Serial.begin(9600);
  NfcSetup();
  WebSetup();
  PinSetup();
}
void loop()
{
  NfcServer();
  WebServer();
  AutoCloseDoor();
}

/*custom function*/
// setup function
void NfcSetup()
{
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("Nfc Inited");
}
void WebSetup()
{
  ConnectToWifi();
  ConnectToServer();
}
void PinSetup()
{
  pinMode(opendoor, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(opendoor, HIGH);
  digitalWrite(LED_BUILTIN, HIGH);
}

// loop function
void NfcServer()
{
  if (isdooropen)
    return;
  if (!rfid.PICC_IsNewCardPresent())
    return;
  if (!rfid.PICC_ReadCardSerial())
    return;
  MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
  if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
      piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
      piccType != MFRC522::PICC_TYPE_MIFARE_4K)
  {
    Serial.println(F("Unsported Card Type"));
    return;
  }

  DynamicJsonDocument doc(128);
  for (byte i = 0; i < 4; i++)
    doc["Uid" + String(i)] = rfid.uid.uidByte[i];
  String jsonmsg;
  serializeJson(doc, jsonmsg);

  if (VerifyUser(jsonmsg))
    OpenDoor();
  else
    Serial.println("Verify Failed");

  // Halt PICC
  rfid.PICC_HaltA();
  // Stop encryption on PCD
  rfid.PCD_StopCrypto1();
}
void WebServer()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    ConnectToWifi();
  }
  if (WebClient.available())
  {
    WebClient.poll();
  }
  else
  {
    ConnectToServer();
  }
}

/* other function*/
// verify post
boolean VerifyUser(String UidJson)
{
  WiFiClient client;
  HTTPClient http;
  String verifyapi = api;
  String url = "http://" + verifyapi + ":" + String(port) + verifypath;
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(UidJson);
  if (httpCode == HTTP_CODE_OK)
  {
    DynamicJsonDocument doc(128);
    deserializeJson(doc, http.getString());
    String code = doc["code"];
    if (code == "1")
    {
      http.end();
      return true;
    }
  }
  Serial.println(httpCode);
  http.end();
  return false;
}

// open door
void OpenDoor()
{
  digitalWrite(opendoor, LOW);
  digitalWrite(LED_BUILTIN, LOW);
  isdooropen = true;
  starttime = millis();
}

// auto close door
void AutoCloseDoor()
{
  if (millis() - starttime >= dooropentime)
  {
    digitalWrite(opendoor, HIGH);
    digitalWrite(LED_BUILTIN, HIGH);
    isdooropen = false;
  }
}

// websocket function
void onMessageCallback(WebsocketsMessage message)
{
  DynamicJsonDocument doc(128);
  DynamicJsonDocument msg(128);
  deserializeJson(doc, message.data());
  String operate = doc["operate"];
  Serial.println(operate);

  if (operate == "opendoor")
  {
    if (isdooropen)
    {
      msg["code"] = 0;
      msg["operate"] = operate;
      msg["msg"] = "busy";
    }
    else
    {
      OpenDoor();
      msg["code"] = 1;
      msg["operate"] = operate;
      msg["msg"] = "success";
    }
  }
  else
  {
    msg["code"] = 0;
    msg["operate"] = operate;
    msg["msg"] = "operate Error";
  }

  String jsonmsg;
  serializeJson(msg, jsonmsg);
  WebClient.send(jsonmsg);
}

void onEventsCallback(WebsocketsEvent event, String data)
{
  if (event == WebsocketsEvent::ConnectionOpened)
  {
    Serial.println("Connnection Opened");
  }
  else if (event == WebsocketsEvent::ConnectionClosed)
  {
    Serial.println("Connnection Closed");
  }
  else if (event == WebsocketsEvent::GotPing)
  {
    Serial.println("Got a Ping!");
  }
  else if (event == WebsocketsEvent::GotPong)
  {
    Serial.println("Got a Pong!");
  }
}

void ConnectToWifi()
{
  WifiMulti.addAP("HiWiFi_10D32A", "wifi.sends.cc");
  WifiMulti.addAP("Sends_506", "wifi.sends.cc");
  // WifiMulti.addAP("sends_huawei_wifi", "wifi.sends.cc");
  while (WifiMulti.run() != WL_CONNECTED)
  {
    Serial.print("Connecting to wifi......");
    delay(500);
  }
}

void ConnectToServer()
{
  WebClient.onMessage(onMessageCallback);
  WebClient.onEvent(onEventsCallback);
  String path = websocketpath + "?token=" + websockettoken + "&Uid=" + websocketuid;
  WebClient.connect(api, port, path);
  int count = 0;
  while(!WebClient.available() && count<=10){
    Serial.println("Connecting to server......");
    delay(500);
    ++count;
  }
}
/*
######################################################################
##      Integração das tecnologias da REMOTE IO com Node IOT        ##
##                          Versão 1.0                              ##
##   Código base para implementação de projetos de digitalização de ##
##   processos, automação, coleta de dados e envio de comandos com  ##
##   controle embarcado e na nuvem.                                 ##
##                                                                  ##
######################################################################
*/

#ifndef ESP8266RemoteIO_h
#define ESP8266RemoteIO_h

#define VERSION "1.4.0"

#define JSON_DOCUMENT_CAPACITY 4096

#define INICIALIZATION 0    // First state after start, never connected to nodeiot. 
#define CONNECTED 1         // Connected to nodeiot, available to esp_now as well.
#define NO_WIFI 2           // No Wi-Fi network, disconnected from nodeiot.
#define DISCONNECTED 3      // No websocket connection, disconnected from nodeiot.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <WiFiClientSecure.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESPAsyncTCP.h>
#include <ESP8266mDNS.h>

class RemoteIO 
{
  public:
    RemoteIO();
    void begin(void (*userCallbackFunction)(String ref, String value));
    void loop();
    void updatePinOutput(String ref);
    void updatePinInput(String ref);
    int espPOST(String variable, String value);

    JsonObject setIO;
    
  private:
    void notFound(AsyncWebServerRequest *request);
    void setIOsAndEvents(JsonDocument document);
    void tryWiFiConnection();
    void tryAuthenticate();    
    void fetchLatestData();
    void openLocalServer();
    void switchState();
    void stateLogic();
    void socketIOConnect();
    void nodeIotConnection(void (*userCallbackFunction)(String ref, String value));
    void socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length);
    void rebootDevice();
    void eraseDeviceSettings();
    void infoUpdatedEventHandler(JsonDocument payload_doc);
    void extractIPAddress(String url);
    void getPCBModel();
    void startAccessPoint();
    int espPOST(String Router, String variable, String value);

    void (*storedCallbackFunction)(String ref, String value);

    StaticJsonDocument<JSON_DOCUMENT_CAPACITY> configurationDocument;
    JsonArray configurations;

    SocketIOclient socketIO;
    AsyncWebServer* server;

    bool Connected;
    int Socketed;
    unsigned long messageTimestamp;

    String _ssid;
    String _password;
    String _companyName;
    String _deviceId;
    String _model;
    String _appHost;
    uint16_t _appPort;
    
    String appBaseUrl;
    String appVerifyUrl;
    String appLastDataUrl;
    String appPostData;
    
    long start_debounce_time;
    long start_reconnect_time;

    String state;
    String token;

    int connection_state;
    int next_state;

    int reconnect_counter;
};

#endif 




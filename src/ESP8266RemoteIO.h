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

#define VERSION "1.1.2"

#define JSON_DOCUMENT_CAPACITY 4096

#define INICIALIZATION 0    // First state after start, never connected to nodeiot. 
#define CONNECTED 1         // Connected to nodeiot, available to esp_now as well.
#define NO_WIFI 2           // No Wi-Fi network, disconnected from nodeiot.
#define DISCONNECTED 3      // No websocket connection, disconnected from nodeiot.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <ArduinoOTA.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <WiFiClientSecure.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESPAsyncTCP.h>
#include <ESP8266mDNS.h>
#include <time.h>

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
    void localHttpUpdateMsg(String ref, String value);
    static void IRAM_ATTR interruptCallback(void* arg);
    static void timerEventCallback(void* arg);
    void tryAuthenticate();    
    void fetchLatestData();
    void browseService(const char * service, const char * proto);
    void sendDataFromQueue();
    void switchState();
    void stateLogic();
    void socketIOConnect();
    void nodeIotConnection(void (*userCallbackFunction)(String ref, String value));
    void socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length);
    void extractIPAddress(String url);
    void startAccessPoint();
    void checkResetting(long timeInterval);
    void updateEventArray();
    void setTimer();
    int espPOST(JsonDocument arrayDoc);
    int espPOST(String Router, String variable, String value);

    void (*storedCallbackFunction)(String ref, String value);
    os_timer_t timer;

    StaticJsonDocument<JSON_DOCUMENT_CAPACITY> configurationDocument;
    JsonArray configurations;
    StaticJsonDocument<512> monitor_doc;
    
    JsonDocument event_doc;
    JsonArray event_array;

    SocketIOclient socketIO;
    AsyncWebServer* server;

    bool Connected;
    bool ssidAuth;
    int Socketed;
    unsigned long messageTimestamp;

    String _ssid;
    String _password;
    String _companyName;
    String _deviceId;
    String _model;
    String _appHost;
    uint16_t _appPort;

    String anchor_route;
    String anchored_route;
    
    String appBaseUrl;
    String appVerifyUrl;
    String appLastDataUrl;
    String appSideDoor;
    String appPostData;
    String appPostMultiData;
    String appPostDataFromAnchored;

    const char* ntp_server1 = "pool.ntp.org";
    const char* ntp_server2 = "time.nist.gov";
    
    long start_debounce_time;
    long start_browsing_time;
    long start_reconnect_time;
    long start_config_time; 
    long start_reset_time;

    long gmtOffset_sec;
    int daylightOffset_sec;
    struct tm timeinfo;

    String state;
    String token;

    String anchor_IP;
    String anchored_IP;
    String send_to_niot_buffer;
    String send_to_anchor_buffer;
    String send_to_anchored_buffer;

    int connection_state;
    int next_state;

    bool anchored;
    bool anchoring;
    int lastIP_index;
    int reconnect_counter;
};

#endif 




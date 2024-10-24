/*
######################################################################
##      Integração das tecnologias da REMOTE IO com Node IOT        ##
##                          Version 1.0                             ##
##   Código base para implementação de projetos de digitalização de ##
##   processos, automação, coleta de dados e envio de comandos com  ##
##   controle embarcado e na nuvem.                                 ##
##                                                                  ##
######################################################################
*/

#include "ESP8266RemoteIO.h";
#include "index_html.h";

typedef struct interrupt_data 
{
  RemoteIO* remoteio_pointer;
  String ref_arg;
} interrupt_data;

typedef struct event_data
{
  RemoteIO* remoteio_pointer;
  JsonDocument* actions_arg;
} event_data;

DynamicJsonDocument post_data_queue(1024);
unsigned long last_queue_sent_time = 0;
volatile bool timer_expired;

RemoteIO::RemoteIO()
{
  _appPort = 5000;
  server = new AsyncWebServer(80);

  anchor_route = "http://anchor_IP/post-message";
  anchored_route = "http://anchored_IP/post-message";

  state = "";
  token = "";
    
  configurations = configurationDocument.to<JsonArray>();
  setIO = configurations.createNestedObject();

  event_array = event_doc.to<JsonArray>();

  local_mode = false;
  Connected = false;
  Socketed = 0;
  messageTimestamp = 0;

  connection_state = INICIALIZATION;
  next_state = INICIALIZATION;

  start_debounce_time = 0;
  start_browsing_time = 0;
  start_reconnect_time = 0;
  start_reset_time = 0;

  lastIP_index = -1;
  anchored = false;
  anchoring = false;
  reconnect_counter = 0;
}

void RemoteIO::openLocalServer()
{
  server->on("/monitor", HTTP_GET, [this](AsyncWebServerRequest *request) {  
    request->send_P(200, "text/html", page_monitor);
  });

  server->on("/monitor-reset", HTTP_GET, [this](AsyncWebServerRequest *request) {
    SPIFFS.remove("/config.json");
    request->send(200, "text/plain", "Reset de credenciais efetuado com sucesso! Reiniciando dispositivo...");
    delay(1000);
    ESP.restart();
  });

  server->on("/monitor-data", HTTP_GET, [this](AsyncWebServerRequest *request) {
    String wifi_state; 
    
    if (WiFi.status() == WL_CONNECTED) wifi_state = "Conectado";
    else wifi_state = "Desconectado";
    
    if (state == "accepted") monitor_doc["NodeIoT"]["authentication"] = "Verificado";
    else monitor_doc["NodeIoT"]["authentication"] = "Não verificado";

    if (Connected) monitor_doc["NodeIoT"]["connection"] = "Conectado";
    else monitor_doc["NodeIoT"]["connection"] = "Desconectado";

    char timeString[64];
    if (!getLocalTime(&timeinfo))
    {
      sprintf(timeString, "Desconectado");
    }
    else
    {
      strftime(timeString, sizeof(timeString), "%A, %B %d %Y %H:%M:%S", &timeinfo);
    }

    monitor_doc["Wi-Fi"]["ssid"] = _ssid;
    monitor_doc["Wi-Fi"]["ipLocal"] = WiFi.localIP().toString();
    monitor_doc["Wi-Fi"]["state"] = wifi_state;
    monitor_doc["Wi-Fi"]["rssi"] = WiFi.RSSI();
    monitor_doc["RemoteIO"]["model"] = _model;
    monitor_doc["RemoteIO"]["memory"] = String((ESP.getFlashChipRealSize() / 1024));
    monitor_doc["RemoteIO"]["version"] = VERSION;
    monitor_doc["RemoteIO"]["localTime"] = String(timeString);
    monitor_doc["NodeIoT"]["companyName"] = _companyName;
    monitor_doc["NodeIoT"]["deviceId"] = _deviceId;

    String monitor_info;
    serializeJson(monitor_doc, monitor_info);
    monitor_doc.clear();
    
    request->send(200, "application/json", monitor_info);
  });
}

void RemoteIO::begin(void (*userCallbackFunction)(String ref, String value))
{
  storedCallbackFunction = userCallbackFunction;
  Serial.begin(115200);

  if (!SPIFFS.begin()) 
  {
    Serial.println("Erro ao montar o sistema de arquivos");
    ESP.restart();
  }

  StaticJsonDocument<JSON_DOCUMENT_CAPACITY> nvsDoc;
  bool hasConfig = true;

  File file = SPIFFS.open("/config.json", "r");

  if (!file) 
  {
    hasConfig = false;
  }
  else 
  {
    deserializeJson(nvsDoc, file);
  }
  file.close();

  String NVS_SSID = nvsDoc["ssid"].as<String>();
  String NVS_PASSWORD = nvsDoc["password"].as<String>();
  String NVS_COMPANYNAME = nvsDoc["companyName"].as<String>();
  String NVS_DEVICEID = nvsDoc["deviceId"].as<String>();
  String NVS_MODEL = nvsDoc["model"].as<String>();
  String NVS_IOSETTINGS = nvsDoc["ioSettings"].as<String>();
  bool NVS_SSIDAUTH = nvsDoc["ssidAuth"].as<bool>();

  if (!hasConfig)
  {
    startAccessPoint();
  }
  else
  {
    _ssid = NVS_SSID;
    _password = NVS_PASSWORD;
    _companyName = NVS_COMPANYNAME;
    _deviceId = NVS_DEVICEID;
    _model = NVS_MODEL;
    ssidAuth = NVS_SSIDAUTH;

    appBaseUrl = "https://api-dev.orlaguaiba.com.br/api"; //"https://api.nodeiot.app.br/api";
    appVerifyUrl = appBaseUrl + "/devices/verify";
    appPostData = appBaseUrl + "/broker/data/";
    appPostMultiData = appBaseUrl + "/broker/multidata";
    appSideDoor = appBaseUrl + "/devices/devicedisconnected";
    appPostDataFromAnchored = appBaseUrl + "/broker/ahamdata";
    appLastDataUrl = appBaseUrl + "/devices/getdata/" + _companyName + "/" + _deviceId;

    timer_expired = false;
    nodeIotConnection(userCallbackFunction);

    String LOCAL_DOMAIN = String("niot-") + String(_deviceId);
    LOCAL_DOMAIN.toLowerCase();

    if (!MDNS.begin(LOCAL_DOMAIN)) 
    {
      Serial.println("Erro ao configurar o mDNS");
    }

    AsyncCallbackJsonWebHandler* handler = new AsyncCallbackJsonWebHandler("/post-message", [this](AsyncWebServerRequest *request, JsonVariant &json) {
      StaticJsonDocument<250> data;
      String response;

      if (json.is<JsonArray>()) data = json.as<JsonArray>();
      else if (json.is<JsonObject>()) data = json.as<JsonObject>();
      
      //Serial.print("[AsyncCallback]: ");
      //Serial.println(data.as<String>());

      if (data.containsKey("status"))
      {
        if (connection_state == CONNECTED)
        {
          data.remove("status");
          data["ipAddress"] = request->client()->remoteIP().toString();

          serializeJson(data, send_to_niot_buffer);

          if (espPOST(appSideDoor, "", send_to_niot_buffer) == HTTP_CODE_OK)
          {
            send_to_niot_buffer.clear();
            data.clear();
            
            if (anchoring) 
            {
              data["msg"] = "ok";
              anchoring = false;
            }
            else data["msg"] = "received";

            serializeJson(data, response);
            request->send(200, "application/json", response);
          }
          else 
          {
            send_to_niot_buffer.clear();
            data.clear();
            data["msg"] = "post to niot failed";
            serializeJson(data, response);
            request->send(500, "application/json", response);
          }
        }
        else 
        {
          if (request->client()->remoteIP().toString() == anchor_IP) 
          {
            anchor_IP.clear();
            anchored = false;
          }

          data.clear();
          data["msg"] = "disconnected";
          serializeJson(data, response);
          request->send(500, "application/json", response);
        }
      }
      
      else if (data.containsKey("ref") && !data.containsKey("deviceId") && connection_state == DISCONNECTED)
      {
        anchor_IP.clear();
        anchor_IP = request->client()->remoteIP().toString();
        anchored = true;
        
        String ref = data["ref"].as<String>();
        setIO[ref]["value"] = data["value"];
        
        if (setIO[ref]["type"] == "OUTPUT")
        {
          updatePinOutput(ref);
        }

        data.clear();
        data["msg"] = "ok";
        serializeJson(data, response);
        request->send(200, "application/json", response);

        if (ref == "restart") ESP.restart();
        else if (ref == "reset")
        {
          if (SPIFFS.remove("/config.json")) Serial.println("/config.json removido com sucesso!");
          else Serial.println("Falha ao remover /config.json");
          delay(1000);
          ESP.restart();
        }
      }
      
      else if (data.containsKey("deviceId"))
      {
        if (connection_state == CONNECTED)
        {
          serializeJson(data, send_to_niot_buffer);
          if (espPOST(appPostDataFromAnchored, "", send_to_niot_buffer) == HTTP_CODE_OK)
          {
            send_to_niot_buffer.clear();
            data.clear();
            data["msg"] = "ok";
            serializeJson(data, response);
            request->send(200, "application/json", response);
          }
          else 
          {
            send_to_niot_buffer.clear();
            data.clear();
            data["msg"] = "post to niot failed";
            serializeJson(data, response);
            request->send(500, "application/json", response);
          }
        }
        else 
        {
          data.clear();
          data["msg"] = "disconnected";
          serializeJson(data, response);
          request->send(500, "application/json", response);
        }
      }
      else 
      {
        data.clear();
        data["msg"] = "unhandled message";
        serializeJson(data, response);
        request->send(500, "application/json", response);
      }
    });
    server->addHandler(handler);
  }

  openLocalServer();
  MDNS.addService("http", "tcp", 80);
  
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  server->onNotFound(std::bind(&RemoteIO::notFound, this, std::placeholders::_1));

  ArduinoOTA.begin();
  server->begin();
}

void RemoteIO::checkResetting(long timeInterval)
{
  if (digitalRead(setIO["reset"]["pin"].as<int>()) == LOW)
  {
    if (start_reset_time == 0) start_reset_time = millis();
    else if (millis() - start_reset_time >= timeInterval)
    {
      SPIFFS.remove("/config.json");
      delay(1000);
      ESP.restart();
    }
  }
  else start_reset_time = 0;
}

void RemoteIO::startAccessPoint()
{
  StaticJsonDocument<100> model_doc;

  File model_file = SPIFFS.open("/model.json", "r");
  deserializeJson(model_doc, model_file);
  model_file.close();
  _model = model_doc["model"].as<String>();
  if (_model == "") _model = "ESP_8266";
  model_doc.clear();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);

  IPAddress apIP(192, 168, 4, 1);

  bool result = WiFi.softAP("RemoteIO");
  if (!result) 
  {
    Serial.println("Erro ao configurar o ponto de acesso");
    ESP.restart();
  }
  
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  
  Serial.println("Ponto de acesso iniciado");

  IPAddress IP = WiFi.softAPIP();
  
  String LOCAL_DOMAIN = String("remoteio");

  if (!MDNS.begin(LOCAL_DOMAIN)) 
  {
    Serial.println("Erro ao configurar o mDNS");
  }

  server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", page_setup);
  });

  server->on("/get", HTTP_GET, [this](AsyncWebServerRequest *request) {

    if (request->hasParam("ssid") && request->hasParam("password") && request->hasParam("companyName") && request->hasParam("deviceId")) 
    {
      String arg_ssid = request->getParam("ssid")->value();
      String arg_password = request->getParam("password")->value();
      String arg_companyName = request->getParam("companyName")->value();
      String arg_deviceId = request->getParam("deviceId")->value();

      StaticJsonDocument<400> doc;
      doc["ssid"] = arg_ssid;
      doc["password"] = arg_password;
      doc["companyName"] = arg_companyName;
      doc["deviceId"] = arg_deviceId;
      doc["model"] = _model;
      doc["ssidAuth"] = false;

      File file = SPIFFS.open("/config.json", "w");
      serializeJson(doc, file);
      file.close();
      doc.clear();

      request->send(200, "text/plain", "Credenciais recebidas com sucesso. Tentando conexão...");
      delay(1000); 
      ESP.restart();
    }
    else 
    {
      request->send(400, "text/plain", "Parâmetros ausentes");
    }
  });
}

void RemoteIO::sendDataFromQueue()
{
  if (post_data_queue.size() >= 1)
  {
    last_queue_sent_time = millis();
    espPOST(post_data_queue);
    post_data_queue.clear();
  }
}

void RemoteIO::loop()
{
  ArduinoOTA.handle();
  switchState();
  stateLogic();
  //checkResetting(5000); 
  updateEventArray();
  sendDataFromQueue();
}

void RemoteIO::browseService(const char * service, const char * proto)
{
  int n = MDNS.queryService(service, proto);
  if (n == 0) 
  {
    lastIP_index = -1;
  } 
  else 
  {
    for (int i = 0; i < n; i++) 
    {
      if ((MDNS.hostname(i).indexOf("niot") != -1) || (MDNS.hostname(i).indexOf("esp32") != -1) || (MDNS.hostname(i).indexOf("esp8266") != -1))
      {
        if (i > lastIP_index)
        {
          lastIP_index = i;
          anchor_IP = MDNS.IP(i).toString();
          return;
        }
        else lastIP_index = -1;
      }
    }
  }
}

void RemoteIO::switchState()
{
  switch (connection_state)
  {
    case INICIALIZATION:
      if ((WiFi.status() == WL_CONNECTED) && (Connected == true))
      {
        Serial.println("[INICIALIZATION] vai pro CONNECTED");
        next_state = CONNECTED;
      }
      else if (local_mode)
      {
        Serial.println("[INICIALIZATION] vai pro DISCONNECTED");
        next_state = DISCONNECTED;
      }
      else
      {
        next_state = INICIALIZATION;
      }
      break;
      
    case CONNECTED:
      if (WiFi.status() != WL_CONNECTED)
      {
        //Serial.println("[CONNECTED] vai pro NO_WIFI");
        next_state = NO_WIFI;
      }
      else if (!Connected)
      {
        //Serial.println("[CONNECTED] vai pro DISCONNECTED");
        next_state = DISCONNECTED;
      }
      else 
      {
        next_state = CONNECTED;
      }
      break;
      
    case NO_WIFI:
      if (WiFi.status() == WL_CONNECTED)
      {
        //Serial.println("[NO_WIFI] vai pro DISCONNECTED");
        start_debounce_time = 0;
        next_state = DISCONNECTED;
      }
      else 
      {
        next_state = NO_WIFI;
      }
      break;
      
    case DISCONNECTED:
      if (Connected)
      {
        reconnect_counter = 0;
        //Serial.println("[DISCONNECTED] vai pro CONNECTED");
        next_state = CONNECTED;
      }
      else if (WiFi.status() != WL_CONNECTED)
      {
        //Serial.println("[DISCONNECTED] vai pro NO_WIFI");  
        next_state = NO_WIFI;
      }
      else 
      {
        next_state = DISCONNECTED;
      }
      break;
  }
  connection_state = next_state;
}

void RemoteIO::stateLogic()
{
  switch (connection_state)
  {
    case INICIALIZATION:
      
      socketIO.loop(); 
      if (Connected == false)
      {
        socketIOConnect();
      }
      break;
      
    case CONNECTED:
      
      socketIO.loop();
      if (setIO["disconnect"]["value"] == "1")
      {
        socketIO.sendEVENT("disconnect");
      }

      break;
      
    case NO_WIFI:

      if (millis() - start_reconnect_time >= 10000)
      {
        start_reconnect_time = millis();
        start_debounce_time = millis();
        nodeIotConnection(storedCallbackFunction); 
      }
      break;

    case DISCONNECTED:
      
      if (setIO["disconnect"]["value"] == "0")
      {
        socketIO.loop();
        socketIOConnect();
      }
      
      if ((!anchored) && (millis() - start_browsing_time >= 5000))
      {
        browseService("http", "tcp");
        start_browsing_time = millis();

        if (anchor_IP.length() > 0)
        {
          StaticJsonDocument<250> doc;
          doc["status"] = "disconnected";
          doc["mac"] = WiFi.macAddress();
          send_to_anchor_buffer.clear();
          serializeJson(doc, send_to_anchor_buffer);
          doc.clear();

          espPOST(anchor_route, "", send_to_anchor_buffer);          
        }
      }
      
      if (millis() - start_reconnect_time >= 60000)
      {
        if (reconnect_counter >= 3) ESP.restart();
        else reconnect_counter++;
        start_reconnect_time = millis();
        start_debounce_time = millis();
        nodeIotConnection(storedCallbackFunction); 
      }
      break;
  }
}

void RemoteIO::rebootDevice()
{
  ESP.restart();
}

void RemoteIO::eraseDeviceSettings()
{
  if (SPIFFS.remove("/config.json")) Serial.printf("\nApagando configurações salvas na memória não volátil...\n");
  else Serial.printf("\nFalha ao remover configurações armazenadas na memória não volátil. Por favor, tente novamente.\n");
  delay(1000);
  ESP.restart();
}

void RemoteIO::infoUpdatedEventHandler(JsonDocument payload_doc)
{
  String function = payload_doc[1]["function"];

  if (function == "restart") rebootDevice();
  else if (function == "reset") eraseDeviceSettings();
}

void RemoteIO::socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
    case sIOtype_DISCONNECT:
      Connected = false;
      break;
    case sIOtype_CONNECT:
      socketIO.send(sIOtype_CONNECT, "/");
      break;
    case sIOtype_EVENT:
      char *sptr = NULL;
      int id = strtol((char *)payload, &sptr, 10);

      //Serial.printf("[IOc] get event: %s id: %d\n", payload, id);
      
      if (id)
      {
        payload = (uint8_t *)sptr;
      }

      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, payload, length);

      if (error) return;
      
      String eventName = doc[0];

      if (eventName == "infoUpdated")
      {
        infoUpdatedEventHandler(doc);
      }
      else 
      {
        if (doc[1].containsKey("ipdest")) 
        {
          StaticJsonDocument<250> doc2;
          doc2["ref"] = doc[1]["ref"];
          doc2["value"] = doc[1]["value"];
          
          anchored_IP = doc[1]["ipdest"].as<String>();
          serializeJson(doc2, send_to_anchored_buffer);
          
          doc2.clear();
          
          espPOST(anchored_route, "", send_to_anchored_buffer);
          send_to_anchored_buffer.clear();
        }
        else 
        {
          String ref = doc[1]["ref"];
          String value = doc[1]["value"];

          if (ref == "restart") rebootDevice();
          else if (ref == "reset") eraseDeviceSettings();

          setIO[ref]["value"] = value;

          if (setIO[ref]["type"] == "OUTPUT")
          {
            updatePinOutput(ref);
          }
        }
        doc.clear();
        break;
      }
  }
}

void RemoteIO::nodeIotConnection(void (*userCallbackFunction)(String ref, String value))
{
  String hostname = String("niot-") + String(_deviceId);
  hostname.toLowerCase();
  Connected = false;

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(_ssid, _password);
  WiFi.waitForConnectResult();
  WiFi.setHostname(hostname.c_str());

  long wifi_conn_time = 0;

  if (!ssidAuth) 
  {
    wifi_conn_time = millis();
  }

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    if ((wifi_conn_time > 0) && (millis() - wifi_conn_time >= 10000))
    {
      if (SPIFFS.remove("/config.json")) 
      {
        delay(1000);
        ESP.restart();
      }
    }
    if ((start_debounce_time != 0) && (millis() - start_debounce_time >= 2000))
    {
      WiFi.disconnect();
      return;
    }
  }

  if (!ssidAuth) 
  {
    File file = SPIFFS.open("/config.json", "r");
    JsonDocument nvsDoc;
    deserializeJson(nvsDoc, file);
    file.close();
    nvsDoc["ssidAuth"] = true;
    SPIFFS.remove("/config.json"); 
    file = SPIFFS.open("/config.json", "w");
    serializeJson(nvsDoc, file);
    nvsDoc.clear();
    file.close();
  }

  Serial.printf("[nodeIotConnection] WiFi Connected %s\n\n", WiFi.localIP().toString().c_str());

  appVerifyUrl.replace(" ", "%20");
  appLastDataUrl.replace(" ", "%20");
  
  gmtOffset_sec = (-3) * 3600;
  daylightOffset_sec = 0;
  configTime(gmtOffset_sec, daylightOffset_sec, ntp_server1, ntp_server2);

  while (state != "accepted")
  {
    if ((start_debounce_time != 0) && (millis() - start_debounce_time >= 2000))
    {
      return;
    }
    tryAuthenticate();

    if (local_mode)
    {
      //Serial.println("[nodeIotConnection] local_mode true, interrompendo tentativas de autenticacao");
      break;
    }
  }
  
  if (!local_mode)
  {
    String appSocketPath = "/socket.io/?token=" + token + "&EIO=4";

    fetchLatestData();

    socketIO.begin(_appHost, _appPort, appSocketPath); 
    socketIO.onEvent([this, userCallbackFunction](socketIOmessageType_t type, uint8_t* payload, size_t length) 
    {
      this->socketIOEvent(type, payload, length);

      if ((userCallbackFunction != nullptr) && (type == sIOtype_EVENT)) 
      {
        char *sptr = NULL;
        int id = strtol((char *)payload, &sptr, 10);

        if (id)
        {
          payload = (uint8_t *)sptr;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload, length);

        if (error) return;

        String ref = doc[1]["ref"];
        String value = doc[1]["value"];

        userCallbackFunction(ref, value);
      }
    });
  }
}

void RemoteIO::socketIOConnect()
{
  uint64_t now = millis();
  if (Socketed == 0)
  {
    StaticJsonDocument<JSON_DOCUMENT_CAPACITY> doc;
    JsonArray array = doc.to<JsonArray>();
    array.add("connection");
    JsonObject query = array.createNestedObject();
    query["Query"]["token"] = token;

    String output;
    serializeJson(doc, output);
    Socketed = socketIO.sendEVENT(output);
    Socketed = 1;
  }
  if ((Socketed == 1) && (now - messageTimestamp > 2000) && (Connected == 0))
  {
    StaticJsonDocument<JSON_DOCUMENT_CAPACITY> doc;
    JsonArray array = doc.to<JsonArray>();
    messageTimestamp = now;
    array.add("joinRoom");
    String output;
    serializeJson(doc, output);
    Connected = socketIO.sendEVENT(output);
    if (Connected) Serial.println("[socketIOConnect] Connected");
    //else Serial.println("[socketIOConnect] Failed connecting");
  }
}

void IRAM_ATTR RemoteIO::interruptCallback(void* arg)
{
  interrupt_data* obj = (interrupt_data*)arg;
  
  if (post_data_queue.size() <= 10)
  {
    unsigned long reading_timestamp = millis();
    int value = digitalRead(obj->remoteio_pointer->setIO[obj->ref_arg]["pin"].as<int>());
    
    DynamicJsonDocument doc(75);
    doc["ref"] = obj->ref_arg;
    doc["value"] = value;
    doc["timestamp"] = reading_timestamp;

    obj->remoteio_pointer->setIO[obj->ref_arg]["value"] = value;
    obj->remoteio_pointer->setIO[obj->ref_arg]["timestamp"] = reading_timestamp;

    post_data_queue.add(doc);
  }
}

void RemoteIO::timerEventCallback(void *arg)
{
  event_data* obj = (event_data*) arg;
  JsonDocument& actions = *(obj->actions_arg);

  for (size_t i = 0; i < actions.size(); i++)
  {
    String ref = actions[i]["ref"].as<String>();
    String refType = obj->remoteio_pointer->setIO[ref]["type"].as<String>();

    obj->remoteio_pointer->setIO[ref]["value"] = actions[i]["value"];
    if (refType == "OUTPUT") obj->remoteio_pointer->updatePinOutput(ref);

    JsonDocument doc;
    doc["ref"] = ref;
    doc["value"] = actions[i]["value"];
    doc["timestamp"] = time(nullptr); //unix time seconds, gmt+0

    post_data_queue.add(doc);
    timer_expired = true;
  }
}

void RemoteIO::updateEventArray()
{
  if ((timer_expired) && (event_array.size() > 0))
  {
    for (size_t i = 0; i < event_array.size(); i++)
    {
      if (event_array[i]["active"].as<bool>())
      {
        if (event_array[i]["repeat"].as<int>() > 0)
        {
          JsonDocument obj = event_array[0];
          String targetTimestamp = obj["targetTimestamp"].as<String>();
          obj["targetTimestamp"] = ((strtol(targetTimestamp.c_str(), NULL, 10)) + ((obj["delay"].as<int>())/1000));
          obj["active"] = false;
          event_array.add(obj);
          obj.clear();
        }

        event_array.remove(i);
        timer_expired = false;
        setTimer();
        return;
      }
    }
  }
}

void RemoteIO::setTimer()
{
  if (getLocalTime(&timeinfo) && event_array.size() > 0)
  {
    int next_event_position = 0;
    String next_event_timestamp_string = event_array[0]["targetTimestamp"].as<String>();
    long next_event_timestamp = strtol(next_event_timestamp_string.c_str(), NULL, 10);

    for (size_t i = 1; i < event_array.size(); i++)
    {
      String temp_event_timestamp_string = event_array[i]["targetTimestamp"].as<String>();
      long temp_event_timestamp = strtol(next_event_timestamp_string.c_str(), NULL, 10);
      
      if (temp_event_timestamp < next_event_timestamp) 
      {
        next_event_position = i;
        next_event_timestamp_string = temp_event_timestamp_string;
        next_event_timestamp = temp_event_timestamp;
      }
    }
    
    if (!event_array[next_event_position]["active"].as<bool>()) 
    {
      time_t now = time(nullptr);
      String unix_time_s_string = event_array[next_event_position]["targetTimestamp"].as<String>();

      long unix_time_s = strtol(unix_time_s_string.c_str(), NULL, 10);
      int delaySeconds = unix_time_s - now;

      Serial.printf("\n[setTimer] Event will trigger in %d\n", delaySeconds);
      
      event_data* arg = new event_data();
      arg->remoteio_pointer = this;
      
      JsonDocument* actions_pointer = new JsonDocument();

      for (size_t i = 0; i < event_array[next_event_position]["actions"].size(); i++)
      {
        actions_pointer->add(event_array[next_event_position]["actions"][i]);
      }

      arg->actions_arg = actions_pointer;

      os_timer_setfn(&timer, timerEventCallback, arg);
      os_timer_arm(&timer, delaySeconds * 1000, false);
      event_array[next_event_position]["active"] = true;
    }
  }
  else if (event_array.size() > 0)
  {
    Serial.println("[setTimer] tenho eventos, mas não consegui sincronizar o horário");
  }
  else 
  {
    Serial.println("[setTimer] sem eventos");
  }
}

void RemoteIO::setIOsAndEvents(JsonDocument document)
{
  token = document["token"].as<String>();
  extractIPAddress(document["serverAddr"].as<String>());

  for (size_t i = 0; i < document["gpio"].size(); i++)
  {
    String ref = document["gpio"][i]["ref"];
    int pin = document["gpio"][i]["pin"].as<int>();
    String type = document["gpio"][i]["type"];
    String mode = document["gpio"][i]["mode"]; // modo de operação. Ex. p/ INPUTs: interrupção, cíclica, em horário definido...

    if (type == "INPUT" || type == "INPUT_ANALOG")
    {
      setIO[ref]["pin"] = pin;
      setIO[ref]["type"] = type;
      setIO[ref]["mode"] = mode;
      pinMode(pin, INPUT);

      if (mode == "interrupt")
      {
        interrupt_data* arg = new interrupt_data();
        arg->remoteio_pointer = this;
        arg->ref_arg = document["gpio"][i]["ref"].as<String>();
        attachInterruptArg(digitalPinToInterrupt(pin), interruptCallback, (void*)arg, CHANGE);
      }
    }
    else if (type == "INPUT_PULLUP")
    {
      setIO[ref]["pin"] = pin;
      setIO[ref]["type"] = type;
      setIO[ref]["mode"] = mode;
      pinMode(pin, INPUT_PULLUP);

      if (mode == "interrupt")
      {
        interrupt_data* arg = new interrupt_data();
        arg->remoteio_pointer = this;
        arg->ref_arg = document["gpio"][i]["ref"].as<String>();
        attachInterruptArg(digitalPinToInterrupt(pin), interruptCallback, (void*)arg, CHANGE);
      }
    }
    else if (type == "OUTPUT")
    {
      setIO[ref]["pin"] = pin;
      setIO[ref]["type"] = type;
      pinMode(pin, OUTPUT);
    } 
    else 
    {
      setIO[ref]["pin"] = pin;
      setIO[ref]["type"] = "N/L";
    }
  }

  for (size_t i = 0; i < document["events"].size(); i++)
  {
    document["events"][i]["active"] = false; 
    event_array.add(document["events"][i]);
  }
  setTimer();
}

void RemoteIO::tryAuthenticate()
{
  WiFiClientSecure client;
  HTTPClient https;
  StaticJsonDocument<JSON_DOCUMENT_CAPACITY> document;
  String request;

  client.setInsecure();

  document["companyName"] = _companyName;
  document["deviceId"] = _deviceId;
  document["mac"] = WiFi.macAddress();
  document["ipAddress"] = WiFi.localIP().toString();
  document["model"] = _model;
  document["version"] = VERSION;

  serializeJson(document, request);

  https.begin(client, appVerifyUrl);
  https.addHeader("Content-Type", "application/json");

  int statusCode = https.POST(request);
  String response = https.getString(); 

  document.clear();
  deserializeJson(document, response);
  //Serial.println(response);

  if (statusCode == HTTP_CODE_OK)
  {
    state = document["state"].as<String>();
    local_mode = false;

    if (state != "accepted") 
    {
      document.clear();
      https.end();
      return;
    }

    JsonDocument doc;
    File file = SPIFFS.open("/config.json", "r");
    deserializeJson(doc, file);
    file.close();

    doc["deviceSettings"] = document;

    file = SPIFFS.open("/config.json", "w");
    serializeJson(doc, file);
    file.close();

    doc.clear();
    setIOsAndEvents(document);
  }
  else
  {
    File file = SPIFFS.open("/config.json", "r");
    document.clear();
    deserializeJson(document, file);
    file.close();
    
    if (!document.isNull())
    {
      setIOsAndEvents(document["deviceSettings"]);
      local_mode = true;
    }
  }
  document.clear();
  https.end();
}

void RemoteIO::fetchLatestData()
{ 
  WiFiClient client;
  HTTPClient http;

  http.begin(client, appLastDataUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + token);

  int statusCode = http.GET();

  if (statusCode == HTTP_CODE_OK)
  {
    StaticJsonDocument<JSON_DOCUMENT_CAPACITY> document;
    deserializeJson(document, http.getStream());

    for (size_t i = 0; i < document.size(); i++)
    {
      String auxRef = document[i]["ref"];
      String auxValue = document[i]["data"]["value"];

      if (auxValue == "null")
      {
        auxValue = "0";
      }
      
      setIO[auxRef]["value"] = auxValue;
      
      if (setIO[auxRef]["type"] == "OUTPUT")
      {
        updatePinOutput(auxRef);
      }
    }
    document.clear();
  }

  http.end();
}

void RemoteIO::extractIPAddress(String url)
{
  String new_url;

  if (appBaseUrl == "https://api-dev.orlaguaiba.com.br/api") 
  {
    new_url = "https://54.88.219.77:5000"; // url atual do back (dev)
  }
  else 
  {
    new_url = url;
  }

  int startIndex = new_url.indexOf("//") + 2; // Encontra o início do endereço IP
  int endIndex = new_url.indexOf(":", startIndex); // Encontra o fim do endereço IP

  _appHost = new_url.substring(startIndex, endIndex); // Extrai o endereço IP
}

void RemoteIO::localHttpUpdateMsg (String ref, String value)
{
  StaticJsonDocument<250> doc;
  send_to_anchor_buffer.clear();
  setIO[ref]["value"] = value;
  doc["deviceId"] = _deviceId;
  doc["ref"] = ref;
  doc["value"] = value;
  serializeJson(doc, send_to_anchor_buffer);
  doc.clear();
  espPOST(anchor_route, "", send_to_anchor_buffer);
}

void RemoteIO::updatePinOutput(String ref)
{
  int PinRef = setIO[ref]["pin"].as<int>();
  int ValueRef = setIO[ref]["value"].as<int>();
  
  digitalWrite(PinRef, ValueRef);
}

void RemoteIO::updatePinInput(String ref)
{
  int pinRef = setIO[ref]["pin"].as<int>();
  String typeRef = setIO[ref]["type"].as<String>();
  int delayTime = setIO[ref]["delay"].as<int>() * 1000; // variável de configuração sincronizada com a plataforma
  int timestamp = setIO[ref]["timestamp"].as<int>();  // variável de configuração local, dessincronizada
  
  // garantir pelo menos 5 seg de delay
  if (delayTime < 5000) 
  {
    delayTime = 5000; // ms
    setIO[ref]["delay"] = 5; // s
  }

  if (millis() - timestamp >= delayTime)
  {
    setIO[ref]["timestamp"] = millis();

    // executa ação de leitura conforme tipo de variável ou processo utilizado
    if (typeRef == "INPUT" || typeRef == "INPUT_PULLDOWN" || typeRef == "INPUT_PULLUP")
    {
      int valueRef = digitalRead(pinRef);
      if (connection_state == CONNECTED) espPOST(ref, String(valueRef));
      else if (anchored) localHttpUpdateMsg(ref, String(valueRef));
    }
    else if (typeRef == "INPUT_ANALOG")
    {
      float valueRef = analogRead(pinRef);
      if (connection_state == CONNECTED) espPOST(ref, String(valueRef));
      else if (anchored) localHttpUpdateMsg(ref, String(valueRef));
    }
  }
}

void RemoteIO::notFound(AsyncWebServerRequest *request)
{
  request->send(404, "application/json", "{\"message\":\"Not found\"}");
}

int RemoteIO::espPOST(JsonDocument arrayDoc)
{
  JsonDocument doc;
  String value;
  doc["deviceId"] = _deviceId;
  doc["dataArray"] = arrayDoc;
  serializeJson(doc, value);
  return espPOST(appPostMultiData, "", value);
}

int RemoteIO::espPOST(String variable, String value)
{
    return espPOST(appPostData, variable, value);
}

int RemoteIO::espPOST(String Router, String variable, String value)
{
  if ((WiFi.status() == WL_CONNECTED))
  {
    String route;

    if (Router == anchor_route) route = "http://" + anchor_IP + "/post-message";
    else if (Router == anchored_route) route = "http://" + anchored_IP + "/post-message";
    else route = Router;

    WiFiClientSecure client;
    HTTPClient https;
    StaticJsonDocument<1024> document;
    String request;

    client.setInsecure();

    if (Router == appPostData)
    {
      document["deviceId"] = _deviceId;
      document["ref"] = variable;
      document["value"] = value;
      document["timestamp"] = time(nullptr);
      setIO[variable]["value"] = value;
      serializeJson(document, request);
    }
    else if (Router == appPostMultiData)
    {
      route = appPostMultiData;
      request = value;
    }
    else request = value; 
    
    https.begin(client, route); 
    https.addHeader("Content-Type", "application/json");
    https.addHeader("authorization", "Bearer " + token);

    //Serial.print("[espPOST] Request: ");
    //Serial.println(request);
    
    int httpCode = https.POST(request);

    String response = https.getString(); 
    document.clear();
    deserializeJson(document, response);
    //Serial.println(response);

    if (httpCode == HTTP_CODE_OK)
    {
      if (Router == appSideDoor && document.containsKey("data"))
      {
        if (document["data"]["actived"].as<bool>() == true)
        {
          anchored_IP = document["data"]["ipdest"].as<String>();
          anchoring = true; 
        } 
      }
      else if (Router == anchor_route && document["msg"] == "ok")
      {
        anchored = true;
      }
    }
    else if (httpCode != HTTP_CODE_OK) 
    {
      if (Router == anchor_route)
      {
        anchored = false;
      }
    }
    document.clear();
    https.end();
    return httpCode;
  }
  return 0;
}
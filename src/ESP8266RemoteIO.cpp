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

RemoteIO::RemoteIO()
{
  _appPort = 5000;
  server = new AsyncWebServer(80);

  appBaseUrl = "https://api.nodeiot.app.br/api";
  appVerifyUrl = appBaseUrl + "/devices/verify";
  appPostData = appBaseUrl + "/broker/data/";

  state = "";
  token = "";
    
  configurations = configurationDocument.to<JsonArray>();
  setIO = configurations.createNestedObject();

  Connected = false;
  Socketed = 0;
  messageTimestamp = 0;

  connection_state = INICIALIZATION;
  next_state = INICIALIZATION;

  start_debounce_time = 0;
  start_reconnect_time = 0;
  reconnect_counter = 0;
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

  getPCBModel();

  JsonDocument nvsDoc;
  bool hasConfig = true;
  File file = SPIFFS.open("/config.json", "r");

  if (!file) 
  {
    Serial.println("[begin] Não encontrei credenciais na spiffs");
    hasConfig = false;
  }
  else 
  {
    Serial.println("[begin] Encontrei credenciais na spiffs");
    deserializeJson(nvsDoc, file);
    serializeJson(nvsDoc, Serial);
  }
  file.close();

  _companyName = nvsDoc["companyName"].as<String>();
  _deviceId = nvsDoc["deviceId"].as<String>();
  _ssid = nvsDoc["ssid"].as<String>();
  _password = nvsDoc["password"].as<String>();

  startAccessPoint();
  openLocalServer();

  if ((_ssid != "") && (_ssid != "null") && (_password != "") && (_password != "null"))
  {
    nodeIotConnection(userCallbackFunction);
  }
}

void RemoteIO::getPCBModel()
{
  File file = SPIFFS.open("/model.json", "r");
  
  if (!file)
  {
    Serial.println("Failed to open file for reading");
  }

  JsonDocument document;
  deserializeJson(document, file);
  file.close();

  if (document["model"].as<String>() == "") _model = "ESP_8266";
  else _model = document["model"].as<String>();
}

void RemoteIO::startAccessPoint()
{
  WiFi.mode(WIFI_AP_STA);

  IPAddress apIP(192, 168, 4, 1);

  bool result = WiFi.softAP("RemoteIO");
  
  if (!result) 
  {
    Serial.println("Erro ao configurar o ponto de acesso");
    ESP.restart();
  }
  
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  IPAddress IP = WiFi.softAPIP();

  Serial.print("[startAccessPoint] IP do ponto de acesso: ");
  Serial.println(IP);
  
  String LOCAL_DOMAIN = String("remoteio-device");

  if (!MDNS.begin(LOCAL_DOMAIN)) 
  {
    Serial.println("Erro ao configurar o mDNS");
  }
}

void RemoteIO::openLocalServer()
{
  Serial.println("[openLocalServer] Opening local http endpoints");

  server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", page_setup);
  });

  server->on("/get", HTTP_GET, [this](AsyncWebServerRequest *request) {

    if (request->hasParam("ssid") && request->hasParam("password") && request->hasParam("companyName") && request->hasParam("deviceId"))
    {
      String arg_deviceId = request->getParam("deviceId")->value();;
      String arg_ssid = request->getParam("ssid")->value();
      String arg_password = request->getParam("password")->value();
      String arg_companyName = request->getParam("companyName")->value();

      JsonDocument doc;
      
      doc["deviceId"] = arg_deviceId;
      doc["companyName"] = arg_companyName;
      doc["ssid"] = arg_ssid;
      doc["password"] = arg_password;
      doc["model"] = _model;
      doc["ssidAuth"] = false;

      File file = SPIFFS.open("/config.json", "w");
      serializeJson(doc, file);
      
      file.close();
      doc.clear();

      Serial.println("[/get] Credenciais recebidas!");
      request->send(200, "text/plain", "Credenciais recebidas com sucesso. Tentando conexão...");
      delay(2000); 
      ESP.restart();
    }
    else 
    {
      request->send(400, "text/plain", "Parâmetros ausentes");
    }
  });

  MDNS.addService("http", "tcp", 80);

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  server->onNotFound(std::bind(&RemoteIO::notFound, this, std::placeholders::_1));
  server->begin();
}

void RemoteIO::loop()
{
  switchState();
  stateLogic();
}

void RemoteIO::switchState()
{
  switch (connection_state)
  {
    case INICIALIZATION:
      if ((WiFi.status() == WL_CONNECTED) && (Connected == true))
      {
        Serial.println("[INICIALIZATION] vai pro CONNECTED");

        WiFi.mode(WIFI_STA);

        next_state = CONNECTED;
      }
      else
      {
        next_state = INICIALIZATION;
      }
      break;
      
    case CONNECTED:
      if (WiFi.status() != WL_CONNECTED)
      {
        Serial.println("[CONNECTED] vai pro NO_WIFI");
        startAccessPoint();
        openLocalServer();
        next_state = NO_WIFI;
      }
      else if (!Connected)
      {
        Serial.println("[CONNECTED] vai pro DISCONNECTED");
        startAccessPoint();
        openLocalServer();
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
        Serial.println("[NO_WIFI] vai pro DISCONNECTED");
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
        Serial.println("[DISCONNECTED] vai pro CONNECTED");
        WiFi.mode(WIFI_STA); 
        next_state = CONNECTED;
      }
      else if (WiFi.status() != WL_CONNECTED)
      {
        Serial.println("[DISCONNECTED] vai pro NO_WIFI");  
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
      
      socketIO.loop();
      socketIOConnect();
      
      if (millis() - start_reconnect_time >= 60000)
      {
        if (reconnect_counter >= 3) ESP.restart();
        else reconnect_counter++;
        start_reconnect_time = millis();
        start_debounce_time = millis();
        Serial.println("[DISCONNECTED] Trying reconnection...");
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
  String function = payload_doc[1]["function"].as<String>();

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

      Serial.printf("[IOc] get event: %s id: %d\n", payload, id);
      
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
        String ref = doc[1]["ref"].as<String>();
        String value = doc[1]["value"].as<String>();

        if (ref == "restart") rebootDevice();
        else if (ref == "reset") eraseDeviceSettings();

        setIO[ref]["value"] = value;

        if (setIO[ref]["type"].as<String>() == "OUTPUT")
        {
          updatePinOutput(ref);
        }
        
        doc.clear();
        break;
      }
  }
}

void RemoteIO::tryWiFiConnection()
{
  Connected = false;

  if ((_ssid == "") || (_ssid == "null") || (_password == "") || (_password == "null"))
  {
    Serial.println("[tryWiFiConnection] No Wi-Fi network info available...");
    return;
  }

  if (_deviceId != "" && _deviceId != "null")
  {
    String hostname = String("niot-") + String(_deviceId);
    hostname.toLowerCase();
    WiFi.setHostname(hostname.c_str());
  }

  Serial.printf("\n[tryWiFi] Trying connection on %s...\n", _ssid);
  WiFi.begin(_ssid, _password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    if ((start_debounce_time != 0) && (millis() - start_debounce_time >= 2000) && (connection_state == NO_WIFI))
    {
      WiFi.disconnect();
      return;
    }
  }

}

void RemoteIO::nodeIotConnection(void (*userCallbackFunction)(String ref, String value))
{
  if (connection_state == INICIALIZATION || connection_state == NO_WIFI) tryWiFiConnection();
  
  if (WiFi.status() == WL_CONNECTED) 
  {
    Serial.printf("[nodeIotConnection] WiFi connected %s\n", WiFi.localIP().toString().c_str());
  }

  appLastDataUrl.replace(" ", "%20");

  while (state != "accepted")
  {
    if ((start_debounce_time != 0) && (millis() - start_debounce_time >= 2000))
    {
      return;
    }
    tryAuthenticate();
  }
  
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
    else Serial.println("[socketIOConnect] Failed connecting");
  }
}

void RemoteIO::setIOsAndEvents(JsonDocument document)
{
  if (document.containsKey("token")) token = document["token"].as<String>();
  
  if (document.containsKey("serverAddr")) extractIPAddress(document["serverAddr"].as<String>());
  
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
    }
    else if (type == "INPUT_PULLUP")
    {
      setIO[ref]["pin"] = pin;
      setIO[ref]["type"] = type;
      setIO[ref]["mode"] = mode;
      pinMode(pin, INPUT_PULLUP);
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
}

void RemoteIO::tryAuthenticate()
{
  WiFiClientSecure client;
  HTTPClient https;
  StaticJsonDocument<JSON_DOCUMENT_CAPACITY> document;
  String request;
  
  client.setInsecure();

  if (_deviceId != "" && _deviceId != "null") document["deviceId"] = _deviceId;
  document["companyName"] = _companyName;
  document["mac"] = WiFi.macAddress();
  document["ipAddress"] = WiFi.localIP().toString();
  document["deviceModel"] = _model;
  document["version"] = VERSION;

  serializeJson(document, request);

  https.begin(client, appVerifyUrl);
  https.addHeader("Content-Type", "application/json");

  int statusCode = https.POST(request);
  String response = https.getString(); 

  document.clear();
  deserializeJson(document, response);
  Serial.println(response);

  if (statusCode == HTTP_CODE_OK)
  {
    state = document["state"].as<String>();

    if (state != "accepted") 
    {
      document.clear();
      https.end();
      return;
    }

    setIOsAndEvents(document);
  }
  
  document.clear();
  https.end();
}

void RemoteIO::fetchLatestData()
{ 
  WiFiClientSecure client;
  HTTPClient https;

  client.setInsecure();
  
  appLastDataUrl = appBaseUrl + "/devices/getdata";

  https.begin(client, appLastDataUrl);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + token);

  int statusCode = https.GET();

  if (statusCode == HTTP_CODE_OK)
  {
    StaticJsonDocument<JSON_DOCUMENT_CAPACITY> document;
    deserializeJson(document, https.getStream());

    Serial.println("");
    Serial.println("[fetchLatestData] HTTP_CODE 200");
    serializeJson(document, Serial);
    Serial.println("");
    
    for (size_t i = 0; i < document.size(); i++)
    {
      String auxRef = document[i]["ref"].as<String>();
      String auxValue = document[i]["data"][0]["value"].as<String>();

      if (auxValue == "null")
      {
        auxValue = "0";
      }
      
      setIO[auxRef]["value"] = auxValue;
      
      if (setIO[auxRef]["type"].as<String>() == "OUTPUT")
      {
        updatePinOutput(auxRef);
      }
    }
    document.clear();
  }

  https.end();
}

void RemoteIO::extractIPAddress(String url)
{
  int startIndex = url.indexOf("//") + 2; // Encontra o início do endereço IP
  int endIndex = url.indexOf(":", startIndex); // Encontra o fim do endereço IP

  _appHost = url.substring(startIndex, endIndex); // Extrai o endereço IP
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
    }
    else if (typeRef == "INPUT_ANALOG")
    {
      float valueRef = analogRead(pinRef);
      if (connection_state == CONNECTED) espPOST(ref, String(valueRef));
    }
  }
}

void RemoteIO::notFound(AsyncWebServerRequest *request)
{
  request->send(404, "application/json", "{\"message\":\"Not found\"}");
}

int RemoteIO::espPOST(String variable, String value)
{
    return espPOST(appPostData, variable, value);
}

int RemoteIO::espPOST(String Router, String variable, String value)
{
  if ((WiFi.status() == WL_CONNECTED))
  {
    String route = Router;

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
      Serial.println("[espPOST] HTTP_CODE 200");
    }
    else if (httpCode != HTTP_CODE_OK) 
    {
      Serial.printf("[espPOST] HTTP_CODE %i\n", httpCode);
    }
    document.clear();
    https.end();
    return httpCode;
  }
  return 0;
}
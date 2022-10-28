#include <FS.h>                   
#include <WiFiManager.h>
#include <SPIFFS.h>

#include <ArduinoJson.h> 
#include <LoRa.h>
#include <WiFi.h>
#include <PubSubClient.h>

char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_user[34] = "username";
char mqtt_pass[34] = "password";
char ss[3] = "18";
char rst[3] = "14";
char dio[3] = "26";
char syncw[6] = "0x34";

bool shouldSaveConfig = false;

bool shouldReadSaved = true;

void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

WiFiManager wfm;
WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);
  while (!Serial);

  WiFiManagerParameter mqttip("server", "MQTT Broker", "192.168.178.1", 50);
  WiFiManagerParameter mqttuser("port", "MQTT User", "mqttuser", 50);
  WiFiManagerParameter mqttpass("user", "MQTT Password", "password", 50);
  WiFiManagerParameter mqttport("pass", "MQTT Port", "1883", 6);
  WiFiManagerParameter pinss("ss", "LoRa SS Pin", "18", 2);
  WiFiManagerParameter pinrst("rst", "LoRa RST Pin", "14", 2);
  WiFiManagerParameter pindio("di0", "LoRa DI0 Pin", "26", 2);
  WiFiManagerParameter syncword("syncw", "LoRa Sync Word", "0x34", 5);

  Serial.println("mounting FS...");

  if (SPIFFS.begin(true)) {
    Serial.println("mounted file system");
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

 #if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
#else
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
#endif
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_broker"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_pass, json["mqtt_password"]);
          strcpy(ss, json["ss"]);
          strcpy(rst, json["rst"]);
          strcpy(dio, json["dio"]);
          strcpy(syncw, json["syncw"]);
          shouldReadSaved = false;
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  wfm.setDebugOutput(true);
  wfm.setSaveConfigCallback(saveConfigCallback);
  
  // Add custom parameter
  wfm.addParameter(&mqttip);
  wfm.addParameter(&mqttport);
  wfm.addParameter(&mqttuser);
  wfm.addParameter(&mqttpass);
  wfm.addParameter(&pinss);
  wfm.addParameter(&pinrst);
  wfm.addParameter(&pindio);
  wfm.addParameter(&syncword);

  if (!wfm.autoConnect("LoRaHub")) {
    // Did not connect, print error message
    Serial.println("failed to connect and hit timeout");

    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }
  
  if(shouldReadSaved){
    strcpy(mqtt_server, mqttip.getValue());
    strcpy(mqtt_port, mqttport.getValue());
    strcpy(mqtt_user, mqttuser.getValue());
    strcpy(mqtt_pass, mqttpass.getValue());
    strcpy(ss, pinss.getValue());
    strcpy(rst, pinrst.getValue());
    strcpy(dio, pindio.getValue());
    strcpy(syncw, syncword.getValue());
  }
  
  if (shouldSaveConfig) {
    Serial.println("saving config");
 #if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
    DynamicJsonDocument json(1024);
  #else
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
  #endif
    json["mqtt_broker"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_password"] = mqtt_pass;
    json["ss"] = ss;
    json["rst"] = rst;
    json["dio"] = dio;
    json["syncw"] = syncw;
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

  #if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
    serializeJson(json, Serial);
    serializeJson(json, configFile);
  #else
    json.printTo(Serial);
    json.printTo(configFile);
  #endif
    configFile.close();
    //end save
  }
  
  int port = String(mqtt_port).toInt();
  int ssi = String(ss).toInt();
  int rsti = String(rst).toInt();
  int dioi = String(dio).toInt();
  
  client.setServer(mqtt_server, port);
  Serial.println(String(ss) + String(rst) + String(dio) + String(syncw));
  
  Serial.println("LoRa Receiver");
  LoRa.setPins(ssi, rsti, dioi);
  LoRa.setSignalBandwidth(250E3);
  while (!LoRa.begin(866E6)) {
        Serial.println(".");
        delay(500);
        }
        
  LoRa.setSyncWord((int) strtol(syncw, 0, 16));
  LoRa.receive(); 
}

void loop() {
 int count=0;
 while (!client.connected()) {
     String client_id = "LoraHub";
     client_id += String(WiFi.macAddress());
     if (client.connect(client_id.c_str(), mqtt_user, mqtt_pass)) {
         Serial.println("Broker connected");
     } else {
         if(count++ < 10){
         Serial.print("failed with state ");
         Serial.println(client.state());
         delay(5000);
         } else {          
            wfm.resetSettings();
            Serial.println("Settings deleted, REBOOT");
            ESP.restart();
         }
     }
  }
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    while (LoRa.available()) {
      String LoRaData = LoRa.readString();
      int rssi = LoRa.packetRssi();
      if (rssi < 70){
         String topic = getValues(LoRaData, '?', 0);
         String payload = getValues(LoRaData, '?', 1);
         uint16_t topic_length = topic.length() + 1;
         char topic_value_char[topic_length];
         topic.toCharArray(topic_value_char, topic_length);
         uint16_t pay_length = payload.length() - 1;
         char pay_value_char[pay_length];
         payload.toCharArray(pay_value_char, pay_length);
         client.loop();
         client.publish(topic_value_char, pay_value_char, true);
      }
    }
    Serial.println(LoRa.packetRssi());
  }
  
}

String getValues(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

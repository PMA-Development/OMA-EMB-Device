#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ArduinoMqttClient.h>
#include <ArduinoJson.h>
#include <Effortless_SPIFFS.h>

// ######## WI-Fi Manager ########
#define TRIGGER_PIN 0 // Button to trigger ConfigPortal
#define SSID_NAME "SensorAP-2"
#define SSID_PWD "password"
#define CLOSE_PORTAL_AFTER 30 // How long the portal should stay open.

// ######## Configuration ########
#define JSON_CONFIG_FILE "/config.json"

// ######## MQTT ########
#define MQTT_TOPIC_PING "device/outbound/ping"
#define MQTT_TOPIC_BEACON "device/inbound/beacon"
#define MQTT_TOPIC_DEVICE_ROOT "device/outbound/"

// ######## State LEDS ########
#define LED_OFF 12
#define LED_ON 14
#define LED_SERVICE_MODE BUILTIN_LED

// off = 0, on = 1, serviceMode = 2
int sensorState = 0;

int telemetryDelay = 2000; // milli Seconds
unsigned int lastTelemetryUpdate = 0;

// Config Related
eSPIFFS fileSystem;
bool shouldSaveConfig = false;

// wifimanager can run in a blocking mode or a non blocking mode
// Be sure to know how to process loops with no delay() if using non blocking
bool wm_nonblocking = false; // change to true to use non blocking

WiFiManager wm; // global wm instance
WiFiManagerParameter field_mqtt_host; // global param ( for non blocking w params )
WiFiManagerParameter field_mqtt_port; // global param ( for non blocking w params )
WiFiManagerParameter field_mqtt_client_id; // global param ( for non blocking w params )
WiFiManagerParameter field_mqtt_username; // global param ( for non blocking w params )
WiFiManagerParameter field_mqtt_pwd; // global param ( for non blocking w params )

// Network Related
byte mac[6];
String macAddressStr = "";

// MQTT related
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
int publishTelemetryDelay = 5000; // milli Seconds
unsigned int lastTelemetryPublish = 0;
String mqttTopicDevice = "";

String mqttHost             = "";
int    mqttPort             = 1883;
String mqttTelemetryTopic   = "telemetry";
String mqttClientId         = "";
String mqttUsername         = "";
String mqttPassword         = "";

const int analogVoltagePin = A0;  // ESP8266 Analog Pin ADC0 = A0
int sensorValue = 0;  // value read from the pot
float outputValue = 0;  // value to output to a PWM pin


float readDCVoltage()
{
  float value = float(map(sensorValue, 0, 1024, 0, 3000))/ 1000.0;
  // Fix unstable reading, because resistor does not stabilize the reading.
  if (value < 0.1) {
    return 0.0;
  }
  return value;
}

void saveConfigFile() {
  Serial.println("Saving Config file...");

  StaticJsonDocument<512> jsonConfig;
  jsonConfig["mqttHost"] = mqttHost;
  jsonConfig["mqttPort"] = mqttPort;
  jsonConfig["mqttClientId"] = mqttClientId;
  jsonConfig["mqttUsername"] = mqttUsername;
  jsonConfig["mqttPassword"] = mqttPassword;
  jsonConfig["sensorState"] = sensorState;
  jsonConfig["telemetryDelay"] = telemetryDelay;

  String jsonData = "";
  serializeJson(jsonConfig, jsonData);
  serializeJson(jsonConfig, Serial);

  if (fileSystem.saveToFile(JSON_CONFIG_FILE, jsonData)) {
    Serial.println("Successfully wrote data to file");
    shouldSaveConfig = false;
  }
  else {
    Serial.println("Failed to write data to file");
  }
}

bool loadConfigFile()
// Load existing configuration file
{
  // Uncomment if we need to format filesystem
  // SPIFFS.format();
 
  // Read configuration from FS json
  Serial.println("Mounting File System...");

  String jsonData = "";
  if (fileSystem.openFromFile(JSON_CONFIG_FILE, jsonData)) {
    StaticJsonDocument<512> json;
    DeserializationError error = deserializeJson(json, jsonData);
    serializeJsonPretty(json, Serial);
    if (!error)
    {
      Serial.println("Parsing JSON");

      char host[20] = "";
      strcpy(host, json["mqttHost"]);
      mqttHost = String(host);
      mqttPort = json["mqttPort"].as<int>();
      char clientId[20] = "";
      strcpy(clientId, json["mqttClientId"]);
      mqttClientId = String(clientId);
      char username[20] = "";
      strcpy(username, json["mqttUsername"]);
      mqttUsername = String(username);
      char password[20] = "";
      strcpy(password, json["mqttPassword"]);
      mqttPassword = String(password);
      sensorState = json["sensorState"];
      telemetryDelay = json["telemetryDelay"];

      // avoid reading lower then 1 sec.
      if (telemetryDelay < 1000) {
        telemetryDelay = 1000;
      }

      return true;
    }
    else
    {
      // Error loading JSON data
      Serial.println("Failed to load json config");
    }
  }
 
  return false;
}

void saveConfigCallback()
// Callback notifying us of the need to save configuration
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setupLEDs() {
  pinMode(LED_OFF, OUTPUT);
  pinMode(LED_ON, OUTPUT);
  pinMode(LED_SERVICE_MODE, OUTPUT);
}

void getMacAddress() {
  WiFi.macAddress(mac);
  macAddressStr += String(mac[5], HEX) + ":";
  macAddressStr += String(mac[4], HEX) + ":";
  macAddressStr += String(mac[3], HEX) + ":";
  macAddressStr += String(mac[2], HEX) + ":";
  macAddressStr += String(mac[1], HEX) + ":";
  macAddressStr += String(mac[0], HEX);
  Serial.println("MAC: " + macAddressStr);
}


void setup() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP  
  Serial.begin(115200);
  Serial.setDebugOutput(true);  
  delay(3000);
  Serial.println("\n Starting");

  // displaySetup();
  setupLEDs();

  // Change to true when testing to force configuration every time we run
  bool forceConfig = false;

  bool spiffsSetup = loadConfigFile();
  if (!spiffsSetup)
  {
    Serial.println(F("Forcing config mode as there is no saved config"));
    forceConfig = true;
  }

  pinMode(TRIGGER_PIN, INPUT);
  
  // wm.resetSettings(); // wipe settings

  // Set config save notify callback
  wm.setSaveConfigCallback(saveConfigCallback);

  if(wm_nonblocking) wm.setConfigPortalBlocking(false);

  // add a custom input field
  int customFieldLength = 40;


  new (&field_mqtt_host) WiFiManagerParameter("field_mqtt_host", "MQTT Host", "", customFieldLength,"placeholder=\"\"");
  wm.addParameter(&field_mqtt_host);
  new (&field_mqtt_port) WiFiManagerParameter("field_mqtt_port", "MQTT Port", "1883", customFieldLength,"placeholder=\"\"");
  wm.addParameter(&field_mqtt_port);
  new (&field_mqtt_client_id) WiFiManagerParameter("field_mqtt_client_id", "MQTT Client ID", "", customFieldLength,"placeholder=\"\"");
  wm.addParameter(&field_mqtt_client_id);
  new (&field_mqtt_username) WiFiManagerParameter("field_mqtt_username", "MQTT Username", "", customFieldLength,"placeholder=\"\"");
  wm.addParameter(&field_mqtt_username);
  new (&field_mqtt_pwd) WiFiManagerParameter("field_mqtt_pwd", "MQTT Password", "", customFieldLength,"placeholder=\"\"");
  wm.addParameter(&field_mqtt_pwd);

  wm.setSaveParamsCallback(saveParamCallback);

  // custom menu via array or vector
  // 
  // menu tokens, "wifi","wifinoscan","info","param","close","sep","erase","restart","exit" (sep is seperator) (if param is in menu, params will not show up in wifi page!)
  // const char* menu[] = {"wifi","info","param","sep","restart","exit"}; 
  // wm.setMenu(menu,6);
  std::vector<const char *> menu = {"wifi","info","param","sep","restart","exit"};
  wm.setMenu(menu);

  // set dark theme
  wm.setClass("invert");

  wm.setConfigPortalTimeout(CLOSE_PORTAL_AFTER); // auto close configportal after n seconds
  wm.setAPClientCheck(true); // avoid timeout if client connected to softap

  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  // res = wm.autoConnect("AutoConnectAP"); // anonymous ap
  res = wm.autoConnect(SSID_NAME,SSID_PWD); // password protected ap

  if(!res) {
    Serial.println("Failed to connect or hit timeout");
    ESP.restart();
  } 
  else {
    //if you get here you have connected to the WiFi    
    Serial.println("connected to Wi-Fi... :)");
  }

  Serial.println("WiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.print("Custom Field MQTT Host: ");
  Serial.println(mqttHost);

  Serial.print("Custom Field MQTT Port: ");
  Serial.println(mqttPort);

  Serial.print("Custom Field MQTT Client ID: ");
  Serial.println(mqttClientId);

  Serial.print("Custom Field MQTT Username: ");
  Serial.println(mqttUsername);

  Serial.print("Custom Field MQTT Password: ");
  Serial.println(mqttPassword);

  // Save the custom parameters to FS
  if (shouldSaveConfig)
  {
    saveConfigFile();
  }

  updateLEDs();

  getMacAddress();
  if (mqttClientId.length() == 0) {
    mqttClientId = macAddressStr;
  }
}

void updateLEDs() {
  if (sensorState == 0) {
    digitalWrite(LED_OFF, HIGH);
    digitalWrite(LED_ON, LOW);
    digitalWrite(LED_SERVICE_MODE, LOW);
  }
  else if (sensorState == 1) {
    digitalWrite(LED_ON, HIGH);
    digitalWrite(LED_OFF, LOW);
    digitalWrite(LED_SERVICE_MODE, LOW);
  }
  else if (sensorState == 2) {
    digitalWrite(LED_SERVICE_MODE, HIGH);
    digitalWrite(LED_OFF, LOW);
    digitalWrite(LED_ON, LOW);
  }
}

void readTelemetry() {
  if ((millis() - lastTelemetryUpdate) > telemetryDelay) {
    lastTelemetryUpdate = millis();
    sensorValue = analogRead(analogVoltagePin);
    // map it to the range of the PWM out
    outputValue = readDCVoltage();
    
    // print the readings in the Serial Monitor
    Serial.print("sensor = ");
    Serial.print(sensorValue);
    Serial.print("\t output = ");
    Serial.println(outputValue);

    if (!mqttClient.connected()) {
        connectToBroker();
        return;
      }
      else {
      }
      // Publish Telemetry data
      publishMqttTelemetry();
  }
}

void checkButton(){
  // check for button press
  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    // poor mans debounce/press-hold, code not ideal for production
    delay(50);
    if( digitalRead(TRIGGER_PIN) == LOW ){
      Serial.println("Button Pressed");
      // still holding button for 3000 ms, reset settings, code not ideaa for production
      delay(3000); // reset delay hold
      if( digitalRead(TRIGGER_PIN) == LOW ){
        Serial.println("Button Held");
        Serial.println("Erasing Config, restarting");
        wm.resetSettings();
        ESP.restart();
      }
      
      // displaySetup();
      // start portal w delay
      Serial.println("Starting config portal");
      wm.setConfigPortalTimeout(120);
      
      if (!wm.startConfigPortal(SSID_NAME,SSID_PWD)) {
        Serial.println("failed to connect or hit timeout");
        delay(3000);
        // ESP.restart();
      } else {
        //if you get here you have connected to the WiFi
        Serial.println("connected...yeey :)");
      }
    }
  }
}


String getParam(String name){
  //read parameter from server, for customhmtl input
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}


bool connectToBroker() {
  if (mqttClientId.length() == 0) {
    Serial.println("ClientId missing, cannot connect to broker...");
    return false;
  }
  mqttClient.setId(mqttClientId);
  mqttClient.setUsernamePassword(mqttUsername, mqttPassword);

  if (!mqttClient.connect(mqttHost.c_str(), mqttPort)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());
    return false;
  }
  // set the message receive callback
  mqttClient.onMessage(onMqttMessage);

  Serial.print("Subscribing to topic: ");
  Serial.println(MQTT_TOPIC_PING);
  mqttClient.subscribe(MQTT_TOPIC_PING);

  mqttTopicDevice = String(MQTT_TOPIC_DEVICE_ROOT) + mqttClientId + "/";
  Serial.print("Subscribing to topic: ");
  Serial.println(mqttTopicDevice + "#");
  mqttClient.subscribe(mqttTopicDevice + "#");

  mqttPublishBeacon();

  return true;
}

void saveParamCallback(){
  mqttHost = field_mqtt_host.getValue();
  String _portStr = field_mqtt_port.getValue();
  mqttPort = _portStr.toInt();
  mqttClientId = field_mqtt_client_id.getValue();
  mqttUsername = field_mqtt_username.getValue();
  mqttPassword = field_mqtt_pwd.getValue();
  if (mqttClientId.length() == 0) {
    mqttClientId = macAddressStr;
  }
  connectToBroker();
  // updateDisplay();
}

void publishMqttTelemetry(){
  if (!mqttClient.connected()) {
    if (!connectToBroker()) {
      return;
    }
  }
  StaticJsonDocument<200> doc;

  doc["Id"] = macAddressStr;
  doc["Type"] = "Generator";

  JsonArray jsonAttributeArr = doc.createNestedArray("Attributes");
  JsonObject jsonAttribute_0 = jsonAttributeArr.createNestedObject();
  jsonAttribute_0["Name"] = "Voltage";
  jsonAttribute_0["Value"] = outputValue;

  mqttClient.beginMessage(mqttTelemetryTopic);
  serializeJson(doc, mqttClient);
  serializeJson(doc, Serial);
  mqttClient.endMessage();
  Serial.println();
}

void mqttPublishBeacon() {
  StaticJsonDocument<300> doc;
  doc["Id"] = mqttClientId;
  doc["Type"] = "Sensor";
  doc["CollectionInterval"] = telemetryDelay;
  if (sensorState == 0) {
    doc["State"] = "Off";
  }
  else if (sensorState == 1) {
    doc["State"] = "On";
  }
  else if (sensorState == 2) {
    doc["State"] = "ServiceMode";
  }

  mqttClient.beginMessage(MQTT_TOPIC_BEACON);
  serializeJson(doc, mqttClient);
  serializeJson(doc, Serial);
  mqttClient.endMessage();
  Serial.println();
}

void onMqttMessage(int messageSize) {
  // we received a message, print out the topic and contents
  String topic = mqttClient.messageTopic();
  Serial.println("Received a message with topic '");
  Serial.println(topic);

  String msg_message = mqttClient.readString();
  Serial.println(msg_message);

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, msg_message);
  if (!error){
    if (topic == MQTT_TOPIC_PING) {
      mqttPublishBeacon();
    }
    else if (topic == mqttTopicDevice + "settings") {
      if (doc.containsKey("CollectionInterval")) {
        telemetryDelay = doc["CollectionInterval"].as<int>() * 1000; // Convert sec to millies.
        shouldSaveConfig = true; // Save to config file;
      }
      else {
        Serial.println("Json does not contain key: CollectionInterval");
      }
    }
    else if (topic == mqttTopicDevice + "changestate") {
      if (doc.containsKey("Value")) {
        String state = doc["Value"];
        if (state == "Off") {
          sensorState = 0;
        }
        else if (state == "On") {
          sensorState = 1;
        }
        if (state == "ServiceMode") {
          sensorState = 2;
        }
        updateLEDs();
        shouldSaveConfig = true; // Save to config file;
      }
      else {
        Serial.println("Json does not contain key: Value");
      }
    }
    else {
      Serial.println("No topics matched!");
    }
  }
  else {
    // Error loading JSON data
    Serial.print("deserializeJson() failed with code ");
    Serial.println(error.c_str());
  }
  Serial.println();
  Serial.println();
}

void mqttHandler() {
  // call poll() regularly to allow the library to send MQTT keep alive which
  // avoids being disconnected by the broker
  mqttClient.poll();
}

void loop() {
  mqttHandler();
  if(wm_nonblocking) wm.process(); // avoid delays() in loop when non-blocking and other long running code  
  checkButton();
  // put your main code here, to run repeatedly:

  readTelemetry();
  if (shouldSaveConfig)
  {
    saveConfigFile();
  }
}

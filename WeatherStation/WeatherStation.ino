#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <DHT.h>
#include <ArduinoMqttClient.h>
#include <ArduinoJson.h>
#include <Effortless_SPIFFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ######## OLED ########
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)

// ######## DHT ########
#define DHT_SENSOR_PIN  13 // The ESP8266 pin D7 connected to DHT22 sensor
#define DHT_SENSOR_TYPE DHT22

int getOffset(char str[],int *width,int *textSize);

// ######## WI-Fi Manager ########
#define TRIGGER_PIN 0 // Button to trigger ConfigPortal
#define SSID_NAME "SensorAP-1"
#define SSID_PWD "password"
#define CLOSE_PORTAL_AFTER 30 // How long the portal should stay open.

// ######## Configuration ########
#define JSON_CONFIG_FILE "/config.json"

// ######## State LEDS ########
#define LED_OFF 12
#define LED_ON 14
#define LED_SERVICE_MODE BUILTIN_LED

// ######## MQTT ########
#define MQTT_TOPIC_PING "device/outbound/ping"
#define MQTT_TOPIC_BEACON "device/inbound/beacon"
#define MQTT_TOPIC_DEVICE_ROOT "device/outbound/"

// off = 0, on = 1, serviceMode = 2
int sensorState = 0;

void getMacAddress();
void checkButton();
void mqttHandler();
void readTelemetry();
void publishMqttTelemetry();
void saveParamCallback();
bool connectToBroker();
String getParam(String name);

// Config Related
eSPIFFS fileSystem;
bool shouldSaveConfig = false;

// Network Related
byte mac[6];
String macAddressStr = "";


// wifimanager can run in a blocking mode or a non blocking mode
// Be sure to know how to process loops with no delay() if using non blocking
bool wm_nonblocking = false; // change to true to use non blocking

WiFiManager wm; // global wm instance
WiFiManagerParameter field_mqtt_host; // global param ( for non blocking w params )
WiFiManagerParameter field_mqtt_port; // global param ( for non blocking w params )
WiFiManagerParameter field_mqtt_client_id; // global param ( for non blocking w params )
WiFiManagerParameter field_mqtt_username; // global param ( for non blocking w params )
WiFiManagerParameter field_mqtt_pwd; // global param ( for non blocking w params )

// DHT Related
DHT dht_sensor(DHT_SENSOR_PIN, DHT_SENSOR_TYPE);
int updateDHTDelay = 2000; // milli Seconds
unsigned int lastDHTUpdate = 0;
int tempC;
int humidity;


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

// OLED Related
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void setupLEDs() {
  pinMode(LED_OFF, OUTPUT);
  pinMode(LED_ON, OUTPUT);
  pinMode(LED_SERVICE_MODE, OUTPUT);
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
  jsonConfig["updateDHTDelay"] = updateDHTDelay;

  String jsonData = "";
  serializeJson(jsonConfig, jsonData);
  serializeJson(jsonConfig, Serial);

  if (fileSystem.saveToFile(JSON_CONFIG_FILE, jsonData)) {
    Serial.println("Successfully wrote data to file");
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
      updateDHTDelay = json["updateDHTDelay"];

      // avoid reading lower then 1 sec.
      if (updateDHTDelay < 1000) {
        updateDHTDelay = 1000;
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

void displaySetup() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    return;
  }

  display.clearDisplay();

  char str[] = "";
  sprintf(str, "Setup...");
  displayTextCenter(str, 0, 1, 1);

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 18);
  // Display static text
  display.println(mqttHost + ":" + mqttPort);
  display.setCursor(0, 28);
  display.println(mqttClientId);
  display.setCursor(0, 38);
  display.println("SSID:" + String(SSID_NAME));
  display.setCursor(0, 48);
  display.println("PWD:" + String(SSID_PWD));
  display.display();
}

void setup() {


  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP  
  Serial.begin(115200);
  Serial.setDebugOutput(true);  
  delay(3000);
  Serial.println("\n Starting");

  displaySetup();
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

  dht_sensor.begin(); // initialize the DHT sensor

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
      
      displaySetup();
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
  updateDisplay();
}

void publishMqttTelemetry(){
  if (!mqttClient.connected()) {
    if (!connectToBroker()) {
      return;
    }
  }
  StaticJsonDocument<200> doc;

  doc["Id"] = macAddressStr;
  doc["Type"] = "Weather";

  JsonArray jsonAttributeArr = doc.createNestedArray("Attributes");
  JsonObject jsonAttribute_0 = jsonAttributeArr.createNestedObject();
  jsonAttribute_0["Name"] = "Temperature";
  jsonAttribute_0["Value"] = tempC;

  JsonObject jsonAttribute_1 = jsonAttributeArr.createNestedObject();
  jsonAttribute_1["Name"] = "Humidity";
  jsonAttribute_1["Value"] = humidity;

  mqttClient.beginMessage(mqttTelemetryTopic);
  serializeJson(doc, mqttClient);
  serializeJson(doc, Serial);
  mqttClient.endMessage();
  Serial.println();
}

void readTelemetry() {
  if ((millis() - lastDHTUpdate) > updateDHTDelay) {
    lastDHTUpdate = millis();
    // read humidity
    humidity  = dht_sensor.readHumidity();
    // read temperature in Celsius
    tempC = dht_sensor.readTemperature();
    // read temperature in Fahrenheit
    float temperature_F = dht_sensor.readTemperature(true);

    // check whether the reading is successful or not
    if ( isnan(tempC) || isnan(temperature_F) || isnan(humidity)) {
      Serial.println("Failed to read from DHT sensor!");
    } else {
      Serial.print("Humidity: ");
      Serial.print(humidity);
      Serial.print("%");

      Serial.print("  |  ");

      Serial.print("Temperature: ");
      Serial.print(tempC);
      Serial.print("°C  ~  ");
      Serial.print(temperature_F);
      Serial.println("°F");

      updateDisplay();

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
}

void mqttHandler() {
  // call poll() regularly to allow the library to send MQTT keep alive which
  // avoids being disconnected by the broker
  mqttClient.poll();
}

void updateDisplay() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    return;
  }

  display.clearDisplay();

  char str[] = "";
  if (sensorState == 0) {
    sprintf(str, "State: Off");
  }
  else if (sensorState == 1) {
    sprintf(str, "State: On");
  }
  else if (sensorState == 2) {
    sprintf(str, "State: Service");
  }
  displayTextCenter(str, 0, 1, 1);

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 18);
  // Display static text
  display.println(mqttHost + ":" + mqttPort);
  display.setCursor(0, 28);
  display.println(mqttClientId);
  display.setCursor(0, 38);
  display.println("Humidity: " + String(humidity) + "%");
  display.setCursor(0, 48);
  display.println("Temperature: " + String(tempC) + "C");
  display.display();
}

int getOffset(char str[], int *width, int *textSize){
  // We fist calculate the midle of the area
  // then we take the text size and multiply the size by 6, because 1 = 6 pixels
  // when we have pixel by char, the we multiply the string length with the pixels
  // then we divide by 2, to get the middle of the word, and then we extract that
  // from the screen area, we extract the string middle, and get the offset.
  return ((SCREEN_WIDTH - *width) / 2 ) - (strlen(str)*(6*(*textSize)) / 2);
}

void displayTextCenter(char str[], int x, int y, int textSize){
  display.setTextSize(textSize); // Normal 1:1 pixel scale
	display.setTextColor(WHITE); // Draw white text
  int offset = getOffset(str, &x, &textSize);
	display.setCursor(offset,y); // Start at top-left corner
	display.print(str);
}

void mqttPublishBeacon() {
  StaticJsonDocument<300> doc;
  doc["CliendId"] = mqttClientId;
  doc["Type"] = "Sensor";
  doc["CollectionInterval"] = updateDHTDelay;
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
        updateDHTDelay = doc["CollectionInterval"].as<int>() * 1000; // Convert sec to millies.
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

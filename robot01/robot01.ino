
/*
* Hardware
*/
#include <Wire.h>
#include <VL53L1X.h>
#include "wrapper_bno08x.h"
#include "roboFace.h"
#include "bodyControl.h"
#include <ESP_I2S.h>

/*
* Webserver
*/
#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>

// http client
#include <ArduinoHttpClient.h>

#define FILESYSTEM LittleFS
// AsyncFsWebServer WebServer instance
AsyncFsWebServer WebServer(80, FILESYSTEM, "robot01 server");
bool apMode = false;

/*
* Robot name
*/
String ROBOT_NAME = "Sappie";

/*
* I2S - UDP
*/
#include "AsyncUDP.h"
AsyncUDP udp;
#define AUDIO_UDP_PORT 9000       // UDP audio over I2S

uint8_t *i2sbuffer = new uint8_t [1500]; // A little over packet length to buffer

#define robot01_I2S_BCK 16
#define robot01_I2S_WS 17
#define robot01_I2S_OUT 21

bool audioStreamRunning = false;  // If Audio is listening on udp
I2SClass I2S;

#define MAX_VOLUME 6
// Set divider for I2S output multiplier
int i2sGainMultiplier = 2;

/*
* Sensors assignments
*/
VL53L1X vl53l1x_sensor;
wrapper_bno08x wrapper_bno08x;

/*
* LED Display
*/
Adafruit_SSD1306 ledMatrix(128, 64, &Wire, -1);
/*
* Instance of roboFace, for methods for display actions on LED display
*/
roboFace roboFace;

/*
* Instance of bodyControl for movement actions & hand LEDs
*/
bodyControl bodyControl;

/*
* Task handlers
*/
TaskHandle_t wrapper_bno08xUpdateTaskHandle;
TaskHandle_t AudioStreamTaskHandle;
TaskHandle_t robotRoutineTask;

/*
* Initialization/setup
*/
void setup() {

  // Serial
  Serial.begin(115200);
  Serial.println("Booting robot01\n");

  // Wire init.
  Wire.begin();
  Wire.setClock(400000);  // use 400 kHz I2C

  //Random seed
  randomSeed(analogRead(0));

  roboFace.begin();
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "Boot");

  // XAIO Sense/Camera wake up pin assignment
  pinMode(25, OUTPUT);
  digitalWrite(25, LOW);
  delay(100);

  // Body init.
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "Body");
  bodyControl.begin();
  delay(100);

  // Webserver init.
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "Server");
  Serial.println("Webserver starting");

  startFilesystem();

  // Try to connect to stored SSID, start AP if fails after timeout
  IPAddress myIP = WebServer.startWiFi(15000, "ESP32_robot01", "123456789");

  // HTML server
  WebServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(FILESYSTEM, "/index.html"); });
  WebServer.on("/controls.html", HTTP_GET, webhandler_file );
  WebServer.on("/display.html", HTTP_GET, webhandler_file );
  WebServer.on("/sensors.html", HTTP_GET, webhandler_file );
  // Add request handlers to webserver
  WebServer.on("/VL53L1X_Info", HTTP_GET, webhandler_VL53L1X_Info);
  WebServer.on("/BNO08X_Info", HTTP_GET, webhandler_wrapper_bno08xInfo);
  WebServer.on("/audiostream", HTTP_GET, audiostream_handler);
  WebServer.on("/volume", HTTP_GET, volume_handler);
  WebServer.on("/bodyaction", HTTP_GET, body_action_handler);
  WebServer.on("/displayaction", HTTP_GET, display_action_handler);
  WebServer.on("/wakeupsense", HTTP_GET, wakeupsense_handler);

  String config_master_ip;
  WebServer.addOptionBox("robot01 Options");
  WebServer.addOption("Remote-Master-IP-Address", config_master_ip);

  // Start webserver
  Serial.println("Starting Webserver");
  WebServer.init();
  WebServer.getOptionValue("Remote-Master-IP-Address", config_master_ip);

  Serial.print(F("ESP Web Server started on IP Address: "));
  Serial.println(myIP);
  Serial.println(F("Open /setup page to configure optional parameters"));

  // Enable content editor
  WebServer.enableFsCodeEditor();

  delay(100);

  // Audio / I2S init.
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "AUDIO/I2S");
  I2S.setPins(robot01_I2S_BCK, robot01_I2S_WS, robot01_I2S_OUT, -1, 0); //SCK, WS, SDOUT, SDIN, MCLK

  delay(100);

  // Starting VL53L1X
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "VL53L1X");
  Serial.println("Booting vl53l1x (range) sensor\n");

  vl53l1x_sensor.setTimeout(500);
  if (vl53l1x_sensor.init()) {
    vl53l1x_sensor.setDistanceMode(VL53L1X::Long);
    vl53l1x_sensor.setMeasurementTimingBudget(50000);
    vl53l1x_sensor.startContinuous(50);
  } else {
    Serial.println("Failed to detect and initialize vl53l1x (range) sensor!");
  }
  delay(100);

  // Starting wrapper_bno08x
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "BNO08x");
  Serial.println("Booting wrapper_bno08x (gyro) sensor\n");

  if (!wrapper_bno08x.begin()) {
    Serial.println("Failed to detect and initialize wrapper_bno08x (gyro) sensor!");
  }
  delay(100);

  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "Tasks");

  // Starting tasks
  Serial.println("Tasks starting");

  Serial.println("wrapper_bno08x update task starting");
  xTaskCreatePinnedToCore(bno08xUpdateTask, "wrapper_bno08x", 4096, NULL, 10, &wrapper_bno08xUpdateTaskHandle, 0);

  // Register with remote master
  Serial.printf("Registering with remote master : %s \n", config_master_ip.c_str());
  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Master registration " + WiFi.localIP().toString());

  WiFiClient httpWifiInstance;
  HttpClient http(httpWifiInstance, config_master_ip, 5000);

  String httpPath = "/api/register_ip?ip=" + WiFi.localIP().toString() + "&device=robot01";

  int http_err = http.get(httpPath);
  if (http_err == 0) {
    Serial.println(http.responseBody());
  } else {
    Serial.printf("Got status code: %u \n", http_err);
  }

  // Boot complete
  Serial.println("Boot complete...");
  delay(100);

  String register_string =  "Hello, I am " + ROBOT_NAME + " @ " + WiFi.localIP().toString();
  roboFace.exec(faceAction::SCROLLTEXT,register_string, 100);
  delay(500);
  
  while (roboFace.actionRunning) { delay(100); }
  roboFace.exec(faceAction::DISPLAYIMG, "", 39);
  delay(1000);
  roboFace.exec(faceAction::NEUTRAL);
  vTaskDelete(NULL);
}

/*
* Main task
*/
void loop() {
  delay(10000);
}

////////////////////////////////  Tasks  /////////////////////////////////////////

/*
* bno08x continues updates, executing as vtask
*/
void bno08xUpdateTask(void *pvParameters) {
  for (;;) {
    wrapper_bno08x.update();
    delay(200);
  }
}

////////////////////////////////  Web handlers  /////////////////////////////////////////

/*
* Handler to get webfiles with header
*/
void webhandler_file(AsyncWebServerRequest *request) {
  Serial.println(request->url());
  AsyncWebServerResponse *response = request->beginResponse(FILESYSTEM, request->url() );
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

/*
* Handler to get vl53l1x values
*/
void webhandler_VL53L1X_Info(AsyncWebServerRequest *request) {

  vl53l1x_sensor.read();

  uint16_t range_mm = vl53l1x_sensor.ranging_data.range_mm;
  uint8_t range_status = vl53l1x_sensor.ranging_data.peak_signal_count_rate_MCPS;
  float peak_signal = vl53l1x_sensor.ranging_data.peak_signal_count_rate_MCPS;
  float ambient_count = vl53l1x_sensor.ranging_data.ambient_count_rate_MCPS;

  String json = "{\"range_mm\": " + String(range_mm) + ",\"range_status\": " + String(range_status) + ",\"peak_signal\": " + String(peak_signal)
                + ",\"ambient_count\" : " + String(ambient_count) + " }";


  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

/*
* Handler to get bno08x values
*/
void webhandler_wrapper_bno08xInfo(AsyncWebServerRequest *request) {

  String json = "{\"geoMagRotationVector_real\":" + String(wrapper_bno08x.geoMagRotationVector_real)
                + ",\"geoMagRotationVector_i\":" + String(wrapper_bno08x.geoMagRotationVector_i)
                + ",\"geoMagRotationVector_j\":" + String(wrapper_bno08x.geoMagRotationVector_j)
                + ",\"geoMagRotationVector_k\":" + String(wrapper_bno08x.geoMagRotationVector_k)
                + ",\"accelerometer_x\":" + String(wrapper_bno08x.accelerometer_x)
                + ",\"accelerometer_y\":" + String(wrapper_bno08x.accelerometer_y)
                + ",\"accelerometer_z\":" + String(wrapper_bno08x.accelerometer_z)
                + ",\"gyroscope_x\":" + String(wrapper_bno08x.gyroscope_x)
                + ",\"gyroscope_y\":" + String(wrapper_bno08x.gyroscope_y)
                + ",\"gyroscope_z\":" + String(wrapper_bno08x.gyroscope_z)
                + ",\"stability\":\"" + String(wrapper_bno08x.stability) + "\""
                + ",\"shake\":\"" + String(wrapper_bno08x.shake) + "\""
                + " }";

  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Access-Control-Allow-Origin", "*");

  request->send(response);
}

/*
* Handler to enable/disable audiostream
*/
void audiostream_handler(AsyncWebServerRequest *request) {

  if (request->hasArg("on")) {
    int value = request->arg("on").toInt();
    if (value == 1 && !audioStreamRunning) {
      Serial.println("Audio streaming starting\n");
      startAudio(true);
    }
    if (value == 0 && audioStreamRunning) {
      Serial.println("Audio streaming stopping\n");
      startAudio(false);
    }
  }
  
  String json = "{\"audiostream\": " + String(audioStreamRunning) + " }";

  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Access-Control-Allow-Origin", "*");

  request->send(response);
}

/*
* Handler to adjust output volume
*/
void volume_handler(AsyncWebServerRequest *request) {

  if (request->hasArg("power")) {
    int value = request->arg("power").toInt();
    if (value >= 0 && value <= MAX_VOLUME) {
      i2sGainMultiplier = value;
    } 
  }

  String json = "{\"volume\": " +  String(i2sGainMultiplier) + " }";
  
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Access-Control-Allow-Origin", "*");

  request->send(response);
}

/*
* Handler for body test task
*/
void body_action_handler(AsyncWebServerRequest *request) {

  String json = "{ \"command\" : \"failed\" }";

  if (request->hasArg("action")) {

    int bodyPart = request->arg("action").toInt();

    int direction = 0;
    if (request->hasArg("direction")) { direction = request->arg("direction").toInt(); }

    int duration = 0;
    if (request->hasArg("duration")) { duration = request->arg("duration").toInt(); }

    if (duration < 3000) {
      json = "{ \"command\" : \"succes\" }";
      bodyControl.exec(bodyPart, direction, duration);
    }

  } else {
    json = "{ \"command\" : \"succes\" }";
    xTaskCreatePinnedToCore(bodyTestRoutine, "BodyTestRoutine", 4096, NULL, 5, &robotRoutineTask, 1);
  }

  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

/*
* Handler for display action
*/
void display_action_handler(AsyncWebServerRequest *request) {

  String json = "{ \"command\" : \"failed\" }";

  if (request->hasArg("action")) {
    int index = request->hasArg("index") ? request->arg("index").toInt() : 0;
    String text = request->hasArg("text") ? request->arg("text") : "";

    roboFace.exec(request->arg("action").toInt(), text, index);

    json = "{ \"command\" : \"succes\" }";
  }

  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

/*
* Handler for waking up XAIO sense/camera from sleep
*/
void wakeupsense_handler(AsyncWebServerRequest *request) {

  String json = "{ \"command\" : \"succes\" }";

  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);

  digitalWrite(25, HIGH);
  delay(200);
  digitalWrite(25, LOW);
}

////////////////////////////////  Services  /////////////////////////////////////////

/*
* UDP Audio listening callback
*/
IRAM_ATTR void udpRXCallBack(AsyncUDPPacket &packet) {

  const uint8_t *packet_data = (const uint8_t *)packet.data();
  const size_t packet_length = packet.length();
  float sample;
  uint8_t *f_ptr = (uint8_t *)&sample;
  uint32_t i2sValue;

  for (size_t i = 0; i <= packet_length; i = i + 4) {

    f_ptr[0] = packet_data[i + 0];
    f_ptr[1] = packet_data[i + 1];
    f_ptr[2] = packet_data[i + 2];
    f_ptr[3] = packet_data[i + 3];
    
    i2sValue = static_cast<int32_t>((sample * 32767.0f) * i2sGainMultiplier);

    i2sbuffer[i] = static_cast<uint8_t>(i2sValue & 0xFF);
    i2sbuffer[i + 1] = static_cast<uint8_t>((i2sValue >> 8) & 0xFF);
    i2sbuffer[i + 2] = static_cast<uint8_t>((i2sValue >> 16) & 0xFF);
    i2sbuffer[i + 3] = static_cast<uint8_t>((i2sValue >> 24) & 0xFF);

  };

  I2S.write((uint8_t *)i2sbuffer, packet_length);
};

/*
* Start / Stop Audio UDP listening
*/
void startAudio(bool start) {

  if (start) {

    if (udp.listen(AUDIO_UDP_PORT)) {
      
      I2S.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_LEFT);

      Serial.print("UDP Listening on IP: ");
      Serial.println(WiFi.localIP());

      udp.onPacket(udpRXCallBack);

      audioStreamRunning = true;

    }
  } else {
    Serial.print("UDP stopped Listening");
    udp.flush();
    delay(100);
    udp.close();
    delay(500);
    I2S.end();

    audioStreamRunning = false;
  }
}

////////////////////////////////  Robot Functions  /////////////////////////////////////////

/*
* Body test Routine
*/
void bodyTestRoutine(void *pVParameters) {

  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "LEFT UPPER ARM");
  bodyControl.exec(bodyAction::LEFTUPPERARM, true, 1000);
  delay(1000);
  bodyControl.exec(bodyAction::LEFTUPPERARM, false, 1000);
  delay(1000);
  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "LEFT LOWER ARM");
  bodyControl.exec(bodyAction::LEFTLOWERARM, true, 1000);
  delay(1000);
  bodyControl.exec(bodyAction::LEFTLOWERARM, false, 1000);
  delay(1000);

  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "RIGHT UPPER ARM");
  bodyControl.exec(bodyAction::RIGHTUPPERARM, true, 1000);
  delay(1000);
  bodyControl.exec(bodyAction::RIGHTUPPERARM, false, 1000);
  delay(1000);

  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "RIGHT LOWER ARM");
  bodyControl.exec(bodyAction::RIGHTLOWERARM, true, 1000);
  delay(1000);
  bodyControl.exec(bodyAction::RIGHTLOWERARM, false, 1000);
  delay(1000);

  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "LEFT LEG");
  bodyControl.exec(bodyAction::LEFTLEG, true, 1000);
  delay(1000);
  bodyControl.exec(bodyAction::LEFTLEG, false, 1000);
  delay(1000);

  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "RIGHT LEG");
  bodyControl.exec(bodyAction::RIGHTLEG, true, 1000);
  delay(1000);
  bodyControl.exec(bodyAction::RIGHTLEG, false, 1000);
  delay(1000);

  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "HIP LEFT");
  bodyControl.exec(bodyAction::HIP, true, 1000);
  delay(1000);
  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "HIP RIGHT");
  bodyControl.exec(bodyAction::HIP, false, 1000);
  delay(1000);

  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "LEFT HAND LIGHT");
  for (int8_t x = 0; x <= 6; x++) {
    bodyControl.exec(bodyAction::LEFTHANDLIGHT, true, 150);
    delay(200);
    bodyControl.exec(bodyAction::LEFTHANDLIGHT, false, 150);
    delay(200);
  }

  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "RIGHT HAND LIGHT");
  for (int8_t x = 0; x <= 6; x++) {
    bodyControl.exec(bodyAction::RIGHTHANDLIGHT, true, 150);
    delay(200);
    bodyControl.exec(bodyAction::RIGHTHANDLIGHT, false, 150);
    delay(200);
  }

  roboFace.exec(faceAction::NEUTRAL);

  vTaskDelete(NULL);
};

////////////////////////////////  Support Functions  /////////////////////////////////////////

/*
* Filesystem for webserver
*/
void startFilesystem() {
  // FILESYSTEM INIT
  if (!FILESYSTEM.begin()) {
    Serial.println("ERROR on mounting filesystem. It will be formmatted!");
    FILESYSTEM.format();
    ESP.restart();
  }
}

/*
* Getting FS info (total and free bytes) is strictly related to
* filesystem library used (LittleFS, FFat, SPIFFS etc etc) and ESP framework
* ESP8266 FS implementation has methods for total and used bytes (only label is missing)
*/
#ifdef ESP32
void getFsInfo(fsInfo_t *fsInfo) {
  fsInfo->fsName = "LittleFS";
  fsInfo->totalBytes = LittleFS.totalBytes();
  fsInfo->usedBytes = LittleFS.usedBytes();
}
#else
void getFsInfo(fsInfo_t *fsInfo) {
  fsInfo->fsName = "LittleFS";
}
#endif

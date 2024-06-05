/*
* Hardware
*/
#include <Wire.h>
#include <VL53L1X.h>
#include "wrapper_bno08x.h"
#include "roboFace.h"
#include "bodyControl.h"
#include "driver/i2s.h"

/*
* Webserver
*/
#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>

// http client
#include <ArduinoHttpClient.h>

/*
* I2S - UDP
*/
#include "AsyncUDP.h"
AsyncUDP udp;
#define AUDIO_UDP_PORT 9000       // UDP audio over I2S
bool audioStreamRunning = false;  // If Audio is listening on udp

i2s_pin_config_t i2s_pins = {
  .bck_io_num = 16,
  .ws_io_num = 17,
  .data_out_num = 21,
  .data_in_num = I2S_PIN_NO_CHANGE
};

i2s_driver_config_t i2s_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = 16000,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
  .communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 512,
  .use_apll = false,
  .tx_desc_auto_clear = true,
  .bits_per_chan = I2S_BITS_PER_CHAN_16BIT,
};

int i2sPortNo = 0;
// Set divider for I2S output multiplier
int i2sGainDivider = 20;

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

#define FILESYSTEM LittleFS
// AsyncFsWebServer WebServer instance
AsyncFsWebServer WebServer(80, FILESYSTEM, "Sappie server");
bool apMode = false;

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
  Serial.println("Booting Sappie\n");

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
  IPAddress myIP = WebServer.startWiFi(15000, "ESP32_SAPPIE", "123456789");

  // HTML server
  WebServer.on("/", HTTP_ANY, [](AsyncWebServerRequest *request) {
    request->send(FILESYSTEM, "/Index.html");
  });

  // Add request handlers to webserver
  WebServer.on("/VL53L1X_Info", HTTP_GET, webhandler_VL53L1X_Info);
  WebServer.on("/BNO08X_Info", HTTP_GET, webhandler_wrapper_bno08xInfo);
  WebServer.on("/audiostream", HTTP_GET, audiostream_handler);
  WebServer.on("/volume", HTTP_GET, volume_handler);
  WebServer.on("/bodyaction", HTTP_GET, body_action_handler);
  WebServer.on("/displayaction", HTTP_GET, display_action_handler);
  WebServer.on("/wakeupsense", HTTP_GET, wakeupsense_handler);

  WebServer.enableFsCodeEditor();
  String config_master_ip;
  WebServer.addOptionBox("Sappie Options");
  WebServer.addOption("Remote-Master-IP-Address", config_master_ip);
  Serial.println("Starting Webserver");
  // Start webserver
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

  if (i2s_driver_install((i2s_port_t)i2sPortNo, &i2s_config, 0, NULL) != ESP_OK) {
    Serial.print("I2S Failure to initialize");
  }

  i2s_set_pin((i2s_port_t)i2sPortNo, &i2s_pins);

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
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "Master registration " + WiFi.localIP().toString());

  WiFiClient httpWifiInstance;
  HttpClient http(httpWifiInstance, config_master_ip, 5000);

  String httpPath = "/api/register_sappie?ip=" + WiFi.localIP().toString();

  int http_err = http.get(httpPath);
  if (http_err == 0) {
    Serial.println(http.responseBody());
  } else {
    Serial.printf("Got status code: %u \n", http_err);
  }

  // Boot complete
  Serial.println("Boot complete...");
  delay(100);
  roboFace.exec(faceAction::SCROLLTEXT, "Hello, I am Sappie @ " + WiFi.localIP().toString(), 100);
  delay(500);
  while (roboFace.actionRunning) { delay(100); }
  roboFace.exec(faceAction::DISPLAYIMG, "", 39);

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

  String json = "{ \"command\" : \"failed\" }";

  if (request->hasArg("on")) {
    int value = request->arg("on").toInt();
    if (value == 1 && !audioStreamRunning) {
      Serial.println("Audio streaming starting\n");
      startAudio(true);
      json = "{\"audiostream\": 1}";
    }
    if (value == 0 && audioStreamRunning) {
      Serial.println("Audio streaming stopping\n");
      startAudio(false);
      json = "{\"audiostream\": 0 }";
    }
  }

  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Access-Control-Allow-Origin", "*");

  request->send(response);
}

/*
* Handler to adjust output volume
*/
void volume_handler(AsyncWebServerRequest *request) {

  String json = "{ \"command\" : \"failed\" }";

  if (request->hasArg("power")) {
    int value = request->arg("power").toInt();
    if (value >= 0 && value < 30) {
      i2sGainDivider = value;
    } 
    String json = "{ \"command\" : \"succes\" }";
  }

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
      bodyControl.exec(bodyPart, direction, duration);
      String json = "{ \"command\" : \"succes\" }";
    }

  } else {
    String json = "{ \"command\" : \"succes\" }";
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

    String json = "{ \"command\" : \"succes\" }";
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
* Start / Stop Audio UDP listening
*/
void startAudio(bool start) {

  if (start) {

    if (udp.listen(AUDIO_UDP_PORT)) {

      Serial.print("UDP Listening on IP: ");
      Serial.println(WiFi.localIP());

      udp.onPacket([](AsyncUDPPacket packet) {
        size_t bytes_written;
        int32_t SampleValue;
        int32_t i2sValue;
        const uint8_t *data = (const uint8_t *)packet.data();

        for (size_t i = 0; i <= packet.length(); i = i + 4) {
          float sample;
          uint8_t *f_ptr = (uint8_t *)&sample;
          f_ptr[0] = data[i + 0];
          f_ptr[1] = data[i + 1];
          f_ptr[2] = data[i + 2];
          f_ptr[3] = data[i + 3];

          if (i2sGainDivider != 0) {
            SampleValue = static_cast<int32_t>(sample * 32767.0f);
            i2sValue = SampleValue / (30 - i2sGainDivider);
          } else {
            i2sValue = 0;
          }
          esp_err_t err = i2s_write((i2s_port_t)i2sPortNo, (const char *)&i2sValue, sizeof(data), &bytes_written, portMAX_DELAY);
          if (err != 0) {
            Serial.printf("I2C error : %i \n",err);
          }
        }
      });

      audioStreamRunning = true;
    }
  } else {
    Serial.print("UDP stopped Listening");
    udp.close();
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

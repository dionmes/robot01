/*
* Hardware
*/
#include <Wire.h>
#include <VL53L1X.h>
#include "wrapper_bno08x.h"
#include "roboFace.h"
#include "bodyControl.h"
#include <ESP_I2S.h>
#include <FS.h>
#include <SPIFFS.h>
#define FORMAT_SPIFFS_IF_FAILED true

/*
* Master server or 'brain'
*/
IPAddress master_server_ip;

#include <WiFiManager.h>
WiFiManager wm;
bool shouldSaveConfig = false;

/*
* Webserver
*/
#include "esp_http_server.h"
httpd_handle_t server_httpd = NULL;

// http client
#include <ArduinoHttpClient.h>

#include <ArduinoJson.h>

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
* saveConfigCallback for wifimanager
*
* Indicate with shouldSaveConfig that config needs to be saved during setup
*/
void saveConfigCallback() {
  shouldSaveConfig = true;
}

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

  char config_master_ip[40];

  // Load config.json from SPIFFS
  if (SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      Serial.println(" SPIFFS.open ");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        JsonDocument json;
        String input = configFile.readString();
        if (input != NULL) {

          DeserializationError deserializeError = deserializeJson(json, input);

          if (!deserializeError) {
            strcpy(config_master_ip, json["master_ip"]);
            Serial.println("config_master_ip");
          } else {
            Serial.println("failed to load json config");
          }
          configFile.close();
        }
      } else {
        Serial.println("No config file.");
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }

  // Wifi Manager
  wm.setConnectTimeout(20);
  wm.setMinimumSignalQuality(10);
  // Custom title HTML for the WiFiManager portal
  String customTitle = "<h1>Robot01 config</h1>";
  wm.setCustomHeadElement(customTitle.c_str());

  WiFiManagerParameter custom_masterserver("server", "Master server IP", config_master_ip, 40);
  wm.addParameter(&custom_masterserver);

  wm.setSaveConfigCallback(saveConfigCallback);
  
  bool wificonnect = wm.autoConnect("ESP32_robot01", "123456789");

  if (!wificonnect) {
    Serial.println("Failed to connect");
    delay(3000);
    ESP.restart();
  } else {
    Serial.println("connected... :)");
  }

  if (shouldSaveConfig) {
    Serial.println("saving config");
    JsonDocument json;
    strcpy(config_master_ip, custom_masterserver.getValue());
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }
    json["master_ip"] = config_master_ip;
    serializeJson(json, configFile);
    configFile.close();
  }

  master_server_ip.fromString(config_master_ip);
  Serial.printf("Remote master server IP: %s \n", master_server_ip.toString().c_str());

  // HTTP API server
  Serial.println("Webserver starting");

  httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
  httpd_config.max_uri_handlers = 16;

  // Add request handlers to webserver
  httpd_uri_t VL53L1X_url = { .uri = "//VL53L1X_Info", .method = HTTP_GET, .handler = webhandler_VL53L1X_Info, .user_ctx = NULL };
  httpd_uri_t BNO08X_url = { .uri = "/BNO08X_Info", .method = HTTP_GET, .handler = webhandler_wrapper_bno08xInfo, .user_ctx = NULL };
  httpd_uri_t audiostream_url = { .uri = "/audiostream", .method = HTTP_GET, .handler = audiostream_handler, .user_ctx = NULL };
  httpd_uri_t volume_url = { .uri = "/volume", .method = HTTP_GET, .handler = volume_handler, .user_ctx = NULL };
  httpd_uri_t bodyaction_url = { .uri = "/bodyaction", .method = HTTP_GET, .handler = body_action_handler, .user_ctx = NULL };
  httpd_uri_t displayaction_url = { .uri = "/displayaction", .method = HTTP_GET, .handler = display_action_handler, .user_ctx = NULL };
  httpd_uri_t wakeupsense_url = { .uri = "/wakeupsense", .method = HTTP_GET, .handler = wakeupsense_handler, .user_ctx = NULL };
  httpd_uri_t health_url = { .uri = "/health", .method = HTTP_GET, .handler = webhealth_handler, .user_ctx = NULL };
  httpd_uri_t erase_url = { .uri = "/eraseconfig", .method = HTTP_GET, .handler = config_erase_handler, .user_ctx = NULL };
  httpd_uri_t reset_url = { .uri = "/reset", .method = HTTP_GET, .handler = reset_handler, .user_ctx = NULL };

  if (httpd_start(&server_httpd, &httpd_config) == ESP_OK) {
    httpd_register_uri_handler(server_httpd, &VL53L1X_url);
    httpd_register_uri_handler(server_httpd, &BNO08X_url);
    httpd_register_uri_handler(server_httpd, &audiostream_url);
    httpd_register_uri_handler(server_httpd, &volume_url);
    httpd_register_uri_handler(server_httpd, &bodyaction_url);
    httpd_register_uri_handler(server_httpd, &displayaction_url);
    httpd_register_uri_handler(server_httpd, &wakeupsense_url);
    httpd_register_uri_handler(server_httpd, &health_url);
    httpd_register_uri_handler(server_httpd, &reset_url);
    httpd_register_uri_handler(server_httpd, &erase_url);

    Serial.printf("Server on port: %d \n", httpd_config.server_port);
  } else {
    Serial.printf("Server failed");
  }

  delay(100);
  Serial.println("Robot01 API IP : ");
  Serial.print(WiFi.localIP());

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
  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Master registration " + WiFi.localIP().toString());
  WiFiClient httpWifiInstance;
  HttpClient http(httpWifiInstance, config_master_ip, 5000);

  String httpPath = "/api/register_ip?ip=" + WiFi.localIP().toString() + "&device=robot01";
  int http_err = http.get(httpPath);
  http_err == 0 ? Serial.println(http.responseBody()) : Serial.printf("Got status code: %u \n", http_err);

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
* Handler to get vl53l1x values
*/
esp_err_t webhandler_VL53L1X_Info(httpd_req_t *request) {
  
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  JsonDocument json_obj;

  vl53l1x_sensor.read();

  uint16_t range_mm = vl53l1x_sensor.ranging_data.range_mm;
  uint8_t range_status = vl53l1x_sensor.ranging_data.peak_signal_count_rate_MCPS;
  float peak_signal = vl53l1x_sensor.ranging_data.peak_signal_count_rate_MCPS;
  float ambient_count = vl53l1x_sensor.ranging_data.ambient_count_rate_MCPS;

  json_obj["range_mm"] = String(range_mm);
  json_obj["range_status"] = String(range_status);
  json_obj["peak_signal"] = String(peak_signal);
  json_obj["ambient_count"] = String(ambient_count);

  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );

  httpd_resp_send(request, json_response , jsonSize );

  return ESP_OK;
}

/*
* Handler to get bno08x values
*/
esp_err_t webhandler_wrapper_bno08xInfo(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  JsonDocument json_obj;

  json_obj["geoMagRotationVector_real"] = String(wrapper_bno08x.geoMagRotationVector_real);
  json_obj["geoMagRotationVector_i"] = String(wrapper_bno08x.geoMagRotationVector_i);
  json_obj["geoMagRotationVector_j"] = String(wrapper_bno08x.geoMagRotationVector_j);
  json_obj["geoMagRotationVector_k"] = String(wrapper_bno08x.geoMagRotationVector_k);
  json_obj["accelerometer_x"] = String(wrapper_bno08x.accelerometer_x);
  json_obj["accelerometer_y"] = String(wrapper_bno08x.accelerometer_y);
  json_obj["accelerometer_z"] = String(wrapper_bno08x.accelerometer_z);
  json_obj["gyroscope_x"] = String(wrapper_bno08x.gyroscope_x);
  json_obj["gyroscope_y"] = String(wrapper_bno08x.gyroscope_y);
  json_obj["gyroscope_z"] = String(wrapper_bno08x.gyroscope_z);
  json_obj["stability"] = String(wrapper_bno08x.stability);
  json_obj["shake"] = String(wrapper_bno08x.shake);

  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );

  httpd_resp_send(request, json_response , jsonSize );

  return ESP_OK;
}

/*
* Handler to enable/disable audiostream
*/
esp_err_t audiostream_handler(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);
  
  const char* param_name = "on";
  String param_value = get_url_param(request, param_name, 2);

  if (param_value != "invalid") {

    int value = param_value.toInt();
    
    if (value == 1 && !audioStreamRunning) {
      Serial.println("Audio streaming starting\n");
      startAudio(true);
    }

    if (value == 0 && audioStreamRunning) {
      Serial.println("Audio streaming stopping\n");
      startAudio(false);
    }

  }
  
  JsonDocument json_obj;
  json_obj["audiostream"] = String(audioStreamRunning);
  
  Serial.print("Audio streaming :");
  Serial.println(String(audioStreamRunning));

  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );

  httpd_resp_send(request, json_response , jsonSize );

  return ESP_OK;
}

/*
* Handler to adjust output volume
*/
esp_err_t volume_handler(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);
  
  const char* param_name = "power";
  String param_value = get_url_param(request,param_name, 4);

  if (param_value != "invalid") {
    int value = param_value.toInt();
    if (value >= 0 && value <= MAX_VOLUME) {
      i2sGainMultiplier = value;
    } 
  }
  
  JsonDocument json_obj;
  
  json_obj["volume"] = i2sGainMultiplier;

  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );
  
  httpd_resp_send(request, json_response , jsonSize );

  return ESP_OK;
}

/*
* Handler for body test task
*/
esp_err_t body_action_handler(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  JsonDocument json_obj;
  json_obj["command"] = "";

  const char* param_name1 = "action";
  String param_value_action = get_url_param(request, param_name1, 10);
  const char* param_name2 = "direction";
  String param_value_direction = get_url_param(request, param_name2, 10);
  const char* param_name3 = "steps";
  String param_value_steps = get_url_param(request, param_name3, 10);

  if (param_value_action != "invalid") {

    int action = param_value_action.toInt();

    int direction = param_value_direction != "invalid" ? param_value_direction.toInt() : 0;
    int steps = param_value_steps != "invalid" ? param_value_steps.toInt() : 0;
    
    if (steps < 3000) {
      json_obj["command"] = "success";
      bodyControl.exec(action, direction, steps);
    }

  } else {
    json_obj["command"] = "success";
    xTaskCreatePinnedToCore(bodyTestRoutine, "BodyTestRoutine", 4096, NULL, 5, &robotRoutineTask, 1);
  }

  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );

  httpd_resp_send(request, json_response , jsonSize );

  return ESP_OK;
}

/*
* Handler for display action
*/
esp_err_t display_action_handler(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  JsonDocument json_obj;
  json_obj["command"] = "";

  const char* param_name1 = "action";
  String param_value_action = get_url_param(request, param_name1, 10);
  const char* param_name2 = "index";
  String param_value_direction = get_url_param(request, param_name2, 10);
  const char* param_name3 = "text";
  String param_value_steps = get_url_param(request, param_name3, 300);

  if (param_value_action != "invalid") {

    int index = param_value_direction != "invalid" ? param_value_direction.toInt() : 0;
    String text = param_value_steps != "invalid" ? param_value_steps : "";

    roboFace.exec(param_value_action.toInt(), text, index);

    json_obj["command"] = "succes";
  }

  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );

  httpd_resp_send(request, json_response , jsonSize );

  return ESP_OK;
}

/*
* Handler for waking up XAIO sense/camera from sleep
*/
esp_err_t wakeupsense_handler(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  JsonDocument json_obj;
  json_obj["command"] = "succes";
  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );

  digitalWrite(25, HIGH);
  delay(200);
  digitalWrite(25, LOW);

  httpd_resp_send(request, json_response , jsonSize );  
  return ESP_OK;
}

/* 
* health_handler for httpd
*
* Response with ok
*/
esp_err_t webhealth_handler(httpd_req_t *req) {
  
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, HTTPD_TYPE_JSON);  

  JsonDocument json_obj;
  json_obj["status"] = "ok";
  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );
  
  httpd_resp_send(req, json_response , jsonSize );
  
  return ESP_OK;
}

/*
* reset_handler for httpd
*
* Restart ESP32
*/
static esp_err_t reset_handler(httpd_req_t *req) {
  delay(1000);
  Serial.print("Resetting on request.");
  esp_restart();
  // No return response neccesary
}

/*
* config_erase_handler for httpd
*
* Erase config and wifi settings and Restart ESP32
*/
static esp_err_t config_erase_handler(httpd_req_t *req) {
  Serial.print("Config erase on request.");
  delay(2000);
  wm.erase();
  esp_restart();
  // No return response neccesary
}

/////////////////////////////// helper function to get param from webrequest //////////////////////////////////////

/*
* helper for http url parameters. 
* - httpd_req_t *req
* - const char *param : Parameter name
* - const char *param : Parameter name
* - int value_size : Expected parameter size for buffer
*
* get_url_param
*/
static String get_url_param(httpd_req_t *req, const char *param, int value_size) {

    char buf[300];  // Buffer to store query string
    char value[value_size] = {"invalid\0"}; // return value

    /* Get the query string */
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        /* Extract value of 'name' key */
        if (httpd_query_key_value(buf, param, value, sizeof(value)) == ESP_OK) {
        }
    } 

    return value;
}

////////////////////////////////  Services  //////////////////////////////////////////////////////////////////////

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

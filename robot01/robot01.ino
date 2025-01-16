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
#include <WiFiManager.h>
#include "esp_http_server.h"
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include "AsyncUDP.h"

#define FORMAT_SPIFFS_IF_FAILED true
/*
* Define CPU cores for vtasks
*/
#define HTTPD_TASK_CORE 1
#define bno08x_TASK_CORE 1

/*
* Master server or 'brain'
*/
char config_master_ip[40];

WiFiManager wm;
WiFiManagerParameter custom_masterserver("server", "Master server IP", config_master_ip, 40);

bool shouldSaveConfig = false;

/*
* Webserver
*/
httpd_handle_t server_httpd = NULL;

/*
* Robot name
*/
String ROBOT_NAME = "Sappie";

/*
* I2S - UDP
*/
AsyncUDP udp;
#define AUDIO_UDP_PORT 9000       // UDP audio over I2S

#define I2S_BUFFER_SIZE 35328 // x packets of 1472 bytes
uint8_t *i2sbuffer = new uint8_t [I2S_BUFFER_SIZE + 1472]; 
uint16_t i2sbuffer_pointer = 0;

#define robot01_I2S_BCK 1
#define robot01_I2S_WS 2
#define robot01_I2S_OUT 3

bool audioStreamRunning = false;  // If Audio is listening on udp
I2SClass I2S;

#define MAX_VOLUME 100
// default volume
int audio_volume = 50;

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
TaskHandle_t robotRoutineTask;

/*
* Initialization/setup
*/
void setup() {
  
  // Serial
  Serial.begin(115200);
  Serial.println("Booting robot01");

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
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "Wifi connecting");
  Serial.println("Wifi connecting");

  // load json config
  loadGlobalConfig();
  custom_masterserver.setValue(config_master_ip, 40);

  // Wifi Manager
  wm.setConnectTimeout(60);
  wm.setMinimumSignalQuality(10);
  // Custom title HTML for the WiFiManager portal
  String customTitle = "<h1>Robot01 config</h1>";
  wm.setCustomHeadElement(customTitle.c_str());
  wm.addParameter(&custom_masterserver);
  wm.setSaveConfigCallback(saveConfigCallback);
  
  bool wificonnect = wm.autoConnect("ESP32_robot01", "123456789");
  
  // Save config if requested
  if (shouldSaveConfig) {
    safeConfig();
  }

  if (!wificonnect) {
    Serial.println("Failed to connect");
    delay(3000);
    ESP.restart();
  } else {
    Serial.println("connected... :)");
  }

  // Save config if requested
  if (shouldSaveConfig) {
    safeConfig();
  }

  // HTTP API server
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "WebServer starting");
  Serial.println("Webserver starting");

  httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
  httpd_config.max_uri_handlers = 16;
  httpd_config.core_id =  HTTPD_TASK_CORE;
  httpd_config.task_priority = configMAX_PRIORITIES - 10;

  // Add request handlers to webserver
  httpd_uri_t VL53L1X_url = { .uri = "/VL53L1X_Info", .method = HTTP_GET, .handler = webhandler_VL53L1X_Info, .user_ctx = NULL };
  httpd_uri_t BNO08X_url = { .uri = "/BNO08X_Info", .method = HTTP_GET, .handler = webhandler_wrapper_bno08xInfo, .user_ctx = NULL };
  httpd_uri_t volume_url = { .uri = "/volume", .method = HTTP_GET, .handler = volume_handler, .user_ctx = NULL };
  httpd_uri_t bodyaction_url = { .uri = "/bodyaction", .method = HTTP_GET, .handler = body_action_handler, .user_ctx = NULL };
  httpd_uri_t displayaction_url = { .uri = "/displayaction", .method = HTTP_GET, .handler = display_action_handler, .user_ctx = NULL };
  httpd_uri_t drawbmp_url = { .uri = "/drawbmp", .method = HTTP_POST, .handler = drawbmp_handler, .user_ctx = NULL };
  httpd_uri_t wakeupsense_url = { .uri = "/wakeupsense", .method = HTTP_GET, .handler = wakeupsense_handler, .user_ctx = NULL };
  httpd_uri_t audiostream_url = { .uri = "/audiostream", .method = HTTP_GET, .handler = audiostream_handler, .user_ctx = NULL };
  httpd_uri_t health_url = { .uri = "/health", .method = HTTP_GET, .handler = webhealth_handler, .user_ctx = NULL };
  httpd_uri_t erase_url = { .uri = "/eraseconfig", .method = HTTP_GET, .handler = config_erase_handler, .user_ctx = NULL };
  httpd_uri_t reset_url = { .uri = "/reset", .method = HTTP_GET, .handler = reset_handler, .user_ctx = NULL };
  httpd_uri_t wifimanager = { .uri = "/wifimanager", .method = HTTP_GET, .handler = wm_handler, .user_ctx = NULL };

  // Start httpd server
  if (httpd_start(&server_httpd, &httpd_config) == ESP_OK) {
    httpd_register_uri_handler(server_httpd, &VL53L1X_url);
    httpd_register_uri_handler(server_httpd, &BNO08X_url);
    httpd_register_uri_handler(server_httpd, &audiostream_url);
    httpd_register_uri_handler(server_httpd, &volume_url);
    httpd_register_uri_handler(server_httpd, &bodyaction_url);
    httpd_register_uri_handler(server_httpd, &displayaction_url);
    httpd_register_uri_handler(server_httpd, &drawbmp_url);
    httpd_register_uri_handler(server_httpd, &wakeupsense_url);
    httpd_register_uri_handler(server_httpd, &health_url);
    httpd_register_uri_handler(server_httpd, &reset_url);
    httpd_register_uri_handler(server_httpd, &erase_url);
    httpd_register_uri_handler(server_httpd, &wifimanager);

    Serial.printf("Server on port: %d \n", httpd_config.server_port);
  } else {
    Serial.printf("Server failed");
  }

  delay(100);
  Serial.print("Robot01 API IP : ");
  Serial.println(WiFi.localIP());

  delay(100);

  Serial.printf("Remote master server IP: %s \n", config_master_ip);

  // Register with remote master
  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Registering with master");
  WiFiClient httpWifiInstance;

  HttpClient http_client(httpWifiInstance, config_master_ip, 5000);

  String httpPath = "http://" + String(config_master_ip) + "/api/setting?item=robot01_ip&value=" + WiFi.localIP().toString();
  int http_err = http_client.get(httpPath);
  if (http_err != 0) { Serial.println("Error registering at master"); };

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

  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "Tasks");
  // Starting tasks
  Serial.println("Tasks starting");

  // Starting wrapper_bno08x
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "BNO08x");
  Serial.println("Booting wrapper_bno08x (gyro) sensor\n");

  if (!wrapper_bno08x.begin()) {
    Serial.println("Failed to detect and initialize wrapper_bno08x (gyro) sensor!");
  } else {
    Serial.println("wrapper_bno08x update task starting");
    xTaskCreatePinnedToCore(bno08xUpdateTask, "wrapper_bno08x", 4096, NULL, 10, &wrapper_bno08xUpdateTaskHandle, bno08x_TASK_CORE);
  }
  delay(100);
  
  // Audio / I2S init.
  Serial.println("Start audio");
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "AUDIO/I2S");

  // Pin configuration
	gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_DISABLE; //disable interrupt
  io_conf.mode = GPIO_MODE_OUTPUT; //set as output mode
  io_conf.pin_bit_mask = GPIO_NUM_1 | GPIO_NUM_2 | GPIO_NUM_3; //bit mask of the pins that you want to set,e.g.GPIO21/22
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; //disable pull-down mode
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE; //disable pull-up mode
  gpio_config(&io_conf); //configure GPIO with the given settings

  I2S.setPins(robot01_I2S_BCK, robot01_I2S_WS, robot01_I2S_OUT, -1, -1); //SCK, WS, SDOUT, SDIN, MCLK
  I2S.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_LEFT);

  startAudio(true);

  // Boot complete
  Serial.println("Boot complete...");
  delay(100);

  String register_string =  "Hello, I am " + ROBOT_NAME + " @ " + WiFi.localIP().toString();
  roboFace.exec(faceAction::SCROLLTEXT,register_string, 100);
  delay(8000);  
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

  json_obj["range_mm"] = vl53l1x_sensor.ranging_data.range_mm;
  json_obj["range_status"] = vl53l1x_sensor.ranging_data.peak_signal_count_rate_MCPS;
  json_obj["peak_signal"] = vl53l1x_sensor.ranging_data.peak_signal_count_rate_MCPS;
  json_obj["ambient_count"] = vl53l1x_sensor.ranging_data.ambient_count_rate_MCPS;

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

  json_obj["geoMagRotationVector_real"] = wrapper_bno08x.geoMagRotationVector_real;
  json_obj["geoMagRotationVector_i"] = wrapper_bno08x.geoMagRotationVector_i;
  json_obj["geoMagRotationVector_j"] = wrapper_bno08x.geoMagRotationVector_j;
  json_obj["geoMagRotationVector_k"] = wrapper_bno08x.geoMagRotationVector_k;
  json_obj["accelerometer_x"] = wrapper_bno08x.accelerometer_x;
  json_obj["accelerometer_y"] = wrapper_bno08x.accelerometer_y;
  json_obj["accelerometer_z"] = wrapper_bno08x.accelerometer_z;
  json_obj["gyroscope_x"] = wrapper_bno08x.gyroscope_x;
  json_obj["gyroscope_y"] = wrapper_bno08x.gyroscope_y;
  json_obj["gyroscope_z"] = wrapper_bno08x.gyroscope_z;
  json_obj["stability"] = wrapper_bno08x.stability;
  json_obj["shake"] = wrapper_bno08x.shake;

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

    roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Audio : " + param_value, 2000);

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
  json_obj["audiostream"] = audioStreamRunning;
  
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
    roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Volume : " + param_value, 2000);

    int value = param_value.toInt();
    if (value >= 0 && value <= MAX_VOLUME) {
      audio_volume = value;
    } 
  }
  
  JsonDocument json_obj;
  
  json_obj["volume"] = audio_volume;

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
      size_t jsonSize = measureJson(json_obj);
      char json_response[jsonSize];
      serializeJson(json_obj, json_response, jsonSize );

      httpd_resp_send(request, json_response , jsonSize );

      bodyControl.exec(action, direction, steps);
    } 
  }
  
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
  String param_value_action = get_url_param(request, param_name1, 5);
  const char* param_name2 = "index";
  String param_value_index = get_url_param(request, param_name2, 5);
  const char* param_name3 = "text";
  String param_value_text = get_url_param(request, param_name3, 1024);

  if (param_value_action != "invalid") {

    json_obj["command"] = "succes";
    size_t jsonSize = measureJson(json_obj);
    char json_response[jsonSize];
    serializeJson(json_obj, json_response, jsonSize );
    httpd_resp_send(request, json_response , jsonSize );

    int action = param_value_action.toInt();
    int index = param_value_index != "invalid" ? param_value_index.toInt() : 0;
    String text = "";
    text = param_value_text != "invalid" ? urlDecode(param_value_text) : "";

    roboFace.exec(action, text, index);
  }

  return ESP_OK;
}

/*
* Handler for imagetest
*/
esp_err_t drawbmp_handler(httpd_req_t *req) {

  size_t recv_size = (req->content_len < roboFaceConstants::BUFFER_SIZE ? req->content_len : roboFaceConstants::BUFFER_SIZE);
  char content[recv_size];  
  int ret = httpd_req_recv(req, content, recv_size);
  
  roboFace.drawbmp(content);
  
  /* Send a simple response */
  const char resp[] = "Imagetest received";
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
  
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

  return ESP_OK;
}

/*
* Handler for waking up XAIO sense/camera from sleep
*/
esp_err_t wakeupsense_handler(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Wake up Sense", 2000);

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
  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Restart");
  delay(1000);
  Serial.print("Resetting on request.");
  
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, HTTPD_TYPE_JSON);  

  JsonDocument json_obj;
  json_obj["Reset"] = "ok";
  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );
  httpd_resp_send(req, json_response , jsonSize );

  delay(1000);

  esp_restart();

  return ESP_OK;
}

/*
* wm_handler for httpd
*
* Wifimanager on demand
*/
static esp_err_t wm_handler(httpd_req_t *req) {
  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Wifimanager");

  wm.setDebugOutput(true, WM_DEBUG_VERBOSE);
  
  wm.setHttpPort(81);
  wm.setEnableConfigPortal(true);

  // Redirect to portal
  httpd_resp_set_type(req, "text/html");
  String redirect_url = "http://" + WiFi.localIP().toString() + ":81/";
  String redirect_page = "<br><A href='" + redirect_url + "'> Click for redirect to wifimanager. </A><br>";
  httpd_resp_send(req, redirect_page.c_str() , redirect_page.length());
  Serial.println(redirect_url);

  wm.startConfigPortal();

  // Save config if requested
  if (shouldSaveConfig) {
    safeConfig();
  }

  ESP.restart();

  return ESP_OK;
}

/*
* config_erase_handler for httpd
*
* Erase config and wifi settings and Restart ESP32
*/
static esp_err_t config_erase_handler(httpd_req_t *req) {
  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Config erase");
  
  Serial.print("Config erase on request.");
  delay(2000);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, HTTPD_TYPE_JSON);  

  JsonDocument json_obj;
  json_obj["Erase"] = "ok";
  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );
  httpd_resp_send(req, json_response , jsonSize );

  delay(1000);

  wm.erase();
  esp_restart();

  return ESP_OK;
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

/////////////////////////////// helper function to decode a URL-encoded string //////////////////////////////////////

String urlDecode(String input) {
  String decoded = "";
  char temp[] = "0x00"; // Temporary storage for hex values
  unsigned int hexValue;

  for (unsigned int i = 0; i < input.length(); i++) {
    if (input[i] == '%') {
      // Decode the next two characters as a hex value
      if (i + 2 < input.length()) {
        temp[2] = input[i + 1];
        temp[3] = input[i + 2];
        hexValue = strtoul(temp, NULL, 16);
        decoded += char(hexValue);
        i += 2; // Skip the next two characters
      }
    } else if (input[i] == '+') {
      // Convert '+' to space
      decoded += ' ';
    } else {
      // Copy regular character
      decoded += input[i];
    }
  }
  return decoded;
}

////////////////////////////////  Audio  //////////////////////////////////////////////////////////////////////

/*
* UDP Audio listening callback
*/
IRAM_ATTR void udpRXCallBack(AsyncUDPPacket &packet) {

  const uint8_t *packet_data = (const uint8_t *)packet.data();
  const size_t packet_length = packet.length();

  float sample;
  uint8_t *f_ptr = (uint8_t *)&sample;
  uint32_t i2sValue;
  float adjusted_gain = audio_volume * 0.05;

  // Flush buffer on packet of size 4 or smaller (happens when buffer is larger than sample)
  if (packet_length <= 4) {
    I2S.write((uint8_t *)i2sbuffer, i2sbuffer_pointer);
    i2sbuffer_pointer = 0;
    return;
  }

  for (size_t i = 0; i <= packet_length; i = i + 4) {

    f_ptr[0] = packet_data[i + 0];
    f_ptr[1] = packet_data[i + 1];
    f_ptr[2] = packet_data[i + 2];
    f_ptr[3] = packet_data[i + 3];
    
    i2sValue = static_cast<int32_t>(sample * (32767.0f * adjusted_gain) ) ;

    i2sbuffer[i2sbuffer_pointer + i] = static_cast<uint8_t>(i2sValue & 0xFF);
    i2sbuffer[i2sbuffer_pointer + i + 1] = static_cast<uint8_t>((i2sValue >> 8) & 0xFF);
    i2sbuffer[i2sbuffer_pointer + i + 2] = static_cast<uint8_t>((i2sValue >> 16) & 0xFF);
    i2sbuffer[i2sbuffer_pointer + i + 3] = static_cast<uint8_t>((i2sValue >> 24) & 0xFF);

  };
  
  i2sbuffer_pointer = i2sbuffer_pointer + packet_length;

  if (i2sbuffer_pointer > I2S_BUFFER_SIZE) {
    I2S.write((uint8_t *)i2sbuffer, i2sbuffer_pointer);
    i2sbuffer_pointer = 0;
  }

};

/*
* Start / Stop Audio UDP listening
*/
void startAudio(bool start) {

  if (start) {

    if (udp.listen(AUDIO_UDP_PORT)) {
      
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

    audioStreamRunning = false;
  }
}

////////////////////////////////  Config helpers  //////////////////////////////////////////////////////////////////////

/*
* saveConfigCallback for wifimanager
*
* Indicate with shouldSaveConfig that config needs to be saved during setup
*/
void saveConfigCallback() {
  shouldSaveConfig = true;
}

/*
* Load json config file. put item(s) in global variables
*/
void loadGlobalConfig() {

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
            // Global variables from config
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
};

/*
* Save json config file. from item(s) in global variables
*/
void safeConfig() {
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

////////////////////////////////  Div  //////////////////////////////////////////////////////////////////////

/*
* Body test Routine
*/
void bodyTestRoutine(void *pVParameters) {
  roboFace.exec(faceAction::NEUTRAL);
  vTaskDelete(NULL);
};


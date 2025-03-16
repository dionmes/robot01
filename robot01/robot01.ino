/*
* Include
*/

#include <Wire.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <ArduinoHttpClient.h>
#include "driver/i2s_std.h"
#include "esp_http_server.h"
#include "DistanceSensor.h"
#include "motiondetect.h"
#include "roboDisplay.h"
#include "bodyControl.h"
#include "tcp2audio.h"

#define FORMAT_SPIFFS_IF_FAILED false

/*
* Define CPU cores and priority for vtasks
* defined with 24 as configMAX_PRIORITIES
*/
#define HTTPD_TASK_CORE 1
#define HTTPD_TASK_PRIORITY 20

#define TCP_AUDIO_TASK_CORE 0
#define TCP_AUDIO_TASK_PRIORITY 10

#define I2S_AUDIO_TASK_CORE 0
#define I2S_AUDIO_TAK_PRIORITY 20

#define motion_detect_TASK_CORE 1
#define motion_detect_PRIORITY 15

#define distance_sensor_TASK_CORE 1
#define distance_sensor_PRIORITY 5

#define display_TASK_CORE 0
#define display_TASK_PRIORITY 10

#define body_TASK_CORE 1
#define body_TASK_PRIORITY 5

// TCP Audio sample rate
#define I2S_SAMPLE_RATE 16000

// Master server or 'brain' 
char config_master_ip[40];

/*
* Wifi Manager
*/
WiFiManager wm;
WiFiManagerParameter custom_masterserver("server", "Master server IP", config_master_ip, 40);
bool shouldSaveConfig = false;

// ToF sensor; Front facing distance
DistanceSensor distance_sensor;

// Movement sensor; Accelerometer, Orientation
motiondetect motion_detect;

// Instance of roboDisplay, for methods for display actions on LED display
roboDisplay roboDisplay;
#define Display_MESSAGE_WAIT_TIME 20

// Instance of bodyControl for movement actions & hand LEDs
bodyControl bodyControl;

// Instance of tcp2audio for receiving audio
tcp2audio tcp_2_audio;

// API Server
httpd_handle_t server_httpd = NULL;

// HTTP wifi Client
WiFiClient httpWifiInstance;

/*
* Initialization/setup
*/
void setup() {
  
  // Serial
  Serial.begin(115200);
  delay(1000);
  
  // Wire init.
  Wire.begin();
  Wire.setClock(100000);
  Wire.setTimeout(10);

  //Random seed
  randomSeed(analogRead(GPIO_NUM_6));

  roboDisplay.begin(display_TASK_CORE, display_TASK_PRIORITY);
  roboDisplay.exec(DisplayAction::DISPLAYTEXTLARGE, "Boot");

  // XAIO Sense/Camera wake up pin assignment
  pinMode(25, OUTPUT);
  digitalWrite(25, LOW);
  delay(100);

  // Body init.
  roboDisplay.exec(DisplayAction::DISPLAYTEXTLARGE, "Body");
  bodyControl.begin(body_TASK_CORE,body_TASK_PRIORITY);
  delay(100);

  // Wifi
  roboDisplay.exec(DisplayAction::DISPLAYTEXTLARGE, "Wifi connecting");
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
    Serial.println("Failed to connect WIFI");
    delay(3000);
    ESP.restart();
  } else {
    Serial.println("WIFI connected... :)");
  }

  roboDisplay.exec(DisplayAction::DISPLAYTEXTSMALL, "Tasks&Sensors");
  // Starting tasks
  Serial.println("Tasks starting");

  // Starting Distance sensor
  roboDisplay.exec(DisplayAction::DISPLAYTEXTSMALL, "Distance sensor");
  Serial.println("Booting vl53l1x (range) sensor");

  if (!distance_sensor.begin(distance_sensor_TASK_CORE, distance_sensor_PRIORITY)) {
    Serial.println("Failed to detect and initialize distance sensor!");
  } else {
    Serial.println("Distance sensor update task starting");
  }
  delay(100);

  // Starting motion_detect
  roboDisplay.exec(DisplayAction::DISPLAYTEXTSMALL, "Motion sensor");
  Serial.println("Booting motion_detect (gyro) sensor.");

  if (!motion_detect.begin(motion_detect_TASK_CORE, motion_detect_PRIORITY)) {
    Serial.println("Failed to detect and initialize motion_detect (gyro) sensor!");
  } else {
    Serial.println("motion_detect update task starting");
  }
  delay(100);
  
  // Audio / I2S init.
  Serial.println("Start audio");
  roboDisplay.exec(DisplayAction::DISPLAYTEXTLARGE, "AUDIO/I2S");
  tcp_2_audio.begin(I2S_SAMPLE_RATE,TCP_AUDIO_TASK_CORE,TCP_AUDIO_TASK_PRIORITY,I2S_AUDIO_TASK_CORE,I2S_AUDIO_TAK_PRIORITY);  
  tcp_2_audio.start();
  delay(100);

  // HTTP API server
  roboDisplay.exec(DisplayAction::DISPLAYTEXTLARGE, "WebServer starting");
  Serial.println("API server starting");
  start_apiserver();

  // Boot complete
  Serial.println("Systems Boot complete...");

  delay(100);
  Serial.print("Robot01 API IP : ");
  Serial.println(WiFi.localIP());

  // Register with remote master
  Serial.printf("Remote master server IP: %s \n", config_master_ip);
  roboDisplay.exec(DisplayAction::DISPLAYTEXTSMALL, "Registering with master");

  HttpClient master_http_client(httpWifiInstance, config_master_ip, 5000);
  String httpPath = "http://" + String(config_master_ip) + "/api/setting?item=robot01_ip&value=" + WiFi.localIP().toString();
  int http_err = master_http_client.get(httpPath);
  if (http_err != 0) { Serial.println("Error registering at master"); };

  String register_string =  "Hello, I am Sappie @ " + WiFi.localIP().toString();
  roboDisplay.exec(DisplayAction::SCROLLTEXT,register_string, 100);
  delay(8000);  
  roboDisplay.exec(DisplayAction::NEUTRAL);


}

/*
* Main task
*/
void loop() {
    delay(10000);
}

/*
* API server 
*/
void start_apiserver() {

  httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
  httpd_config.max_uri_handlers = 16;
  httpd_config.core_id =  HTTPD_TASK_CORE;
  httpd_config.task_priority = HTTPD_TASK_PRIORITY;

  // Add request handlers to webserver
  httpd_uri_t distanceSensor_url = { .uri = "/distanceSensor_info", .method = HTTP_GET, .handler = webhandler_DistanceSensor_Info, .user_ctx = NULL };
  httpd_uri_t motion_detect_url = { .uri = "/motionSensor_info", .method = HTTP_GET, .handler = webhandler_motion_detectInfo, .user_ctx = NULL };
  httpd_uri_t volume_url = { .uri = "/volume", .method = HTTP_GET, .handler = webhandler_volume, .user_ctx = NULL };
  httpd_uri_t bodyaction_url = { .uri = "/bodyaction", .method = HTTP_GET, .handler = webhandler_body_action, .user_ctx = NULL };
  httpd_uri_t displayaction_url = { .uri = "/displayaction", .method = HTTP_GET, .handler = webhandler_display_action, .user_ctx = NULL };
  httpd_uri_t drawbmp_url = { .uri = "/drawbmp", .method = HTTP_POST, .handler = webhandler_drawbmp, .user_ctx = NULL };
  httpd_uri_t wakeupsense_url = { .uri = "/wakeupsense", .method = HTTP_GET, .handler = webhandler_wakeupsense, .user_ctx = NULL };
  httpd_uri_t audiostream_url = { .uri = "/audiostream", .method = HTTP_GET, .handler = webhandler_audiostream, .user_ctx = NULL };
  httpd_uri_t health_url = { .uri = "/health", .method = HTTP_GET, .handler = webhandler_health, .user_ctx = NULL };
  httpd_uri_t erase_url = { .uri = "/eraseconfig", .method = HTTP_GET, .handler = webhandler_config_erase, .user_ctx = NULL };
  httpd_uri_t reset_url = { .uri = "/reset", .method = HTTP_GET, .handler = webhandler_reset, .user_ctx = NULL };
  httpd_uri_t wifimanager = { .uri = "/wifimanager", .method = HTTP_GET, .handler = webhandler_wifimanager, .user_ctx = NULL };

  // Start httpd server
  if (httpd_start(&server_httpd, &httpd_config) == ESP_OK) {
    httpd_register_uri_handler(server_httpd, &distanceSensor_url);
    httpd_register_uri_handler(server_httpd, &motion_detect_url);
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
}

/*
* Handler to get Disancesensor values
*
* url: /distanceSensor_info
*
*/
esp_err_t webhandler_DistanceSensor_Info(httpd_req_t *request) {
  
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  JsonDocument json_obj;

  json_obj["range_mm"] = distance_sensor.sensorData.range_mm;
  json_obj["range_status"] = distance_sensor.sensorData.range_status;
  json_obj["peak_signal"] = distance_sensor.sensorData.peak_signal;
  json_obj["ambient_count"] = distance_sensor.sensorData.ambient_count;

  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );

  httpd_resp_send(request, json_response , jsonSize );

  return ESP_OK;
}

/*
* Handler to get motion sensor values
*
* url: /motionSensor_info
*
*/
esp_err_t webhandler_motion_detectInfo(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  JsonDocument json_obj;

  json_obj["linearAcceleration_x"] = motion_detect.sensorData.linearAcceleration_x;
  json_obj["linearAcceleration_y"] = motion_detect.sensorData.linearAcceleration_y;
  json_obj["linearAcceleration_z"] = motion_detect.sensorData.linearAcceleration_z;
  
  json_obj["yaw"] = motion_detect.sensorData.yaw;
  json_obj["pitch"] = motion_detect.sensorData.pitch;
  json_obj["roll"] = motion_detect.sensorData.roll;
  json_obj["ypr_accuracy"] = motion_detect.sensorData.ypr_accuracy;

  json_obj["accuracy"] = motion_detect.sensorData.accuracy;
  json_obj["shake"] = motion_detect.sensorData.shake;

  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );

  httpd_resp_send(request, json_response , jsonSize );

  return ESP_OK;
}

/*
* Handler to enable/disable audiostream
*
* url: GET /audiostream?on=0/1
*
*/
esp_err_t webhandler_audiostream(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);
  
  const char* param_name = "on";
  String param_value = get_url_param(request, param_name, 2);
  
  if (param_value != "invalid") {

    roboDisplay.exec(DisplayAction::DISPLAYTEXTSMALL, "Audio : " + param_value, Display_MESSAGE_WAIT_TIME);
    int value = param_value.toInt();
    
    if (value == 1 && !tcp_2_audio._running) {
      tcp_2_audio.start();
    }
    if (value == 0 && tcp_2_audio._running) {
      tcp_2_audio.stop();
    }
  }
  
  JsonDocument json_obj;
  json_obj["audiostream"] = tcp_2_audio._running;
  
  Serial.print("Audio streaming :");
  Serial.println(String(tcp_2_audio._running));

  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );

  httpd_resp_send(request, json_response , jsonSize );

  return ESP_OK;
}

/*
* Handler to adjust output volume
*
* url: GET /volume?power=(0-50)
*
*/
esp_err_t webhandler_volume(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);
  
  const char* param_name = "power";
  String param_value = get_url_param(request,param_name, 4);

  if (param_value != "invalid") {

    int value = param_value.toInt();
    if (value >= 0) {
      tcp_2_audio.setvolume(value);
      roboDisplay.exec(DisplayAction::DISPLAYTEXTSMALL, "Volume : " + param_value, Display_MESSAGE_WAIT_TIME);
    } 
  }
  
  JsonDocument json_obj;
  json_obj["volume"] = tcp_2_audio.getvolume();
  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );
  httpd_resp_send(request, json_response , jsonSize );

  return ESP_OK;
}

/*
* Handler for body test task
*
* url: GET /bodyaction?action=n&direction=0/1&steps=i
*
*/
esp_err_t webhandler_body_action(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  JsonDocument json_obj;
  json_obj["command"] = "";

  const char* param_name1 = "action";
  String param_action = get_url_param(request, param_name1, 10);
  const char* param_name2 = "direction";
  String param_direction = get_url_param(request, param_name2, 10);
  const char* param_name3 = "value";
  String param_value = get_url_param(request, param_name3, 10);

  if (param_action != "invalid") {

    int action = param_action.toInt();

    int direction = param_direction != "invalid" ? param_direction.toInt() : 0;
    int value = param_value != "invalid" ? param_value.toInt() : 0;
    
    if (value < 3000) {
      json_obj["command"] = "success";
      size_t jsonSize = measureJson(json_obj);
      char json_response[jsonSize];
      serializeJson(json_obj, json_response, jsonSize );
      httpd_resp_send(request, json_response , jsonSize );
  
      bodyControl.exec(action, direction, value);
    } 
  }
  return ESP_OK;
}

/*
* Handler for display action
*
* url: GET /displayaction?action=n&index=I&text=“”
*
*/
esp_err_t webhandler_display_action(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  JsonDocument json_obj;
  json_obj["command"] = "";

  const char* param_name1 = "action";
  String param_action = get_url_param(request, param_name1, 5);
  const char* param_name2 = "index";
  String param_index = get_url_param(request, param_name2, 5);
  const char* param_name3 = "text";
  String param_text = get_url_param(request, param_name3, 1024);

  if (param_action != "invalid") {

    json_obj["command"] = "succes";
    size_t jsonSize = measureJson(json_obj);
    char json_response[jsonSize];
    serializeJson(json_obj, json_response, jsonSize );
    httpd_resp_send(request, json_response , jsonSize );

    int action = param_action.toInt();
    int index = param_index != "invalid" ? param_index.toInt() : 0;
    String text = "";
    text = param_text != "invalid" ? urlDecode(param_text) : "";

    roboDisplay.exec(action, text, index);
  }

  return ESP_OK;
}

/*
* Handler for imagetest
*
* url: POST /drawbmp
*
*/
esp_err_t webhandler_drawbmp(httpd_req_t *req) {

  size_t recv_size = (req->content_len < roboDisplayConstants::image_buffer_size ? req->content_len : roboDisplayConstants::image_buffer_size);
  char content[recv_size];  
  int ret = httpd_req_recv(req, content, recv_size);
  
  roboDisplay.drawbmp(content);
  
  /* Send a simple response */
  const char resp[] = "Imagetest received";
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
  
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

  return ESP_OK;
}

/*
* Handler for waking up XAIO sense/camera from sleep
*
* url: GET /wakeupsense
*
*/
esp_err_t webhandler_wakeupsense(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  roboDisplay.exec(DisplayAction::DISPLAYTEXTSMALL, "Wake up Sense", Display_MESSAGE_WAIT_TIME);

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
* url: GET /health
*
*/
esp_err_t webhandler_health(httpd_req_t *req) {
  
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
* webhandler_reset for httpd
*
* url: GET /reset
*
*/
static esp_err_t webhandler_reset(httpd_req_t *req) {
  roboDisplay.exec(DisplayAction::DISPLAYTEXTSMALL, "Restart");
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
* Instant start wifimanager
*
* url: GET /wifimanager
*
*/
static esp_err_t webhandler_wifimanager(httpd_req_t *req) {
  roboDisplay.exec(DisplayAction::DISPLAYTEXTSMALL, "Wifimanager");

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
*
* Erase config and wifi settings and Restart ESP32
*
* url: GET /eraseconfig
*
*/
static esp_err_t webhandler_config_erase(httpd_req_t *req) {
  roboDisplay.exec(DisplayAction::DISPLAYTEXTSMALL, "Config erase");
  
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

////////////////////////////////  Send notifications ////////////////////////////////////////////////////////

/*
* Send notification to master
*/
void sendMasterNotification(char *message) {

  HttpClient master_http_client(httpWifiInstance, config_master_ip, 5000);
  
  std::string httpPath = std::string("http://") + config_master_ip + "/api/notification?message=" + message;
  int http_err = master_http_client.get(httpPath.c_str());

  if (http_err != 0) { Serial.println("Error sending notification to master"); };
  Serial.println(message);
}

////////////////////////////////  Config methods  //////////////////////////////////////////////////////////////////////

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
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        JsonDocument json;
        String input = configFile.readString();
        if (input != NULL) {

          DeserializationError deserializeError = deserializeJson(json, input);

          if (!deserializeError) {
            // Global variables from config
            strcpy(config_master_ip, json["master_ip"]);            
            Serial.println("Loaded json config from SPIFFS");          
          } else {
            Serial.println("failed to load json config from SPIFFS");
          }
          configFile.close();
        }
      } else {
        Serial.println("No config file.");
      }
    }
  } else {
    Serial.println("failed to mount SPIFFS");
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


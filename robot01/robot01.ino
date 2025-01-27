/*
* Include
*/
#include <Wire.h>
#include <VL53L1X.h>
#include <ESP_I2S.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include "esp_http_server.h"
#include <Adafruit_SSD1306.h>
#include "motiondetect.h"
#include "roboFace.h"
#include "bodyControl.h"

#define FORMAT_SPIFFS_IF_FAILED false

/*
* Define CPU cores and priority for vtasks
* defined with 24 as configMAX_PRIORITIES
*/
#define HTTPD_TASK_CORE 0
#define HTTPD_TASK_PRIORITY 20

#define TCP_AUDIO_TASK_CORE 1
#define TCP_AUDIO_TAK_PRIORITY 20

#define motiondetect_TASK_CORE 1
#define motiondetect_PRIORITY 5

#define display_TASK_CORE 0
#define display_TASK_PRIORITY 5

#define body_TASK_CORE 0
#define body_TASK_PRIORITY 5

// Master server or 'brain' 
char config_master_ip[40];

/*
* Wifi Manager
*/
WiFiManager wm;
WiFiManagerParameter custom_masterserver("server", "Master server IP", config_master_ip, 40);
bool shouldSaveConfig = false;

/*
*  Audio existing of I2S & TCP Streaming
*/
#define AUDIO_TCP_PORT 9000 // TCP port for audio 
TaskHandle_t tcpAudioTaskHandle;

WiFiServer audioServer(AUDIO_TCP_PORT);
WiFiClient tcpClientAudio;

#define AUDIO_TCP_CHUNK_SIZE 6144 // TCP Buffer

uint8_t *i2sbuffer = new uint8_t [AUDIO_TCP_CHUNK_SIZE]; 
uint8_t tcp_audio_buffer[AUDIO_TCP_CHUNK_SIZE];

#define robot01_I2S_BCK 14
#define robot01_I2S_LRC 12
#define robot01_I2S_OUT 11

#define I2S_SAMPLE_RATE 16000
bool audioStreamRunning = false;  // If Audio is listening on tcp
I2SClass I2S;

#define MAX_VOLUME 100
// default volume
int audio_volume = 50;

// Sensors assignments
VL53L1X vl53l1x_sensor;

// Movement sensor; Accelerometer, Orientation
motiondetect motiondetect;

// LED Display for face
Adafruit_SSD1306 ledMatrix(128, 64, &Wire, -1);

// Instance of roboFace, for methods for display actions on LED display
roboFace roboFace;
#define FACE_MESSAGE_WAIT_TIME 20

// Instance of bodyControl for movement actions & hand LEDs
bodyControl bodyControl;

// API Server
httpd_handle_t server_httpd = NULL;

/*
* Initialization/setup
*/
void setup() {
  
  // Serial
  Serial.begin(115200);
  Serial.println("Booting robot01");

  // Wire init.
  Wire.begin();
  Wire.setClock(100000);
  Wire.setTimeout(10);
  //Random seed
  randomSeed(analogRead(GPIO_NUM_6));

  roboFace.begin(display_TASK_CORE, display_TASK_PRIORITY);
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "Boot");

  // XAIO Sense/Camera wake up pin assignment
  pinMode(25, OUTPUT);
  digitalWrite(25, LOW);
  delay(100);

  // Body init.
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "Body");
  bodyControl.begin(body_TASK_CORE,body_TASK_PRIORITY);
  delay(100);

  // Wifi
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
    Serial.println("Failed to connect WIFI");
    delay(3000);
    ESP.restart();
  } else {
    Serial.println("WIFI connected... :)");
  }

  // HTTP API server
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "WebServer starting");
  Serial.println("API server starting");
  start_apiserver();

  delay(100);
  Serial.print("Robot01 API IP : ");
  Serial.println(WiFi.localIP());

  // Register with remote master
  Serial.printf("Remote master server IP: %s \n", config_master_ip);
  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Registering with master");
  WiFiClient httpWifiInstance;
  HttpClient master_http_client(httpWifiInstance, config_master_ip, 5000);
  master_http_client.setConnectTimeout(10000);
  String httpPath = "http://" + String(config_master_ip) + "/api/setting?item=robot01_ip&value=" + WiFi.localIP().toString();
  int http_err = master_http_client.get(httpPath);
  if (http_err != 0) { Serial.println("Error registering at master"); };

  // Starting VL53L1X
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "VL53L1X");
  Serial.println("Booting vl53l1x (range) sensor");

  vl53l1x_sensor.setTimeout(50);
  if (vl53l1x_sensor.init()) {
    vl53l1x_sensor.setDistanceMode(VL53L1X::Short);
    vl53l1x_sensor.setMeasurementTimingBudget(50000);
    vl53l1x_sensor.startContinuous(50);
  } else {
    Serial.println("Failed to detect/initialize vl53l1x (range) sensor!");
  }

  delay(500);

  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "Tasks");
  // Starting tasks
  Serial.println("Tasks starting");

  // Starting motiondetect
  roboFace.exec(faceAction::DISPLAYTEXTLARGE, "BNO08x");
  Serial.println("Booting motiondetect (gyro) sensor.");
  if (!motiondetect.begin(motiondetect_TASK_CORE, motiondetect_PRIORITY)) {
    Serial.println("Failed to detect and initialize motiondetect (gyro) sensor!");
  } else {
    Serial.println("motiondetect update task starting");
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

  I2S.setPins(robot01_I2S_BCK, robot01_I2S_LRC, robot01_I2S_OUT, -1, -1); //SCK, WS, SDOUT, SDIN, MCLK
  I2S.begin(I2S_MODE_STD, I2S_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_LEFT);

  // TCP Audio receive task
  xTaskCreatePinnedToCore(tcpReceiveAudioTask,"TCPReceiveAudioTask", 12288, NULL,TCP_AUDIO_TAK_PRIORITY ,&tcpAudioTaskHandle,TCP_AUDIO_TASK_CORE);
  audioServer.begin();
  startAudio(true);

  // Boot complete
  Serial.println("Boot complete...");
  delay(100);

  String register_string =  "Hello, I am Sappie @ " + WiFi.localIP().toString();
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

// Send notification message to master
void masterNotification(char message[255]) {
  Serial.println(message);
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
  httpd_uri_t VL53L1X_url = { .uri = "/VL53L1X_Info", .method = HTTP_GET, .handler = webhandler_VL53L1X_Info, .user_ctx = NULL };
  httpd_uri_t BNO08X_url = { .uri = "/BNO08X_Info", .method = HTTP_GET, .handler = webhandler_motiondetectInfo, .user_ctx = NULL };
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
}

/*
* Handler to get vl53l1x values
*/
esp_err_t webhandler_VL53L1X_Info(httpd_req_t *request) {
  
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  JsonDocument json_obj;

  vl53l1x_sensor.read();

  json_obj["range_mm"] = vl53l1x_sensor.ranging_data.range_mm;
  json_obj["range_status"] = vl53l1x_sensor.ranging_data.range_status;
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
esp_err_t webhandler_motiondetectInfo(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  JsonDocument json_obj;

  json_obj["linearAcceleration_x"] = motiondetect.mData.linearAcceleration_x;
  json_obj["linearAcceleration_y"] = motiondetect.mData.linearAcceleration_y;
  json_obj["linearAcceleration_z"] = motiondetect.mData.linearAcceleration_z;
  
  json_obj["yaw"] = motiondetect.mData.yaw;
  json_obj["pitch"] = motiondetect.mData.pitch;
  json_obj["roll"] = motiondetect.mData.roll;

  json_obj["accuracy"] = motiondetect.mData.accuracy;
  json_obj["shake"] = motiondetect.mData.shake;

  size_t jsonSize = measureJson(json_obj);
  char json_response[jsonSize];
  serializeJson(json_obj, json_response, jsonSize );

  httpd_resp_send(request, json_response , jsonSize );

  return ESP_OK;
}

/*
* Handler to enable/disable audiostream
*/
esp_err_t webhandler_audiostream(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);
  
  const char* param_name = "on";
  String param_value = get_url_param(request, param_name, 2);
  
  if (param_value != "invalid") {

    roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Audio : " + param_value, FACE_MESSAGE_WAIT_TIME);
    int value = param_value.toInt();
    
    if (value == 1 && !audioStreamRunning) {
      Serial.println("Audio streaming starting.");
      startAudio(true);
    }

    if (value == 0 && audioStreamRunning) {
      Serial.println("Audio streaming stopping.");
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
esp_err_t webhandler_volume(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);
  
  const char* param_name = "power";
  String param_value = get_url_param(request,param_name, 4);

  if (param_value != "invalid") {
    roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Volume : " + param_value, FACE_MESSAGE_WAIT_TIME);

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

    roboFace.exec(action, text, index);
  }

  return ESP_OK;
}

/*
* Handler for imagetest
*/
esp_err_t webhandler_drawbmp(httpd_req_t *req) {

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
esp_err_t webhandler_wakeupsense(httpd_req_t *request) {

  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(request, HTTPD_TYPE_JSON);

  roboFace.exec(faceAction::DISPLAYTEXTSMALL, "Wake up Sense", FACE_MESSAGE_WAIT_TIME);

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
* Restart ESP32
*/
static esp_err_t webhandler_reset(httpd_req_t *req) {
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
* webhandler_wifimanager for httpd
*
* Wifimanager on demand
*/
static esp_err_t webhandler_wifimanager(httpd_req_t *req) {
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
* webhandler_config_erase for httpd
*
* Erase config and wifi settings and Restart ESP32
*/
static esp_err_t webhandler_config_erase(httpd_req_t *req) {
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
* Start / Stop Audio TCP listening
*/
void startAudio(bool start) {
  if (start) {
    audioStreamRunning = true;
  } else {
    tcpClientAudio.stop();
    audioStreamRunning = false;
  }
}

// Task function to handle incoming TCP data for Audio 
IRAM_ATTR void tcpReceiveAudioTask(void *pvParameters) {

    float sample;
    uint8_t *sample_ptr = (uint8_t *)&sample;
    uint32_t i2sValue;
    uint16_t bytesReceived;
    uint16_t i2s_written;

    while (true) {

      if (!tcpClientAudio || !tcpClientAudio.connected()) {
          tcpClientAudio = audioServer.accept();  // Check for new clients
      }

      while (tcpClientAudio && tcpClientAudio.connected()) {
          
          bytesReceived = tcpClientAudio.read(tcp_audio_buffer, AUDIO_TCP_CHUNK_SIZE);
          //Serial.println(bytesReceived);

          if (bytesReceived > 0) {
            float adjusted_gain = audio_volume * 0.05;
            // Convert samples
            for (size_t i = 0; i <= bytesReceived; i = i + 4) {

              sample_ptr[0] = tcp_audio_buffer[i + 0];
              sample_ptr[1] = tcp_audio_buffer[i + 1];
              sample_ptr[2] = tcp_audio_buffer[i + 2];
              sample_ptr[3] = tcp_audio_buffer[i + 3];
              
              i2sValue = static_cast<int32_t>(sample * (32767.0f * adjusted_gain) ) ;
              //i2sValue = static_cast<int32_t>(0) ;

              i2sbuffer[i] = static_cast<uint8_t>(i2sValue & 0xFF);
              i2sbuffer[i + 1] = static_cast<uint8_t>((i2sValue >> 8) & 0xFF);
              i2sbuffer[i + 2] = static_cast<uint8_t>((i2sValue >> 16) & 0xFF);
              i2sbuffer[i + 3] = static_cast<uint8_t>((i2sValue >> 24) & 0xFF);

            };

            i2s_written = I2S.write((uint8_t *)i2sbuffer, bytesReceived);
            if (i2s_written < bytesReceived) {
              Serial.println("I2S Buffer overflow");
            }
          } else {
              I2S.flush();
              vTaskDelay(30);
          }

          // Delay to prevent CPU overload
          vTaskDelay(5);
      }

      if (tcpClientAudio && !tcpClientAudio.connected()) {
        Serial.println("tcpClientAudio disconnected.");
        tcpClientAudio.stop();
      }

      vTaskDelay(200);
    }
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


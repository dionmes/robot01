/*
* WiFi & Config
*/
#include <WiFi.h>
#include <WiFiUdp.h>
#include <FS.h>
#include <SPIFFS.h>
#define FORMAT_SPIFFS_IF_FAILED true
#include <ArduinoJson.h>
#include <ArduinoHttpClient.h>

#include <WiFiManager.h>
WiFiManager wm;
bool shouldSaveConfig = false;

/*
* Master server or 'brain'
*/
IPAddress master_server_ip;

/*
* Webserver
*/
#include "esp_http_server.h"
httpd_handle_t server_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

/*
* Camera / streaming
*/
#define CAMERA_MODEL_XIAO_ESP32S3
#include "esp_camera.h"
#include "camera_pins.h"

#define PART_BOUNDARY "!!!FRAME!!!"
#define CAM_FRAMERATE "15"

camera_config_t camera_config;

static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

// Start/Stop streaming
bool camIsStreaming = false;

const char *cam_framerate = CAM_FRAMERATE;
const uint8_t framerate_delay = 1000 / (int)atol(cam_framerate);

/*
* UDP Mic streaming
*/
#include <ESP_I2S.h>
#include <math.h>
#define SAMPLE_RATE 16000
#define I2S_BUFFER_LEN 512

I2SClass I2S;

WiFiUDP Udp;
int outPort = 3000;

TaskHandle_t micStreamTaskHandle;
bool micIsStreaming = false;

uint16_t mic_gain_factor = 5;
int available_bytes, read_bytes;
char i2sBuffer[I2S_BUFFER_LEN * 2];

/*
* MicStreamTask task handling mic streaming over udp
*
* Streaming raw audio (f32le) from I2S over udp (port 3000)
*/
void MicStreamTask(void *pvParameters) {
  // vtask stop notify
  uint32_t notifyStopValue;

  String ip = master_server_ip.toString();
  Serial.printf("Micstream running to : %s , port %u \n", ip.c_str(), outPort);

  while (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) != pdTRUE) {
    // Get I2S data and place in data buffer

    I2S.read();
    available_bytes = I2S.available();
    if(available_bytes < I2S_BUFFER_LEN) {
      read_bytes = I2S.readBytes(i2sBuffer, available_bytes);
    } else {
      read_bytes = I2S.readBytes(i2sBuffer, I2S_BUFFER_LEN);
    }

    if (read_bytes > 0) {

      int16_t *sample_buffer = (int16_t *)i2sBuffer;

      int16_t samples_read = read_bytes / 2;
      float audio_block_float[samples_read];

      for (size_t i = 0; i < samples_read; i++) {
        sample_buffer[i] = mic_gain_factor * sample_buffer[i];
        //Max for signed int16_t is 2^15
        audio_block_float[i] = sample_buffer[i] / 32768.f;
      }

      // Send raw audio 32bit float samples over UDP
      Udp.beginPacket(master_server_ip, outPort);
      Udp.write((const uint8_t *)audio_block_float, read_bytes * 2);
      Udp.endPacket();
    } else {
        Serial.println("BAD ESP result... ");
    }
  }

  vTaskDelay(500 / portTICK_PERIOD_MS);
  vTaskDelete(NULL);

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////// handlers for httpd     ////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
* stream_handler for httpd
*
* Camera stream over http as multipart/x-mixed-replace with Content-Type: image/jpeg
*/
static esp_err_t stream_handler(httpd_req_t *req) {

  Serial.println("Stream request received.");

  camera_fb_t *fb = NULL;
  struct timeval _timestamp;
  struct timeval _old_timestamp;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[128];

  static int64_t last_frame = 0;
  if (!last_frame) {
    last_frame = esp_timer_get_time();
  }

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", cam_framerate);

  while (camIsStreaming) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      _timestamp.tv_sec = fb->timestamp.tv_sec;
      _timestamp.tv_usec = fb->timestamp.tv_usec;

      // Framerate delay
      float_t frameRateDelayDifference = (_timestamp.tv_usec - _old_timestamp.tv_usec) / 1000;
      _old_timestamp.tv_usec = _timestamp.tv_usec;

      if (frameRateDelayDifference > framerate_delay) {
        const TickType_t frameRateDelayDifferencexDelay = (frameRateDelayDifference - framerate_delay) / portTICK_PERIOD_MS;
        vTaskDelay(frameRateDelayDifferencexDelay);
      }

      _jpg_buf_len = fb->len;
      _jpg_buf = fb->buf;

    }

    // Send stream boundary header
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    } else {
      Serial.println("Capture failed during stream.");
    }

    // Send stream part header
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    } else {
      Serial.println("Sending failed during stream.");
    }

    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    } else {
      Serial.println("Capture failed during stream.");
    }

    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      Serial.println("Send frame failed");
      break;
    }
  }

  return res;
}

/*
* capture_handler for httpd
*
* Image / JPEG Capture
*/
static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  bool camWasStreaming = camIsStreaming;
  camIsStreaming = false;
  delay(50);

  fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char ts[32];
  snprintf(ts, 32, "%ld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);

  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);
  res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  camIsStreaming = camWasStreaming;

  return res;
}


/* 
* health_handler for httpd
*
* Response with ok
*/
static esp_err_t health_handler(httpd_req_t *req) {
  delay(1000);
  Serial.print("Health ok");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, "ok", 2);
}

/*
* helper for cmd_handler 
*
* Helper for cmd handler for camera settings
*/
static esp_err_t parse_get(httpd_req_t *req, char **obuf) {
  char *buf = NULL;
  size_t buf_len = 0;

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      *obuf = buf;
      return ESP_OK;
    }
    free(buf);
  }
  httpd_resp_send_404(req);
  return ESP_FAIL;
}

/*
* cmd_handler for httpd
*
* cmd handler for camera / mic settings & streaming
*/
static esp_err_t cmd_handler(httpd_req_t *req) {
  char *buf = NULL;
  char req_setting[32];
  char param_value[32];

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }

  if (httpd_query_key_value(buf, "setting", req_setting, sizeof(req_setting)) != ESP_OK) {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  int req_value = (httpd_query_key_value(buf, "param", param_value, sizeof(param_value)) == ESP_OK) ? atoi(param_value) : -1;

  free(buf);

  sensor_t *s = esp_camera_sensor_get();
  uint8_t res = 0;

  if (!strcmp(req_setting, "framesize")) {
    res = (req_value != -1) ? camera_config.frame_size = (framesize_t)req_value : camera_config.frame_size;
    camera_init();
  } else if (!strcmp(req_setting, "quality"))
    res = (req_value != -1) ? s->set_quality(s, req_value) : s->status.quality;
  else if (!strcmp(req_setting, "contrast"))
    res = (req_value != -1) ? s->set_contrast(s, req_value) : s->status.contrast;
  else if (!strcmp(req_setting, "brightness"))
    res = (req_value != -1) ? s->set_brightness(s, req_value) : s->status.brightness;
  else if (!strcmp(req_setting, "saturation"))
    res = (req_value != -1) ? s->set_saturation(s, req_value) : s->status.saturation;
  else if (!strcmp(req_setting, "gainceiling"))
    res = (req_value != -1) ? s->set_gainceiling(s, (gainceiling_t)req_value) : s->status.gainceiling;
  else if (!strcmp(req_setting, "colorbar"))
    res = (req_value != -1) ? s->set_colorbar(s, req_value) : s->status.colorbar;
  else if (!strcmp(req_setting, "awb"))
    res = (req_value != -1) ? s->set_whitebal(s, req_value) : s->status.awb;
  else if (!strcmp(req_setting, "agc"))
    res = (req_value != -1) ? s->set_gain_ctrl(s, req_value) : s->status.agc_gain;
  else if (!strcmp(req_setting, "aec"))
    res = (req_value != -1) ? s->set_exposure_ctrl(s, req_value) : s->status.aec_value;
  else if (!strcmp(req_setting, "hmirror"))
    res = (req_value != -1) ? s->set_hmirror(s, req_value) : s->status.hmirror;
  else if (!strcmp(req_setting, "vflip"))
    res = (req_value != -1) ? s->set_vflip(s, req_value) : s->status.vflip;
  else if (!strcmp(req_setting, "awb_gain"))
    res = (req_value != -1) ? s->set_awb_gain(s, req_value) : s->status.awb_gain;
  else if (!strcmp(req_setting, "agc_gain"))
    res = (req_value != -1) ? s->set_agc_gain(s, req_value) : s->status.agc_gain;
  else if (!strcmp(req_setting, "aec_value"))
    res = (req_value != -1) ? s->set_aec_value(s, req_value) : s->status.aec_value;
  else if (!strcmp(req_setting, "aec2"))
    res = (req_value != -1) ? s->set_aec2(s, req_value) : s->status.aec2;
  else if (!strcmp(req_setting, "dcw"))
    res = (req_value != -1) ? s->set_dcw(s, req_value) : s->status.dcw;
  else if (!strcmp(req_setting, "bpc"))
    res = (req_value != -1) ? s->set_bpc(s, req_value) : s->status.bpc;
  else if (!strcmp(req_setting, "wpc"))
    res = (req_value != -1) ? s->set_wpc(s, req_value) : s->status.wpc;
  else if (!strcmp(req_setting, "raw_gma"))
    res = (req_value != -1) ? s->set_raw_gma(s, req_value) : s->status.raw_gma;
  else if (!strcmp(req_setting, "lenc"))
    res = (req_value != -1) ? s->set_lenc(s, req_value) : s->status.lenc;
  else if (!strcmp(req_setting, "special_effect"))
    res = (req_value != -1) ? s->set_special_effect(s, req_value) : s->status.special_effect;
  else if (!strcmp(req_setting, "wb_mode"))
    res = (req_value != -1) ? s->set_wb_mode(s, req_value) : s->status.wb_mode;
  else if (!strcmp(req_setting, "ae_level"))
    res = (req_value != -1) ? s->set_ae_level(s, req_value) : s->status.ae_level;
  else if (!strcmp(req_setting, "camstreaming")) {
    if (req_value != -1) {
      camIsStreaming = (req_value == 1) ? true : false;
    }
    res = camIsStreaming ? 1 : 0;
  } else if (!strcmp(req_setting, "micstreaming")) {

    if (req_value != -1) {

      if (req_value == 1 && !micIsStreaming) {
        micIsStreaming = true;
        xTaskCreatePinnedToCore(MicStreamTask, "Micstream", 5000, NULL, 10, &micStreamTaskHandle, 0);
      } else if (micIsStreaming) {
        xTaskNotify( micStreamTaskHandle, 1, eSetValueWithOverwrite );
        micIsStreaming = false;
      }
    }
    res = micIsStreaming ? 1 : 0;

  } else if (!strcmp(req_setting, "micgain")) {
    if (req_value >= 0 and req_value <= 50) {
      mic_gain_factor = req_value;
    }
    res = mic_gain_factor;
  } else {
    Serial.printf("Unknown command: %s \n", req_setting);
    return httpd_resp_send_500(req);
  }

  String json_response_string = "{ \"" + String(req_setting) + "\", \"response\" : " + String(res) + " }";
  
  uint8_t response_len = json_response_string.length();
  char json_response[response_len];
  json_response_string.toCharArray(json_response, response_len);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, response_len);
}

/* 
* go2sleep_handler for httpd
*
* Put ESP into light sleep mode
*/
static esp_err_t go2sleep_handler(httpd_req_t *req) {
  delay(1000);
  Serial.print("Going to sleep.");

  gpio_wakeup_enable(GPIO_NUM_9, GPIO_INTR_HIGH_LEVEL);

  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();

  delay(3000);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
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
}

/*
* camera_init 
*
* de-initialize and initalize camera with camera config
* return bool (true if succesful)
*/
static bool camera_init() {
 
  delay(150);
  esp_camera_deinit();
  delay(150);

  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x \n", err);
    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////// Main //////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
* saveConfigCallback for wifimanager
*
* Indicate with shouldSaveConfig that config needs to be saved during setup
*/
void saveConfigCallback() {
  shouldSaveConfig = true;
}

/*
* Init
*/
void setup() {
  Serial.begin(115200);
  delay(1000);
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  Serial.printf("Wake up cause : %i \n", cause);

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
  wm.setMinimumSignalQuality();

  WiFiManagerParameter custom_masterserver("server", "Master server IP", config_master_ip, 40);
  wm.addParameter(&custom_masterserver);

  wm.setSaveConfigCallback(saveConfigCallback);

  bool wificonnect = wm.autoConnect("ESP-SENSE-CAM", "password123");
  
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

  Serial.print(WiFi.localIP());
  Serial.println("' to connect to server ");

  camera_config.ledc_channel = LEDC_CHANNEL_0;
  camera_config.ledc_timer = LEDC_TIMER_0;
  camera_config.pin_d0 = Y2_GPIO_NUM;
  camera_config.pin_d1 = Y3_GPIO_NUM;
  camera_config.pin_d2 = Y4_GPIO_NUM;
  camera_config.pin_d3 = Y5_GPIO_NUM;
  camera_config.pin_d4 = Y6_GPIO_NUM;
  camera_config.pin_d5 = Y7_GPIO_NUM;
  camera_config.pin_d6 = Y8_GPIO_NUM;
  camera_config.pin_d7 = Y9_GPIO_NUM;
  camera_config.pin_xclk = XCLK_GPIO_NUM;
  camera_config.pin_pclk = PCLK_GPIO_NUM;
  camera_config.pin_vsync = VSYNC_GPIO_NUM;
  camera_config.pin_href = HREF_GPIO_NUM;
  camera_config.pin_sccb_sda = SIOD_GPIO_NUM;
  camera_config.pin_sccb_scl = SIOC_GPIO_NUM;
  camera_config.pin_pwdn = PWDN_GPIO_NUM;
  camera_config.pin_reset = RESET_GPIO_NUM;
  camera_config.xclk_freq_hz = 20000000;
  camera_config.frame_size = FRAMESIZE_QVGA;
  camera_config.pixel_format = PIXFORMAT_JPEG;
  camera_config.grab_mode = CAMERA_GRAB_LATEST;
  camera_config.jpeg_quality = 10;
  camera_config.fb_count = 2;

  if (psramFound()) {
    Serial.printf("PSRAM Found for camera\n");
    camera_config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    camera_config.fb_location = CAMERA_FB_IN_DRAM;
  }

  // camera init
  esp_err_t err = esp_camera_init(&camera_config);

  // I2S Init for mic streaming
  I2S.setPinsPdmRx(42, 41);

  I2S.setPins(-1, 42, 41, -1, -1);
  if (! I2S.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("Failed to initialize I2S!");
  }
  // Webserver init.
  Serial.println("Webserver starting");

  httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
  httpd_config.max_uri_handlers = 16;

  httpd_uri_t health_uri = { .uri = "/health", .method = HTTP_GET, .handler = health_handler, .user_ctx = NULL };
  httpd_uri_t cmd_uri = { .uri = "/control", .method = HTTP_GET, .handler = cmd_handler, .user_ctx = NULL };
  httpd_uri_t capture_uri = { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL };
  httpd_uri_t go2sleep_url = { .uri = "/go2sleep", .method = HTTP_GET, .handler = go2sleep_handler, .user_ctx = NULL };
  httpd_uri_t reset_url = { .uri = "/reset", .method = HTTP_GET, .handler = reset_handler, .user_ctx = NULL };
  httpd_uri_t erase_url = { .uri = "/eraseconfig", .method = HTTP_GET, .handler = config_erase_handler, .user_ctx = NULL };

  if (httpd_start(&server_httpd, &httpd_config) == ESP_OK) {
    httpd_register_uri_handler(server_httpd, &health_uri);
    httpd_register_uri_handler(server_httpd, &cmd_uri);
    httpd_register_uri_handler(server_httpd, &capture_uri);
    httpd_register_uri_handler(server_httpd, &go2sleep_url);
    httpd_register_uri_handler(server_httpd, &reset_url);
    httpd_register_uri_handler(server_httpd, &erase_url);

    Serial.printf("Server on port: %d \n", httpd_config.server_port);
  } else {
    Serial.printf("Server failed");
  }

  httpd_config.server_port += 1;
  httpd_config.ctrl_port += 1;

  httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };

  if (httpd_start(&stream_httpd, &httpd_config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.printf("Stream server on port: %d \n", httpd_config.server_port);
  } else {
    Serial.printf("Stream server failed \n");
  }

  // Register with remote master
  Serial.println("Registering with master server");
  WiFiClient httpWifiInstance;
  HttpClient http(httpWifiInstance, config_master_ip, 5000);

  String httpPath = "/api/register_ip?ip=" + WiFi.localIP().toString() + "&device=robot01sense";

  int http_err = http.get(httpPath);
  if ( http_err == 0 ) {
    Serial.println(http.responseBody());
  } else {
    Serial.printf("Got status code: %u \n", http_err);
  }

}

void loop() {
  // Do nothing. Everything is done in another task by the web server
  delay(10000);
}

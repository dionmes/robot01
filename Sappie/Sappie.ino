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
#include <esp-fs-webserver.h>

/*
* SD Card
*/
#include "SdFat.h"
#include "sdios.h"
#define SPI_CLOCK SD_SCK_MHZ(25)
#define SD_CONFIG SdSpiConfig(14, SHARED_SPI, SPI_CLOCK)

SdFat32 sd;
File32 file;
File32 root;

/*
* I2S - UDP
*/
#include "AsyncUDP.h"
AsyncUDP udp;
#define AUDIO_UDP_PORT 9000 // UDP audio over I2S
bool audioStreamRunning = false; // If Audio is listening on udp

i2s_pin_config_t i2s_pins = {
  .bck_io_num = 16,
  .ws_io_num = 17,
  .data_out_num = 21,
  .data_in_num = I2S_PIN_NO_CHANGE
};

i2s_driver_config_t i2s_config = {
  .mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX ),
  .sample_rate = 16000,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
  .communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 128,
  .use_apll = false,
  .tx_desc_auto_clear = true,
  .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT,
};

int i2sPortNo = 0;

/*
* Sensors assignments
*/
VL53L1X vl53l1x_sensor;
wrapper_bno08x wrapper_bno08x;

/*
* LED Display
*/
Adafruit_SSD1306 ledMatrix(128, 64, &Wire, -1);
roboFace roboFace;

/*
* Body control
*/
bodyControl bodyControl;

#define FILESYSTEM LittleFS
FSWebServer WebServer(FILESYSTEM, 80);
bool apMode = false;

/*
* Task handlers
*/
TaskHandle_t WebserverTaskHandle;
TaskHandle_t wrapper_bno08xUpdateTaskHandle;
TaskHandle_t AudioStreamTaskHandle;

/*
* Initialization/setup
*/
void setup(){

  // Serial
  Serial.begin(115200);
  Serial.println("Booting Sappie\n");

  // Wire init.
  Wire.begin();
  Wire.setClock(400000); // use 400 kHz I2C

  //Random seed
  randomSeed(analogRead(0));

  // Roboface / LED Display init.
  roboFace.begin();
  roboFace.exec( faceAction::DISPLAYTEXTLARGE, "Boot");

  // XAIO Sense/Camera wake up pin assignment
  pinMode(25, OUTPUT);
  digitalWrite(25, LOW);

  // Body init.
  roboFace.exec( faceAction::DISPLAYTEXTLARGE, "Body");
  bodyControl.begin();
  bodyControl.bothArmsDown();

  // Webserver init.
  roboFace.exec( faceAction::DISPLAYTEXTLARGE, "Server");
  startWebserver();

  // Audio / I2S init.
  roboFace.exec( faceAction::DISPLAYTEXTLARGE, "AUDIO/I2S");

  if (i2s_driver_install((i2s_port_t)i2sPortNo, &i2s_config, 0, NULL) != ESP_OK)
  {
    Serial.print("I2S Failure to initialize");
  }
  i2s_set_pin((i2s_port_t)i2sPortNo, &i2s_pins);

  startAudio(true);

  // SD Card init.
  roboFace.exec( faceAction::DISPLAYTEXTLARGE, "SD Card");
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt();
  }

  // Starting VL53L1X 
  roboFace.exec( faceAction::DISPLAYTEXTLARGE, "VL53L1X");
  Serial.println("Booting vl53l1x (range) sensor\n");

  vl53l1x_sensor.setTimeout(500);
  if (vl53l1x_sensor.init()) {
    vl53l1x_sensor.setDistanceMode(VL53L1X::Long);
    vl53l1x_sensor.setMeasurementTimingBudget(50000);
    vl53l1x_sensor.startContinuous(50);
  } else {
      Serial.println("Failed to detect and initialize vl53l1x (range) sensor!");
  }

  // Starting wrapper_bno08x
  roboFace.exec( faceAction::DISPLAYTEXTLARGE, "BNO08x");
  Serial.println("Booting wrapper_bno08x (gyro) sensor\n");

  if (!wrapper_bno08x.begin()) {
      Serial.println("Failed to detect and initialize wrapper_bno08x (gyro) sensor!");
  }

  roboFace.exec( faceAction::DISPLAYTEXTLARGE, "Tasks");
  // Starting tasks
  Serial.println("Tasks starting\n");

  Serial.println("Webserver task starting\n");
  xTaskCreatePinnedToCore(WebServerTask, "Webserver", 8192, NULL, 5, &WebserverTaskHandle, 0);  
  Serial.println("wrapper_bno08x update task starting\n");
  xTaskCreatePinnedToCore(bno08xUpdateTask, "wrapper_bno08x", 4096, NULL, 10, &wrapper_bno08xUpdateTaskHandle, 0);
  Serial.println("Boot complete...\n");

  roboFace.exec(faceAction::DISPLAYIMG);
  delay(2000);
  
  //vTaskDelete( NULL );

}

/*
* Main task
*/
void loop() {

  int wait = int(random(3000, 8000));

  delay(wait);
  int action = int(random(4,12));
  int actionWait = int(random(50,200));
  roboFace.exec(action,"",actionWait);

}

////////////////////////////////  Tasks  /////////////////////////////////////////

/*
* Webserver continues running, executing as vtask
*/
void WebServerTask(void* pvParameters) {
  Serial.print("Webserver running on core ");
  Serial.println(xPortGetCoreID());
  for(;;){ 
    WebServer.run();
    delay(200);
  }
}

/*
* bno08x continues updates, executing as vtask
*/
void bno08xUpdateTask(void* pvParameters) {
  for(;;){ 
    wrapper_bno08x.update();
    delay(200);
  }
}

////////////////////////////////  Web handlers  /////////////////////////////////////////

/*
* Handler to get vl53l1x values
*/
void webhandler_VL53L1X_Info() {
  vl53l1x_sensor.read();
  
  uint16_t range_mm = vl53l1x_sensor.ranging_data.range_mm;
  uint8_t range_status = vl53l1x_sensor.ranging_data.peak_signal_count_rate_MCPS;
  float peak_signal = vl53l1x_sensor.ranging_data.peak_signal_count_rate_MCPS;
  float ambient_count = vl53l1x_sensor.ranging_data.ambient_count_rate_MCPS;

  String json = "{\"range_mm\": " + String(range_mm) + ",\"range_status\": " + String(range_status) + ",\"peak_signal\": " + String(peak_signal)
              + ",\"ambient_count\" : " + String(ambient_count) + " }";
  
  WebServer.send(200, "application/json", json);
}

/*
* Handler to get bno08x values
*/
void webhandler_wrapper_bno08xInfo() {

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
  
  WebServer.send(200, "application/json", json);

}

/*
* Handler to show LittleFS stats
*/
void FileSystemInfoPage() {
  File f = LittleFS.open("/FileSystemInfo.html", "r");
  String html = f.readString();
  f.close();
  html.replace("filesystem_name", "LittleFS");
  html.replace("totalBytes", String(LittleFS.totalBytes()) );
  html.replace("usedBytes" , String(LittleFS.usedBytes()) );
  html.replace("freeBytes", String(LittleFS.totalBytes() - LittleFS.usedBytes()) );

  WebServer.send(200, "text/HTML", html);
}

/*
* Handler to enable/disable audiostream
*/
void audiostream_handler() {
  if(WebServer.hasArg("on")) {
    int value = WebServer.arg("on").toInt();
    if (value == 1 && !audioStreamRunning) {
      Serial.println("Audio streaming starting\n");
      startAudio(true);
      WebServer.send(200, "text/plain", "Audiostream task started");
      return;
    } 
    if (value == 0 && audioStreamRunning) {
      Serial.println("Audio streaming stopping\n");
      startAudio(false);
      WebServer.send(200, "text/plain", "Audiostream task stopped");
      return;
    }
    WebServer.send(200, "text/plain", "Invalid argument");
  } else {
    WebServer.send(422, "application/json", "Missing argument");
  }
}

/*
* Handler for body test task
*/
void bodyAction_handler() {
  
  WebServer.send(200, "text/plain", "Body Test started");
}

/*
* Handler for displaying text
*/
void displaytext_handler() {
    if(WebServer.hasArg("text")) {
      WebServer.send(200, "text/plain", "Text display starting.");
    } else {
      WebServer.send(200, "text/plain", "No text");
    }
}

/*
* Handler waking up XAIO sense/camera from sleep
*/
void wakeupsense_handler() {
  digitalWrite(25, HIGH);
  delay(200);
  digitalWrite(25, LOW);
  WebServer.send(200, "text/plain", "Wake up sent.");
}

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
        const uint8_t* data = (const uint8_t *)packet.data();
        
        for(size_t i = 0 ; i <= packet.length() ; i=i+4) {
          float sample;
          uint8_t *f_ptr = (uint8_t *) &sample;
          f_ptr[0] = data[i+0];
          f_ptr[1] = data[i+1];
          f_ptr[2] = data[i+2];
          f_ptr[3] = data[i+3];

          int32_t i2sValue = static_cast<int32_t>((sample * 32767.0f) / 2.0f); 
          esp_err_t err = i2s_write((i2s_port_t)i2sPortNo, (const char*)&i2sValue, sizeof(data), &bytes_written, portMAX_DELAY);
        }
      });

      audioStreamRunning = true;

    } else {
      udp.close();
      audioStreamRunning = false;
    }
  } 
}

/*
* Start Webserver
*/
void startWebserver() {
  
  Serial.println("Webserver -> Booting Filesystem\n");
  // FILESYSTEM INIT
  startFilesystem();

  // Try to connect to stored SSID, start AP if fails after timeout
  WebServer.setAP("ESP_AP", "123456789");
  IPAddress myIP = WebServer.startWiFi(15000);
  Serial.println("\n");

  // Add custom request handlers to webserver
  WebServer.on("/FileSystemInfo", HTTP_GET, FileSystemInfoPage);
  WebServer.on("/VL53L1X_Info", HTTP_GET, webhandler_VL53L1X_Info);
  WebServer.on("/BNO08X_Info", HTTP_GET, webhandler_wrapper_bno08xInfo);
  WebServer.on("/audiostream", HTTP_GET, audiostream_handler );
  WebServer.on("/body", HTTP_GET, body_handler );
  WebServer.on("/displaytext", HTTP_GET, displaytext_handler );
  WebServer.on("/wakeupsense", HTTP_GET, wakeupsense_handler );

  Serial.println("Starting Webserver\n");
  // Start webserver
  WebServer.begin();
  Serial.print(F("ESP Web Server started on IP Address: "));
  Serial.println(myIP);
  Serial.println(F("Open /setup page to configure optional parameters"));

  // Enable content editor
  WebServer.enableFsCodeEditor(getFsInfo);

}

/*
* Filesystem for webserver
*/
void startFilesystem() {
  // FILESYSTEM INIT
  if ( !FILESYSTEM.begin()) {
    Serial.println("ERROR on mounting filesystem. It will be formmatted!");
    FILESYSTEM.format();
    ESP.restart();
  }
  WebServer.printFileList(LittleFS, Serial, "/", 2);
}

/*
* Getting FS info (total and free bytes) is strictly related to
* filesystem library used (LittleFS, FFat, SPIFFS etc etc) and ESP framework
* ESP8266 FS implementation has methods for total and used bytes (only label is missing)
*/
#ifdef ESP32
void getFsInfo(fsInfo_t* fsInfo) {
	fsInfo->fsName = "LittleFS";
	fsInfo->totalBytes = LittleFS.totalBytes();
	fsInfo->usedBytes = LittleFS.usedBytes();
}
#else
void getFsInfo(fsInfo_t* fsInfo) {
	fsInfo->fsName = "LittleFS";
}
#endif

#include <esp-fs-webserver.h>  // https://github.com/cotestatnt/esp-fs-webserver

#include <FS.h>
#include <LittleFS.h>
#include <Wire.h>
#include <VL53L1X.h>
#include "wrapper_bno08x.h"
#include "roboFace.h"
#include "motorControl.h"

// Audio streaming
#include "AudioFileSourceICYStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// Icecast stream URL
const char *URL="http://xavier01.local:8000/speech.mp3";
bool audioStreamRunning = false;

const int I2S_BCLK_pin = 16;
const int I2S_WCLK_pin = 17;
const int I2S_DOUT_pin = 21;

//Sensors
VL53L1X vl53l1x_sensor;
wrapper_bno08x wrapper_bno08x;

// LED Display
roboFace roboFace;
String displayText;

// Motor control
motorControl motorControl;

#define FILESYSTEM LittleFS
FSWebServer WebServer(FILESYSTEM, 80);
bool apMode = false;

TaskHandle_t Webserver;
TaskHandle_t wrapper_bno08xUpdate;
TaskHandle_t AudioStream;
TaskHandle_t motor;
TaskHandle_t face;

void setup(){
  Serial.begin(115200);
  Serial.println("Booting Sappie\n");
  
  // XAIO Sense/Camera wake up ping
  pinMode(25, OUTPUT);
  digitalWrite(25, LOW);

  roboFace.begin();
  
  Wire.begin();
  Wire.setClock(400000); // use 400 kHz I2C

  motorControl.begin(Wire);
  
  startWebserver();

  // Starting wrapper_bno08x
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
  Serial.println("Booting wrapper_bno08x (gyro) sensor\n");
  if (!wrapper_bno08x.begin()) {
      Serial.println("Failed to detect and initialize wrapper_bno08x (gyro) sensor!");
  }

  Serial.println("Boot complete...\n");

  // Starting tasks
  Serial.println("Tasks starting\n");

  motorControl.bothArmsDown();

  Serial.println("Display start task starting\n");
  xTaskCreatePinnedToCore(displayTextTask, "displayText", 5000, NULL, 10, &face, 0);

  Serial.println("Webserver task starting\n");
  xTaskCreatePinnedToCore(WebServerTask, "Webserver", 10000, NULL, 5, &Webserver, 0);
  
  Serial.println("wrapper_bno08x update task starting\n");
  xTaskCreatePinnedToCore(bno08xUpdateTask, "wrapper_bno08x", 5000, NULL, 10, &wrapper_bno08xUpdate, 0);

  roboFace.face_smile();

  vTaskDelete(NULL);

}

void loop() {
}

////////////////////////////////  Tasks  /////////////////////////////////////////

void WebServerTask(void* pvParameters) {
  Serial.print("Webserver running on core ");
  Serial.println(xPortGetCoreID());
  for(;;){ 
    WebServer.run();
    delay(200);
  }
}

void bno08xUpdateTask(void* pvParameters) {
  for(;;){ 
    wrapper_bno08x.update();
    delay(200);
  }
}

void audioStreamTask(void* pvParameters) {

  AudioGeneratorMP3 *audio;
  AudioFileSourceICYStream *file;
  AudioFileSourceBuffer *buff;
  AudioOutputI2S *out;
  
  audio = new AudioGeneratorMP3();

  for(;;){ 
   static int lastms = 0;

    if (audio->isRunning()) {
      
      if (millis()-lastms > 1000) {
        lastms = millis();
      }
      if (!audio->loop()) audio->stop();
    } else {
      Serial.printf("audio done\n");

      file = new AudioFileSourceICYStream(URL);
      buff = new AudioFileSourceBuffer(file, 4096);
      buff->RegisterStatusCB(StatusCallback, (void*)"buffer");
      out = new AudioOutputI2S();
      out->SetGain(50);
      out->SetOutputModeMono(true);

      out->SetPinout(I2S_BCLK_pin, I2S_WCLK_pin, I2S_DOUT_pin);

      audio = new AudioGeneratorMP3();
      audio->RegisterStatusCB(StatusCallback, (void*)"audio");
      audio->begin(buff, out);

      delay(1000);
    }
    delay(500);
  }
  vTaskDelete( NULL );
}

void motorTestTask(void* pvParameters) {
  motorControl.test();
  vTaskDelete( NULL );
}

void displayTextTask(void* pvParameters) {
  roboFace.displayText(displayText);
  vTaskDelete( NULL );
}

////////////////////////////////  Web handlers  /////////////////////////////////////////

// Handler to get vl53l1x values
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

// Handler to get bno08x values
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

// Handler to show LittleFS stats
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

// Handler to enable/disable audiostream
void audiostream_handler() {

  if(WebServer.hasArg("on")) {
    
    int value = WebServer.arg("on").toInt();
    
    if (value == 1 && !audioStreamRunning) {
      Serial.println("Audio streaming starting\n");

      audioStreamRunning = true;
      xTaskCreatePinnedToCore(audioStreamTask, "AudioStream", 5000, NULL, 5, &AudioStream, 1);

      WebServer.send(200, "text/plain", "Audiostream task started");
      return;
    } 
    
    if (value == 0 && audioStreamRunning) {
      Serial.println("Audio streaming stopping\n");

      audioStreamRunning = false;
      vTaskDelete(AudioStream);

      WebServer.send(200, "text/plain", "Audiostream task stopped");
      return;
    }

    WebServer.send(200, "text/plain", "Invalid argument");

  } else {
    WebServer.send(422, "application/json", "Missing argument");
  }

 }

// Handler for motor test task
void motortest_handler() {
  if (eTaskGetState(&motor) != eRunning) {
    xTaskCreatePinnedToCore(motorTestTask, "MotorTest", 5000, NULL, 5, &motor, 0);
    WebServer.send(200, "text/plain", "Motor Test started");
  } else {
    WebServer.send(200, "text/plain", "Motor Test already running");
  }

}

// Handler for motor test task
void displaytext_handler() {
  if (eTaskGetState(&face) != eRunning) {
    if(WebServer.hasArg("text")) {
      displayText = WebServer.arg("text");
      xTaskCreatePinnedToCore(displayTextTask, "displayText", 5000, NULL, 10, &face, 0);
      WebServer.send(200, "text/plain", "Text display starting.");
    } else {
      WebServer.send(200, "text/plain", "No text");
    }
  } else {
    WebServer.send(200, "text/plain", "Text display already running");
  }
}

void wakeupsense_handler() {
  
  digitalWrite(25, HIGH);
  delay(200);
  digitalWrite(25, LOW);
  WebServer.send(200, "text/plain", "Wake up sent.");

}

////////////////////////////////  Webserver  /////////////////////////////////////////

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
  WebServer.on("/motortest", HTTP_GET, motortest_handler );
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

////////////////////////////////  Filesystem for webserver /////////////////////////////////////////
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

//////////////////////////////////////////////////////////////////////////////////
// Audio streaming
//////////////////////////////////////////////////////////////////////////////////
// Called when a metadata event occurs (i.e. an ID3 tag, an ICY block, etc.
void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string)
{
  const char *ptr = reinterpret_cast<const char *>(cbData);
  (void) isUnicode; // Punt this ball for now
  // Note that the type and string may be in PROGMEM, so copy them to RAM for printf
  char s1[32], s2[64];
  strncpy_P(s1, type, sizeof(s1));
  s1[sizeof(s1)-1]=0;
  strncpy_P(s2, string, sizeof(s2));
  s2[sizeof(s2)-1]=0;
  Serial.printf("METADATA(%s) '%s' = '%s'\n", ptr, s1, s2);
  Serial.flush();
}

// Called when there's a warning or error (like a buffer underflow or decode hiccup)
void StatusCallback(void *cbData, int code, const char *string)
{
  const char *ptr = reinterpret_cast<const char *>(cbData);
  // Note that the string may be in PROGMEM, so copy it to RAM for printf
  char s1[64];
  strncpy_P(s1, string, sizeof(s1));
  s1[sizeof(s1)-1]=0;
  Serial.printf("STATUS(%s) '%d' = '%s'\n", ptr, code, s1);
  Serial.flush();
}


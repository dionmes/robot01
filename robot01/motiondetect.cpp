#include <stdint.h>
#include "HardwareSerial.h"
//
// Wrapper for Adafruit_BNO08x
//
#include <Adafruit_BNO08x.h>
#include "motiondetect.h"

# define SENSOR_INIT_RETRIES 5
Adafruit_BNO08x motionsensor(-1);

TaskHandle_t motiondetectUpdateTaskHandle;

motiondetect::motiondetect() {
  motiondetectData_t sensorData;
}

bool motiondetect::begin(int task_core, int task_priority) {
  
  // Try to initialize!
  int n = 0;
  while (! motionsensor.begin_I2C() ) {
    n++;
    if (n > SENSOR_INIT_RETRIES) {
      Serial.println("BNO08X init failed.");
      return false;
    }
    Serial.println("BNO08X reset. Init retry.");
    vTaskDelay(500);
    motionsensor.hardwareReset();
    vTaskDelay(500);
  }

  motionsensor.enableReport(SH2_LINEAR_ACCELERATION);
  motionsensor.enableReport(SH2_ARVR_STABILIZED_RV);
  motionsensor.enableReport(SH2_SHAKE_DETECTOR);

  xTaskCreatePinnedToCore(sensorUpdateTask, "motiondetect", 4096, (void *)&sensorData, task_priority, &motiondetectUpdateTaskHandle, task_core);

  return true;
}

/*
* Motion sensor continues updates, executing as vtask
*/
void motiondetect::sensorUpdateTask(void *pvParameters) {

  motiondetect::motiondetectData_t *sensorData = (motiondetect::motiondetectData_t *) pvParameters;
  sh2_SensorValue_t sensorValue;

  float qr;
  float qi;
  float qj;
  float qk;
  float sqr;
  float sqi;
  float sqj;
  float sqk;

  while (true) {

    while (motionsensor.getSensorEvent(&sensorValue)) {
      
      sh2_ShakeDetector_t detection;
      sensorData->accuracy = sensorValue.status;

      switch (sensorValue.sensorId) {
        case SH2_LINEAR_ACCELERATION:
          sensorData->linearAcceleration_x = sensorValue.un.linearAcceleration.x;
          sensorData->linearAcceleration_y = sensorValue.un.linearAcceleration.y;
          sensorData->linearAcceleration_z = sensorValue.un.linearAcceleration.z;

          break;

        case SH2_ARVR_STABILIZED_RV:

          qr = sensorValue.un.arvrStabilizedRV.real;
          qi = sensorValue.un.arvrStabilizedRV.i;
          qj = sensorValue.un.arvrStabilizedRV.j;
          qk = sensorValue.un.arvrStabilizedRV.k;
          sqr = sq(qr);
          sqi = sq(qi);
          sqj = sq(qj);
          sqk = sq(qk);

          sensorData->yaw = atan2(2.0 * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr));
          sensorData->pitch = asin(-2.0 * (qi * qk - qj * qr) / (sqi + sqj + sqk + sqr));
          sensorData->roll = atan2(2.0 * (qj * qk + qi * qr), (-sqi - sqj + sqk + sqr));

          sensorData->yaw *= -RAD_TO_DEG;
          sensorData->pitch *= RAD_TO_DEG;
          sensorData->roll *= RAD_TO_DEG;

          sensorData->ypr_accuracy = sensorValue.un.arvrStabilizedRV.accuracy;
          
          break;

        case SH2_SHAKE_DETECTOR: 
            detection = sensorValue.un.shakeDetector;
            sensorData->shake = detection.shake;

          break;
      }

      vTaskDelay(100);
    }
  }
}


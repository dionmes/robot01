#include "HardwareSerial.h"
#include "sh2.h"
// Wrapper for Adafruit_BNO08x
//
#include <Adafruit_BNO08x.h>
#include "motiondetect.h"

# define BNO08x_INIT_RETRIES 5
Adafruit_BNO08x motionsensor(-1);

TaskHandle_t motiondetectUpdateTaskHandle;

motiondetect::motiondetect() {
  motiondetectData_t mData;
}

bool motiondetect::begin(int task_core, int task_priority) {
  
  // Try to initialize!
  for (int i = 0; i < BNO08x_INIT_RETRIES; i++) {
    if ( !motionsensor.begin_I2C() ) {
      Serial.println("BNO08X reset.");
      motionsensor.hardwareReset();
      vTaskDelay(500);
    } else {
      // initialized
      break;
    }
    return false;
  }

  motionsensor.enableReport(SH2_LINEAR_ACCELERATION);
  motionsensor.enableReport(SH2_ARVR_STABILIZED_RV);
  motionsensor.enableReport(SH2_SHAKE_DETECTOR);

  xTaskCreatePinnedToCore(motionsensorUpdateTask, "motiondetect", 4096, (void *)&mData, task_priority, &motiondetectUpdateTaskHandle, task_core);

  return true;
}

/*
* bno08x continues updates, executing as vtask
*/
void motiondetect::motionsensorUpdateTask(void *pvParameters) {

  motiondetect::motiondetectData_t *mData = (motiondetect::motiondetectData_t *) pvParameters;
  sh2_SensorValue_t sensorValue;

  float qr;
  float qi;
  float qj;
  float qk;
  float sqr;
  float sqi;
  float sqj;
  float sqk;

  for (;;) {

    while (motionsensor.getSensorEvent(&sensorValue)) {
      
      sh2_ShakeDetector_t detection;
      mData->accuracy = sensorValue.status;

      switch (sensorValue.sensorId) {
        case SH2_LINEAR_ACCELERATION:
          mData->linearAcceleration_x = sensorValue.un.linearAcceleration.x;
          mData->linearAcceleration_y = sensorValue.un.linearAcceleration.y;
          mData->linearAcceleration_z = sensorValue.un.linearAcceleration.z;
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

          mData->yaw = atan2(2.0 * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr));
          mData->pitch = asin(-2.0 * (qi * qk - qj * qr) / (sqi + sqj + sqk + sqr));
          mData->roll = atan2(2.0 * (qj * qk + qi * qr), (-sqi - sqj + sqk + sqr));

          mData->yaw *= -RAD_TO_DEG;
          mData->pitch *= RAD_TO_DEG;
          mData->roll *= RAD_TO_DEG;

          break;

        case SH2_SHAKE_DETECTOR: 
            detection = sensorValue.un.shakeDetector;
            mData->shake = detection.shake;

          break;
      }
      vTaskDelay(200);
    }
  }
}


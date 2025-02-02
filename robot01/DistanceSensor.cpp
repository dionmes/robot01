//
// Wrapper for Adafruit_BNO08x
//
#include <VL53L1X.h>
#include "DistanceSensor.h"

# define SENSOR_INIT_RETRIES 5
VL53L1X tofSensor;

TaskHandle_t sensorUpdateTaskHandle;

DistanceSensor::DistanceSensor() {
  DistanceSensorData_t sensorData;
}

bool DistanceSensor::begin(int task_core, int task_priority) {

  // Try to initialize!
  tofSensor.setTimeout(50);
  int n = 0;
  while (! tofSensor.init() ) {
    n++;
    if (n > SENSOR_INIT_RETRIES) {
      Serial.println("VL53L1X init failed.");
      return false;
    }
    Serial.println("VL53L1X init retry.");
    vTaskDelay(200);
  }

  tofSensor.setDistanceMode(VL53L1X::Short);
  tofSensor.setMeasurementTimingBudget(50000);
  tofSensor.startContinuous(50);

  delay(100);

  xTaskCreatePinnedToCore(sensorUpdateTask, "distancesensor", 4096, (void *)&sensorData, task_priority, &sensorUpdateTaskHandle, task_core);

  return true;
}

/*
* Distance sensor continues updates, executing as vtask
*/
void DistanceSensor::sensorUpdateTask(void *pvParameters) {

  while (true) {
    DistanceSensor::DistanceSensorData_t *sensorData = (DistanceSensor::DistanceSensorData_t *) pvParameters;
    
    tofSensor.read();

    sensorData->range_mm = tofSensor.ranging_data.range_mm;
    sensorData->range_status = tofSensor.ranging_data.range_status;
    sensorData->peak_signal = tofSensor.ranging_data.peak_signal_count_rate_MCPS;
    sensorData->ambient_count = tofSensor.ranging_data.ambient_count_rate_MCPS;

    vTaskDelay(200);
  }
  
}


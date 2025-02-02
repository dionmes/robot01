//
// Wrapper for VL53L1X ToF sensor
//
#include <VL53L1X.h>

#ifndef DistanceSensor_H
#define DistanceSensor_H

class DistanceSensor {
  public:
    DistanceSensor();

    bool begin(int task_core = 1, int task_priority = 10);
    
    //  Sensor data struct
    typedef struct {
      float range_mm = 0;
      uint8_t range_status = 0;
      float peak_signal = 0;
      float ambient_count = 0;

    } DistanceSensorData_t;

    DistanceSensorData_t sensorData;

  private:
    static void sensorUpdateTask(void *pvParameters);

};

#endif
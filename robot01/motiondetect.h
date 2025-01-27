#include <sys/_stdint.h>
// Wrapper for Adafruit_BNO08x
//
#include <Adafruit_BNO08x.h>

#ifndef motiondetect_H
#define motiondetect_H

class motiondetect {
  public:
    motiondetect();
    bool begin(int task_core = 1, int task_priority = 10);
    
    //  Sensor data struct
    typedef struct {

      float linearAcceleration_x = 0;
      float linearAcceleration_y = 0;
      float linearAcceleration_z = 0;

      float yaw = 0;
      float pitch = 0;
      float roll = 0;

      uint8_t accuracy = 0;
      uint16_t shake = 0;

    } motiondetectData_t;

    motiondetectData_t mData;

  private:
    static void motionsensorUpdateTask(void *pvParameters);

};

#endif
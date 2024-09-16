// Wrapper for Adafruit_BNO08x
// 

#ifndef WRAPPER_BNO08X_H
#define WRAPPER_BNO08X_H
#include <Adafruit_BNO08x.h>

class wrapper_bno08x
{
   public:
    wrapper_bno08x();
    
    bool begin();
    void update();
    
    float accelerometer_x;
    float accelerometer_y;
	  float accelerometer_z;

	  float gyroscope_x; 
	  float gyroscope_y; 
	  float gyroscope_z;
    
    float geoMagRotationVector_real;
	  float geoMagRotationVector_i; 
	  float geoMagRotationVector_j; 
	  float geoMagRotationVector_k; 
    
    String stability;
    String shake;

   private:
    void getValues();

    Adafruit_BNO08x bno08x;
    sh2_SensorValue_t sensorValue;
};

#endif
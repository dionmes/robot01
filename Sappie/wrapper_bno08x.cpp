// Wrapper for Adafruit_BNO08x
// 
#include <Adafruit_BNO08x.h>
#include "wrapper_bno08x.h"

wrapper_bno08x::wrapper_bno08x() {

	Adafruit_BNO08x bno08x(-1);
	sh2_SensorValue_t sensorValue;

  	accelerometer_x = 0;
    accelerometer_y = 0;
	accelerometer_z = 0;

	gyroscope_x = 0;
	gyroscope_y = 0;
	gyroscope_z = 0;
    
    geoMagRotationVector_real = 0;
	geoMagRotationVector_i = 0;
	geoMagRotationVector_j = 0;
	geoMagRotationVector_k = 0;
    
    stability = "Unknown";
    shake = "Unknown";
}

bool wrapper_bno08x::begin() {

  // Try to initialize!
  if (!bno08x.begin_I2C()) {
	for (int i = 0; i <= 10; i++) {
	  delay(500);	
	}
	return false;
  }

  bno08x.enableReport(SH2_ACCELEROMETER);
  bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED);
  bno08x.enableReport(SH2_GEOMAGNETIC_ROTATION_VECTOR);
  bno08x.enableReport(SH2_STABILITY_CLASSIFIER);
  bno08x.enableReport(SH2_SHAKE_DETECTOR);

  return true;
}


// Update values if Sensor event
void wrapper_bno08x::update() {
  while (bno08x.getSensorEvent(&sensorValue)) {
	  getValues();
	  delay(50);
  }
  return;  
}

void wrapper_bno08x::getValues() {
  
  switch (sensorValue.sensorId) {

  case SH2_ACCELEROMETER:

    gyroscope_x = sensorValue.un.accelerometer.x;
    gyroscope_y = sensorValue.un.accelerometer.y;
    gyroscope_z = sensorValue.un.accelerometer.z;
    break;

  case SH2_GYROSCOPE_CALIBRATED:
  	accelerometer_x = sensorValue.un.gyroscope.x;
    accelerometer_y = sensorValue.un.gyroscope.y;
	  accelerometer_z = sensorValue.un.gyroscope.z;
    break;
    
  case SH2_GEOMAGNETIC_ROTATION_VECTOR:
	geoMagRotationVector_real = sensorValue.un.geoMagRotationVector.real;
	geoMagRotationVector_i = sensorValue.un.geoMagRotationVector.i;
	geoMagRotationVector_j = sensorValue.un.geoMagRotationVector.j;
	geoMagRotationVector_k = sensorValue.un.geoMagRotationVector.k;
    break;
    
  case SH2_STABILITY_CLASSIFIER: {
    sh2_StabilityClassifier_t stability_class = sensorValue.un.stabilityClassifier;
    
    switch (stability_class.classification) {
    case STABILITY_CLASSIFIER_UNKNOWN:
      stability = "Unknown";
      break;
    case STABILITY_CLASSIFIER_ON_TABLE:
      stability = "On Table";
      break;
    case STABILITY_CLASSIFIER_STATIONARY:
      stability = "Stationary";
      break;
    case STABILITY_CLASSIFIER_STABLE:
      stability = "Stable";
      break;
    case STABILITY_CLASSIFIER_MOTION:
      stability = "In Motion";
      break;
    }
    break;
  }

  case SH2_SHAKE_DETECTOR: {
    sh2_ShakeDetector_t detection = sensorValue.un.shakeDetector;
    switch (detection.shake) {
    case SHAKE_X:
      shake = "X";
      break;
    case SHAKE_Y:
      shake = "Y";
      break;
    case SHAKE_Z:
      shake = "Z";
      break;
    default:
      shake = "None";
      break;
    }
  }

}
}

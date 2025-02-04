#include "HardwareSerial.h"
/**
  Body hardware Control helper class for robot01 via the two MCP23017 extenders
  Also handles the switches and leds in the Robosapien body.
**/

#include <Wire.h>
#include <MCP23017.h>
#include "bodyControl.h"
#include "DistanceSensor.h"
#include "motiondetect.h"
#include <ArduinoHttpClient.h>

#define STEPWAIT 50

// mm before blocking forward walk
#define FW_DISTANCE_TO_STOP 350
// Tolerance for heading while turning
#define TURN_TOLERANCE_DEGREES 3
// Max turn error count
#define MAX_TURN_ERROR 5
// Max turn count
#define MAX_TURN_COUNT 160

// Body actions Queue size
#define BODYACTIONS_QUEUE_SIZE 3

// define pins
#define LEFTLOWERARM_DOWN_PIN 9   // mcp1
#define LEFTLOWERARM_UP_PIN 8     // mcp1
#define LEFTUPPERARM_UP_PIN 11    // mcp1
#define LEFTUPPERARM_DOWN_PIN 10  // mcp1
#define RIGHTLOWERARM_DOWN_PIN 6  // mcp2
#define RIGHTLOWERARM_UP_PIN 7    // mcp2
#define RIGHTUPPERARM_UP_PIN 5    // mcp2
#define RIGHTUPPERARM_DOWN_PIN 4  // mcp2
#define LEFTLEG_FORWARD_PIN 14    // mcp1
#define LEFTLEG_BACK_PIN 15       // mcp1
#define RIGHTLEG_FORWARD_PIN 0    // mcp2
#define RIGHTLEG_BACK_PIN 1       // mcp2
#define HIP_RIGHT_PIN 12          // mcp1
#define HIP_LEFT_PIN 13           // mcp1
#define LEFTHAND_LIGHT_PIN 3      // mcp2
#define RIGHTHAND_LIGHT_PIN 2     // mcp2

MCP23017 mcp1 = MCP23017(0x20, Wire);
MCP23017 mcp2 = MCP23017(0x21, Wire);

// Queue message struct
typedef struct {
  int action;
  int direction;
  int value;
} bodyactionTaskData_t;

// Display queue for display actions
QueueHandle_t bodyactionQueue = xQueueCreate(BODYACTIONS_QUEUE_SIZE, sizeof(bodyactionTaskData_t));

bodyControl::bodyControl(){
  //
};

void bodyControl::begin(int task_core, int task_priority) {

  mcp1.init();
  mcp2.init();

  mcp1.writeRegister(MCP23017Register::GPIO_A, 0x00);  //Reset port A
  mcp1.writeRegister(MCP23017Register::GPIO_B, 0x00);  //Reset port B
  mcp1.portMode(MCP23017Port::A, 0b11111111);          //Port A as input
  mcp1.portMode(MCP23017Port::B, 0);                   //Port B as output

  mcp2.writeRegister(MCP23017Register::GPIO_A, 0x00);  //Reset port A
  mcp2.writeRegister(MCP23017Register::GPIO_B, 0x00);  //Reset port B
  mcp2.portMode(MCP23017Port::A, 0);                   //Port A as output
  mcp2.portMode(MCP23017Port::B, 0b11111111);          //Port B as input

  // Start worker
  xTaskCreatePinnedToCore(bodyControl::worker, "bodyactions_worker", 4096, NULL, task_priority, &bodyTaskHandle, task_core);
};

void bodyControl::exec(int action, bool direction, int value) {

  bodyactionTaskData_t bodyactionTaskData;

  bodyactionTaskData.action = action;
  bodyactionTaskData.direction = direction;
  bodyactionTaskData.value = value;

  if (action == bodyAction::STOP) {

    xQueueReset(bodyactionQueue);
    xTaskNotify(bodyTaskHandle, 1, eSetValueWithOverwrite);

    mcp1.writeRegister(MCP23017Register::GPIO_A, 0x00);  //Reset port A
    mcp1.writeRegister(MCP23017Register::GPIO_B, 0x00);  //Reset port B

    mcp2.writeRegister(MCP23017Register::GPIO_A, 0x00);  //Reset port A
    mcp2.writeRegister(MCP23017Register::GPIO_B, 0x00);  //Reset port B

    vTaskDelay(50 / portTICK_PERIOD_MS);

    return;
  }

  xQueueSend(bodyactionQueue, &bodyactionTaskData, portMAX_DELAY);
}

void bodyControl::worker(void *pvParameters) {

  bodyactionTaskData_t bodyactionTaskData;

  while (true) {
    if (xQueueReceive(bodyactionQueue, &bodyactionTaskData, portMAX_DELAY) == pdPASS) {

      switch (bodyactionTaskData.action) {
        case bodyAction::LEFTLOWERARM:
          bodyControl::leftLowerArm(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::LEFTUPPERARM:
          bodyControl::leftUpperArm(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::RIGHTLOWERARM:
          bodyControl::rightLowerArm(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::RIGHTUPPERARM:
          bodyControl::rightUpperArm(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::BOTH_LOWER_ARMS:
          bodyControl::bothLowerArms(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::BOTH_UPPER_ARMS:
          bodyControl::bothUpperArms(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::LEFTLEG:
          bodyControl::leftLeg(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::RIGHTLEG:
          bodyControl::rightLeg(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::HIP:
          bodyControl::hip(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::LEFTHANDLIGHT:
          bodyControl::leftHandLight(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::RIGHTHANDLIGHT:
          bodyControl::rightHandLight(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::TURN:
          bodyControl::turn(bodyactionTaskData.value);
          break;

        case bodyAction::BODYSHAKE:
          bodyControl::shake(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::BACK_AND_FORTH:
          bodyControl::back_and_forth(bodyactionTaskData.direction, bodyactionTaskData.value);
          break;

        case bodyAction::WALK_FORWARD:
          bodyControl::bodyControl::walk_forward(bodyactionTaskData.value);
          break;

        case bodyAction::WALK_BACKWARD:
          bodyControl::bodyControl::walk_backward(bodyactionTaskData.value);
          break;

        default:
          break;
      };
    }
    vTaskDelay(250);
  }
};

// leftLowerArm
void bodyControl::leftLowerArm(bool up, int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop
  up ? mcp1.digitalWrite(LEFTLOWERARM_UP_PIN, 1) : mcp1.digitalWrite(LEFTLOWERARM_DOWN_PIN, 1);

  // value routine with vtask notify for stopping
  for (int x = 0; x < value; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
      return;
    }
    vTaskDelay(STEPWAIT);
  }

  up ? mcp1.digitalWrite(LEFTLOWERARM_UP_PIN, 0) : mcp1.digitalWrite(LEFTLOWERARM_DOWN_PIN, 0);
};

// leftUpperArm
void bodyControl::leftUpperArm(bool up, int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop
  up ? mcp1.digitalWrite(LEFTUPPERARM_UP_PIN, 1) : mcp1.digitalWrite(LEFTUPPERARM_DOWN_PIN, 1);

  // value routine with vtask notify for stopping
  for (int x = 0; x < value; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
      return;
    }
    vTaskDelay(STEPWAIT);
  }

  up ? mcp1.digitalWrite(LEFTUPPERARM_UP_PIN, 0) : mcp1.digitalWrite(LEFTUPPERARM_DOWN_PIN, 0);
};

// rightLowerArm
void bodyControl::rightLowerArm(bool up, int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop

  up ? mcp2.digitalWrite(RIGHTLOWERARM_UP_PIN, 1) : mcp2.digitalWrite(RIGHTLOWERARM_DOWN_PIN, 1);

  // value routine with vtask notify for stopping
  for (int x = 0; x < value; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
      return;
    }
    vTaskDelay(STEPWAIT);
  }

  up ? mcp2.digitalWrite(RIGHTLOWERARM_UP_PIN, 0) : mcp2.digitalWrite(RIGHTLOWERARM_DOWN_PIN, 0);
};

// rightUpperArm
void bodyControl::rightUpperArm(bool up, int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop

  up ? mcp2.digitalWrite(RIGHTUPPERARM_UP_PIN, 1) : mcp2.digitalWrite(RIGHTUPPERARM_DOWN_PIN, 1);

  // value routine with vtask notify for stopping
  for (int x = 0; x < value; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
      return;
    }
    vTaskDelay(STEPWAIT);
  }

  up ? mcp2.digitalWrite(RIGHTUPPERARM_UP_PIN, 0) : mcp2.digitalWrite(RIGHTUPPERARM_DOWN_PIN, 0);
};

// bothLowerArms
void bodyControl::bothLowerArms(bool up, int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop

  up ? mcp2.digitalWrite(RIGHTLOWERARM_UP_PIN, 1) : mcp2.digitalWrite(RIGHTLOWERARM_DOWN_PIN, 1);
  up ? mcp1.digitalWrite(LEFTLOWERARM_UP_PIN, 1) : mcp1.digitalWrite(LEFTLOWERARM_DOWN_PIN, 1);

  // value routine with vtask notify for stopping
  for (int x = 0; x < value; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
      return;
    }
    vTaskDelay(STEPWAIT);
  }

  up ? mcp2.digitalWrite(RIGHTLOWERARM_UP_PIN, 0) : mcp2.digitalWrite(RIGHTLOWERARM_DOWN_PIN, 0);
  up ? mcp1.digitalWrite(LEFTLOWERARM_UP_PIN, 0) : mcp1.digitalWrite(LEFTLOWERARM_DOWN_PIN, 0);
};

// bothUpperArms
void bodyControl::bothUpperArms(bool up, int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop

  up ? mcp2.digitalWrite(RIGHTUPPERARM_UP_PIN, 1) : mcp2.digitalWrite(RIGHTUPPERARM_DOWN_PIN, 1);
  up ? mcp1.digitalWrite(LEFTUPPERARM_UP_PIN, 1) : mcp1.digitalWrite(LEFTUPPERARM_DOWN_PIN, 1);

  // value routine with vtask notify for stopping
  for (int x = 0; x < value; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
      return;
    }
    vTaskDelay(STEPWAIT);
  }

  up ? mcp2.digitalWrite(RIGHTUPPERARM_UP_PIN, 0) : mcp2.digitalWrite(RIGHTUPPERARM_DOWN_PIN, 0);
  up ? mcp1.digitalWrite(LEFTUPPERARM_UP_PIN, 0) : mcp1.digitalWrite(LEFTUPPERARM_DOWN_PIN, 0);
};

// leftLeg
void bodyControl::leftLeg(bool forward, int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop

  forward ? mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 1) : mcp1.digitalWrite(LEFTLEG_BACK_PIN, 1);

  // value routine with vtask notify for stopping
  for (int x = 0; x < value; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
      return;
    }
    vTaskDelay(STEPWAIT);
  }

  forward ? mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 0) : mcp1.digitalWrite(LEFTLEG_BACK_PIN, 0);
};

// rightLeg
void bodyControl::rightLeg(bool forward, int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop

  forward ? mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 1) : mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 1);

  // value routine with vtask notify for stopping
  for (int x = 0; x < value; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
      return;
    }
    vTaskDelay(STEPWAIT);
  }

  forward ? mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 0) : mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 0);
};

// hip
void bodyControl::hip(bool left, int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop

  left ? mcp1.digitalWrite(HIP_LEFT_PIN, 1) : mcp1.digitalWrite(HIP_RIGHT_PIN, 1);

  // value routine with vtask notify for stopping
  for (int x = 0; x < value; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
      return;
    }
    vTaskDelay(STEPWAIT);
  }

  left ? mcp1.digitalWrite(HIP_LEFT_PIN, 0) : mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
};

// leftHandLight
void bodyControl::leftHandLight(bool on, int value) {
  on ? mcp2.digitalWrite(LEFTHAND_LIGHT_PIN, 1) : mcp2.digitalWrite(LEFTHAND_LIGHT_PIN, 0);
}

// rightHandLight
void bodyControl::rightHandLight(bool on, int value) {
  on ? mcp2.digitalWrite(RIGHTHAND_LIGHT_PIN, 1) : mcp2.digitalWrite(RIGHTHAND_LIGHT_PIN, 0);
}

// shake
void bodyControl::shake(bool left, int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop

  for (int x = 0; x < value; x = ++x) {

    mcp1.digitalWrite(HIP_LEFT_PIN, 1);
    mcp1.digitalWrite(HIP_RIGHT_PIN, 0);

    // Duration routine with vtask notify for stopping
    for (int y = 0; y < 3; y = ++y) {
      if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
      }
      vTaskDelay(STEPWAIT);
    }

    mcp1.digitalWrite(HIP_LEFT_PIN, 0);
    mcp1.digitalWrite(HIP_RIGHT_PIN, 1);

    // Duration routine with vtask notify for stopping
    for (int y = 0; y < 3; y = ++y) {
      if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
      }
      vTaskDelay(STEPWAIT);
    }
  }

  mcp1.digitalWrite(HIP_LEFT_PIN, 0);
  mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
};

// back_and_forth
void bodyControl::back_and_forth(bool left, int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop
  
  for (int x = 0; x < value; x = ++x) {

    mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 1);
    mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 0);
    mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 1);
    mcp1.digitalWrite(LEFTLEG_BACK_PIN, 0);

    // Duration routine with vtask notify for stopping
    for (int y = 0; y < 6; y = ++y) {
      if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
      }
      vTaskDelay(STEPWAIT);
    }

    mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 0);
    mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 1);
    mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 0);
    mcp1.digitalWrite(LEFTLEG_BACK_PIN, 1);

    // Duration routine with vtask notify for stopping
    for (int y = 0; y < 6; y = ++y) {
      if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
      }
      vTaskDelay(STEPWAIT);
    }
  }

  mcp1.digitalWrite(LEFTLEG_BACK_PIN, 0);
  mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 0);
};

// walk_forward
void bodyControl::walk_forward(int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop

  for (int x = 0; x < value; x = ++x) {

    mcp1.digitalWrite(HIP_LEFT_PIN, 1);
    mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
    vTaskDelay(150);

    mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 0);
    mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 1);
    mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 1);
    mcp1.digitalWrite(LEFTLEG_BACK_PIN, 0);

    // Duration routine with vtask notify for stopping
    for (int y = 0; y < 6; y = ++y) {
      if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        sendMasterNotification("walking_stopped");
        return;
      }
      vTaskDelay(50);
    }

    mcp1.digitalWrite(HIP_LEFT_PIN, 0);
    mcp1.digitalWrite(HIP_RIGHT_PIN, 1);
    vTaskDelay(150);

    mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 1);
    mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 0);
    mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 0);
    mcp1.digitalWrite(LEFTLEG_BACK_PIN, 1);

    // Duration routine with vtask notify for stopping
    for (int y = 0; y < 6; y = ++y) {
      if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        sendMasterNotification("walking_stopped");
        return;
      }
      vTaskDelay(50);
    }

    if (distance_sensor.sensorData.range_mm < FW_DISTANCE_TO_STOP) {
      sendMasterNotification("walking_blocked");
      return;
    }

  }

  sendMasterNotification("walking_ended");
  
  mcp1.digitalWrite(HIP_LEFT_PIN, 0);
  mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
  mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 0);
  mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 0);
  mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 0);
  mcp1.digitalWrite(LEFTLEG_BACK_PIN, 0);

};

// walk_backward
void bodyControl::walk_backward(int value) {
  uint32_t notifyStopValue;  // vtask notifier to stop

  for (int x = 0; x < value; x = ++x) {

    mcp1.digitalWrite(HIP_LEFT_PIN, 1);
    mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
    vTaskDelay(150);

    mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 1);
    mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 0);
    mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 0);
    mcp1.digitalWrite(LEFTLEG_BACK_PIN, 1);

    // Duration routine with vtask notify for stopping
    for (int y = 0; y < 7; y = ++y) {
      if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
      }
      vTaskDelay(50);
    }

    mcp1.digitalWrite(HIP_LEFT_PIN, 0);
    mcp1.digitalWrite(HIP_RIGHT_PIN, 1);
    vTaskDelay(150);  // small correction for veering to the left

    mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 0);
    mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 1);
    mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 1);
    mcp1.digitalWrite(LEFTLEG_BACK_PIN, 0);

    // Duration routine with vtask notify for stopping
    for (int y = 0; y < 7; y = ++y) {
      if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
      }
      vTaskDelay(50);
    }
  }

  mcp1.digitalWrite(HIP_LEFT_PIN, 0);
  mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
  mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 0);
  mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 0);
  mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 0);
  mcp1.digitalWrite(LEFTLEG_BACK_PIN, 0);
};

/**
 
  Turning function in degrees. Heading between -180 and 180. 0 is approximately north. Negative is counter clockwise / left.
  Counter for number of steps with maximum
  Additional check if heading changes enough or if possibly stuck with oldheadingDifference and turnCheckInterval
**/
void bodyControl::turn(int value) {

  // vtask notifier to stop
  uint32_t notifyStopValue;  

  // Current heading from sensor
  float currentHeading;
  // Old heading from sensor
  float oldCurrentHeading;
  // Desiredheading
  float desiredHeading = value;

  // difference in heading between current heading and desired heading
  float headingDifference = 0;

  // old heading difference for actual turning check. Init with high value to force check
  float oldheadingDifference = 0;
  // Turn check interval counter
  int turnErrorCount = 0;

  // Counter to prevent indefinite turning
  int turncount = 0;
  // Turning direction. 0=neutral, 1=right, 2=left
  int turndirection = 0; 
  // headingReadingWaitCounter
  int headingReadingWaitCounter = 0; 
  
  // Set headings as start values
  currentHeading = motion_detect.sensorData.yaw;
  oldCurrentHeading = currentHeading;

  while (true) {

    currentHeading = motion_detect.sensorData.yaw;
    while (currentHeading == oldCurrentHeading) {
      currentHeading = motion_detect.sensorData.yaw;
      vTaskDelay(200);
      headingReadingWaitCounter++;
      if (headingReadingWaitCounter > 30) {
        sendMasterNotification("turn_sensor_error");
        Serial.println("turn_sensor_error");
        break;
      }
    }

    oldCurrentHeading = currentHeading;
    headingReadingWaitCounter = 0;

    headingDifference = desiredHeading - currentHeading;
    headingDifference = fmod(headingDifference + 180.0, 360.0) - 180.0;
    
    // Desired heading succesful reached
    if (abs(headingDifference) < TURN_TOLERANCE_DEGREES) {
      sendMasterNotification("turn_ended");
      Serial.println("turn_ended");
      break;
    }

    // Check for turn blocking
    if ( (abs(headingDifference) < ( abs(oldheadingDifference) + 3 ) && abs(headingDifference) > abs(oldheadingDifference) - 3) || turncount == 0 ) {
      turnErrorCount = turnErrorCount + 1;
    } else {
      turnErrorCount = 0;
    }
    
    oldheadingDifference = headingDifference;

    if (turnErrorCount > MAX_TURN_ERROR) {
      Serial.println("turn_blocked");
      sendMasterNotification("turn_blocked");
      break; 
    } 

    // Max nr. of turn steps
    if (turncount > MAX_TURN_COUNT) { 
      Serial.println("turn_error");
      sendMasterNotification("turn_error");
      break; 
    }  
    turncount++;
    
    // Check for stop signal
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE ) { 
      Serial.println("turn_stopped");
      sendMasterNotification("turn_stopped");
      break; 
    }

    Serial.printf("Desired heading : %f  \n", desiredHeading );
    Serial.printf("Current heading : %f  \n", currentHeading );
    Serial.printf("Heading Difference : %f \n", headingDifference );
    Serial.printf("Old heading Difference : %f \n", oldheadingDifference );
    Serial.printf("Heading direction : %i \n", turndirection );
    Serial.printf("Turn count : %i \n", turncount );
    Serial.printf("Turn error count : %i \n", turnErrorCount );

    // Direction change
    if (headingDifference > 0.0 && turndirection != 1) {
      
      turndirection = 1;
      // Initial step right
      mcp1.digitalWrite(HIP_LEFT_PIN, 1);
      mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
      bodyControl::rightLeg(0, 3);
      bodyControl::leftLeg(1, 2);
      vTaskDelay(100);

    } else if (headingDifference < 0.0 && turndirection != 2) {        
      
      turndirection = 2;
      // Initial step left
      Serial.println("left");
      mcp1.digitalWrite(HIP_LEFT_PIN, 0);
      mcp1.digitalWrite(HIP_RIGHT_PIN, 1);
      bodyControl::leftLeg(0, 3);
      bodyControl::rightLeg(1, 2);
      vTaskDelay(200);

    }

    if (turndirection == 1) {
      // Turn right
      mcp1.digitalWrite(HIP_LEFT_PIN, 1);
      mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
      bodyControl::rightLeg(0, 3);
      bodyControl::leftLeg(1, 2);

      mcp1.digitalWrite(HIP_LEFT_PIN, 0);
      mcp1.digitalWrite(HIP_RIGHT_PIN, 1);
      bodyControl::rightLeg(0, 3);
      bodyControl::leftLeg(1, 2);

    } else if (turndirection == 2) {
      // Turn left
      mcp1.digitalWrite(HIP_LEFT_PIN, 0);
      mcp1.digitalWrite(HIP_RIGHT_PIN, 1);
      bodyControl::leftLeg(0, 3);
      bodyControl::rightLeg(1, 2);
      vTaskDelay(50);
      mcp1.digitalWrite(HIP_LEFT_PIN, 1);
      mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
      bodyControl::leftLeg(0, 3);
      bodyControl::rightLeg(1, 2);

    }
  }

  mcp1.digitalWrite(HIP_LEFT_PIN, 0);
  mcp1.digitalWrite(HIP_RIGHT_PIN, 0);

  mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 0);
  mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 0);
  mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 0);
  mcp1.digitalWrite(LEFTLEG_BACK_PIN, 0);

  vTaskDelay(500);

}

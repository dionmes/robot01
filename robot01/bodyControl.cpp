/**
  Body hardware Control helper class for robot01 via the two MCP23017 extenders
  Also handles the switches and leds in the Robosapien body.
**/

#include <Wire.h>
#include <MCP23017.h>
#include "bodyControl.h"

#define STEPWAIT 50

// vTask core
#define ROBOT_TASK_CORE 1
// Body actions Queue size
#define BODYACTIONS_QUEUE_SIZE 3

// define pins
#define LEFTLOWERARM_DOWN_PIN 9 // mcp1
#define LEFTLOWERARM_UP_PIN 8 // mcp1
#define LEFTUPPERARM_UP_PIN 11 // mcp1
#define LEFTUPPERARM_DOWN_PIN 10 // mcp1
#define RIGHTLOWERARM_DOWN_PIN 6 // mcp2
#define RIGHTLOWERARM_UP_PIN 7 // mcp2
#define RIGHTUPPERARM_UP_PIN 5 // mcp2
#define RIGHTUPPERARM_DOWN_PIN 4 // mcp2
#define LEFTLEG_FORWARD_PIN 14 // mcp1
#define LEFTLEG_BACK_PIN 15 // mcp1
#define RIGHTLEG_FORWARD_PIN 0  // mcp2
#define RIGHTLEG_BACK_PIN 1 // mcp2
#define HIP_RIGHT_PIN 12 // mcp1
#define HIP_LEFT_PIN 13 // mcp1
#define LEFTHAND_LIGHT_PIN 3 // mcp2
#define RIGHTHAND_LIGHT_PIN 2 // mcp2

MCP23017 mcp1 = MCP23017(0x20, Wire);
MCP23017 mcp2 = MCP23017(0x21, Wire);

// Queue message struct
typedef struct {
    int action; 
    int direction;
    int steps;
} bodyactionTaskData_t;

// Display queue for display actions
QueueHandle_t bodyactionQueue = xQueueCreate(BODYACTIONS_QUEUE_SIZE, sizeof(bodyactionTaskData_t));

bodyControl::bodyControl(){
  //
};

void bodyControl::begin() {

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
  xTaskCreatePinnedToCore(bodyControl::worker, "bodyactions_worker", 4096, NULL, 10, &bodyTaskHandle, ROBOT_TASK_CORE);

};

void bodyControl::exec(int action, bool direction, int steps) {
  
  bodyactionTaskData_t bodyactionTaskData;

  bodyactionTaskData.action = action;
  bodyactionTaskData.direction = direction;
  bodyactionTaskData.steps = steps;

  if ( action == bodyAction::STOP ) {

    xQueueReset(bodyactionQueue);    
    xTaskNotify( bodyTaskHandle, 1, eSetValueWithOverwrite );

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
            bodyControl::leftLowerArm(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::LEFTUPPERARM:
            bodyControl::leftUpperArm(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::RIGHTLOWERARM:
            bodyControl::rightLowerArm(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::RIGHTUPPERARM:
            bodyControl::rightUpperArm(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::BOTH_LOWER_ARMS:
            bodyControl::bothLowerArms(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::BOTH_UPPER_ARMS:
            bodyControl::bothUpperArms(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::LEFTLEG:
            bodyControl::leftLeg(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::RIGHTLEG:
            bodyControl::rightLeg(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::HIP:
            bodyControl::hip(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::LEFTHANDLIGHT:
            bodyControl::leftHandLight(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::RIGHTHANDLIGHT:
            bodyControl::rightHandLight(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::TURN:
            bodyControl::turn(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::BODYSHAKE:
            bodyControl::shake(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::BACK_AND_FORTH:
            bodyControl::back_and_forth(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::WALK_FORWARD:
            bodyControl::bodyControl::walk_forward(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          case bodyAction::WALK_BACKWARD:
            bodyControl::bodyControl::walk_backward(bodyactionTaskData.direction, bodyactionTaskData.steps);
            break;

          default:
            break;
        };
      }
  }
};

void bodyControl::leftLowerArm(bool up, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop
  up ? mcp1.digitalWrite(LEFTLOWERARM_UP_PIN, 1) : mcp1.digitalWrite(LEFTLOWERARM_DOWN_PIN, 1);

  // steps routine with vtask notify for stopping  
  for (int x = 0; x < steps; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
    }
    vTaskDelay(STEPWAIT);
  }

  up ? mcp1.digitalWrite(LEFTLOWERARM_UP_PIN, 0) : mcp1.digitalWrite(LEFTLOWERARM_DOWN_PIN, 0);
};

void bodyControl::leftUpperArm(bool up, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop
  up ? mcp1.digitalWrite(LEFTUPPERARM_UP_PIN, 1) : mcp1.digitalWrite(LEFTUPPERARM_DOWN_PIN, 1);

  // steps routine with vtask notify for stopping  
  for (int x = 0; x < steps; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
    }
    vTaskDelay(STEPWAIT);
  }

  up ? mcp1.digitalWrite(LEFTUPPERARM_UP_PIN, 0) : mcp1.digitalWrite(LEFTUPPERARM_DOWN_PIN, 0);
};

void bodyControl::rightLowerArm(bool up, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop

  up ? mcp2.digitalWrite(RIGHTLOWERARM_UP_PIN, 1) : mcp2.digitalWrite(RIGHTLOWERARM_DOWN_PIN, 1);

  // steps routine with vtask notify for stopping  
  for (int x = 0; x < steps; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
    }
    vTaskDelay(STEPWAIT);
  }

  up ? mcp2.digitalWrite(RIGHTLOWERARM_UP_PIN, 0) : mcp2.digitalWrite(RIGHTLOWERARM_DOWN_PIN, 0);
};

void bodyControl::rightUpperArm(bool up, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop

  up ? mcp2.digitalWrite(RIGHTUPPERARM_UP_PIN, 1) : mcp2.digitalWrite(RIGHTUPPERARM_DOWN_PIN, 1);

  // steps routine with vtask notify for stopping  
  for (int x = 0; x < steps; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
    }
    vTaskDelay(STEPWAIT);
  }

  up ? mcp2.digitalWrite(RIGHTUPPERARM_UP_PIN, 0) : mcp2.digitalWrite(RIGHTUPPERARM_DOWN_PIN, 0);
};

void bodyControl::bothLowerArms(bool up, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop

  up ? mcp2.digitalWrite(RIGHTLOWERARM_UP_PIN, 1) : mcp2.digitalWrite(RIGHTLOWERARM_DOWN_PIN, 1);
  up ? mcp1.digitalWrite(LEFTLOWERARM_UP_PIN, 1) : mcp1.digitalWrite(LEFTLOWERARM_DOWN_PIN, 1);

  // steps routine with vtask notify for stopping  
  for (int x = 0; x < steps; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
    }
    vTaskDelay(STEPWAIT);
  }

  up ? mcp2.digitalWrite(RIGHTLOWERARM_UP_PIN, 0) : mcp2.digitalWrite(RIGHTLOWERARM_DOWN_PIN, 0);
  up ? mcp1.digitalWrite(LEFTLOWERARM_UP_PIN, 0) : mcp1.digitalWrite(LEFTLOWERARM_DOWN_PIN, 0);
};

void bodyControl::bothUpperArms(bool up, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop

  up ? mcp2.digitalWrite(RIGHTUPPERARM_UP_PIN, 1) : mcp2.digitalWrite(RIGHTUPPERARM_DOWN_PIN, 1);
  up ? mcp1.digitalWrite(LEFTUPPERARM_UP_PIN, 1) : mcp1.digitalWrite(LEFTUPPERARM_DOWN_PIN, 1);

  // steps routine with vtask notify for stopping  
  for (int x = 0; x < steps; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
    }
    vTaskDelay(STEPWAIT);
  }

  up ? mcp2.digitalWrite(RIGHTUPPERARM_UP_PIN, 0) : mcp2.digitalWrite(RIGHTUPPERARM_DOWN_PIN, 0);
  up ? mcp1.digitalWrite(LEFTUPPERARM_UP_PIN, 0) : mcp1.digitalWrite(LEFTUPPERARM_DOWN_PIN, 0);
};

void bodyControl::leftLeg(bool forward, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop

  forward ? mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 1) : mcp1.digitalWrite(LEFTLEG_BACK_PIN, 1);

  // steps routine with vtask notify for stopping  
  for (int x = 0; x < steps; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
    }
    vTaskDelay(STEPWAIT);
  }

  forward ? mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 0) : mcp1.digitalWrite(LEFTLEG_BACK_PIN, 0);
};

void bodyControl::rightLeg(bool forward, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop

  forward ? mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 1) : mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 1);

  // steps routine with vtask notify for stopping  
  for (int x = 0; x < steps; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
    }
    vTaskDelay(STEPWAIT);
  }

  forward ? mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 0) : mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 0);
};

void bodyControl::hip(bool left, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop

  left ? mcp1.digitalWrite(HIP_LEFT_PIN, 1) : mcp1.digitalWrite(HIP_RIGHT_PIN, 1);

  // steps routine with vtask notify for stopping  
  for (int x = 0; x < steps; x = ++x) {
    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
    }
    vTaskDelay(STEPWAIT);
  }

  left ? mcp1.digitalWrite(HIP_LEFT_PIN, 0) : mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
};

void bodyControl::leftHandLight(bool on, int steps) {
  on ? mcp2.digitalWrite(LEFTHAND_LIGHT_PIN, 1) : mcp2.digitalWrite(LEFTHAND_LIGHT_PIN, 0);
}

void bodyControl::rightHandLight(bool on, int steps) {
  on ? mcp2.digitalWrite(RIGHTHAND_LIGHT_PIN, 1) : mcp2.digitalWrite(RIGHTHAND_LIGHT_PIN, 0);
}

void bodyControl::shake(bool left, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop

  for (int x = 0; x < steps; x = ++x) {
  
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

void bodyControl::back_and_forth(bool left, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop

  for (int x = 0; x < steps; x = ++x) {
  
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

void bodyControl::walk_forward(bool left, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop

  for (int x = 0; x < steps; x = ++x) {
  
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
        vTaskDelay(5);
        vTaskDelete(NULL);
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

void bodyControl::walk_backward(bool left, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop

  for (int x = 0; x < steps; x = ++x) {
  
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
    vTaskDelay(150); // small correction for veering to the left

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

void bodyControl::turn(bool left, int steps) {
  uint32_t notifyStopValue; // vtask notifier to stop


  if(left) {

    Serial.println("left");
    mcp1.digitalWrite(HIP_LEFT_PIN, 0);
    mcp1.digitalWrite(HIP_RIGHT_PIN, 1);
    bodyControl::leftLeg(0,3);
    bodyControl::rightLeg(1,2);  
    vTaskDelay(200);

    for (int x = 0; x < steps; x = ++x) {

      mcp1.digitalWrite(HIP_LEFT_PIN, 0);
      mcp1.digitalWrite(HIP_RIGHT_PIN, 1);
      bodyControl::leftLeg(0,3);
      bodyControl::rightLeg(1,2);
      vTaskDelay(50);
      mcp1.digitalWrite(HIP_LEFT_PIN, 1);
      mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
      bodyControl::leftLeg(0,3);
      bodyControl::rightLeg(1,2);

      if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
      }

    }
    
  } else {

    Serial.println("right");

    mcp1.digitalWrite(HIP_LEFT_PIN, 1);
    mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
    bodyControl::rightLeg(0,3);
    bodyControl::leftLeg(1,2);
    vTaskDelay(100);

    for (int x = 0; x < steps; x = ++x) {
    
      mcp1.digitalWrite(HIP_LEFT_PIN, 1);
      mcp1.digitalWrite(HIP_RIGHT_PIN, 0);
      bodyControl::rightLeg(0,3);
      bodyControl::leftLeg(1,2);

      mcp1.digitalWrite(HIP_LEFT_PIN, 0);
      mcp1.digitalWrite(HIP_RIGHT_PIN, 1);
      bodyControl::rightLeg(0,3);
      bodyControl::leftLeg(1,2);

      if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        return;
      }

    }
  }

  mcp1.digitalWrite(HIP_LEFT_PIN, 0);
  mcp1.digitalWrite(HIP_RIGHT_PIN, 0);

  mcp2.digitalWrite(RIGHTLEG_FORWARD_PIN, 0);
  mcp2.digitalWrite(RIGHTLEG_BACK_PIN, 0);
  mcp1.digitalWrite(LEFTLEG_FORWARD_PIN, 0);
  mcp1.digitalWrite(LEFTLEG_BACK_PIN, 0);

}

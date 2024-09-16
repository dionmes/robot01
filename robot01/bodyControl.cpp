/**
  Body hardware Control helper class for robot01 via the two MCP23017 extenders
  Also handles the switches and leds in the Robosapien body.
**/

#include <Wire.h>
#include <MCP23017.h>
#include "bodyControl.h"

#define HANDLIGHTS_MCP mcp2
#define HIP_MCP mcp1
#define LEFT_MCP mcp1
#define RIGHT_MCP mcp2

#define LEFTLOWERARM_DOWN_PIN 9
#define LEFTLOWERARM_UP_PIN 8
#define LEFTUPPERARM_UP_PIN 11
#define LEFTUPPERARM_DOWN_PIN 10
#define RIGHTLOWERARM_DOWN_PIN 6
#define RIGHTLOWERARM_UP_PIN 7
#define RIGHTUPPERARM_UP_PIN 5
#define RIGHTUPPERARM_DOWN_PIN 4
#define LEFTLEG_FORWARD_PIN 14
#define LEFTLEG_BACK_PIN 15
#define RIGHTLEG_FORWARD_PIN 0
#define RIGHTLEG_BACK_PIN 1
#define HIP_RIGHT_PIN 12
#define HIP_LEFT_PIN 13
#define LEFTHAND_LIGHT_PIN 3
#define RIGHTHAND_LIGHT_PIN 2

MCP23017 mcp1 = MCP23017(0x20, Wire);
MCP23017 mcp2 = MCP23017(0x21, Wire);

bodyControl::bodyControl(){
  //
};

void bodyControl::begin() {
  mcp1.init();
  mcp2.init();

  mcp1.portMode(MCP23017Port::A, 0b11111111);          //Port B as input
  mcp1.portMode(MCP23017Port::B, 0);                   //Port A as output
  mcp1.writeRegister(MCP23017Register::GPIO_A, 0x00);  //Reset port A
  mcp1.writeRegister(MCP23017Register::GPIO_B, 0x00);  //Reset port B

  mcp2.portMode(MCP23017Port::A, 0);                   //Port B as output
  mcp2.portMode(MCP23017Port::B, 0b11111111);          //Port A as input
  mcp2.writeRegister(MCP23017Register::GPIO_A, 0x00);  //Reset port A
  mcp2.writeRegister(MCP23017Register::GPIO_B, 0x00);  //Reset port B
};

void bodyControl::exec(int action, bool direction, int duration) {

  _action = action;
  _direction = direction;
  _duration = duration;


  xTaskCreatePinnedToCore(bodyActionTask, "bodyActionTask", 4096, (void*)this, 10, &bodyTaskHandle, 1);
}

void bodyControl::bodyActionTask(void* bodyControlInstance) {

  bodyControl* bodyControlRef = (bodyControl*)bodyControlInstance;
  Serial.printf("Action : %i \n", bodyControlRef->_action);
  Serial.printf("Direction : %i \n", bodyControlRef->_direction);

  switch (bodyControlRef->_action) {
    case bodyAction::LEFTLOWERARM:
      bodyControlRef->leftLowerArm(bodyControlRef->_direction, bodyControlRef->_duration);
      break;

    case bodyAction::LEFTUPPERARM:
      bodyControlRef->leftUpperArm(bodyControlRef->_direction, bodyControlRef->_duration);
      break;

    case bodyAction::RIGHTLOWERARM:
      bodyControlRef->rightLowerArm(bodyControlRef->_direction, bodyControlRef->_duration);
      break;

    case bodyAction::RIGHTUPPERARM:
      bodyControlRef->rightUpperArm(bodyControlRef->_direction, bodyControlRef->_duration);
      break;

    case bodyAction::LEFTLEG:
      bodyControlRef->leftLeg(bodyControlRef->_direction, bodyControlRef->_duration);
      break;

    case bodyAction::RIGHTLEG:
      bodyControlRef->rightLeg(bodyControlRef->_direction, bodyControlRef->_duration);
      break;

    case bodyAction::HIP:
      bodyControlRef->hip(bodyControlRef->_direction, bodyControlRef->_duration);
      break;

    case bodyAction::LEFTHANDLIGHT:
      bodyControlRef->leftHandLight(bodyControlRef->_direction, bodyControlRef->_duration);
      break;

    case bodyAction::RIGHTHANDLIGHT:
      bodyControlRef->rightHandLight(bodyControlRef->_direction, bodyControlRef->_duration);
      break;

    case bodyAction::TURN:
      bodyControlRef->turn(bodyControlRef->_direction, bodyControlRef->_duration);
      break;

    default:
      break;
  };

  vTaskDelay(100);
  vTaskDelete(NULL);
};

void bodyControl::leftLowerArm(bool up, int duration) {
  int _duration = duration == 0 ? 100 : duration;
  up ? LEFT_MCP.digitalWrite(LEFTLOWERARM_UP_PIN, 1) : LEFT_MCP.digitalWrite(LEFTLOWERARM_DOWN_PIN, 1);
  vTaskDelay(_duration);
  up ? LEFT_MCP.digitalWrite(LEFTLOWERARM_UP_PIN, 0) : LEFT_MCP.digitalWrite(LEFTLOWERARM_DOWN_PIN, 0);
};

void bodyControl::leftUpperArm(bool up, int duration) {
  int _duration = duration == 0 ? 100 : duration;
  up ? LEFT_MCP.digitalWrite(LEFTUPPERARM_UP_PIN, 1) : LEFT_MCP.digitalWrite(LEFTUPPERARM_DOWN_PIN, 1);
  vTaskDelay(_duration);
  up ? LEFT_MCP.digitalWrite(LEFTUPPERARM_UP_PIN, 0) : LEFT_MCP.digitalWrite(LEFTUPPERARM_DOWN_PIN, 0);
};

void bodyControl::rightLowerArm(bool up, int duration) {
  int _duration = duration == 0 ? 100 : duration;
  up ? RIGHT_MCP.digitalWrite(RIGHTLOWERARM_UP_PIN, 1) : RIGHT_MCP.digitalWrite(RIGHTLOWERARM_DOWN_PIN, 1);
  vTaskDelay(_duration);
  up ? RIGHT_MCP.digitalWrite(RIGHTLOWERARM_UP_PIN, 0) : RIGHT_MCP.digitalWrite(RIGHTLOWERARM_DOWN_PIN, 0);
};

void bodyControl::rightUpperArm(bool up, int duration) {
  int _duration = duration == 0 ? 100 : duration;
  up ? RIGHT_MCP.digitalWrite(RIGHTUPPERARM_UP_PIN, 1) : RIGHT_MCP.digitalWrite(RIGHTUPPERARM_DOWN_PIN, 1);
  vTaskDelay(_duration);
  up ? RIGHT_MCP.digitalWrite(RIGHTUPPERARM_UP_PIN, 0) : RIGHT_MCP.digitalWrite(RIGHTUPPERARM_DOWN_PIN, 0);
};

void bodyControl::leftLeg(bool forward, int duration) {
  int _duration = duration == 0 ? 100 : duration;
  forward ? LEFT_MCP.digitalWrite(LEFTLEG_FORWARD_PIN, 1) : LEFT_MCP.digitalWrite(LEFTLEG_BACK_PIN, 1);
  vTaskDelay(_duration);
  forward ? LEFT_MCP.digitalWrite(LEFTLEG_FORWARD_PIN, 0) : LEFT_MCP.digitalWrite(LEFTLEG_BACK_PIN, 0);
};

void bodyControl::rightLeg(bool forward, int duration) {
  int _duration = duration == 0 ? 100 : duration;
  forward ? RIGHT_MCP.digitalWrite(RIGHTLEG_FORWARD_PIN, 1) : RIGHT_MCP.digitalWrite(RIGHTLEG_BACK_PIN, 1);
  vTaskDelay(_duration);
  forward ? RIGHT_MCP.digitalWrite(RIGHTLEG_FORWARD_PIN, 0) : RIGHT_MCP.digitalWrite(RIGHTLEG_BACK_PIN, 0);
};

void bodyControl::hip(bool left, int duration) {
  int _duration = duration == 0 ? 100 : duration;
  left ? HIP_MCP.digitalWrite(HIP_LEFT_PIN, 1) : HIP_MCP.digitalWrite(HIP_RIGHT_PIN, 1);
  vTaskDelay(_duration);
  left ? HIP_MCP.digitalWrite(HIP_LEFT_PIN, 0) : HIP_MCP.digitalWrite(HIP_RIGHT_PIN, 0);
};

void bodyControl::leftHandLight(bool on, int duration) {
  on ? HANDLIGHTS_MCP.digitalWrite(LEFTHAND_LIGHT_PIN, 1) : HANDLIGHTS_MCP.digitalWrite(LEFTHAND_LIGHT_PIN, 0);
  if (duration > 0) {
    vTaskDelay(duration);
    HANDLIGHTS_MCP.digitalWrite(LEFTHAND_LIGHT_PIN, 0);
  } 
}

void bodyControl::rightHandLight(bool on, int duration) {
  on ? HANDLIGHTS_MCP.digitalWrite(RIGHTHAND_LIGHT_PIN, 1) : HANDLIGHTS_MCP.digitalWrite(RIGHTHAND_LIGHT_PIN, 0);
  if (duration > 0) {
    vTaskDelay(duration);
    HANDLIGHTS_MCP.digitalWrite(RIGHTHAND_LIGHT_PIN, 0);
  } 
}

void bodyControl::turn(bool left, int duration) {

  if(left) {

    LEFT_MCP.digitalWrite(LEFTLEG_BACK_PIN, 0);
    LEFT_MCP.digitalWrite(LEFTLEG_FORWARD_PIN, 1);

    RIGHT_MCP.digitalWrite(RIGHTLEG_FORWARD_PIN, 0);
    RIGHT_MCP.digitalWrite(RIGHTLEG_BACK_PIN, 1);

    HIP_MCP.digitalWrite(HIP_LEFT_PIN, 1);
    HIP_MCP.digitalWrite(HIP_RIGHT_PIN, 0);
    vTaskDelay(300);

    LEFT_MCP.digitalWrite(LEFTLEG_BACK_PIN, 0);
    LEFT_MCP.digitalWrite(LEFTLEG_FORWARD_PIN, 0);

    RIGHT_MCP.digitalWrite(RIGHTLEG_FORWARD_PIN, 0);
    RIGHT_MCP.digitalWrite(RIGHTLEG_BACK_PIN, 0);
    
    HIP_MCP.digitalWrite(HIP_LEFT_PIN, 0);
    HIP_MCP.digitalWrite(HIP_RIGHT_PIN, 0);

    vTaskDelay(100);

  } else {
    LEFT_MCP.digitalWrite(LEFTLEG_BACK_PIN, 1);
    LEFT_MCP.digitalWrite(LEFTLEG_FORWARD_PIN, 0);

    RIGHT_MCP.digitalWrite(RIGHTLEG_FORWARD_PIN, 1);
    RIGHT_MCP.digitalWrite(RIGHTLEG_BACK_PIN, 0);

    HIP_MCP.digitalWrite(HIP_LEFT_PIN, 0);
    HIP_MCP.digitalWrite(HIP_RIGHT_PIN, 1);
    vTaskDelay(300);

    LEFT_MCP.digitalWrite(LEFTLEG_BACK_PIN, 0);
    LEFT_MCP.digitalWrite(LEFTLEG_FORWARD_PIN, 0);

    RIGHT_MCP.digitalWrite(RIGHTLEG_FORWARD_PIN, 0);
    RIGHT_MCP.digitalWrite(RIGHTLEG_BACK_PIN, 0);

    HIP_MCP.digitalWrite(HIP_LEFT_PIN, 0);
    HIP_MCP.digitalWrite(HIP_RIGHT_PIN, 0);

    vTaskDelay(100);
  
  }

}

  
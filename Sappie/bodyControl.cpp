/**
  Body hardware Control helper class for Sappie via the two MCP23017 extenders
  Also handles the switches and leds in the Robosapien body.
**/

#include <Wire.h>
#include <MCP23017.h>
#include "bodyControl.h"

#define LEFTUPPERARM_PIN = 
#define RIGHTTUPPERARM_PIN = 
#define LEFTLOWERARM_PIN =
#define RIGHTLOWERARM_PIN = 
#

bodyControl::bodyControl() {
};

void bodyControl::begin() {
  MCP23017 mcp1 = MCP23017(0x20, wire);
  MCP23017 mcp2 = MCP23017(0x21, wire);

  mcp1.init();
  mcp2.init();
  
  mcp1.portMode(MCP23017Port::A, 0b11111111); //Port B as input
  mcp1.portMode(MCP23017Port::B, 0); //Port A as output
  mcp1.writeRegister(MCP23017Register::GPIO_A, 0x00);  //Reset port A
  mcp1.writeRegister(MCP23017Register::GPIO_B, 0x00);  //Reset port B

  mcp2.portMode(MCP23017Port::A, 0); //Port B as output
  mcp2.portMode(MCP23017Port::B, 0b11111111); //Port A as input
  mcp2.writeRegister(MCP23017Register::GPIO_A, 0x00);  //Reset port A
  mcp2.writeRegister(MCP23017Register::GPIO_B, 0x00);  //Reset port B

};

static void bodyControl::testRoutine(){

  for (int8_t x=0; x<=15; x++) {
    bodyControl::leftHandLight(true);
    delay(200);
    bodyControl::leftHandLight(false);
    delay(200);
  }

  for (int8_t x=0; x<=15; x++) {
    bodyControl::rightHandLight(true);
    delay(200);
    bodyControl::rightHandLight(false); 
    delay(200);
  }

};

static void bodyControl::leftUpperArm(bool up, int duration) {
    mcp1.digitalWrite(11,1);
    delay(duration); 
    mcp1.digitalWrite(11,0);  
};

static void bodyControl::rightUpperArm(bool up, int duration) {
    mcp2.digitalWrite(5,1);
    delay(duration); 
    mcp2.digitalWrite(5,0);    
};

static void bodyControl::leftLowerArm(bool up, int duration) {
  mcp1.digitalWrite(9,1);
  delay(duration); 
  mcp1.digitalWrite(9,0);
};

static void bodyControl::rightLowerArm(bool up, int duration) {
    mcp2.digitalWrite(6,1);
    delay(duration); 
    mcp2.digitalWrite(6,0);  
};

static void bodyControl::leftLeg(bool up, int duration) {
    mcp1.digitalWrite(14,1);
    delay(duration); 
    mcp1.digitalWrite(14,0);  
};

static void bodyControl::rightLeg(bool forward, int duration) {
    mcp2.digitalWrite(0,1);
    delay(duration); 
    mcp2.digitalWrite(0,0);    
};

static void bodyControl::hip(bool forward, int duration) {
    mcp1.digitalWrite(13,1);
    delay(duration); 
    mcp1.digitalWrite(13,0);
  
};

static void bodyControl::leftHandLight(bool on) {
  if (on) {
    mcp2.digitalWrite(3,1);
  } else {
    mcp2.digitalWrite(3,1);
  }
}

static void bodyControl::rightHandLight(bool on) {
  if (on) {
    mcp2.digitalWrite(2,1);
  } else {
    mcp2.digitalWrite(2,1);
  }
}


/**
MOTOR Control helper class for Sappie via the two MCP23017 extenders
Also handles the switches and leds in the Robosapien body.
**/

#include <Wire.h>
#include <MCP23017.h>
#include "motorControl.h"

motorControl::motorControl() {
};

void motorControl::begin(TwoWire &wire) {

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

void motorControl::test(){

  Serial.println("leftUpperArmUp");
  motorControl::leftUpperArmUp();
  delay(500);

  Serial.println("rightUpperArmUp");
  motorControl::rightUpperArmUp();
  delay(500);

  Serial.println("leftUpperArmDown");
  motorControl::leftUpperArmDown();
  delay(500);

  Serial.println("rightUpperArmDown");
  motorControl::rightUpperArmDown();
  delay(500);
  
  Serial.println("leftlowerArmUp");
  motorControl::leftlowerArmUp();
  delay(500);

  Serial.println("rightlowerArmUp");
  motorControl::rightlowerArmUp();
  delay(500);

  Serial.println("leftLowerArmDown");
  motorControl::leftLowerArmDown();
  delay(500);

  Serial.println("rightLowerArmDown");
  motorControl::rightLowerArmDown();
  delay(500);

  Serial.println("leftLegForward");
  motorControl::leftLegForward();
  delay(500);

  Serial.println("rightLegForward");
  motorControl::rightLegForward();
  delay(500);
  
  Serial.println("leftLegBack");
  motorControl::leftLegBack();
  delay(500);

  Serial.println("rightLegBack");
  motorControl::rightLegBack();
  delay(500);

  Serial.println("hipLeft");
  motorControl::hipLeft();
  delay(500);
  
  Serial.println("hipRight");
  motorControl::hipRight();
  delay(500);

  for (int8_t x=0; x<=15; x++) {
    motorControl::leftHandLightOn();
    delay(200);
    motorControl::leftHandLightOff();
    delay(200);
  }

  for (int8_t x=0; x<=15; x++) {
    motorControl::rightHandLightOn();
    delay(200);
    motorControl::rightHandLightOff(); 
    delay(200);
  }

};


void motorControl::bothArmsUp() {

    mcp1.digitalWrite(8,1);
    mcp1.digitalWrite(11,1);
    mcp2.digitalWrite(7,1);
    mcp2.digitalWrite(5,1);
    delay(1500);
    mcp1.digitalWrite(8,0);
    mcp1.digitalWrite(11,0);
    mcp2.digitalWrite(7,0);
    mcp2.digitalWrite(5,0);

}

void motorControl::bothArmsDown() {

    mcp1.digitalWrite(9,1);
    mcp1.digitalWrite(10,1);
    mcp2.digitalWrite(6,1);
    mcp2.digitalWrite(4,1);
    delay(1500);
    mcp1.digitalWrite(9,0);
    mcp1.digitalWrite(10,0);
    mcp2.digitalWrite(6,0);
    mcp2.digitalWrite(4,0);

}

void motorControl::leftLowerArmDown(int duration) {
  mcp1.digitalWrite(9,1);
  delay(duration); 
  mcp1.digitalWrite(9,0);
};

void motorControl::leftlowerArmUp(int duration) {
    mcp1.digitalWrite(8,1);
    delay(duration); 
    mcp1.digitalWrite(8,0);  
};

void motorControl::leftUpperArmUp(int duration) {
    mcp1.digitalWrite(11,1);
    delay(duration); 
    mcp1.digitalWrite(11,0);  
};

void motorControl::leftUpperArmDown(int duration) {
    mcp1.digitalWrite(10,1);
    delay(duration); 
    mcp1.digitalWrite(10,0);  
};

void motorControl::leftLegForward(int duration) {
    mcp1.digitalWrite(14,1);
    delay(duration); 
    mcp1.digitalWrite(14,0);  
};

void motorControl::leftLegBack(int duration) {
    mcp1.digitalWrite(15,1);
    delay(duration); 
    mcp1.digitalWrite(15,0);  
};

void motorControl::rightLowerArmDown(int duration) {
    mcp2.digitalWrite(6,1);
    delay(duration); 
    mcp2.digitalWrite(6,0);  
};

void motorControl::rightlowerArmUp(int duration) {
    mcp2.digitalWrite(7,1);
    delay(duration); 
    mcp2.digitalWrite(7,0);  
};

void motorControl::rightUpperArmUp(int duration) {
    mcp2.digitalWrite(5,1);
    delay(duration); 
    mcp2.digitalWrite(5,0);    
};

void motorControl::rightUpperArmDown(int duration) {
    mcp2.digitalWrite(4,1);
    delay(duration); 
    mcp2.digitalWrite(4,0);    
};

void motorControl::rightLegForward(int duration) {
    mcp2.digitalWrite(0,1);
    delay(duration); 
    mcp2.digitalWrite(0,0);    
};

void motorControl::rightLegBack(int duration) {
    mcp2.digitalWrite(1,1);
    delay(duration); 
    mcp2.digitalWrite(1,0);    
};

void motorControl::hipLeft(int duration) {
    mcp1.digitalWrite(13,1);
    delay(duration); 
    mcp1.digitalWrite(13,0);
  
};

void motorControl::hipRight(int duration) {
    mcp1.digitalWrite(12,1);
    delay(duration); 
    mcp1.digitalWrite(12,0);
};

void motorControl::leftHandLightOn() {
  mcp2.digitalWrite(3,1);
}
void motorControl::leftHandLightOff() {
  mcp2.digitalWrite(3,0);
}
void motorControl::rightHandLightOn() {
  mcp2.digitalWrite(2,1);
}
void motorControl::rightHandLightOff() {
    mcp2.digitalWrite(2,0);  
}


/**
MOTOR Control helper class for Sappie via the two MCP23017 extenders
Also handles the switches and leds in the Robosapien body.
**/
#ifndef MOTORCONTROL_H
#define MOTORCONTROL_H

#include <Wire.h>
#include <MCP23017.h>

class motorControl {
  public:
    motorControl();
    void begin(TwoWire &wire);
    void test();
    void leftLowerArmDown(int duration = 1000);
    void leftlowerArmUp(int duration = 1000);
    void leftUpperArmUp(int duration = 1000);
    void leftUpperArmDown(int duration = 1000);
    void leftLegForward(int duration = 1000);
    void leftLegBack(int duration = 1000);
    void rightLowerArmDown(int duration = 1000);
    void rightlowerArmUp(int duration = 1000);
    void rightUpperArmUp(int duration = 1000);
    void rightUpperArmDown(int duration = 1000);
    void rightLegForward(int duration = 1000);
    void rightLegBack(int duration = 1000);
    void hipLeft(int duration = 1000);
    void hipRight(int duration = 1000);
    void bothArmsUp();
    void bothArmsDown();

    void leftHandLightOn();
    void leftHandLightOff();
    void rightHandLightOn();
    void rightHandLightOff();

  private:    
    MCP23017 mcp1 = MCP23017(0x20, Wire);
    MCP23017 mcp2 = MCP23017(0x21, Wire);
};

#endif


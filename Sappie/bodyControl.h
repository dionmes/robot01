/**
Body Control helper class for Sappie via the two MCP23017 extenders
Also handles the switches and leds in the Robosapien body.
**/
#ifndef MOTORCONTROL_H
#define MOTORCONTROL_H

#include <Wire.h>
#include <MCP23017.h>

// Refer to initialized LED matrix in main
extern TwoWire wire;

MCP23017 mcp1 = MCP23017(0x20, Wire);
MCP23017 mcp2 = MCP23017(0x21, Wire);

// Actions
enum bodyAction { 
  LEFTUPPERARM = 1, 
  RIGHTUPPERARM = 2,
  LEFTLOWERARM = 3, 
  RIGHTLOWERARM = 4,
  LEFTLEG = 5,
  RIGHTLEG = 6,
  HIP = 7,
  TEST = 8,
};

class bodyControl {
  public:
    bodyControl();
    void begin();

    // Action - enum bodyAction, Bool direction, Int duration
    void exec(int action, bool direction, int duration = 0);

    // left Hand ligt, on = true
    void leftHandLight(bool on);
    // right Hand ligt, on = true
    void rightHandLight(bool on);

  private:    
    // Task handler for actions
    TaskHandle_t bodyTaskHandle;

    // Struct to pass params to tasks
    struct bodyActionParams {
      int action;
      bool direction;
      int duration;
    };

    // left Upper Arm movement, pass direction boolean (up = true), and duration of movement.
    static void leftUpperArm(bool up, int duration = 0);
    // right Upper Arm movement, pass direction boolean (up = true), and duration of movement.
    static void rightUpperArm(bool up, int duration = 0);
    // left Lower Arm movement, pass direction boolean (up = true), and duration of movement.
    static void leftLowerArm(bool up, int duration = 0);
    // right Lower Arm movement, pass direction boolean (up = true), and duration of movement.
    static void rightLowerArm(int duration = 0);
    // left leg movement, pass direction boolean (forward = true), and duration of movement.
    static void leftLeg(bool forward, int duration = 0);
    // right leg movement, pass direction boolean (forward = true), and duration of movement.
    static void rightLeg(int duration = 0);
    // hip movement, pass direction boolean (left = true), and duration of movement.
    static void hip(bool left, int duration = 0);

    // Test routine
    static void testRoutine();
};

#endif


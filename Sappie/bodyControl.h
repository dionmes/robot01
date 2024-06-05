/**
Body Control helper class for Sappie via the two MCP23017 extenders
Also handles the switches and leds in the Robosapien body.
**/
#ifndef MOTORCONTROL_H
#define MOTORCONTROL_H

#include <Wire.h>
#include <MCP23017.h>
#include "bodyControl.h"

// Refer to initialized LED matrix in main
extern TwoWire wire;

// Body Actions
enum bodyAction {
  LEFTUPPERARM = 1,
  RIGHTUPPERARM = 2,
  LEFTLOWERARM = 3,
  RIGHTLOWERARM = 4,
  LEFTLEG = 5,
  RIGHTLEG = 6,
  HIP = 7,
  LEFTHANDLIGHT = 8,
  RIGHTHANDLIGHT = 9,
  TURN = 10,
};

/*
* Class bodyControl for movement actions & hand LEDs
*
*/
class bodyControl {
public:
  bodyControl();
  void begin();

  // Action - enum bodyAction, Bool direction, Int duration
  void exec(int action, bool direction, int duration = 0);

private:
  // Task handler for actions
  TaskHandle_t bodyTaskHandle;

  // Values for tasks
  int _action;
  bool _direction;
  int _duration;

  // Task function to call. Pass struct bodyActionParams for action parameters.
  static void bodyActionTask(void* actionParams);

  // left Upper Arm movement, pass direction boolean (up = true), and duration of movement.
  void leftUpperArm(bool up, int duration = 0);
  // right Upper Arm movement, pass direction boolean (up = true), and duration of movement.
  void rightUpperArm(bool up, int duration = 0);
  // left Lower Arm movement, pass direction boolean (up = true), and duration of movement.
  void leftLowerArm(bool up, int duration = 0);
  // right Lower Arm movement, pass direction boolean (up = true), and duration of movement.
  void rightLowerArm(bool up, int duration = 0);
  // left leg movement, pass direction boolean (forward = true), and duration of movement.
  void leftLeg(bool forward, int duration = 0);
  // right leg movement, pass direction boolean (forward = true), and duration of movement.
  void rightLeg(bool forward, int duration = 0);
  // hip movement, pass direction boolean (left = true), and duration of movement.
  void hip(bool left, int duration = 0);
  // left Hand ligt, on = true, duration of staying on, 0 is indifinite
  void leftHandLight(bool on, int duration = 0);
  // right Hand ligt, on = true, duration of staying on, 0 is indifinite
  void rightHandLight(bool on, int duration = 0);
  // Turn body , left = true
  void turn(bool left, int duration);

};

#endif

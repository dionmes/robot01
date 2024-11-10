/**
Body Control helper class for robot01 via the two MCP23017 extenders
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
  BODYSHAKE = 11,
  STOP = 12,
  BACK_AND_FORTH = 13,
  WALK_FORWARD = 14,
  WALK_BACKWARD = 15,
  BOTH_UPPER_ARMS=16,
  BOTH_LOWER_ARMS=17
};

/*
* Class bodyControl for movement actions & hand LEDs
*
*/
class bodyControl {
public:
  bodyControl();
  void begin();

  // Action - enum bodyAction, Bool direction, Int steps
  void exec(int action, bool direction, int steps = 0);

private:
  // Task handler for actions
  TaskHandle_t bodyTaskHandle;   

  // Values for tasks
  int _action;
  bool _direction;
  int _steps;

  // indicator of running actions
  bool actionRunning;   

  // Task function to call. Pass struct bodyActionParams for action parameters.
  static void bodyActionTask(void* actionParams);

  // left Upper Arm movement, params: pass direction boolean (up = true), and steps of movement.
  void leftUpperArm(bool up, int steps = 0);
  // right Upper Arm movement, params: pass direction boolean (up = true), and steps of movement.
  void rightUpperArm(bool up, int steps = 0);
  // left Lower Arm movement, params: pass direction boolean (up = true), and steps of movement.
  void bothUpperArms(bool up, int steps = 0);
  // both upper Arm simultanious movement, params: pass direction boolean (up = true), and steps of movement.
  void leftLowerArm(bool up, int steps = 0);
  // right Lower Arm movement, params: pass direction boolean (up = true), and steps of movement.
  void rightLowerArm(bool up, int steps = 0);
  // left leg movement, params: pass direction boolean (forward = true), and steps of movement.
  void bothLowerArms(bool up, int steps = 0);
  // both Lower Arm simultanious movement, params: pass direction boolean (up = true), and steps of movement.
  void leftLeg(bool forward, int steps = 0);
  // right leg movement, params: pass direction boolean (forward = true), and steps of movement.
  void rightLeg(bool forward, int steps = 0);
  // hip movement, params: pass direction boolean (left = true), and steps of movement.
  void hip(bool left, int steps = 0);
  // left Hand ligt, params: on = true, steps not used
  void leftHandLight(bool on, int steps = 0);
  // right Hand ligt, params: on = true, steps not used
  void rightHandLight(bool on, int steps = 0);
  // Turn body , params: left = true for turning left
  void turn(bool left, int steps);
  // Shake body , params: left and steps not used
  void shake(bool left, int steps);
  // Back and Forth, params: left and steps not used
  void back_and_forth(bool left, int steps);
  // Walk forward, params: left not used
  void walk_forward(bool left, int steps);
  // Walk backward, params: left not used
  void walk_backward(bool left, int steps);

};

#endif

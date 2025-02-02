/**
Body Control helper class for robot01 via the two MCP23017 extenders
Also handles the switches and leds in the Robosapien body.
**/
#ifndef MOTORCONTROL_H
#define MOTORCONTROL_H

#include <Wire.h>
#include <MCP23017.h>
#include "bodyControl.h"
#include "DistanceSensor.h"
#include "motiondetect.h"
#include <ArduinoHttpClient.h>

// Global objects and functions needed
extern TwoWire wire;
extern DistanceSensor distance_sensor;
extern motiondetect motion_detect;
extern void sendMasterNotification(char *message);

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
  BOTH_UPPER_ARMS = 16,
  BOTH_LOWER_ARMS = 17
};

/*
* Class bodyControl for movement actions & hand LEDs
*
*/
class bodyControl {
public:
  bodyControl();
  void begin(int task_core = 1, int task_priority = 10);

  // Action - enum bodyAction, Bool direction, Int value
  void exec(int action, bool direction, int value = 0);

  // left Upper Arm movement, params: pass direction boolean (up = true), and value of movement.
  static void leftUpperArm(bool up, int value = 0);
  // right Upper Arm movement, params: pass direction boolean (up = true), and value of movement.
  static void rightUpperArm(bool up, int value = 0);
  // left Lower Arm movement, params: pass direction boolean (up = true), and value of movement.
  static void bothUpperArms(bool up, int value = 0);
  // both upper Arm simultanious movement, params: pass direction boolean (up = true), and value of movement.
  static void leftLowerArm(bool up, int value = 0);
  // right Lower Arm movement, params: pass direction boolean (up = true), and value of movement.
  static void rightLowerArm(bool up, int value = 0);
  // left leg movement, params: pass direction boolean (forward = true), and value of movement.
  static void bothLowerArms(bool up, int value = 0);
  // both Lower Arm simultanious movement, params: pass direction boolean (up = true), and value of movement.
  static void leftLeg(bool forward, int value = 0);
  // right leg movement, params: pass direction boolean (forward = true), and value of movement.
  static void rightLeg(bool forward, int value = 0);
  // hip movement, params: pass direction boolean (left = true), and value of movement.
  static void hip(bool left, int value = 0);
  // left Hand ligt, params: on = true, value not used
  static void leftHandLight(bool on, int value = 0);
  // right Hand ligt, params: on = true, value not used
  static void rightHandLight(bool on, int value = 0);
  // Turn body , params: direction in degrees (-180 - 180 degrees.)
  static void turn(int value);
  // Shake body , params: left and value not used
  static void shake(bool left, int value);
  // Back and Forth, params: left and value not used
  static void back_and_forth(bool left, int value);
  // Walk forward, params: left not used
  static void walk_forward(int value);
  // Walk backward, params: left not used
  static void walk_backward(int value);

  // Worker task taskhandle
  TaskHandle_t bodyTaskHandle;

private:
  // Queue worker for handling display actions
  static void worker(void *pvParameters);
};

#endif

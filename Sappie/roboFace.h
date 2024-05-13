/**
 FACE Led screen helper class for Sappie 
 for Adafruit_SSD1306 OLED Driver
**/
#ifndef ROBOFACE_H
#define ROBOFACE_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "roboFace.h"

// Refer to initialized LED matrix in main
extern Adafruit_SSD1306 ledMatrix;

// Action
enum faceAction { 
  DISPLAYTEXTSMALL = 1, 
  DISPLAYTEXTLARGE = 2,
  NEUTRAL = 3, 
  SMILE = 4,
  LOOKLEFT = 5,
  LOOKRIGHT = 6,
  STARTUP = 7,
  BLINK = 8,
  WINK = 9,
  SHAKE = 10,
  TESTFILLRECT = 11,
  DISPLAYIMG = 12,
  CYLON = 13,
  SCROLLTEXT=14,
  SCROLLLEFT=15,
  SCROLLRIGHT=16,
  STOPSCROLL=17,
  GEAR_ANIMATION=18,
  RECORD_ANIMATION=19,
  HOURGLASS_ANIMATION=20,
  LOADER_ANIMATION=21,
  BELL_ANIMATION=22,
  CHAT_ANIMATION=23,
};

class roboFace {
  public:
    roboFace();
    void begin();
    // Action - enum faceAction, String (optional text if Action requires it), Int optional delay dependent on action.
    void exec( int action, String text = "", int intValue = 0);
    // Boolean to see if task is running.
    bool actionRunning;

  private:
    // Task handler for actions
    TaskHandle_t faceTaskHandle;
    // Values for tasks
    int _action;
    String _text;
    int _intValue;

    // Task function to call. Pass struct faceActionParams for action parameters.
    void static displayTask(void * parameters);

    // display Text with size 2 - medium, or 3 - Large. (to be called from displayTask inside a vtask)
    void displayText(String text, int size);
    //  Scrolling text. To be implemented. (to be called from displayTask inside a vtask)
    void scrollText(String text, int size);
    // Smile.(to be called from displayTask inside a vtask)
    void smile(int wait);
    // Set Face neutral. (to be called from displayTask inside a vtask)
    void neutral();
    // drawbitmap. (to be called from displayTask inside a vtask)
    void drawbitmap(int index);
    // Look Left Animation. (to be called from displayTask inside a vtask)
    void lookLeftAni(int wait);
    // Look Right animation. (to be called from displayTask inside a vtask)
    void lookRightAni(int wait);
    // Startup sequence of multiple animations. (to be called from displayTask inside a vtask)
    void startUp();
    // Blink eyes. (to be called from displayTask inside a vtask)
    void blink(int wait);
    // Wink left eye. (to be called from displayTask inside a vtask)
    void wink(int wait);
    // Shake eyes left to right and right to left. (to be called from displayTask inside a vtask)
    void shake(int wait);
    // Cylon function, moving leds from left to righ and vice versa. To be implemented.(to be called from displayTask inside a vtask)
    void cylon(int wait);
    // Fill rectangle patter for test purposes. (to be called from displayTask inside a vtask)
    void testfillrect(int wait);
    // Scroll screen left (to be called from displayTask inside a vtask)
    void scrollScreenLeft();
    // Scroll screen right (to be called from displayTask inside a vtask)
    void scrollScreenRight();
    // Stop scrolling
    void stopScrolling();
    // Animation
    void animation(const byte frames[][512], int loop);

};

#endif
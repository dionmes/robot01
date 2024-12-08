/**
 FACE Led screen helper class for robot01 
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

// faceAction Actions
enum faceAction { 
  DISPLAYTEXTSMALL = 1, 
  DISPLAYTEXTLARGE = 2,
  NEUTRAL = 3, 
  SMILE = 4,
  LOOKLEFT = 5,
  LOOKRIGHT = 6,
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
  IMG_LOOP=24
};

/*
* roboFace class
* methods for display actions on LED display
*/
class roboFace {
  public:
    roboFace();
    void begin();
    // Action - enum faceAction, String - (optional text if Action requires it), Int - index of image or optional delay dependent on action.
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
    //  Display text for amount of seconds
    void displayTextTimed(String text, int seconds );
    //  Scrolling text. 
    void scrollText(String text, int size);
    // Set Face neutral. 
    void neutral();
    // Smile.
    void smile();
    // drawbitmap. 
    void drawbitmap(int index);
    // IMG Loop
    void imgloop();
    // Look Left Animation.
    void lookLeftAni(int wait);
    // Look Right animation. 
    void lookRightAni(int wait);
    // Blink with both eyes
    void blink(int wait);
    // Wink left eye.
    void wink(int wait);
    // Shake eyes left to right and right to left. 
    void shake(int wait);
    // Cylon function, moving leds from left to righ and vice versa. To be implemented.
    void cylon(int wait);
    // Fill rectangle patter for test purposes. 
    void testfillrect(int wait);
    // Scroll screen left
    void scrollScreenLeft();
    // Scroll screen right
    void scrollScreenRight();
    // Stop scrolling
    void stopScrolling();
    // Animation
    void animation(const byte frames[][512], int loop);
    // Chat animation
    void chat();
    // Small circle on face, part ofChat animation
    void mouth_small_circle();
    // large circle on face, part ofChat animation
    void mouth_large_circle();
    // Small rounded rectangle on face, part ofChat animation
    void mouth_small_rrect();
    // Large rounded rectangle on face, part ofChat animation
    void mouth_large_rrect();

};

#endif
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

// Actions
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
  CYLON = 13
};

class roboFace {
  public:
    roboFace();
    void begin();
    // Action - enum faceAction, String (optional text if Action requires it), Int optional delay dependent on action.
    void exec( int action, String text = "", int time = 0);

  private:
    // Task handler for actions
    TaskHandle_t faceTaskHandle;

    // Struct to pass params to tasks
    struct faceActionParams {
      int action;
      String text;
      int time;
    };

    // Task function to call. Pass struct faceActionParams for action parameters.
    static void displayTask(void * parameters);
    
    // display Text with size 2 - medium, or 3 - Large. (to be called from displayTask inside a vtask)
    static void displayText(String text, int size);
    //  Scrolling text. To be implemented. (to be called from displayTask inside a vtask)
    static void scrollText(String text);
    // Smile.(to be called from displayTask inside a vtask)
    static void smile(int wait);
    // Set Face neutral. (to be called from displayTask inside a vtask)
    static void neutral();
    // drawbitmap. (to be called from displayTask inside a vtask)
    static void drawbitmap(int wait);
    // Look Left Animation. (to be called from displayTask inside a vtask)
    static void lookLeftAni(int wait);
    // Look Right animation. (to be called from displayTask inside a vtask)
    static void lookRightAni(int wait);
    // Startup sequence of multiple animations. (to be called from displayTask inside a vtask)
    static void startUp();
    // Blink eyes. (to be called from displayTask inside a vtask)
    static void blink(int wait);
    // Wink left eye. (to be called from displayTask inside a vtask)
    static void wink(int wait);
    // Shake eyes left to right and right to left. (to be called from displayTask inside a vtask)
    static void shake(int wait);
    // Cylon function, moving leds from left to righ and vice versa. To be implemented.(to be called from displayTask inside a vtask)
    static void cylon(int wait);
    // Fill rectangle patter for test purposes. (to be called from displayTask inside a vtask)
    static void testfillrect(int wait);

};


#endif

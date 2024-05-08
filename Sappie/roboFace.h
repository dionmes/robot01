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

extern Adafruit_SSD1306 ledMatrix;

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
    void exec( int action, String text = "", int time = 0);
    void begin();

  private:
    TaskHandle_t faceTaskHandle;

    struct faceActionParams {
      int action;
      String text;
      int time;
    };

    static void displayTask(void * parameters);

    static void displayText(String text, int size);
    static void scrollText(String text);
    static void smile(int wait);
    static void neutral();
    static void drawbitmap(String name, int wait);
    static void lookLeftAni(int wait);
    static void lookRightAni(int wait);
    static void startUp();
    static void blink(int wait);
    static void wink(int wait);
    static void shake(int wait);
    static void cylon(int wait);
    static void testfillrect(int wait);

};


#endif

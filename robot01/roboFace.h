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
  DRAWBMP_FROM_INDEX = 12,
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
  IMG_LOOP=24,
  DRAWBMP=25
};

enum roboFaceConstants {
  BUFFER_SIZE = 1024
};

/*
* roboFace class
* Class for queuing and worker
*/
class roboFace {

  public:
    roboFace();
    void begin();

    // exec - enum faceAction, String - (optional text if Action requires it), Int - index of image or optional delay dependend on action.
    void exec( int action, String text = "", int intValue = 0);
    void imagetest(char *bmp_data);

    // display Text with size 2 - medium, or 3 - Large. (to be called from displayTask inside a vtask)
    static void text(const char *text, int size);
    //  Display text for amount of seconds
    static void scrollText(const char *text, int size);
    // Set Face neutral. 
    static void neutral();
    // Smile.
    static void smile();
    // drawbitmap. 
    static void drawbitmap_from_index(int index);
    // Draw bitmap from string
    static void drawbmp(const char *text);
    // IMG Loop
    static void imgloop();
    // Look Left Animation.
    static void lookLeftAni(int wait);
    // Look Right animation. 
    static void lookRightAni(int wait);
    // Blink with both eyes
    static void blink(int wait);
    // Wink left eye.
    static void wink(int wait);
    // Shake eyes left to right and right to left. 
    static void shake(int wait);
    // Cylon function, moving leds from left to righ and vice versa. To be implemented.
    static void cylon(int wait);
    // Fill rectangle patter for test purposes. 
    static void testfillrect(int wait);
    // Scroll screen left
    static void scrollScreenLeft();
    // Scroll screen right
    static void scrollScreenRight();
    // Stop scrolling
    static void stopScrolling();
    // Animation
    static void animation(const byte frames[][512], int loop);
    // Chat animation
    static void chat();
    // Small circle on face, part ofChat animation
    static void mouth_small_circle();
    // large circle on face, part ofChat animation
    static void mouth_large_circle();
    // Small rounded rectangle on face, part ofChat animation
    static void mouth_small_rrect();
    // Large rounded rectangle on face, part ofChat animation
    static void mouth_large_rrect();

    private:
    // Queue worker for handling display actions
    static void worker(void *pvParameters);
};

#endif
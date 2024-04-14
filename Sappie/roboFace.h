/**
 FACE Led screen helper class for Sappie 
 for IS31FL3731 LED Driver
**/
#ifndef ROBOFACE_H
#define ROBOFACE_H

#include <Adafruit_GFX.h>
#include <Adafruit_IS31FL3731.h>

// Subclassed Adafruit_IS31FL3731 for correct matrix pixel map.
class customMatrix : public Adafruit_IS31FL3731 {
  public:
  customMatrix(uint8_t x, uint8_t y) : Adafruit_IS31FL3731(x,y) {}

  const byte PIXEL_MAP[77] = {
            6, 22, 38, 54, 70, 86, 14, 30, 46, 62, 78,
            5, 21, 37, 53, 69, 85, 13, 29, 45, 61, 77,
            4, 20, 36, 52, 68, 84, 12, 28, 44, 60, 76,
            3, 19, 35, 51, 67, 83, 11, 27, 43, 59, 75,
            2, 18, 34, 50, 66, 82, 10, 26, 42, 58, 74,
            1, 17, 33, 49, 65, 81,  9, 25, 41, 57, 73,
            0, 16, 32, 48, 64, 80,  8, 24, 40, 56, 72
        };
  
  virtual void drawPixel(int16_t x, int16_t y, uint16_t color) {
    if ((x < 0) || (x >= 11))
      return;
    if ((y < 0) || (y >= 7))
      return;
    if (color > 255)
      color = 255; // PWM 8bit max

    setLEDPWM(PIXEL_MAP[ (y*11) + x], color, _frame);  
  }

  uint16_t getPixel(int16_t x, int16_t y) {
    if ((x < 0) || (x >= 11))
      return 0;
    if ((y < 0) || (y >= 7))
      return 0;

    uint8_t lednum = PIXEL_MAP[(y*11) + x];

    if (lednum >= 144)
      return 0;

    return readRegister8(_frame, 0x24 + lednum );
  }

};

static const uint8_t PROGMEM
  smile_bmp[] =
  { 0b00000000,0b00000000,
    0b01100000,0b11000000,
    0b01100000,0b11000000,
    0b00000000,0b00000000,
    0b01000000,0b01000010,
    0b00111111,0b10000000,
    0b00000000,0b00000000
    },
  neutral_bmp[] =
  { 0b00000000,0b00000000,
    0b01100000,0b11000000,
    0b01100000,0b11000000,
    0b00000000,0b00000000,
    0b00000000,0b00000000,
    0b01111111,0b11000000,
    0b00000000,0b00000000
    },
  frown_bmp[] =
  { 0b00000000,0b00000000,
    0b01100000,0b11000000,
    0b01100000,0b11000000,
    0b00000000,0b00000000,
    0b00000000,0b00000000,
    0b00111111,0b10000000,
    0b01000000,0b01000000
    },
  wink_bmp[] =
  { 0b00000000,0b00000000,
    0b00000000,0b11000000,
    0b01100000,0b11000000,
    0b00000000,0b00000000,
    0b00000000,0b01000000,
    0b00111111,0b10000000,
    0b00000000,0b00000000
    },
  oh_bmp[] =
  { 0b00000000,0b00000000,
    0b01100000,0b11000000,
    0b01100000,0b11000000,
    0b00000000,0b00000000,
    0b00001110,0b00000000,
    0b00010001,0b00000000,
    0b00001110,0b00000000
    };

class roboFace {
  public:
    roboFace();
    void begin();
    void displayText(String text);
    void face_smile();
  private:
    customMatrix matrix = customMatrix(11,7);
};


#endif

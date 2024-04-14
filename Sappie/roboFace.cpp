/**
 FACE Led screen helper class for Sappie 
 for IS31FL3731 LED Driver
**/
#include <Adafruit_GFX.h>
#include <Adafruit_IS31FL3731.h>
#include "roboFace.h"

roboFace::roboFace()
{
  // 11x7 LED matrix
  customMatrix matrix = customMatrix(11,7);
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

};

void roboFace::begin() {

  if (! matrix.begin(0x75)) {
    Serial.println("IS31 not found");
  }
  matrix.setTextColor(0xFFFF, 0x0000);

};

void roboFace::face_smile(){
  matrix.clear();
  matrix.drawBitmap(0, 0, smile_bmp, 11, 7, 64);
}

void roboFace::displayText(String text) {
    
    for (int i=0; i<=(text.length() * 6)+5; i++) {
    
      int x = -i;
    
      matrix.setCursor(x+11,0);
      matrix.clear();
      matrix.print(text);
      delay(100);
    }

    face_smile();
};

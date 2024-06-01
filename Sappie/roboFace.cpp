/**
 FACE Led screen helper class for Sappie 
 for Adafruit_SSD1306 OLED Driver
**/
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "roboFace.h"
#include "images.h"
#include "animations.h"

// Face coordinates
#define rectX1 0
#define rectY1 0
#define rectX2 127
#define rectY2 63
#define rectRadius 25
#define leftEyeX 36
#define leftEyeY 32
#define rightEyeX 90
#define rightEyeY 32
#define eyeRadius 20

roboFace::roboFace(){};

void roboFace::begin() {

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!ledMatrix.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, true)) {
    Serial.println(F("SSD1306 allocation failed"));
  }

  ledMatrix.clearDisplay();
  ledMatrix.stopscroll();
  ledMatrix.display();
  ledMatrix.setTextColor(SSD1306_WHITE);
  ledMatrix.setTextWrap(false);

  vTaskDelay(200);
};

void roboFace::exec(int action, String text, int intValue) {

  if (this->actionRunning) {
    return;

    if( faceTaskHandle != NULL ) {
      Serial.println("Deleting display task.");
      vTaskDelete(faceTaskHandle);

      // Recover time and procedure before starting next task after sudden break
      vTaskDelay(200);
      ledMatrix.clearDisplay();
      ledMatrix.stopscroll();
      ledMatrix.display();
      vTaskDelay(200);
    }

  }

  _action = action;
  _text = text;
  _intValue = intValue;

  xTaskCreatePinnedToCore(this->displayTask, "displayTask", 8192, (void*)this, 10, &faceTaskHandle, 1);
};

void roboFace::displayTask(void* roboFaceInstance) {

  roboFace* roboFaceRef = (roboFace*)roboFaceInstance;
  roboFaceRef->actionRunning = true;

  switch (roboFaceRef->_action) {
    case faceAction::DISPLAYTEXTSMALL:
      roboFaceRef->displayText(roboFaceRef->_text, 1);
      break;

    case faceAction::DISPLAYTEXTLARGE:
      roboFaceRef->displayText(roboFaceRef->_text, 2);
      break;

    case faceAction::SCROLLTEXT:
      roboFaceRef->scrollText(roboFaceRef->_text, 1);
      break;

    case faceAction::NEUTRAL:
      roboFaceRef->neutral();
      break;

    case faceAction::SMILE:
      roboFaceRef->smile(roboFaceRef->_intValue);
      break;

    case faceAction::LOOKLEFT:
      roboFaceRef->lookLeftAni(roboFaceRef->_intValue);
      break;

    case faceAction::LOOKRIGHT:
      roboFaceRef->lookRightAni(roboFaceRef->_intValue);
      break;

    case faceAction::STARTUP:
      roboFaceRef->startUp();
      break;

    case faceAction::BLINK:
      roboFaceRef->blink(roboFaceRef->_intValue);
      break;

    case faceAction::WINK:
      roboFaceRef->wink(roboFaceRef->_intValue);
      break;

    case faceAction::SHAKE:
      roboFaceRef->shake(roboFaceRef->_intValue);
      break;

    case faceAction::TESTFILLRECT:
      roboFaceRef->testfillrect(roboFaceRef->_intValue);
      break;

    case faceAction::DISPLAYIMG:
      roboFaceRef->drawbitmap(roboFaceRef->_intValue);
      break;

    case faceAction::CYLON:
      roboFaceRef->cylon(roboFaceRef->_intValue);
      break;

    case faceAction::SCROLLLEFT:
      roboFaceRef->scrollScreenLeft();
      break;

    case faceAction::SCROLLRIGHT:
      roboFaceRef->scrollScreenRight();
      break;

    case faceAction::STOPSCROLL:
      roboFaceRef->stopScrolling();
      break;

    case faceAction::GEAR_ANIMATION:
      roboFaceRef->animation(gear_frames,roboFaceRef->_intValue);
      break;

    case faceAction::RECORD_ANIMATION:
      roboFaceRef->animation(  record_frames,roboFaceRef->_intValue);
      break;

    case faceAction::HOURGLASS_ANIMATION:
      roboFaceRef->animation(hourglass_frames,roboFaceRef->_intValue);
      break;

    case faceAction::LOADER_ANIMATION:
      roboFaceRef->animation(loader_frames,roboFaceRef->_intValue);
      break;

    case faceAction::BELL_ANIMATION:
      roboFaceRef->animation(bell_frames,roboFaceRef->_intValue);
      break;

    case faceAction::CHAT_ANIMATION:
      roboFaceRef->animation(chat_frames,roboFaceRef->_intValue);
      break;

    case faceAction::IMG_LOOP:
      roboFaceRef->imgloop();
      break;

    default:
      roboFaceRef->neutral();
      break;
  };

  vTaskDelay(5);
  vTaskDelete(NULL);
};

void roboFace::startUp() {
  actionRunning = true;

  wink(50);
  vTaskDelay(1000);
  lookLeftAni(50);
  vTaskDelay(100);
  lookRightAni(50);
  vTaskDelay(500);
  neutral();
  vTaskDelay(1000);
  smile(1000);

  actionRunning = false;
};


void roboFace::imgloop() {
  // Test routine
  actionRunning = true;

  for (int i = 0; i <= 60; i++) {
    drawbitmap(i);
    delay(800);
  }
  actionRunning = false;
}

void roboFace::displayText(String text, int size) {
  actionRunning = true;

  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  ledMatrix.setTextSize(1);
  ledMatrix.setTextWrap(true);

  if (size == 1) {
    ledMatrix.setCursor(0, 12);
    ledMatrix.setFont(&FreeSans9pt7b);
  } else {
    ledMatrix.setCursor(0, 18);
    ledMatrix.setFont(&FreeSansBold12pt7b);
  }
  ledMatrix.print(text);
  ledMatrix.display();

  actionRunning = false;
};

void roboFace::scrollText(String text, int wait) {
  actionRunning = true;

  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  ledMatrix.setTextWrap(false);

  ledMatrix.setTextSize(1);
  ledMatrix.setFont(&FreeSans9pt7b);

  int len = (text.length() * 9);

  for (int x = 128; x > -len; x = x - 2) {
    ledMatrix.clearDisplay();
    ledMatrix.setCursor(x, 32);
    ledMatrix.print(text);
    ledMatrix.display();
    vTaskDelay(wait);
  }

  actionRunning = false;
};

void roboFace::drawbitmap(int index) {
  actionRunning = true;

  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();

  ledMatrix.drawBitmap(0, 0, bmp_allArray[index], 128, 63, 1, 0);
  ledMatrix.display();

  actionRunning = false;
};

void roboFace::neutral() {
  actionRunning = true;

  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();

  ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
  ledMatrix.fillCircle(leftEyeX, leftEyeY, eyeRadius, 0);
  ledMatrix.fillCircle(rightEyeX, rightEyeY, eyeRadius, 0);

  ledMatrix.display();
  actionRunning = false;
};

void roboFace::smile(int wait) {
  actionRunning = true;

  neutral();
  ledMatrix.fillTriangle(leftEyeX + 17, leftEyeY + 18, rightEyeX - 17, rightEyeY + 18, leftEyeX + ((rightEyeX - leftEyeX) / 2), leftEyeY + 25, 0);

  ledMatrix.display();

  if (wait > 0) {
    vTaskDelay(wait);
    neutral();
  }
  actionRunning = false;
};

void roboFace::lookLeftAni(int wait) {
  actionRunning = true;

  neutral();

  for (int i = 0; i < 15; i++) {

    ledMatrix.clearDisplay();

    ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
    ledMatrix.fillCircle(leftEyeX - i, leftEyeY, eyeRadius, 0);
    ledMatrix.fillCircle(rightEyeX - i, rightEyeY, eyeRadius, 0);
    ledMatrix.display();

    vTaskDelay(wait);
  }
  actionRunning = false;
};

void roboFace::lookRightAni(int wait) {
  actionRunning = true;

  neutral();

  for (int i = 0; i < 15; i++) {

    ledMatrix.clearDisplay();

    ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
    ledMatrix.fillCircle(leftEyeX + i, leftEyeY, eyeRadius, 0);
    ledMatrix.fillCircle(rightEyeX + i, rightEyeY, eyeRadius, 0);
    ledMatrix.display();

    vTaskDelay(wait);
  }
  actionRunning = false;
};

void roboFace::blink(int wait) {
  actionRunning = true;

  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();

  ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
  ledMatrix.display();

  vTaskDelay(wait);

  neutral();
  actionRunning = false;
};


void roboFace::wink(int wait) {
  actionRunning = true;
  int _wait = wait == 0 ? 50 : wait;
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();

  ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
  ledMatrix.fillCircle(rightEyeX, rightEyeY, eyeRadius, 0);
  ledMatrix.display();

  vTaskDelay(_wait);

  neutral();
  actionRunning = false;
};

void roboFace::shake(int wait) {
  actionRunning = true;

  neutral();
  bool left = true;

  for (int i = 0; i < 15; i++) {

    ledMatrix.clearDisplay();
    if (left) {
      ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
      ledMatrix.fillCircle(leftEyeX - i, leftEyeY, eyeRadius, 0);
      ledMatrix.fillCircle(rightEyeX - i, rightEyeY, eyeRadius, 0);
      ledMatrix.display();
      left = false;
    } else {
      ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
      ledMatrix.fillCircle(leftEyeX + i, leftEyeY, eyeRadius, 0);
      ledMatrix.fillCircle(rightEyeX + i, rightEyeY, eyeRadius, 0);
      ledMatrix.display();
      left = true;
    }

    vTaskDelay(wait);
    neutral();
    vTaskDelay(wait);
  }

  actionRunning = false;
};

void roboFace::cylon(int wait) {
  actionRunning = true;
  
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  int _wait = wait == 0 ? 10 : wait;

  // Three times
  for (int i = 0; i <= 3; i++) {
    for (int x = 0; x <= 110; x = x + 10) {
      ledMatrix.clearDisplay();
      ledMatrix.fillRect(x, 20, 18, 24, 1);
      ledMatrix.display();
      vTaskDelay(wait);
    }

    for (int x = 110; x > 0; x = x - 10) {
      ledMatrix.clearDisplay();
      ledMatrix.fillRect(x, 20, 18, 24, 1);
      ledMatrix.display();
      vTaskDelay(wait);
    }
  }

  ledMatrix.clearDisplay();
  ledMatrix.display();

  actionRunning = false;
}

void roboFace::testfillrect(int wait) {
  actionRunning = true;

  ledMatrix.clearDisplay();

  for (int16_t i = 0; i < ledMatrix.height() / 2; i += 3) {
    // The INVERSE color is used so rectangles alternate white/black
    ledMatrix.fillRect(i, i, ledMatrix.width() - i * 2, ledMatrix.height() - i * 2, SSD1306_INVERSE);
    ledMatrix.display();  // Update screen with each newly-drawn rectangle
    vTaskDelay(1);
  }

  vTaskDelay(wait);
  actionRunning = false;
}

void roboFace::scrollScreenLeft() {
  ledMatrix.startscrollleft(0, 63);
  actionRunning = false;
}

void roboFace::scrollScreenRight() {
  ledMatrix.startscrollright(0, 63);
  actionRunning = false;
}

void roboFace::stopScrolling() {
  ledMatrix.stopscroll();
  actionRunning = false;
}

void roboFace::animation(const byte frames[][512], int loop) {
  int frame_count = 27;
  for(int n = 0; n<=loop; n++) {
    for(int frame = 0; frame <= frame_count; frame++ ) {
    ledMatrix.clearDisplay();
    ledMatrix.drawBitmap(32, 0, frames[frame], 64, 64, 1);
    ledMatrix.display();
    vTaskDelay(42);
    }
  }
  actionRunning = false;
}

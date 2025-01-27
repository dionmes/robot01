/**
 FACE Led screen helper class for robot01 
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
#include "facetalking.h"

// Display Queue size
#define DISPLAY_QUEUE_SIZE 3

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
#define eyeRadius 15

// Queue message struct
typedef struct {
    int action; 
    char text[roboFaceConstants::BUFFER_SIZE];
    int intValue;
} displayTaskData_t;

// Display queue for display actions
QueueHandle_t displayQueue = xQueueCreate(DISPLAY_QUEUE_SIZE, sizeof(displayTaskData_t));

roboFace::roboFace(){};

void roboFace::begin(int task_core, int task_priority) {
  // Start worker
  xTaskCreatePinnedToCore(roboFace::worker, "display_worker", 4096, NULL, task_priority, NULL, task_core);
};

void roboFace::exec(int action, String text, int intValue) {

  displayTaskData_t displayTaskData;
  displayTaskData.action = action;
  strcpy(displayTaskData.text,text.c_str());
  displayTaskData.intValue = intValue;
  xQueueSend(displayQueue, &displayTaskData, portMAX_DELAY);

};

void roboFace::imagetest(char *bmp_data) {

  displayTaskData_t displayTaskData;
  displayTaskData.action = 25;
  memcpy(displayTaskData.text, bmp_data, roboFaceConstants::BUFFER_SIZE);
  displayTaskData.intValue = 0;
  xQueueSend(displayQueue, &displayTaskData, portMAX_DELAY);

};

void roboFace::worker(void *pvParameters) {

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!ledMatrix.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, true)) {
    Serial.println(F("SSD1306 allocation failed"));
  }

  ledMatrix.setTextColor(SSD1306_WHITE);
  ledMatrix.setTextWrap(false);
  displayTaskData_t displayTaskData;

  while (true) {
      if (xQueueReceive(displayQueue, &displayTaskData, portMAX_DELAY) == pdPASS) {
        
        int _intValue = displayTaskData.intValue;
        const char* _text = displayTaskData.text;

        switch (displayTaskData.action) {
          case faceAction::DISPLAYTEXTSMALL:
            roboFace::text(_text, 1, _intValue);
            break;

          case faceAction::DISPLAYTEXTLARGE:
            roboFace::text(_text, 2, _intValue);
            break;

          case faceAction::SCROLLTEXT:
            roboFace::scrollText(_text, 1);            
            break;

          case faceAction::NEUTRAL:
            roboFace::neutral();
            break;

          case faceAction::SMILE:
            roboFace::smile();
            break;

          case faceAction::LOOKLEFT:
            roboFace::lookLeftAni(_intValue);
            break;

          case faceAction::LOOKRIGHT:
            roboFace::lookRightAni(_intValue);
            break;

          case faceAction::BLINK:
            roboFace::blink(_intValue);
            break;

          case faceAction::WINK:
            roboFace::wink(_intValue);
            break;

          case faceAction::SHAKE:
            roboFace::shake(_intValue);
            break;

          case faceAction::TESTFILLRECT:
            roboFace::testfillrect(_intValue);
            break;

          case faceAction::DRAWBMP_FROM_INDEX:
            roboFace::drawbitmap_from_index(_intValue);
            break;

          case faceAction::DRAWBMP:
            roboFace::drawbmp(_text);
            break;

          case faceAction::CYLON:
            roboFace::cylon(_intValue);
            break;

          case faceAction::SCROLLLEFT:
            roboFace::scrollScreenLeft();
            break;

          case faceAction::SCROLLRIGHT:
            roboFace::scrollScreenRight();
            break;

          case faceAction::STOPSCROLL:
            roboFace::stopScrolling();
            break;

          case faceAction::GEAR_ANIMATION:
            roboFace::animation(gear_frames,_intValue);
            break;

          case faceAction::RECORD_ANIMATION:
            roboFace::animation(record_frames,_intValue);
            break;

          case faceAction::HOURGLASS_ANIMATION:
            roboFace::animation(hourglass_frames,_intValue);
            break;

          case faceAction::LOADER_ANIMATION:
            roboFace::animation(loader_frames,_intValue);
            break;

          case faceAction::BELL_ANIMATION:
            roboFace::animation(bell_frames,_intValue);
            break;

          case faceAction::CHAT_ANIMATION:
            roboFace::chat();
            break;

          case faceAction::IMG_LOOP:
            roboFace::imgloop();
            break;

          default:
            roboFace::neutral();
            break;
        };          
      }        
    vTaskDelay(150);
  }
}

void roboFace::imgloop() {

  // Test routine
  for (int i = 0; i <= bmp_allArray_LEN; i++) {
    drawbitmap_from_index(i);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    if (uxQueueMessagesWaiting(displayQueue) > 0 ) {
      return;
    }
  }
}

void roboFace::text(const char *text, int size, int duration) {
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

  // Wait before neutral
  if (duration != 0) {
    for (int i = 0; i < duration; i++) {
      if (uxQueueMessagesWaiting(displayQueue) > 0 ) {
        return;
      }
      vTaskDelay(50);
    }
    roboFace::neutral();
  }
};

void roboFace::scrollText(const char *text, int wait) {  
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  ledMatrix.setTextWrap(false);

  ledMatrix.setTextSize(1);
  ledMatrix.setFont(&FreeSans9pt7b);

  int len = strlen(text) * 9;


  for (int x = 128; x > -len; x = x - 2) {
  
    ledMatrix.clearDisplay();
    ledMatrix.setCursor(x, 32);
    ledMatrix.print(text);
    ledMatrix.display();

    vTaskDelay(wait);

    if (uxQueueMessagesWaiting(displayQueue) > 0 ) {
      return;
    }
  }
};

void roboFace::drawbitmap_from_index(int index) {
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  ledMatrix.drawBitmap(0, 0, bmp_allArray[index], 128, 63, 1, 0);
  ledMatrix.display();

};

void roboFace::drawbmp(const char *data) {
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  const uint8_t *bitmap = reinterpret_cast<const uint8_t*>(data);
  ledMatrix.drawBitmap(0, 0, bitmap, 128, 63, 1, 0);
  ledMatrix.display();
};

void roboFace::neutral() {
  roboFace::drawbitmap_from_index(7);
  ledMatrix.display();
};

void roboFace::smile() {
  roboFace::drawbitmap_from_index(5);
  ledMatrix.display();
};

void roboFace::lookLeftAni(int wait) {
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  for (int i = 0; i < 15; i++) {

    ledMatrix.clearDisplay();

    ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
    ledMatrix.fillCircle(leftEyeX - i, leftEyeY, eyeRadius, 0);
    ledMatrix.fillCircle(rightEyeX - i, rightEyeY, eyeRadius, 0);
    ledMatrix.display();

    vTaskDelay(wait);

    if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
  }
};

void roboFace::lookRightAni(int wait) {
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();

  roboFace::neutral();

  for (int i = 0; i < 15; i++) {

    ledMatrix.clearDisplay();

    ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
    ledMatrix.fillCircle(leftEyeX + i, leftEyeY, eyeRadius, 0);
    ledMatrix.fillCircle(rightEyeX + i, rightEyeY, eyeRadius, 0);
    ledMatrix.display();

    vTaskDelay(wait);

    if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
  }
};

void roboFace::blink(int wait) {
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();

  ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
  ledMatrix.display();

  vTaskDelay(wait);

  roboFace::neutral();
};

void roboFace::wink(int wait) {
  int _wait = wait == 0 ? 50 : wait;
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();

  ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
  ledMatrix.fillCircle(rightEyeX, rightEyeY, eyeRadius, 0);
  ledMatrix.display();

  vTaskDelay(_wait);

  roboFace::neutral();
};

void roboFace::shake(int wait) {

  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
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

    if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }

    roboFace::neutral();
  }
};

void roboFace::cylon(int wait) {
  
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  int _wait = wait == 0 ? 10 : wait;

  // Three times
  while(true) {
    for (int x = 0; x <= 110; x = x + 10) {
      ledMatrix.clearDisplay();
      ledMatrix.fillRect(x, 20, 18, 24, 1);
      ledMatrix.display();
      
      vTaskDelay(wait);

      if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
    }

    for (int x = 110; x > 0; x = x - 10) {
      ledMatrix.clearDisplay();
      ledMatrix.fillRect(x, 20, 18, 24, 1);
      ledMatrix.display();

      vTaskDelay(wait);

      if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
    }
  }

  ledMatrix.clearDisplay();
  ledMatrix.display();
}

void roboFace::testfillrect(int wait) {

  ledMatrix.clearDisplay();
  for (int16_t i = 0; i < ledMatrix.height() / 2; i += 3) {
    // The INVERSE color is used so rectangles alternate white/black
    ledMatrix.fillRect(i, i, ledMatrix.width() - i * 2, ledMatrix.height() - i * 2, SSD1306_INVERSE);
    ledMatrix.display();  // Update screen with each newly-drawn rectangle
    vTaskDelay(10);

    if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
  }
}

void roboFace::scrollScreenLeft() {
  ledMatrix.startscrollleft(0, 63);
}

void roboFace::scrollScreenRight() {
  ledMatrix.startscrollright(0, 63);
}

void roboFace::stopScrolling() {
  ledMatrix.stopscroll();
}

void roboFace::animation(const byte frames[][512], int loop) {

  while(true) {
    int frame_count = 27;
    for(int n = 0; n<=loop; n++) {
      for(int frame = 0; frame <= frame_count; frame++ ) {
        ledMatrix.clearDisplay();
        ledMatrix.drawBitmap(32, 0, frames[frame], 64, 64, 1);
        ledMatrix.display();
  
        vTaskDelay(42 / portTICK_PERIOD_MS);

        if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
      }
    }
  }
}

void roboFace::chat() {
  
  roboFace::drawbitmap_from_index(10);
  
  int randomNumber = random(chat_ani_LEN);
  int speed = 150; // Higher is slower

  while(true) {
    if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
    randomNumber = random(chat_ani_LEN);
    ledMatrix.drawBitmap(23, 31, chat_ani[randomNumber], 80, 32, 1, 0);
    ledMatrix.display();
    vTaskDelay(speed);
  }

}


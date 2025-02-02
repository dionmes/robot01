/**
 Display Led screen helper class for robot01 
 for Adafruit_SSD1306 OLED Driver
**/
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "roboDisplay.h"
#include "images.h"
#include "animations.h"
#include "facetalking.h"

// Display Queue size
#define DISPLAY_QUEUE_SIZE 3

// Display coordinates
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
    char text[roboDisplayConstants::image_buffer_size];
    int intValue;
} displayTaskData_t;

// Display queue for display actions
QueueHandle_t displayQueue = xQueueCreate(DISPLAY_QUEUE_SIZE, sizeof(displayTaskData_t));

// LED Display 
Adafruit_SSD1306 ledMatrix(128, 64, &Wire, -1);

roboDisplay::roboDisplay(){};

void roboDisplay::begin(int task_core, int task_priority) {
  // Start worker
  xTaskCreatePinnedToCore(roboDisplay::worker, "display_worker", 4096, NULL, task_priority, NULL, task_core);
};

void roboDisplay::exec(int action, String text, int intValue) {
  displayTaskData_t displayTaskData;
  displayTaskData.action = action;
  strcpy(displayTaskData.text,text.c_str());
  displayTaskData.intValue = intValue;
  xQueueSend(displayQueue, &displayTaskData, portMAX_DELAY);
};

void roboDisplay::imagetest(char *bmp_data) {

  displayTaskData_t displayTaskData;
  displayTaskData.action = 25;
  memcpy(displayTaskData.text, bmp_data, roboDisplayConstants::image_buffer_size);
  displayTaskData.intValue = 0;
  xQueueSend(displayQueue, &displayTaskData, portMAX_DELAY);

};

void roboDisplay::worker(void *pvParameters) {

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!ledMatrix.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, true)) {
    Serial.println(F("SSD1306 allocation failed"));
  }

  ledMatrix.ssd1306_command(0x03);
  
  ledMatrix.setTextColor(SSD1306_WHITE);
  ledMatrix.setTextWrap(false);
  displayTaskData_t displayTaskData;

  while (true) {
      if (xQueueReceive(displayQueue, &displayTaskData, portMAX_DELAY) == pdPASS) {
        
        int _intValue = displayTaskData.intValue;
        const char* _text = displayTaskData.text;

        switch (displayTaskData.action) {
          case DisplayAction::DISPLAYTEXTSMALL:
            roboDisplay::text(_text, 1, _intValue);
            break;

          case DisplayAction::DISPLAYTEXTLARGE:
            roboDisplay::text(_text, 2, _intValue);
            break;

          case DisplayAction::SCROLLTEXT:
            roboDisplay::scrollText(_text, 1);            
            break;

          case DisplayAction::NEUTRAL:
            roboDisplay::neutral();
            break;

          case DisplayAction::SMILE:
            roboDisplay::smile();
            break;

          case DisplayAction::LOOKLEFT:
            roboDisplay::lookLeftAni(_intValue);
            break;

          case DisplayAction::LOOKRIGHT:
            roboDisplay::lookRightAni(_intValue);
            break;

          case DisplayAction::BLINK:
            roboDisplay::blink(_intValue);
            break;

          case DisplayAction::WINK:
            roboDisplay::wink(_intValue);
            break;

          case DisplayAction::SHAKE:
            roboDisplay::shake(_intValue);
            break;

          case DisplayAction::TESTFILLRECT:
            roboDisplay::testfillrect(_intValue);
            break;

          case DisplayAction::DRAWBMP_FROM_INDEX:
            roboDisplay::drawbitmap_from_index(_intValue);
            break;

          case DisplayAction::DRAWBMP:
            roboDisplay::drawbmp(_text);
            break;

          case DisplayAction::CYLON:
            roboDisplay::cylon(_intValue);
            break;

          case DisplayAction::SCROLLLEFT:
            roboDisplay::scrollScreenLeft();
            break;

          case DisplayAction::SCROLLRIGHT:
            roboDisplay::scrollScreenRight();
            break;

          case DisplayAction::STOPSCROLL:
            roboDisplay::stopScrolling();
            break;

          case DisplayAction::GEAR_ANIMATION:
            roboDisplay::animation(gear_frames,_intValue);
            break;

          case DisplayAction::RECORD_ANIMATION:
            roboDisplay::animation(record_frames,_intValue);
            break;

          case DisplayAction::HOURGLASS_ANIMATION:
            roboDisplay::animation(hourglass_frames,_intValue);
            break;

          case DisplayAction::LOADER_ANIMATION:
            roboDisplay::animation(loader_frames,_intValue);
            break;

          case DisplayAction::BELL_ANIMATION:
            roboDisplay::animation(bell_frames,_intValue);
            break;

          case DisplayAction::CHAT_ANIMATION:
            roboDisplay::chat();
            break;

          case DisplayAction::IMG_LOOP:
            roboDisplay::imgloop();
            break;

          default:
            roboDisplay::neutral();
            break;
        };          
      }        
    vTaskDelay(150);
  }
}

void roboDisplay::imgloop() {

  // Test routine
  for (int i = 0; i <= img_allArray_LEN; i++) {
    drawbitmap_from_index(i);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    if (uxQueueMessagesWaiting(displayQueue) > 0 ) {
      return;
    }
  }
}

void roboDisplay::text(const char *text, int size, int duration) {
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
    roboDisplay::neutral();
  }
};

void roboDisplay::scrollText(const char *text, int wait) {  
  
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

void roboDisplay::drawbitmap_from_index(int index) {
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  ledMatrix.drawBitmap(0, 0, img_allArray[index], 128, 63, 1, 0);
  ledMatrix.display();
};

void roboDisplay::drawbmp(const char *data) {
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  const uint8_t *bitmap = reinterpret_cast<const uint8_t*>(data);
  ledMatrix.drawBitmap(0, 0, bitmap, 128, 63, 1, 0);
  ledMatrix.display();
};

void roboDisplay::neutral() {
  roboDisplay::drawbitmap_from_index(7);
  ledMatrix.display();
};

void roboDisplay::smile() {
  roboDisplay::drawbitmap_from_index(5);
  ledMatrix.display();
};

void roboDisplay::lookLeftAni(int wait) {
  int _wait = wait == 0 ? 100 : wait;
  roboDisplay::neutral();

  for (int i = 0; i < 10; i++) {
    ledMatrix.drawBitmap(42 - i, 13, img_allArray[10], 44, 30, 1, 0); // Eyes only
    ledMatrix.display();

    vTaskDelay(wait);
    if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
  }
};

void roboDisplay::lookRightAni(int wait) {
  int _wait = wait == 0 ? 100 : wait;
  roboDisplay::neutral();

  for (int i = 0; i < 10; i++) {
    ledMatrix.drawBitmap(42 + i, 13, img_allArray[10], 44, 30, 1, 0); // Eyes only
    ledMatrix.display();

    vTaskDelay(wait);
    if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
  }
};

void roboDisplay::wink(int wait) {
  int _wait = wait == 0 ? 100 : wait;
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  roboDisplay::neutral();

  for (int i = 0; i < 12; i = i + 6) {
    ledMatrix.fillRect(47, 15, 20, 18 + i, 1);
    ledMatrix.display();

    vTaskDelay(_wait);
    if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }

  }
  
  vTaskDelay(_wait);
  roboDisplay::neutral();
};

void roboDisplay::blink(int wait) {
  int _wait = wait == 0 ? 100 : wait;
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();

  roboDisplay::neutral();

  for (int i = 0; i < 12; i = i + 6) {
    ledMatrix.fillRect(47, 15, 46, 18 + i, 1);
    ledMatrix.display();

    vTaskDelay(_wait);
    if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
  }
  
  vTaskDelay(_wait);
  roboDisplay::neutral();
};


void roboDisplay::shake(int wait) {
  roboDisplay::neutral();

  int _wait = 0;
  bool left = true;

  for (int i = 0; i < 15; i = i + 2) {
    
    if (left) {
      ledMatrix.drawBitmap(42 - i, 13, img_allArray[10], 44, 30, 1, 0); // Eyes only
      left = false;
    } else {
      ledMatrix.drawBitmap(42 + i, 13, img_allArray[10], 44, 30, 1, 0); // Eyes only
      left = true;
    }

    ledMatrix.display();
    vTaskDelay(_wait);
    if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
  }
  
  roboDisplay::neutral();
};

void roboDisplay::cylon(int wait) {
  
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  int _wait = wait == 0 ? 50 : wait;

  // Three times
  while(true) {
    for (int x = 0; x <= 110; x = x + 10) {
      ledMatrix.clearDisplay();
      ledMatrix.fillRect(x, 20, 18, 24, 1);
      ledMatrix.display();
      
      vTaskDelay(_wait);

      if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
    }

    for (int x = 110; x > 0; x = x - 10) {
      ledMatrix.clearDisplay();
      ledMatrix.fillRect(x, 20, 18, 24, 1);
      ledMatrix.display();

      vTaskDelay(_wait);

      if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
    }

  }

  ledMatrix.clearDisplay();
  ledMatrix.display();
}

void roboDisplay::testfillrect(int wait) {

  ledMatrix.clearDisplay();
  for (int16_t i = 0; i < ledMatrix.height() / 2; i += 3) {
    // The INVERSE color is used so rectangles alternate white/black
    ledMatrix.fillRect(i, i, ledMatrix.width() - i * 2, ledMatrix.height() - i * 2, SSD1306_INVERSE);
    ledMatrix.display();  // Update screen with each newly-drawn rectangle
    vTaskDelay(10);

    if (uxQueueMessagesWaiting(displayQueue) > 0 ) { return; }
  }
}

void roboDisplay::scrollScreenLeft() {
  ledMatrix.startscrollleft(0, 63);
}

void roboDisplay::scrollScreenRight() {
  ledMatrix.startscrollright(0, 63);
}

void roboDisplay::stopScrolling() {
  ledMatrix.stopscroll();
}

void roboDisplay::animation(const byte frames[][512], int loop) {

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

void roboDisplay::chat() {
  
  roboDisplay::neutral();
  
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


/**
 FACE Led screen helper class for Sappie 
 for Adafruit_SSD1306 OLED Driver
**/
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "roboFace.h"
#include "sap_img.h"

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

roboFace::roboFace()
{
};

void roboFace::begin() {

 // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!ledMatrix.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, true)) {
    Serial.println(F("SSD1306 allocation failed"));
  } 

  ledMatrix.clearDisplay();
  ledMatrix.stopscroll();
  ledMatrix.display();
  ledMatrix.setTextColor(SSD1306_WHITE);

  delay(200);
};

void roboFace::exec( int action, String text, int time) {

  faceActionParams actionParams;

  actionParams.action = action;
  actionParams.text = text;
  actionParams.time = time;

  if (eTaskGetState(&faceTaskHandle) == eRunning) {
    vTaskDelete( &faceTaskHandle );

    // Recover time and procedure before starting next task after sudden break
    sleep(200);
    ledMatrix.clearDisplay();
    ledMatrix.stopscroll();
    ledMatrix.display();
    sleep(200);
  }
  
  xTaskCreatePinnedToCore(this->displayTask, "displayTask", 4096, (void*)&actionParams, 10, &faceTaskHandle, 1);  

};

void roboFace::displayTask(void * actionParams){

    faceActionParams* _actionParams = (faceActionParams*)actionParams;

    switch (_actionParams->action) {
    case faceAction::DISPLAYTEXTSMALL:
      roboFace::displayText(_actionParams->text,2);
      break;   
    case faceAction::DISPLAYTEXTLARGE:
      roboFace::displayText(_actionParams->text,3);
      break;   

    case faceAction::NEUTRAL:
      neutral();
      break;

    case faceAction::SMILE:
      smile(_actionParams->time);
      break;

    case faceAction::LOOKLEFT:
      lookLeftAni(_actionParams->time);
      break;

    case faceAction::LOOKRIGHT:
      lookRightAni(_actionParams->time);
      break;
    
    case faceAction::STARTUP:
      startUp();
      break;

    case faceAction::BLINK:
      blink(_actionParams->time);
      break;
      
    case faceAction::SHAKE:
      shake(_actionParams->time);
      break;

    case faceAction::TESTFILLRECT:
      testfillrect(_actionParams->time);
      break;

    case faceAction::CYLON:
      cylon(_actionParams->time);
      break;

    default:
      neutral();
      break;

    };

  sleep(200);
  vTaskDelete(NULL);

};

void roboFace::startUp() {

  lookLeftAni(50);
  delay(100);
  lookRightAni(50);
  
  delay(500);
  neutral();
  delay(1000);  
  wink(50);
  delay(1000);  
  
  smile(1000);

};

void roboFace::displayText(String text,int size) {

  ledMatrix.clearDisplay();
  ledMatrix.setTextSize(0.5);
  ledMatrix.setCursor(2, 30);
  ledMatrix.setFont(&FreeSansBold12pt7b);
  ledMatrix.print(text);
  ledMatrix.display();

};

void roboFace::scrollText(String text) {
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  ledMatrix.setTextSize(2); // Draw 2X-scale text
  ledMatrix.setCursor(0, 0);
  ledMatrix.println(text);
  ledMatrix.display();
  ledMatrix.startscrollleft(0x00, 0x0F);
};

void roboFace::drawbitmap(String name, int wait) {
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  
  //ledMatrix.drawBitmap(0,0, img1, 128, 64);
  
  ledMatrix.display();

  if (wait > 0) {
    delay(wait);
    neutral(); 
  }

};

void roboFace::neutral(){

  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();
  
  ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
  ledMatrix.fillCircle(leftEyeX, leftEyeY, eyeRadius, 0);
  ledMatrix.fillCircle(rightEyeX, rightEyeY, eyeRadius, 0);

  ledMatrix.display();

};

void roboFace::smile(int wait){

  neutral();
  ledMatrix.fillTriangle(leftEyeX + 17, leftEyeY + 18, rightEyeX - 17 , rightEyeY + 18, leftEyeX + ((rightEyeX - leftEyeX) / 2) , leftEyeY + 25, 0);

  ledMatrix.display();

  if (wait > 0 ) {
    delay(wait);
    neutral();  }

};

void roboFace::lookLeftAni(int wait) {

  neutral();

  for (int i = 0; i < 15 ; i++) {

    ledMatrix.clearDisplay();
    
    ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
    ledMatrix.fillCircle(leftEyeX - i, leftEyeY, eyeRadius, 0);
    ledMatrix.fillCircle(rightEyeX - i, rightEyeY, eyeRadius, 0);
    ledMatrix.display();

    delay(wait);

  }

};

void roboFace::lookRightAni(int wait) {

  neutral();
  
  for (int i = 0; i < 15 ; i++) {

    ledMatrix.clearDisplay();
    
    ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
    ledMatrix.fillCircle(leftEyeX + i, leftEyeY, eyeRadius, 0);
    ledMatrix.fillCircle(rightEyeX + i, rightEyeY, eyeRadius, 0);
    ledMatrix.display();

    delay(wait);

  }

};

void roboFace::blink(int wait) {

  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();

  ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
  ledMatrix.display();

  delay(wait);

  neutral();
};


void roboFace::wink(int wait) {
  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();

  ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
  ledMatrix.fillCircle(rightEyeX, rightEyeY, eyeRadius, 0);
  ledMatrix.display();

  delay(wait);

  neutral();
};

void roboFace::shake(int wait) {

  neutral();
  bool left = true;

  for (int i = 0; i < 15 ; i++) {

    ledMatrix.clearDisplay();
    if (left) {
      ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
      ledMatrix.fillCircle(leftEyeX - i, leftEyeY, eyeRadius, 0);
      ledMatrix.fillCircle(rightEyeX - i, rightEyeY, eyeRadius, 0);
      ledMatrix.display();
      left = false;
    }  else {
      ledMatrix.fillRoundRect(rectX1, rectY1, rectX2, rectY2, rectRadius, 1);
      ledMatrix.fillCircle(leftEyeX + i, leftEyeY, eyeRadius, 0);
      ledMatrix.fillCircle(rightEyeX + i, rightEyeY, eyeRadius, 0);
      ledMatrix.display();
      left = true;
    } 

    delay(wait);
    neutral();
    delay(wait);
  }

};

void roboFace::cylon(int wait) {

  ledMatrix.stopscroll();
  ledMatrix.clearDisplay();

}

void roboFace::testfillrect(int wait) {
  ledMatrix.clearDisplay();

  for(int16_t i=0; i<ledMatrix.height()/2; i+=3) {
    // The INVERSE color is used so rectangles alternate white/black
    ledMatrix.fillRect(i, i, ledMatrix.width()-i*2, ledMatrix.height()-i*2, SSD1306_INVERSE);
    ledMatrix.display(); // Update screen with each newly-drawn rectangle
    delay(1);
  }

  delay(wait);
}




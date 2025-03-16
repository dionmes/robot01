/**
 TCP2AUDIO class 
 for receiving raw audio stream over tcp and sending it to i2s
**/

#include "driver/i2s_std.h"
#include <WiFi.h>
#include <esp_wifi.h>  

#ifndef TCP2AUDIO_H
#define TCP2AUDIO_H

/**
  tcp2audio class
  receives TCP Audio Data (PCM16le) and outputs it to I2S
*/

class tcp2audio {
  public:
    tcp2audio();
    // Begin 
    void begin(int sample_rate = 16000, int tcp_task_core = 0, int tcp_task_priority = 15, int i2s_task_core = 1, int i2s_task_priority = 15);

    void start(); // Stop audio tasks
    void stop();  // Start audio tasks
    void setvolume(int volume);  // Start audio tasks
    int getvolume();

    bool _running; // Flag is audio is running

  private:
    static void tcpReceiveAudioTask(void *pvParameters);
    static void i2sAudioTask(void *pvParameters);

    uint32_t _sample_rate; // Sample rate, default 16000
    int _tcp_task_core; // task core for receiving tcp, default 0
    int _tcp_task_priority; // task priority for receiving tcp, default 15
    int _i2s_task_core; // task core for i2s output, default 1
    int _i2s_task_priority; // task priority for i2s output, default 15

};

#endif

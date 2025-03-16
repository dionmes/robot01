#include "driver/i2s_types.h"
#include "soc/clk_tree_defs.h"
#include "hal/i2s_types.h"
#include "esp_intr_alloc.h"

/**
 TCP2AUDIO class 
 for receiving raw audio stream over tcp and sending it to i2s
**/

#include "tcp2audio.h"
#include "driver/i2s_std.h"
#include "freertos/ringbuf.h"
#include <WiFi.h>
#include <esp_wifi.h>  

#define I2S_BCK GPIO_NUM_14
#define I2S_WS GPIO_NUM_12
#define I2S_OUT GPIO_NUM_11

#define I2S_DMA_DESC_NUM 8
#define I2S_DMA_FRAME_NUM 256

#define AUDIO_TCP_PORT 9000 // TCP port for audio 
#define AUDIO_TCP_CHUNK_SIZE 1024 // TCP Buffer

#define AUDIO_RINGBUFFER_SIZE 131072

#define MAX_VOLUME 50
#define DEFAULT_VOLUME 30

// TCP server
WiFiServer audioServer(AUDIO_TCP_PORT);
// TCP client handler
WiFiClient tcpClientAudio;

TaskHandle_t tcpAudioTaskHandle; // Task for receiving Audio from TCP
TaskHandle_t i2sAudioTaskHandle; // Task for outputting Audio via I2S

static i2s_chan_handle_t i2s_tx_handle;  // I2S TX channel handle
static RingbufHandle_t audioRingBuffer;  // ESP-IDF Ring Buffer handle

static volatile int volume_shared = DEFAULT_VOLUME; 
static SemaphoreHandle_t volumeMutex;

tcp2audio::tcp2audio() {
  
  volumeMutex = xSemaphoreCreateMutex();

  audioRingBuffer = xRingbufferCreate(AUDIO_RINGBUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
  if (audioRingBuffer == NULL) {
      printf("Failed to create ring buffer\n");
  } else {
      printf("Ring buffer created successfully\n");
  }

  _running = false;

}

void tcp2audio::begin(int sample_rate, int tcp_task_core, int tcp_task_priority, int i2s_task_core, int i2s_task_priority) {

  _sample_rate = sample_rate;
  _tcp_task_core = tcp_task_core;
  _tcp_task_priority = tcp_task_priority;
  _i2s_task_core = i2s_task_core;
  _i2s_task_priority = i2s_task_priority;

  // GPIO Pin configuration for I2S pins
	gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_DISABLE; //disable interrupt
  io_conf.mode = GPIO_MODE_OUTPUT; //set as output mode
  io_conf.pin_bit_mask = I2S_OUT | I2S_WS | I2S_BCK;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; //disable pull-down mode
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE; //disable pull-up mode
  gpio_config(&io_conf); //configure GPIO with the given settings
 
  // I2S channel config
  i2s_chan_config_t chan_cfg = {
      .id = I2S_NUM_0,
      .role = I2S_ROLE_MASTER,
      .dma_desc_num = I2S_DMA_DESC_NUM,    
      .dma_frame_num = I2S_DMA_FRAME_NUM,  
      .auto_clear = true
  };

  i2s_new_channel(&chan_cfg, &i2s_tx_handle, NULL); // Create TX channel

  // I2S standard config
  i2s_std_config_t i2s_std_cfg = {
      .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(_sample_rate),
      .slot_cfg = I2S_STD_PCM_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = I2S_BCK,
          .ws   = I2S_WS,
          .dout = I2S_OUT,
          .din  = I2S_GPIO_UNUSED
      }
  };

  i2s_channel_init_std_mode(i2s_tx_handle, &i2s_std_cfg);  
  i2s_channel_enable(i2s_tx_handle);

  esp_err_t res = esp_intr_dump(stdout);
  if (res != ESP_OK) {
      printf("Failed to dump interrupt information!\n");
  }
    // Start TCP server
    audioServer.begin();

}

void tcp2audio::start() {
  vTaskDelay(100);
  _running = true;
  // I2S Audio receive task
  xTaskCreatePinnedToCore(i2sAudioTask,"I2SAudioTask", 4096, NULL,_i2s_task_priority,&i2sAudioTaskHandle,_i2s_task_core);
  vTaskDelay(100);
  // TCP Audio receive task
  xTaskCreatePinnedToCore(tcpReceiveAudioTask,"TCPReceiveAudioTask", 8092, NULL,_tcp_task_priority,&tcpAudioTaskHandle,_tcp_task_core);
}

void tcp2audio::stop() {
    _running = false;
  xTaskNotify(i2sAudioTaskHandle, 1, eSetValueWithOverwrite);
  xTaskNotify(tcpAudioTaskHandle, 1, eSetValueWithOverwrite);
  Serial.println("Audio streaming stopping.");
}

void tcp2audio::setvolume(int volume) {
   
    if (xSemaphoreTake(volumeMutex, portMAX_DELAY)) {
      if (volume > MAX_VOLUME) { volume = MAX_VOLUME; }
      volume_shared = volume;
      xSemaphoreGive(volumeMutex);
      Serial.printf("Volume : %i \n",volume_shared );
  }

}

int tcp2audio::getvolume() {
  return volume_shared;
}

// Task function to handle incoming TCP data for Audio 
IRAM_ATTR void tcp2audio::tcpReceiveAudioTask(void *pvParameters) {

  uint32_t notifyStopValue;  // vtask notifier to stop
  uint8_t tcp_audio_buffer[AUDIO_TCP_CHUNK_SIZE]; 

  size_t bytesReceived;
  size_t num_samples;
  float gain;
  int16_t *samples;
  int32_t amplified;
  BaseType_t res;

  Serial.println("Audio TCP host starting.");

  while (true) {

    if (!tcpClientAudio || !tcpClientAudio.connected()) {
        tcpClientAudio = audioServer.accept();  // Check for new clients
    }

    while (tcpClientAudio && tcpClientAudio.connected()) {
        
      bytesReceived = tcpClientAudio.read(tcp_audio_buffer, AUDIO_TCP_CHUNK_SIZE);

      if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
        vTaskDelete(NULL);
      }
    
      if (bytesReceived > 0) {

        num_samples = bytesReceived / 2; // 16-bit samples = 2 bytes per sample
        gain = volume_shared / 10;
        samples = (int16_t *)tcp_audio_buffer;

        for (size_t i = 0; i < num_samples; i++) {
            // Amplify the sample
            amplified = (int32_t)(samples[i] * gain);
            // Clamp the value to the 16-bit range to avoid clipping
            if (amplified > 32767) {
                amplified = 32767;
            } else if (amplified < -32768) {
                amplified = -32768;
            }
            // Store the amplified value back into the buffer
            samples[i] = (int16_t)amplified;
        }

        res = xRingbufferSend(audioRingBuffer, (void *)samples, bytesReceived, pdMS_TO_TICKS(100));
        if (res != pdTRUE) {
            Serial.println("TCP Audio Ring buffer full! Dropping audio data.");
        }

        // Dynamically adjust delay/bandwidth based on free buffer space
        size_t free_size = xRingbufferGetCurFreeSize(audioRingBuffer);
        
        Serial.printf("TCP Audio Ring buffer free space : %zu\n", free_size );
        TickType_t delay;

        if (free_size > (AUDIO_RINGBUFFER_SIZE * 0.90)) {
            delay = 5;  
        } else if (free_size > (AUDIO_RINGBUFFER_SIZE * 0.75)) {
            delay = 25;  
        } else if (free_size < (AUDIO_RINGBUFFER_SIZE * 0.25)) {
            delay = 500; 
        } else {
            delay = 100; 
        }
        Serial.printf("TCP Audio network delay : %zu\n", delay );
      
        vTaskDelay(delay); 

      } else {
        // Delay to prevent CPU overload
        vTaskDelay(200);
      }
    }

    if (tcpClientAudio && !tcpClientAudio.connected()) {
      Serial.println("tcpClientAudio disconnected.");
      tcpClientAudio.stop();
    }

    vTaskDelay(200);
  }
}

// Task function to output data to I2S
IRAM_ATTR void tcp2audio::i2sAudioTask(void *pvParameters) {

  uint32_t notifyStopValue;  // vtask notifier to stop

  size_t bytesRead;
  uint8_t *outputBuffer;
  size_t bytes_written;

  Serial.println("Audio I2S Output starting.");

  while (true) {
    outputBuffer = (uint8_t *)xRingbufferReceive(audioRingBuffer, &bytesRead, pdMS_TO_TICKS(100));

    if (xTaskNotifyWait(0x00, 0x00, &notifyStopValue, 0) == pdTRUE) {
      vTaskDelete(NULL);
    }

    if (outputBuffer != NULL) {
        //Serial.printf("I2S Bytes from buffer : %zu \n",bytesRead);
        i2s_channel_write(i2s_tx_handle, outputBuffer, bytesRead, &bytes_written, portMAX_DELAY);
        //Serial.printf("I2S Bytes written to I2S : %zu \n",bytes_written);
        
        // Free the memory allocated by Ring Buffer
        vRingbufferReturnItem(audioRingBuffer, (void *)outputBuffer);
    } else {
        vTaskDelay(pdMS_TO_TICKS(50)); // No data available, wait
    }

  }

}


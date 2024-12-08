# Robot01

![Robot image](./img/robot01.png)

A ripped Robosapien LLM powered robot.  

- **Hardware**
	- Robosapien V1
	- Nvidia AGX Xavier
	- Adafruit HUZZAH32 ESP32 Feather (Main)
	- XIAO ESP32S3 Sense (camera/mic)
	- DFRobot MAX98357 I2S Amplifier Module - 2.5W
	- VL53L1X Time of Flight Sensor 
	- BNO085 ( 9-DOF Orientation )
	- SSD_1306 - Led Screen I2C
	- Custom motor control board
		- MCP23017 ( input/output expander ) x 2
		- LD293D (Dual H-Bridge Motor Driver) x 4

- **Firmware**  
	Two ESP32 boards
	- Main : body movement, display, speaker and sensors.
	- Sense : the camera and mic (sense).

- **Web interface & AI models, "The brain" **  
	- Implemented on a Nvidia AGX Xavier development board. All models run locally (tts, stt, llm)
	- Python based webserver (Flask) for Wen interface and API calls


## Intro

I am not a hardware guy, so the Hardware is really a mess. It just a bunch of sensors and other peripherals hooked up to two esp32 boards. Use it for inspiration not implementation.  


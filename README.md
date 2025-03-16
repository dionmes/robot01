# Robot01

![Robot image](./img/robot01.png)

A ripped Robosapien Agentic / LLM powered robot.
This is a hobby project on trying out ESP32 microcontrollers while at the same time looking at possibilities of hooking up (local) LLM's to control hardware.

- **Hardware**
	- Robosapien V1
	- Nvidia AGX Xavier 32GB (the brain)
	- Unexpected Maker Feather S3 ESP32 (Main board)
	- XIAO ESP32S3 Sense (camera/mic)
	- DFRobot MAX98357 (I2S Amplifier Module - 2.5W)
	- VL53L1X (Time of Flight Sensor)
	- BNO085 ( 9-DOF Orientation )
	- SSD_1306 - Led Screen (I2C)
	- Custom motor control board
		- MCP23017 ( input/output expander ) x 2
		- LD293D (Dual H-Bridge Motor Driver) x 4

- **ESP32 usage**  
	- Main : body movement, display, sensors and speaker (udp streaming).
	- Sense : the camera and mic (udp streaming).

- **Backend**
	Backend Implemented on a Nvidia AGX Xavier
	- Web interface & AI models
	- All models run locally (tts, stt, llm, agentic)
	- LLM : Ollama engine
		- LLM_MODEL: llama3.2 (can change)
		- AGENT_MODEL: qwen2.5:32b (can change)
		- LLM_EXPRESSION_MODEL: gemma2:2b (can change)
		- VISION_MODEL: llava (can change)
	- Agentic: Langchain
	- TTS: speecht5_tts
	- STT: distil-large-v3, VAD: silero_vad  
	- Python based webserver (Flask) for Web interface and API

- **Frontend**
	- Simple HTML frontend based on PicoCSS and jQuery

## Intro

I am not a hardware guy, so the Hardware is really a mess. 
It just a bunch of sensors and other peripherals hooked up to two esp32 boards.
Use it for inspiration not implementation.



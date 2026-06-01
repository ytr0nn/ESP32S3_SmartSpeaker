# ESP32S3_SmartSpeaker

An open-source, fully functional AI-powered smart speaker built around the **ESP32S3** microcontroller. The project features local wake-word detection, seamless integration with cloud-based AI models (OpenAI & Google Gemini), and dynamic visual feedback using a round LCD display and an addressable LED ring.

## Features

* **Local Wake Word Detection:** Uses the ESPSR framework to recognize the wake word locally, ensuring low latency.
* **Cloud AI Pipeline:** * **STT:** OpenAI Whisper API.
  * **LLM :** Google Gemini 3.5 Flash API for fast and smart responses.
  * **TTS :** OpenAI TTS API.
* **High-Quality Audio:** Employs an I2S digital MEMS microphone for clear voice capture and an I2S DAC for crisp audio playback.
* **Rich User Interface:** * A GC9A01 round display powered by **LVGL** showing device states and real-time clock.
  * A WS2812B addressable LED ring providing smooth, color-coded animations.
* **RTOS Architecture:** Built on **FreeRTOS** using the native **ESP-IDF**.
* **Custom Enclosure:** Designed specifically for 3D printing to optimize acoustics and component mounting.

## 🛠️ Hardware Requirements

* **Development Board:** ESP32S3 DevKit (Must have **8MB PSRAM** for audio buffering and TLS handshake).
* **Microphone:** ICS43434.
* **DAC / Amplifier:** PCM5102 + D class compact Audio Amplifier.
* **Display:** GC9A01 240x240 Round LCD .
* **LED Ring:** WS2812B (32 LEDs).
* **Audio Switch:** CD4053BE Analog Multiplexer (for switching between ESP32 and Bluetooth audio sources).
* **Misc:** Capacitive touch buttons, 3D printed enclosure.

## 💻 Software & APIs

* **Framework:** ESP-IDF v5.3+.
* **Libraries:** LVGL , ESP-SR , cJSON.
* **API Keys Required:**
  * OpenAI API Key Whisper & TTS
  * Google Gemini API Key

## ⚙️ Architecture & Flow

1. **Idle:** The device syncs time via SNTP. The display shows a blue pulsating orb and the LED ring "breathes".
2. **Wake Word:** The `ICS43434` mic feeds data to the `ESPSR` engine. Upon detecting wake word, the UI turns yellow.
3. **Recording:** Voice is recorded into the ESP32's PSRAM. VAD automatically stops recording when silence is detected.
4. **Processing (White UI):** The audio is encapsulated with a WAV header and sent to **OpenAI Whisper**. The recognized text is forwarded to **LLM**.
5. **Playback (Green UI/Rainbow LED):** The text response is sent to **OpenAI TTS**, and the resulting audio stream is played back via the `PCM5102` DAC.

## 🚀 Installation & Setup
```bash
1. Clone the Repository

git clone [https://github.com/ytr0nn/ESP32S3_SmartSpeaker.git](https://github.com/ytr0nn/ESP32S3_SmartSpeaker.git)
cd ESP32S3_SmartSpeaker

2. Configure API Keys and Wi-Fi

Open main/network_api.h and main/ai_agent.h or your config files and insert your credentials

3. ESP-IDF Menuconfig (CRITICAL SETTINGS)

Because AI APIs require large SSL certificates, you must configure mbedTLS to use PSRAM. Run:
Bash

idf.py menuconfig

Apply the following settings:

    ESP-TLS: Enable Use global CA store and Use PSRAM for SSL session.

    mbedTLS: Set Memory allocation strategy to Allocate SSL/TLS memory into PSRAM.

    ESP System Settings: Enable Allow external memory as a heap pool.

    Component config -> ESP Speech Recognition: Ensure your partition table matches the model size.

4. Build and Flash
Bash

idf.py build
idf.py -p (YOUR PORT) flash monitor

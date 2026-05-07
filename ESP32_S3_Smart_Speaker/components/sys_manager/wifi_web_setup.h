#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts the Wi-Fi Access Point and Web Server for configuration.
 * Connect to SSID: "SmartSpeaker_Setup", then go to http://192.168.4.1
 */
void wifi_web_setup_start(void);

#ifdef __cplusplus
}
#endif
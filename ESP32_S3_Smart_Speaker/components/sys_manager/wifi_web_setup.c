#include "wifi_web_setup.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WIFI_SETUP";

// Simple HTML page for Wi-Fi Setup
static const char* setup_html = 
    "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>Wi-Fi Setup</title><style>"
    "body{font-family:Arial,sans-serif; background-color:#121212; color:#ffffff; padding:20px; text-align:center;}"
    "input{display:block; width:90%; margin:10px auto; padding:15px; border-radius:8px; border:none; font-size:16px;}"
    "button{padding:15px; width:95%; background:#007BFF; color:white; border:none; border-radius:8px; font-size:18px; font-weight:bold; cursor:pointer;}"
    "</style></head><body>"
    "<h2>Smart Speaker Setup</h2>"
    "<p>Enter your Wi-Fi credentials</p>"
    "<form action=\"/connect\" method=\"POST\">"
    "<input type=\"text\" name=\"ssid\" placeholder=\"Wi-Fi Name (SSID)\" required>"
    "<input type=\"password\" name=\"password\" placeholder=\"Password\">"
    "<button type=\"submit\">Save & Connect</button>"
    "</form></body></html>";

// GET / handler: Serves the HTML page
static esp_err_t get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, setup_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// POST /connect handler: Parses credentials and saves to NVS
static esp_err_t post_handler(httpd_req_t *req) {
    char buf[128];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Read POST data (Format: ssid=MyNet&password=MyPass)
    if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
        return ESP_FAIL;
    }
    buf[remaining] = '\0';

    char ssid[32] = {0};
    char password[64] = {0};

    // Parse URL-encoded body
    if (httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid)) == ESP_OK) {
        httpd_query_key_value(buf, "password", password, sizeof(password));
        
        ESP_LOGI(TAG, "Received SSID: %s", ssid);

        // Save to NVS
        nvs_handle_t nvs_handle;
        if (nvs_open("wifi_cfg", NVS_READWRITE, &nvs_handle) == ESP_OK) {
            nvs_set_str(nvs_handle, "ssid", ssid);
            nvs_set_str(nvs_handle, "password", password);
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
        }

        // Send Success Response
        const char* success_msg = "<html><body style='font-family:Arial;text-align:center;color:black;'>"
                                  "<h2>Saved!</h2><p>Speaker is restarting to connect to Wi-Fi...</p></body></html>";
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, success_msg, HTTPD_RESP_USE_STRLEN);

        // Restart device after 2 seconds to apply new settings
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }

    return ESP_OK;
}

void wifi_web_setup_start(void) {
    ESP_LOGI(TAG, "Starting AP Mode for Web Setup...");

    esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = "SmartSpeaker_Setup",
            .ssid_len = strlen("SmartSpeaker_Setup"),
            .channel = 1,
            .password = "", // Open network for easy setup
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start HTTP Server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_get);

        httpd_uri_t uri_post = {
            .uri      = "/connect",
            .method   = HTTP_POST,
            .handler  = post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_post);
        
        ESP_LOGI(TAG, "Web server started on http://192.168.4.1");
    }
}
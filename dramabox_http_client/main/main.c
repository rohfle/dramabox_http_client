/* ESP HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/queue.h"

#include "gpios.h"
#include "app_wifi.h"
#include "debounce.h"

#include "esp_http_client.h"

#define MAX_HTTP_RECV_BUFFER 512
static const char *TAG = "HTTP_CLIENT";

/* Root cert for howsmyssl.com, taken from howsmyssl_com_root_cert.pem

   The PEM file was extracted from the output of this command:
   openssl s_client -showcerts -connect www.howsmyssl.com:443 </dev/null

   The CA root cert is the last cert given in the chain of certs.

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern const char howsmyssl_com_root_cert_pem_start[] asm("_binary_howsmyssl_com_root_cert_pem_start");
extern const char howsmyssl_com_root_cert_pem_end[]   asm("_binary_howsmyssl_com_root_cert_pem_end");

QueueHandle_t tsqueue;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Write out data
                // printf("%.*s", evt->data_len, (char*)evt->data);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

static void send_to_server(bool open)
{
    esp_http_client_config_t config = {
        .url = "https://drama-box-log.now.sh/boxstatus",
        .event_handler = _http_event_handler,
        .cert_pem = howsmyssl_com_root_cert_pem_start,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    char *post_data;

    if (open) {
      post_data = "{\"status\":\"opened\"}\n";
    } else {
      post_data = "{\"status\":\"closed\"}\n";
    }
    ESP_LOGI(TAG, "sending data = %s", post_data);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        char *buffer = malloc(MAX_HTTP_RECV_BUFFER);
        int content_length =  esp_http_client_get_content_length(client);
        int total_read_len = 0, read_len;
        if (total_read_len < content_length && content_length <= MAX_HTTP_RECV_BUFFER) {
            read_len = esp_http_client_read(client, buffer, content_length);
            if (read_len <= 0) {
                ESP_LOGE(TAG, "Error read data");
                free(buffer);
                return;
            }
            buffer[read_len] = 0;
            ESP_LOGD(TAG, "read_len = %d", read_len);
            ESP_LOGI(TAG, "buffer = %s", buffer);
        }
        free(buffer);
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);

}

static void http_test_task(void *pvParameters)
{
    app_wifi_wait_connected();

    bool button_state;
    ESP_LOGI(TAG, "Connected to AP, begin http example");
    while (1) {
      xQueueReceive(tsqueue, &button_state, portMAX_DELAY);
      ESP_LOGI(TAG, "Button state change to %i received, sending to server", button_state);
      send_to_server(button_state);
    }
}

void setup_gpios() {
  gpio_pad_select_gpio(WIFI_GPIO);
  gpio_set_direction(WIFI_GPIO, GPIO_MODE_OUTPUT);
  gpio_pad_select_gpio(BOX_OPEN_GPIO);
  gpio_set_direction(BOX_OPEN_GPIO, GPIO_MODE_OUTPUT);
  gpio_pad_select_gpio(BOX_SWITCH_GPIO);
  gpio_set_direction(BOX_SWITCH_GPIO, GPIO_MODE_INPUT);
  gpio_pullup_en(BOX_SWITCH_GPIO);
}

void app_main()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    setup_gpios();
    app_wifi_initialise();

    tsqueue = xQueueCreate(2, sizeof(bool));

    xTaskCreate(&button_poll_task, "button_poll_task", 8192, &tsqueue, 10, NULL);
    xTaskCreate(&http_test_task, "http_test_task", 8192, NULL, 5, NULL);

    do {
      if (app_wifi_connected()) {
        gpio_set_level(WIFI_GPIO, 1);
      } else {
        gpio_set_level(WIFI_GPIO, 0);
      }
      vTaskDelay(100 / portTICK_PERIOD_MS);

    } while(1);
}

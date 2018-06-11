/* 
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "app_wifi.h"
#include "gpios.h"

#define TAG "debounce.c"

void button_poll_task(void* pvParameters)
{
	app_wifi_wait_connected();

	QueueHandle_t *tsqueue = (QueueHandle_t *)pvParameters;

  bool oldState = gpio_get_level(BOX_SWITCH_GPIO);
	bool currentState;


	TickType_t currentTime;

	uint32_t counter = 0;

	#define MAX_COUNT 3
	while (1) {
		currentState = gpio_get_level(BOX_SWITCH_GPIO);
		if (currentState != oldState) {
			counter += 1;
			if (counter >= MAX_COUNT) {
				oldState = currentState;
				counter = 0;
				currentTime = xTaskGetTickCount();
				if (currentState == 0) {
					ESP_LOGI(TAG, "Box is closed, current tick=%d", currentTime);
				} else {
					ESP_LOGI(TAG, "Box is opened, current tick=%d", currentTime);
				}
				// Send to other thread the new status of the box
				xQueueSend(*tsqueue, &currentState, portMAX_DELAY);
			}
		} else {
			counter = 0;
		}
		vTaskDelay(10 / portTICK_PERIOD_MS);
	}
}

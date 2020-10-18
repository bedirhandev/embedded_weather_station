#include "sender.h"
#include "si7021.h"
#include "ldr.h"

#include "nvs_flash.h"
#include "esp_log.h"

static xQueueHandle queue;

void app_main(void)
{
	queue = xQueueCreate(2, sizeof(char*));

//	esp_err_t ret = nvs_flash_init();
//	if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
//		ESP_ERROR_CHECK(nvs_flash_erase());
//		ret = nvs_flash_init();
//	}
//	ESP_ERROR_CHECK(ret);
	
	initialise_wifi();

	// in total around 9KB space occupied
	xTaskCreate(ldr_task, "ldr_task", 1024 * 1, (void*) queue, tskIDLE_PRIORITY + 2, NULL);
	xTaskCreate(i2c_task, "i2c_task", 1024 * 2, (void*) queue, tskIDLE_PRIORITY + 2, NULL);
	xTaskCreate(http_post_task, "http_post_task", 1024 * 3, (void*) queue, tskIDLE_PRIORITY + 1, NULL);
}
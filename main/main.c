#include "sender.h"
#include "si7021.h"

#include "nvs_flash.h"

void app_main(void)
{
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	
	initialise_wifi();

	//start i2c task
	xTaskCreate(i2c_task, "i2c_task", 8096, NULL, tskIDLE_PRIORITY + 2, NULL);
	xTaskCreate(http_post_task, "http_post_task", 8096, NULL, tskIDLE_PRIORITY + 1, NULL);
}
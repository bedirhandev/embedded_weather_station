/*!
 * \file ldr.c
 * \brief Reads the voltage difference across the ldr and a 100K resistor.
 * \author Bedirhan Dincer
*/
#include "ldr.h"

#include <string.h>
#include <stdio.h>

#include <FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include <esp_log.h>
#include <driver/adc.h>

/** A reusable JSON template to send a JSON message onto the queue. */
static const char* JSON_TEMPLATE = "{\"type\":\"%s\",\"value\":\"%s\"}";

/** The structure that will hold the data of the made measurement. */
typedef struct { uint32_t lux; } ldr;

static const char *TAG = "adc1";

/*!
 * \brief A FreeRTOS task for measuring lux using ADC.
 * \param pvParameters contains a reference to the queue.
 */
void ldr_task(void *pvParameters)
{
	/* The ldr will hold the data from the made measurement. */
	ldr* data = (ldr *)malloc(sizeof(ldr));

	/* Message buffer that will hold the measurementand send it to the queue. */
	char *message = (char *)malloc(128 * sizeof(char));
	
	/* The char buffer which holds the conversion value from the made measurement. */
	char lux[32];
	
	uint16_t adc_data;
	adc_config_t adc_config;
	adc_config.mode = ADC_READ_TOUT_MODE;
	adc_config.clk_div = 10;
	adc_init(&adc_config);
	
	while (1)
	{
		if (ESP_OK == adc_read(&adc_data))
		{
			ESP_LOGI(TAG, "ADC read: %d\r\n", adc_data);
		}
		
		/* Get random value */
		data->lux = adc_data;
		
		/* Put the measurements in a readable JSON formatted string. */
		sprintf(message, JSON_TEMPLATE, "lux", gcvt(data->lux, 4, lux));
		
		/* Put the JSON formatted string onto the queue. */
		xQueueSend((QueueHandle_t *)pvParameters, &message, portMAX_DELAY);
		
		/* Delay task for 1 minute. */
		vTaskDelay((1 * 1000 * 60) / portTICK_RATE_MS);
	}
}
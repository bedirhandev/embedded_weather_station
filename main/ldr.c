#include "ldr.h"

#include <string.h>
#include <stdio.h>

#include <FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

static const char* JSON_TEMPLATE = "{\"type\":\"%s\",\"value\":\"%s\"}";

typedef struct
{
	uint32_t lux;
} ldr;

void ldr_task(void *pvParameters)
{
	//UBaseType_t uxHighWaterMark;
	//uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
	//printf("[Start] High Water Mark ldr: %lu", uxHighWaterMark);

	ldr* data = (ldr *)malloc(sizeof(ldr));
	char *message = (char *)malloc(128 * sizeof(char));
	
	char lux[32];
	
	while (1)
	{
		data->lux = esp_random();
		
		sprintf(message, JSON_TEMPLATE, "lux", gcvt(data->lux, 4, lux));
		
		xQueueSend((QueueHandle_t *)pvParameters, &message, portMAX_DELAY);

		//uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
		//printf("[End] High Water Mark ldr: %lu", uxHighWaterMark);
		
		vTaskDelay((1 * 1000 * 60) / portTICK_RATE_MS);
	}
}
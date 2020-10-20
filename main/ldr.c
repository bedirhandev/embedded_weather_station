/*!
 * \file ldr.c
 * \brief This file reads the lux from a ldr.
 * \author Bedirhan Dincer
*/
#include "ldr.h"

#include <string.h>
#include <stdio.h>

#include <FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

/** A reusable JSON template to send a JSON message onto the queue. */
static const char* JSON_TEMPLATE = "{\"type\":\"%s\",\"value\":\"%s\"}";

/** The structure that will hold the data of the made measurement. */
typedef struct { uint32_t lux; } ldr;

/*!
 * \brief A FreeRTOS task for measuring lux using ADC.
 */
void ldr_task(void *pvParameters)
{
	/* The ldr will hold the data from the made measurement. */
	ldr* data = (ldr *)malloc(sizeof(ldr));

	/* Message buffer that will hold the measurementand send it to the queue. */
	char *message = (char *)malloc(128 * sizeof(char));
	
	/* The char buffer which holds the conversion value from the made measurement. */
	char lux[32];
	
	while (1)
	{
		/* Get random value */
		data->lux = esp_random();
		
		/* Put the measurements in a readable JSON formatted string. */
		sprintf(message, JSON_TEMPLATE, "lux", gcvt(data->lux, 4, lux));
		
		/* Put the JSON formatted string onto the queue. */
		xQueueSend((QueueHandle_t *)pvParameters, &message, portMAX_DELAY);
		
		/* Delay task for 1 minute. */
		vTaskDelay((1 * 1000 * 60) / portTICK_RATE_MS);
	}
}
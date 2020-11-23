#include "sender.h"
#include "si7021.h"
#include "ldr.h"
#include "freertos/task.h"
#include "freertos/queue.h"

/** Create the queue for sending and receiving message between FreeRTOS tasks */
static xQueueHandle queue;

/*!
 * \brief The main entry point for creating the queue and the tasks.
 */
void app_main(void)
{
	/* Creating a queue that can hold two char pointers. */
	queue = xQueueCreate(2, sizeof(char*));
	
	/* Creating a tasks which is responsible for measuring lux. */
	xTaskCreate(ldr_task, "ldr_task", 1024 * 2, (void*) queue, tskIDLE_PRIORITY + 2, NULL);
	/* Creating a tasks which is responsible for measuring temperature and humdidity. */
	xTaskCreate(i2c_task, "i2c_task", 1024 * 3, (void*) queue, tskIDLE_PRIORITY + 2, NULL);
	/* Creating a tasks which is responsible for sending the measurements to the server. */
	xTaskCreate(http_post_task, "http_post_task", 1024 * 4, (void*) queue, tskIDLE_PRIORITY + 1, NULL);
}
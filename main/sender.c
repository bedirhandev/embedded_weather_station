/*!
 * \file sender.c
 * \brief Sends measurement data to the server and sleeps for 60 seconds
 * \author Bedirhan Dincer
 * 
 * \par Socket error code:
 * -# See https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/lwip.html#socket-error-reason-code
*/
#include "sender.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include "nvs_flash.h"
#include <netdb.h>

#define WIFI_SSID CONFIG_WIFI_SSID /*!< SSID of Wi-Fi AP. */
#define WIFI_PASS CONFIG_WIFI_PASSWORD /*!< Password of Wi-Fi AP. */

#define WEB_SERVER "europe-west1-tactical-crow-272620.cloudfunctions.net" /*!< The root server. */
#define WEB_PORT "80" /*!< Port number for TCP connection. */
#define WEB_URL "http://europe-west1-tactical-crow-272620.cloudfunctions.net/measurement" /*!< HTTP API endpoint */

#define SA struct sockaddr /*!< Structure for socket connection. */
#define MAX_LINE 1024 /*!< Max bits allowed to send in a HTTP POST request. */
#define MAX_REQUESTS 2 /*!< Max amount of requests possible. 60 seconds a time. */
#define SLEEP_DURATION 60 /*!< The deep sleep duration. */

static const char* JSON_HEAD = "{\"measurements\":["; /*!< The JSON formatted message header. */
static const char* JSON_TAIL = "]}"; /*!< The JSON formatted message tail. */
static const int32_t CONNECTED_BIT = BIT0; /*!< A 32-bit long with only the first bit set. */

static xSemaphoreHandle mutex_bus; /*!< The mutex bus keeps the shared function protected for a moment. */
static const char *TAG = "sender"; /*!< The TAG that is meant as a tag for who is writing to stdout. */

static EventGroupHandle_t wifi_event_group; /*!< The event group handle that is responsible for receiving events from the Wi-Fi driver. */

/*!
 * \brief An ESP event handler that communicates with the Wi-Fi driver.
 * \param ctx reserver for user.
 * \param event application specified event callback.
 * \return The operation was succesfull or failure.
 */
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
	/* For accessing reason codes in case of disconnection */
	system_event_info_t *info = &event->event_info;

	switch (event->event_id) {
		/* Connection to Wi-Fi was started. */
	case SYSTEM_EVENT_STA_START:
		/* A connection to a Wi-Fi AP is being made. */
		esp_wifi_connect();
		break;
		/* It has a connection and obtained a IP-address from DHCP. */
	case SYSTEM_EVENT_STA_GOT_IP:
		xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
		break;
		/* It got disconnected from the AP. */
	case SYSTEM_EVENT_STA_DISCONNECTED:
		ESP_LOGE(TAG, "Disconnect reason : %d", info->disconnected.reason);
		if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
			/*Switch to 802.11 bgn mode */
			esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCAL_11B | WIFI_PROTOCAL_11G | WIFI_PROTOCAL_11N);
		}
		/* Retry connecting to a Wi-Fi AP. */
		esp_wifi_connect();
		/* Reset wifi_event_group bit */
		xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
		break;
	default:
		break;
	}
	return ESP_OK;
}

/*!
 * \brief Setup a Wi-Fi connection with a AP.
 */
static void initialise_wifi(void)
{
	/* Initialize TCP/IP adapter. */
    tcpip_adapter_init();
	/* Creates an event handler which allocated heap from FreeRTOS memory. */
    wifi_event_group = xEventGroupCreate();
	/* Initialize the event loop. The loop checks for occuring event from the Wi-Fi driver. */
    esp_event_loop_init(event_handler, NULL);
	/* Initialise Wi-Fi with default settings. */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
	/* sets the WiFi API configuration storage type. Default is flash, now it will store it in memory from the flash. */
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
	/* Wi-Fi configuration: set the SSID and password of the AP. */
    wifi_config_t wifi_config = {
		.sta = {
			.ssid = WIFI_SSID,
			.password = WIFI_PASS,
		},
	};
	ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
	/* Wi-Fi configuration: station mode or AP mode. */
    esp_wifi_set_mode(WIFI_MODE_STA);
	/* Set the Wi-Fi configuration into the Wi-Fi driver. */
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
	/* Start the connection to the Wi-Fi AP. */
	esp_wifi_start();
}

/*!
 * \brief A FreeRTOS task for sending the measurements to the server.
 * \param pvParameters contains a reference to the queue.
 */
void http_post_task(void *pvParameters)
{
	/* Initialize the mutex handler. */
	mutex_bus = xSemaphoreCreateMutex();
	
	/* Message buffer for receiving messages from the queue. */
	char* message[128];
	
	const struct addrinfo hints =
	{
		.ai_family = AF_INET,
		/* Family ipv4. */
		.ai_socktype = SOCK_STREAM,
		/* SOCK_STREAM for TCP (connection-based protocol). */
	};
	
	struct addrinfo* res;
	
	int sockfd, recv;
	char recv_buf[64];
	
	EventBits_t ux_bits = 0;
	
	unsigned messages_send = 0;
	
	while (1) {
		/* Waits for incoming message on the queue and processes each message on the queue. */
		if (xQueueReceive((QueueHandle_t *)pvParameters, &message, portMAX_DELAY))
		{  
			/* A mutex is placed on the current process to make sure that no unexpected behaviour occurs. */
			if (xSemaphoreTake(mutex_bus, portMAX_DELAY))
			{
				/* If there is no connection made with an AP. Then try to connect. */
				if (ux_bits != CONNECTED_BIT) 
				{
					initialise_wifi();
				}
				
				/* Wait for the callback to set the CONNECTED_BIT in the event group. */
				ux_bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
				
				/* Do a DNS-lookup and store it's information into the var res. */
				if ((getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res)) != 0 || res == NULL)
				{
					/* The error code is obtained from errno. */
					//perror(errno);
				}
			    
				/* Socket allocated. */
				if ((sockfd = socket(res->ai_family, res->ai_socktype, 0)) < 0)
				{
					/* The error code is obtained from errno. */
					//perror(errno);
					freeaddrinfo(res);
				}
		    
				/* Socket connected. */
				if ((connect(sockfd, res->ai_addr, res->ai_addrlen)) != 0)
				{
					/* The error code is obtained from errno. */
					//perror(errno);
					freeaddrinfo(res);
				}
		    
				/* The function frees the allocated memory from the linked-list in a loop. */
				freeaddrinfo(res);
		    
				/* Declaration of the message that will be send with the request. */
				char sendline[MAX_LINE];
				
				/* Declaration of the content that contains the measurement data. */
				char content[512];
		    
				/* Build the JSON formateed messsage */
				int length = 0;
				length += sprintf(content + length, "%s", JSON_HEAD);
				length += sprintf(content + length, "%s", *message);
				length += sprintf(content + length, "%s", JSON_TAIL);
		    
				/* Build the HTTP POST request. */
				snprintf(sendline,
					MAX_LINE,
					"POST %s HTTP/1.0\r\n"
					"Host: %s\r\n"
					"Content-type: application/json\r\n"
					"Content-length: %d\r\n\r\n"
					"%s",
					WEB_URL,
					WEB_SERVER,
					(int)strlen(content),
					content);
		    
				/* Socket send. */
				if (write(sockfd, sendline, strlen(sendline)) < 0)
				{
					/* The error code is obtained from errno. */
					//perror(errno);
				}
					
				/* Read HTTP response.
				 * Repetition is done until there is no data left in the response. */
				do
				{
					/* Reset buffer. */
					bzero(recv_buf, sizeof(recv_buf));
					/* Read HTTP response in chunks. */
					recv = read(sockfd, recv_buf, sizeof(recv_buf) - 1);
					/* Print all characters inside the buffer to the console. */
					for (int i = 0; i < recv; i++) 
					{
						putchar(recv_buf[i]);
					}
				} while (recv > 0);
		    
				if (sockfd != -1) {
					shutdown(sockfd, 0);
					close(sockfd);
				}
		    
				/* Increment sended messages and check if it hits the max amount of requests. */
				if ((++messages_send) == MAX_REQUESTS)
				{
					ESP_LOGI(TAG, "Deep sleep mode for 60 seconds.\n");
					/* Put the uc into deep sleep mode. */
					//esp_deep_sleep(1000000UL * SLEEP_DURATION);
				}
			    
				/* Free the mutex bus. */
				xSemaphoreGive(mutex_bus);
			}
		}
	}
}
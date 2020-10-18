#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include <netdb.h>
#include <sys/socket.h> // It is not particularly necessary because netdb includes socket.h

#include "sender.h"
#include "freertos/queue.h"

#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

#define WEB_SERVER "europe-west1-tactical-crow-272620.cloudfunctions.net"
#define WEB_PORT 80
#define WEB_URL "http://europe-west1-tactical-crow-272620.cloudfunctions.net/measurement"

#define SA struct sockaddr
#define MAX_SUB 1024
#define MAX_LINE MAX_SUB - 1

static const char* JSON_HEAD = "{\"measurements\":[";
static const char* JSON_TAIL = "]}";
static const int CONNECTED_BIT = BIT0;

static xSemaphoreHandle mutexBus;
static const char *TAG = "sender";

static EventGroupHandle_t wifi_event_group;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    /* For accessing reason codes in case of disconnection */
    system_event_info_t *info = &event->event_info;

    switch(event->event_id) {
	// Station starts are we connected to WiFi?
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
	// After we are connected and a DHCP IP address is obtained.
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
	// Station got disconnected from AP.
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGE(TAG, "Disconnect reason : %d", info->disconnected.reason);
        if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCAL_11B | WIFI_PROTOCAL_11G | WIFI_PROTOCAL_11N);
        }
        esp_wifi_connect();
	    // Clear the bit.
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

void initialise_wifi(void)
{
	// init tcp/ip adapter
    tcpip_adapter_init();
	// create an event group allocates heap from freertos is used to indicate if
	// an event is happened. Event bits are often referred as event flags
    wifi_event_group = xEventGroupCreate();
	// initialize an event loop. the loop checks for occuring events in the system.
    esp_event_loop_init(event_handler, NULL);
	// initialise wifi with default configuration for the esp8266
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	// initialise wifi with default configuration. Set wifi event handlers.
    esp_wifi_init(&cfg);
	// sets the WiFi API configuration storage type. default is flash this will store in memory and flash.
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
	// wifi configuration station mode or ap mode
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
	// set wifi mode to station.
    esp_wifi_set_mode(WIFI_MODE_STA);
	// set config for wifi
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
	// start wifi
   esp_wifi_start();
}

void http_post_task(void *pvParameters)
{
	//UBaseType_t uxHighWaterMark;
	//uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
	//printf("[Start] High Water Mark sender: %lu", uxHighWaterMark);
	
	mutexBus = xSemaphoreCreateMutex();

	char* message[128];

	//unsigned int q_size;
	
	// Family iPv4, SOCK_STREAM for TCP (connection-based protocol) connection SOCK_DGRAM for UDP.
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo* res;
	//struct in_addr* addr;
	int s, r;
	char recv_buf[64];
	
    while(1) {
		//q_size = uxQueueMessagesWaiting((QueueHandle_t *)pvParameters);

	    if (xQueueReceive((QueueHandle_t *)pvParameters, &message, portMAX_DELAY))
	    {  
		    if (xSemaphoreTake(mutexBus, portMAX_DELAY))
		    {
				// Wait for the callback to set the CONNECTED_BIT in the event group.
				xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
		    
				getaddrinfo(WEB_SERVER, "80", &hints, &res);

				//addr = &((struct sockaddr_in*)res->ai_addr)->sin_addr;
		    
				s = socket(res->ai_family, res->ai_socktype, 0);
		    
				connect(s, res->ai_addr, res->ai_addrlen);
		    
				freeaddrinfo(res);
		    
				char sendline[MAX_LINE + 1];
			    char content[512];
		    
				int length = 0;
				length += sprintf(content + length, "%s", JSON_HEAD);
				length += sprintf(content + length, "%s", *message);
				length += sprintf(content + length, "%s", JSON_TAIL);
		    
			    // Neat way to put HTTP POST content into the sendline buffer
				snprintf(sendline,
					MAX_SUB,
					"POST %s HTTP/1.0\r\n"
					"Host: %s\r\n"
					"Content-type: application/json\r\n"
					"Content-length: %d\r\n\r\n"
					"%s",
					WEB_URL,
					WEB_SERVER,
					(int)strlen(content),
					content);
		    
				write(s, sendline, strlen(sendline));
		    
			    // read while socket sends response
			    // if the response contains 0 bytes the loop ends
				do 
				{
					// same as memset 0 but shorter
					bzero(recv_buf, sizeof(recv_buf));
					// reads from incoming HTTP response
					r = read(s, recv_buf, sizeof(recv_buf) - 1);
					// print the characters inside the buffer the C way
					for (int i = 0; i < r; i++) 
					{
						putchar(recv_buf[i]);
					}
				} while (r > 0);
		    
			    // close the TCP socket
				close(s);
			}
		    
		    //uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
		    //printf("[End] High Water Mark sender: %lu", uxHighWaterMark);
			    
			xSemaphoreGive(mutexBus);
	    }
    }
}
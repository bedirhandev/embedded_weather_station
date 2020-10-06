/* HTTP POST Example using plain POSIX sockets */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include <netdb.h>
#include <sys/socket.h> // hoef niet persee
#include "shared.h"

#include "sender.h"

#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

#define WEB_SERVER "europe-west1-tactical-crow-272620.cloudfunctions.net"
#define WEB_PORT 80
#define WEB_URL "http://europe-west1-tactical-crow-272620.cloudfunctions.net/measurement"

#define SA struct sockaddr
#define MAX_LINE 4096
#define MAX_SUB 1024

static const char* JSON_HEAD = "{\"measurements\":[";
static const char* JSON_TEMPLATE = "{\"type\":\"%s\",\"value\":\"%s\"}";
static const char* JSON_TAIL = "]}";
static const int CONNECTED_BIT = BIT0;
static const char *TAG = "sender";

static EventGroupHandle_t wifi_event_group;

esp_err_t event_handler(void *ctx, system_event_t *event)
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
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
	// initialise wifi with default configuration for the esp8266
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	// initialise wifi with default configuration. Set wifi event handlers.
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
	// sets the WiFi API configuration storage type. default is flash this will store in memory and flash.
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
	// wifi configuration station mode or ap mode
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
	// set wifi mode to station.
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
	// set config for wifi
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	// start wifi
    ESP_ERROR_CHECK( esp_wifi_start() );
}

void http_post_task(void *pvParameters)
{
	// Family iPv4, SOCK_STREAM for TCP (connection-based protocol) connection SOCK_DGRAM for UDP.
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    while(1) {
        /* Wait for the callback to set the CONNECTED_BIT in the
           event group.
        */
        xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                            false, true, portMAX_DELAY);
        ESP_LOGI(TAG, "Connected to AP");

	    // Hints is about which type of socket is supported. Res is an address of a location
	    // where the function can store a pointer to a linked list to one or more addrinfo structures.
        int err = getaddrinfo(WEB_SERVER, "80", &hints, &res);

	    // If res is a nullptr than the DNS lookup has failed
        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        /* Code to print the resolved IP.*/
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

	    // initialize tcp socket from received data 
        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

	    // connect to address using the address of socket and length of socket address
	    // the socket address is a combination existing of an IP address and a port number.
        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
	    // socket has a valid connection. 
	    // the information that the variable res holds is not needed anymore.
        freeaddrinfo(res);
	    
	    char sendline[MAX_LINE + 1];
	    //ssize_t n;
	    char *postContents = (char*)malloc(128 * sizeof(char));
	    
	    char temperature[50], humidity[50];
	    
	    sprintf(temperature, "%g", data.temperature);
	    sprintf(humidity, "%g", data.humidity);
	    
	    int length = 0;
	    length += sprintf(postContents + length, "%s", JSON_HEAD);
	    length += sprintf(postContents + length, JSON_TEMPLATE, "temperature", temperature);
	    length += sprintf(postContents + length, ",");
	    length += sprintf(postContents + length, JSON_TEMPLATE, "humidity", humidity);
	    length += sprintf(postContents + length, "%s", JSON_TAIL);
	    
	    ESP_LOGI(TAG, "%s", postContents);
	    
	    // 1st arg is a buffer, 2nd arg is the size, 3rd arg is the format
	    snprintf(sendline,
		    MAX_SUB,
		    "POST %s HTTP/1.0\r\n"
		    "Host: %s\r\n"
		    "Content-type: application/json\r\n"
		    "Content-length: %d\r\n\r\n"
		    "%s",
		    WEB_URL,
		    WEB_SERVER,
		    (int)strlen(postContents),
		    postContents);
	    
	    // free the content after sending it
	    free(postContents);
	
	    // send data over a TCP socket
	    if (write(s, sendline, strlen(sendline)) < 0) {
		    ESP_LOGE(TAG, "... socket send failed");
		    close(s);
		    vTaskDelay(4000 / portTICK_PERIOD_MS);
	    }
	   
        ESP_LOGI(TAG, "... socket send success");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... set socket receiving timeout success");

        /* Read HTTP response */
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }
        } while(r > 0);

        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
        close(s);
	    vTaskDelay((1 * 1000 * 60) / portTICK_RATE_MS);
        ESP_LOGI(TAG, "Starting again!");
    }
}
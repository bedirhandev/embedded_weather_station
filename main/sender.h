#pragma once
#include "esp_event_loop.h"

void initialise_wifi(void);

void http_post_task(void *pvParameters);
#pragma once
#include "esp_event_loop.h"

extern int h_errno;

esp_err_t event_handler(void *ctx, system_event_t *event);
void initialise_wifi(void);

void http_post_task(void *pvParameters);
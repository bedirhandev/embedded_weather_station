#pragma once

struct sensor_data { float humidity, temperature; };

extern void initialise_wifi();
extern void http_get_task(void *pvParameters);

extern struct sensor_data data;
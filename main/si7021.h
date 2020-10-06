#pragma once

#include "esp_err.h"
#include "driver/i2c.h"

esp_err_t i2c_master_init();
esp_err_t i2c_master_measure_relative_humidity(i2c_port_t i2c_num, uint8_t* data);
esp_err_t i2c_master_read_temperature_from_relative_humidity(i2c_port_t i2c_num, uint8_t* data);
float get_relative_humidity();
void i2c_task(void *arg);
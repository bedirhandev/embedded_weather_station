#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "si7021.h"

static const char* JSON_TEMPLATE = "{\"type\":\"%s\",\"value\":\"%s\"}";

//static const char* TAG = "si7021";

typedef struct { float humidity, temperature; } data;

/**
* TEST CODE BRIEF
* This project will indicate how to measure the relative humidity and read the temperature from the previous measurement on the si7021 sensor.
* The microcontroller that is been used is the Adafruit Huzzah esp8266.
* A manual for this microcontroller can be found on: https://cdn-learn.adafruit.com/downloads/pdf/adafruit-feather-huzzah-esp8266.pdf last checked on: 16-09-2020
* A datasheet for the sensor could be found on: https://www.silabs.com/documents/public/data-sheets/Si7021-A20.pdf last checked on: 16-09-2020
* 
* Pin assignment:
* 
* - master:
*	GPIO4 is assigned as the data signal of i2c master port.
*	GPIO5 is assigned as the clock signal of i2c master port.
* 
* Connection:
* - connect sda/scl of sensor with GPIO4/GPIO5.
* - no need to add external pull-up resistors, driver will enable internal pull-up resistors.
* 
* Test items:
* - measure relative humidity.
* - read temperature from measurement
*/

#define I2C_MASTER_SCL_IO 5 /* <! gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO 4 /* <! gpio number for I2C master data */
#define I2C_MASTER_NUM I2C_NUM_0 /* <! I2C port number for master dev */
#define I2C_MASTER_TX_BUF_DISABLE 0 /* <! I2C master do not need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /* <! I2C master do not need buffer */

#define WRITE_BIT I2C_MASTER_WRITE /* <! I2C master write */
#define READ_BIT I2C_MASTER_READ /* <! I2C master read */
#define ACK_CHECK_ENABLE 0x1 /* <! I2C master will check ack from slave */
#define ACK_CHECK_DISABLE 0x0 /* <! I2C master will not check ack from slave */
#define ACK_VAL 0x0 /* <! I2C ack value */
#define NACK_VAL 0x1 /* <! I2C nack value */
#define LAST_NACK_VAL 0x2 /* <! I2C last_nack value */
 
/**
* @brief Si7021 register address definitions: 
*/
#define SI7021_SENSOR_ADDRESS 0x40

#define TEMP_MEASURE_HOLD 0xE3
#define HUMD_MEASURE_HOLD 0xE5
#define TEMP_MEASURE_NOHOLD 0xF3
#define HUMD_MEASURE_NOHOLD 0xF5
#define TEMP_PREV 0xE0

// Error codes
#define I2C_TIMEOUT 998
#define BAD_CRC 999

/**
* @brief i2c master initialization
*/
static esp_err_t i2c_master_init()
{
	int i2c_master_port = I2C_MASTER_NUM;
	i2c_config_t conf;
	conf.mode = I2C_MODE_MASTER;
	conf.sda_io_num = I2C_MASTER_SDA_IO;
	conf.sda_pullup_en = 1;
	conf.scl_io_num = I2C_MASTER_SCL_IO;
	conf.scl_pullup_en = 1;
	conf.clk_stretch_tick = 300000000;  // 300 ticks, Clock stretch is about 210us, you can make changes according to the actual situation.
	i2c_driver_install(i2c_master_port, conf.mode);
	i2c_param_config(i2c_master_port, &conf);
	
	return ESP_OK;
}

/**
* @brief i2c master measure and reading relative humidity
*/
static esp_err_t i2c_master_measure_relative_humidity(i2c_port_t i2c_num, uint8_t* data)
{
	int ret;
	
	// create the link with i2c
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();	
	// start communication
	i2c_master_start(cmd);
	
	//send bytes to read from sensor
	i2c_master_write_byte(cmd, SI7021_SENSOR_ADDRESS << 1 | WRITE_BIT, ACK_CHECK_ENABLE);
	i2c_master_write_byte(cmd, HUMD_MEASURE_HOLD, ACK_CHECK_ENABLE);
	
	i2c_master_stop(cmd);
	i2c_master_start(cmd);
	
	i2c_master_write_byte(cmd, SI7021_SENSOR_ADDRESS << 1 | READ_BIT, ACK_CHECK_ENABLE);
	// master clock stretch
	vTaskDelay(50 / portTICK_PERIOD_MS);
	i2c_master_read_byte(cmd, &data[0], I2C_MASTER_ACK);
	i2c_master_read_byte(cmd, &data[1], I2C_MASTER_LAST_NACK);
	i2c_master_stop(cmd);
	ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	
	return ret;
}

/*
 * @brief sequence to read temperature value from previous RH measurement 
 */
static esp_err_t i2c_master_read_temperature_from_relative_humidity(i2c_port_t i2c_num, uint8_t* data)
{
	int ret;

	// create the link with i2c
	// start communication
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();	
	
	//send bytes to read from register
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, SI7021_SENSOR_ADDRESS << 1 | WRITE_BIT, ACK_CHECK_ENABLE);
	i2c_master_write_byte(cmd, TEMP_PREV, ACK_CHECK_ENABLE);
	
	i2c_master_stop(cmd);
	i2c_master_start(cmd);
	
	i2c_master_write_byte(cmd, SI7021_SENSOR_ADDRESS << 1 | READ_BIT, ACK_CHECK_ENABLE);
	i2c_master_read_byte(cmd, &data[0], I2C_MASTER_ACK);
	i2c_master_read_byte(cmd, &data[1], I2C_MASTER_LAST_NACK);
	i2c_master_stop(cmd);
	// start sending
	ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	
	return ret;
}

// measure the relative humidity
static float get_relative_humidity()
{
	uint8_t data[2];
	
	i2c_master_measure_relative_humidity(I2C_MASTER_NUM, data);
	
	// convert the results to actual humidity in percentage
	float RH_Code = ((data[0] * 256.0) + data[1]);
	float RH = ((125 * RH_Code) / 65536.0) - 6;
	
	return RH;
}

// read temperature from previous humidity measurement
// it is not a new measurement
static float get_temp_from_prev_hr_measurement()
{
	uint8_t data[2];
	
	i2c_master_read_temperature_from_relative_humidity(I2C_MASTER_NUM, data);
	
	// convert the results to actual temperature in celcius degrees
	float temp_Code  = ((data[0] * 256.0) + data[1]);
	float temperature_celcius = ((175.72 * temp_Code) / 65536.0) - 46.85;
	
	return temperature_celcius;
}

void i2c_task(void *pvParameters)
{
	//UBaseType_t uxHighWaterMark;
	//uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
	//printf("[Start] High Water Mark si7021: %lu", uxHighWaterMark);

	data *si7021 = (data *)malloc(sizeof(data));
	char *message = (char *)malloc(128 * sizeof(char));
	
	i2c_master_init();
	
	while (1)
	{
		// read humidity
		si7021->humidity = get_relative_humidity();
		
		// read temperature
		si7021->temperature = get_temp_from_prev_hr_measurement();
		
		char humidity[32], temperature[32];
		
		int length = 0;
		length += sprintf(message + length, JSON_TEMPLATE, "humidity", gcvt(si7021->humidity, 4, humidity));
		length += sprintf(message + length, ",");
		length += sprintf(message + length, JSON_TEMPLATE, "temperature", gcvt(si7021->temperature, 4, temperature));
		
		xQueueSend((QueueHandle_t *)pvParameters, &message, portMAX_DELAY);

		//uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
		//printf("[End] High Water Mark si7021: %lu", uxHighWaterMark);
		
		// delay for 1 minute
		vTaskDelay((1 * 1000 * 60) / portTICK_RATE_MS);
	}
	
	i2c_driver_delete(I2C_MASTER_NUM);
}
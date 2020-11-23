/*! 
 * \file si7021.c
 * \brief Reads the temperature and humidity of the SI7021 sensor
 * \author Bedirhan Dincer
 *	
 * \par Datasheet:
 * The datasheet for the sensor is available at https://www.silabs.com/documents/public/data-sheets/Si7021-A20.pdf last checked on: 16-09-2020
 * 
 * \par Pin assignment:
 * -# GPIO4 is assigned as a data signal of i2c master port.
 * -# GPIO5 is assigned as a clock signal of i2c master port.
 * 
 * \par Connection:
 * -# Connect sda/scl of sensor with GPIO4/GPIO5.
 * -# No need to add external pull-up resistors, driver will enable internal pull-up resistors.
 * 
 * \par Test cases:
 * -# Measuring relative humidity.
 * -# Reading temperature from previous measurement.
*/
#include "si7021.h"
#include "freertos/task.h"
#include "freertos/queue.h"


/** A reusable JSON template to send a JSON message onto the queue. */
static const char* JSON_TEMPLATE = "{\"type\":\"%s\",\"value\":\"%s\"}";

/** The structure that will hold the data of the made measurements. */
typedef struct { float humidity, temperature; } data;

#define I2C_MASTER_SCL_IO 5 /*!< GPIO5 is the I2C master clock line */
#define I2C_MASTER_SDA_IO 4 /*!< GPIO4 for i2c data line */
#define I2C_MASTER_NUM I2C_NUM_0 /*!< I2C port number for master dev */
#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master do not need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master do not need buffer */

#define WRITE_BIT I2C_MASTER_WRITE /*!< I2C master write bit */
#define READ_BIT I2C_MASTER_READ /*!< I2C master read bit */
#define ACK_CHECK_ENABLE 0x1 /*!< I2C master will check ack from slave */
#define ACK_CHECK_DISABLE 0x0 /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0 /*!< I2C ack value */
#define NACK_VAL 0x1 /*!< I2C nack value */
#define LAST_NACK_VAL 0x2 /*!< I2C last_nack value */
 
/*!
* \brief SI7021 register address definitions.
*/
#define SI7021_SENSOR_ADDRESS 0x40 /*!< Master address */
#define TEMP_MEASURE_HOLD 0xE3 /*!< Measure temperature address */
#define HUMD_MEASURE_HOLD 0xE5 /*!< Measure relative humidity address */
#define TEMP_PREV 0xE0 /*!< Measure from previous measurement address */

/*!
 * \brief I2C error codes.
*/
#define I2C_TIMEOUT 998
#define BAD_CRC 999

/*!
 * \brief I2C master initialization/configuation settings.
 * \return The initialization has been succesfully made.
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
	conf.clk_stretch_tick = 300000000;
	i2c_driver_install(i2c_master_port, conf.mode);
	i2c_param_config(i2c_master_port, &conf);
	
	return ESP_OK;
}

/*!
 * \brief I2C master measure and reading relative humidity.
 * \param i2c_num i2c port number.
 * \param data buffer contains the relative humidity value.
 * \return The operation was succesfull or failure.
*/
static esp_err_t i2c_master_measure_relative_humidity(i2c_port_t i2c_num, uint8_t* data)
{
	int ret;
	
	/* Create the link with I2C link. */
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();	
	/* Setting the start bit in the I2C link. */
	i2c_master_start(cmd);
	
	/* Setting the write byte in the I2C link. */
	i2c_master_write_byte(cmd, SI7021_SENSOR_ADDRESS << 1 | WRITE_BIT, ACK_CHECK_ENABLE);
	i2c_master_write_byte(cmd, HUMD_MEASURE_HOLD, ACK_CHECK_ENABLE);
	
	/* Setting the start and stop bit in the I2C link.
	 *  To switch over to the reading part of the sensor.
	 */
	i2c_master_stop(cmd);
	i2c_master_start(cmd);
	
	/* Setting the write byte for reading in I2C link. */
	i2c_master_write_byte(cmd, SI7021_SENSOR_ADDRESS << 1 | READ_BIT, ACK_CHECK_ENABLE);
	/* Little master clock stretch in I2C link. 
	 *  Makes sure to give it some time to set the measurement ready to perform a read operation. 
	 */
	vTaskDelay(50 / portTICK_PERIOD_MS);
	
	/* Setting the read byte for reading the I2C link.
	 *  Put the readed data into the buffer. 
	 */
	i2c_master_read_byte(cmd, &data[0], I2C_MASTER_ACK);
	i2c_master_read_byte(cmd, &data[1], I2C_MASTER_LAST_NACK);
	
	/* Setting the stop bit to tell the I2C link that the job is done. */
	i2c_master_stop(cmd);
	
	/* Start sending and wait for return value. */
	ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
	
	/* Closing the I2C link. */
	i2c_cmd_link_delete(cmd);
	
	/* Returns whether operation was succesful or failure */
	return ret;
}

/*!
 * \brief Sequence to read temperature value from previous RH measurement
 * \param i2c_num i2c port number.
 * \param data buffer contains the temperature value.
 * \return The operation was succesfull or failure
*/
static esp_err_t i2c_master_read_temperature_from_relative_humidity(i2c_port_t i2c_num, uint8_t* data)
{
	int ret;

	/* Create the link with I2C link. */
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();	
	
	/* Setting the start bit in the I2C link. */
	i2c_master_start(cmd);
	
	/* Setting the write byte in the I2C link. */
	i2c_master_write_byte(cmd, SI7021_SENSOR_ADDRESS << 1 | WRITE_BIT, ACK_CHECK_ENABLE);
	i2c_master_write_byte(cmd, TEMP_PREV, ACK_CHECK_ENABLE);
	
	/* Setting the start and stop bit in the I2C link.
	 *  To switch over to the reading part of the sensor.
	 */
	i2c_master_stop(cmd);
	i2c_master_start(cmd);
	
	/* Setting the write byte for reading in I2C link. */
	i2c_master_write_byte(cmd, SI7021_SENSOR_ADDRESS << 1 | READ_BIT, ACK_CHECK_ENABLE);
	
	/* Setting the read byte for reading the I2C link.
	 *  Put the readed data into the buffer. 
	 */
	i2c_master_read_byte(cmd, &data[0], I2C_MASTER_ACK);
	i2c_master_read_byte(cmd, &data[1], I2C_MASTER_LAST_NACK);
	
	/* Setting the stop bit to tell the I2C link that the job is done. */
	i2c_master_stop(cmd);
	
	/* Start sending and wait for return value. */
	ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
	
	/* Closing the I2C link. */
	i2c_cmd_link_delete(cmd);
	
	return ret;
}

/*!
 * \brief Measures the relative humidity.
 * \return The measured relative humdity in percentage
*/
static float get_relative_humidity()
{
	uint8_t data[2];
	
	i2c_master_measure_relative_humidity(I2C_MASTER_NUM, data);
	
	/* Decrypts the data from the made measurement */
	float RH_Code = ((data[0] * 256.0) + data[1]);
	float RH = ((125 * RH_Code) / 65536.0) - 6;
	
	return RH;
}

/*!
 * \brief Reads temperature from previous humidity measurement.
 * \return The measured temperature in celcius degrees
 * \note This is however not a new measurement. It gets the value from the registers.
*/
static float get_temp_from_prev_hr_measurement()
{
	uint8_t data[2];
	
	i2c_master_read_temperature_from_relative_humidity(I2C_MASTER_NUM, data);
	
	/* Convert the results to actual temperature in celcius degrees. */
	float temp_Code  = ((data[0] * 256.0) + data[1]);
	float temperature_celcius = ((175.72 * temp_Code) / 65536.0) - 46.85;
	
	return temperature_celcius;
}

/*!
 * \brief A FreeRTOS task for measuring temperature and humidity using I2C.
 * \param pvParameters contains a reference to the queue.
 */
void i2c_task(void *pvParameters)
{
	/* si7021 will hold the data from the made measurements. */
	data *si7021 = (data *)malloc(sizeof(data));
	
	/* Message buffer that will hold the measurement and send it to the queue. */
	char *message = (char *)malloc(128 * sizeof(char));
	
	/* Start initializing I2C. */
	i2c_master_init();
	
	while (1)
	{
		/* Get relative humdity. */
		si7021->humidity = get_relative_humidity();
		
		/* Get temperature from the humidity measurement. */
		si7021->temperature = get_temp_from_prev_hr_measurement();
		
		/* These char buffers will hold the conversion value. */
		char humidity[32], temperature[32];
		
		/* Put the measurements in a readable JSON formatted string. */
		int length = 0;
		length += sprintf(message + length, JSON_TEMPLATE, "humidity", gcvt(si7021->humidity, 4, humidity));
		length += sprintf(message + length, ",");
		length += sprintf(message + length, JSON_TEMPLATE, "temperature", gcvt(si7021->temperature, 4, temperature));
	
		/* Put the JSON formatted string onto the queue. */
		xQueueSend((QueueHandle_t *)pvParameters, &message, portMAX_DELAY);
		
		vTaskDelay(1000 * 60);
	}
	
	/* Close the driver. No longer needed. */
	i2c_driver_delete(I2C_MASTER_NUM);
}
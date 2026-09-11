#ifndef CDS_SENSOR_H
#define CDS_SENSOR_H

#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// CdS sensor circuit configuration
#define CDS_PULLDOWN_RESISTOR  10000.0f        // 10kOhm pull-down resistor
#define CDS_VCC_MV             3300.0f         // Supply voltage 3.3V (3300mV)

typedef struct {
    int raw_adc;            // Raw ADC reading (0 - 4095)
    int voltage_mv;         // Calibrated voltage (mV)
    float resistance_ohm;   // Estimated CdS cell resistance (Ohm)
    float lux;              // Estimated illuminance (Lux)
    uint16_t matter_value;  // Matter Illuminance Measurement (10000 * log10(lux) + 1)
} cds_sensor_data_t;

/**
 * @brief Initialize CdS sensor (ADC1_CH1 / GPIO1)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t cds_sensor_init(void);

/**
 * @brief Read and retrieve current values from the CdS sensor
 * @param data Pointer to store the measurement results
 * @return esp_err_t ESP_OK on success
 */
esp_err_t cds_sensor_read(cds_sensor_data_t *data);

/**
 * @brief Calculate Matter MeasuredValue from Lux (10000 * log10(lux) + 1)
 * @param lux Illuminance in Lux
 * @return uint16_t Matter MeasuredValue
 */
uint16_t cds_sensor_lux_to_matter_value(float lux);

#ifdef __cplusplus
}
#endif

#endif // CDS_SENSOR_H

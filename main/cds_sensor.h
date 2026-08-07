#ifndef CDS_SENSOR_H
#define CDS_SENSOR_H

#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// CdS センサ回路設定
#define CDS_PULLDOWN_RESISTOR  10000.0f        // 10kΩ プルダウン抵抗
#define CDS_VCC_MV             3300.0f         // 電源電圧 3.3V (3300mV)

typedef struct {
    int raw_adc;            // ADC生データ (0 - 4095)
    int voltage_mv;         // キャリブレーション済み電圧 (mV)
    float resistance_ohm;   // CdSセルの推定抵抗値 (Ω)
    float lux;              // 推定照度 (Lux)
    uint16_t matter_value;  // Matter Illuminance Measurement (10000 * log10(lux) + 1)
} cds_sensor_data_t;

/**
 * @brief CdSセンサ（ADC1_CH1 / GPIO1）の初期化
 * @return esp_err_t ESP_OK成功時
 */
esp_err_t cds_sensor_init(void);

/**
 * @brief CdSセンサの現在値を計測して取得
 * @param data 計測結果格納先
 * @return esp_err_t ESP_OK成功時
 */
esp_err_t cds_sensor_read(cds_sensor_data_t *data);

/**
 * @brief Lux値からMatter規格のMeasuredValue(10000*log10(lux)+1)を計算
 * @param lux 照度(Lux)
 * @return uint16_t Matter MeasuredValue
 */
uint16_t cds_sensor_lux_to_matter_value(float lux);

#ifdef __cplusplus
}
#endif

#endif // CDS_SENSOR_H

#include <esp_log.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include "cds_sensor.h"

static const char *TAG = "cds_sensor";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static bool s_cali_enabled = false;

esp_err_t cds_sensor_init(void)
{
    // ADC1 Units init
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = (adc_oneshot_clk_src_t)0,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_config, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ADC1 unit: %d", err);
        return err;
    }

    // Channel config: GPIO1 is ADC_CHANNEL_1 on ESP32-C3
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_1, &chan_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC channel: %d", err);
        return err;
    }

    // Calibration scheme setup
#if CONFIG_IDF_TARGET_ESP32C3
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &s_adc_cali_handle);
    if (err == ESP_OK) {
        s_cali_enabled = true;
        ESP_LOGI(TAG, "ADC Calibration curve fitting initialized successfully");
    } else {
        ESP_LOGW(TAG, "ADC Calibration curve fitting failed (%d), using linear fallback", err);
    }
#endif

    ESP_LOGI(TAG, "CdS Sensor initialized on GPIO1 (ADC1_CH1)");
    return ESP_OK;
}

uint16_t cds_sensor_lux_to_matter_value(float lux)
{
    if (lux < 1.0f) {
        return 1;
    }
    float val = 10000.0f * log10f(lux) + 1.0f;
    if (val > 65534.0f) {
        return 65534;
    }
    return (uint16_t)val;
}

esp_err_t cds_sensor_read(cds_sensor_data_t *data)
{
    if (!s_adc_handle || !data) {
        return ESP_ERR_INVALID_STATE;
    }

    // Multi-sample averaging for noise reduction (10 samples with 1ms delay to mitigate power flicker)
    int samples = 10;
    int sum_raw = 0;
    for (int i = 0; i < samples; i++) {
        int r = 0;
        adc_oneshot_read(s_adc_handle, ADC_CHANNEL_1, &r);
        sum_raw += r;
        if (i < samples - 1) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    int raw = sum_raw / samples;

    int voltage_mv = 0;
    if (s_cali_enabled && s_adc_cali_handle) {
        adc_cali_raw_to_voltage(s_adc_cali_handle, raw, &voltage_mv);
    } else {
        voltage_mv = (raw * 3300) / 4095;
    }

    float v_mv = (float)voltage_mv;
    if (v_mv <= 1.0f) {
        v_mv = 1.0f;
    }
    if (v_mv >= CDS_VCC_MV) {
        v_mv = CDS_VCC_MV - 1.0f;
    }

    // Voltage divider calculation: Vout = Vcc * R_pulldown / (R_cds + R_pulldown)
    // => R_cds = R_pulldown * ((Vcc / Vout) - 1.0)
    float r_cds = CDS_PULLDOWN_RESISTOR * ((CDS_VCC_MV / v_mv) - 1.0f);
    if (r_cds < 1.0f) {
        r_cds = 1.0f;
    }

    // Calibration based on empirical measurements:
    // - Under room lamp (RAW ≈ 2600, Vout ≈ 1910mV, R_cds ≈ 7250 Ohm): ~300 Lux
    // - Dark room (RAW ≈ 660, Vout ≈ 480mV, R_cds ≈ 58700 Ohm): ~1.0 Lux
    float lux = 300.0f * powf(7250.0f / r_cds, 2.727f);
    if (lux < 0.1f) {
        lux = 0.1f;
    }
    if (lux > 100000.0f) {
        lux = 100000.0f;
    }

    // Round to 1 decimal place
    lux = roundf(lux * 10.0f) / 10.0f;

    data->raw_adc = raw;
    data->voltage_mv = voltage_mv;
    data->resistance_ohm = r_cds;
    data->lux = lux;
    data->matter_value = cds_sensor_lux_to_matter_value(lux);

    return ESP_OK;
}

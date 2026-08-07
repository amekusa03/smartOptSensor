#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_matter.h>
#include <app/server/Server.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include "cds_sensor.h"
#include "wifi_creds.h"

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static uint16_t s_endpoint_id = 1;
static volatile bool s_system_ready = false;

#define GPIO_FACTORY_RESET          GPIO_NUM_9
#define FACTORY_RESET_HOLD_MS       3000
#define MATTER_REPORT_DEADBAND      50     // Matter Value 変化閾値 (デッドバンド)
#define MATTER_REPORT_HEARTBEAT_MS  60000  // 最大更新間隔 (ハートビート 60秒)

// 1000ms 周期で CdS センサを計測し、シリアル出力＆最適化されたMatter属性更新を行うタスク
static void cds_sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "CdS Sensor sampling task started (1000ms cycle)");

    cds_sensor_data_t data;
    uint16_t last_reported_val = 0;
    bool has_reported = false;
    int64_t last_report_time_ms = 0;

    while (true) {
        if (cds_sensor_read(&data) == ESP_OK) {
            // 回路確認用の 1000ms 周期シリアルログ出力
            ESP_LOGI(TAG, "[CdS Sensor] RAW: %4d | Volt: %4d mV | Res: %7.1f Ohm | Lux: %7.1f | MatterVal: %u",
                     data.raw_adc,
                     data.voltage_mv,
                     data.resistance_ohm,
                     data.lux,
                     data.matter_value);

            // Matter が開始されていれば条件付きで IlluminanceMeasurement 属性を更新
            if (s_system_ready) {
                int64_t now_ms = esp_timer_get_time() / 1000;
                int diff = abs((int)data.matter_value - (int)last_reported_val);

                bool should_report = !has_reported || 
                                     (diff >= MATTER_REPORT_DEADBAND) || 
                                     (now_ms - last_report_time_ms >= MATTER_REPORT_HEARTBEAT_MS);

                if (should_report) {
                    esp_matter_attr_val_t val = esp_matter_nullable_uint16(data.matter_value);
                    esp_matter::attribute::update(s_endpoint_id,
                                                  IlluminanceMeasurement::Id,
                                                  IlluminanceMeasurement::Attributes::MeasuredValue::Id,
                                                  &val);
                    last_reported_val = data.matter_value;
                    last_report_time_ms = now_ms;
                    has_reported = true;

                    ESP_LOGD(TAG, "Matter Illuminance updated: %u (diff: %d)", data.matter_value, diff);
                }
            }
        } else {
            ESP_LOGE(TAG, "Failed to read CdS sensor");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void factory_reset_task(void *pvParameters)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_FACTORY_RESET),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    while (true) {
        if (gpio_get_level(GPIO_FACTORY_RESET) == 0) {
            int held = 0;
            while (gpio_get_level(GPIO_FACTORY_RESET) == 0 && held < FACTORY_RESET_HOLD_MS) {
                vTaskDelay(pdMS_TO_TICKS(100));
                held += 100;
            }
            if (held >= FACTORY_RESET_HOLD_MS) {
                ESP_LOGI(TAG, "Factory reset triggered!");
                esp_matter::factory_reset();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged: {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t info;
            if (esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0) {
                char ip[20];
                snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
                ESP_LOGI(TAG, "Wi-Fi Connected. IP: %s", ip);
                s_system_ready = true;
            }
        }
        break;
    }
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed");
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id,
                                        uint8_t effect_id, uint8_t effect_variant, void *priv_data)
{
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id,
                                          uint32_t cluster_id, uint32_t attribute_id,
                                          esp_matter_attr_val_t *val, void *priv_data)
{
    return ESP_OK;
}

static void store_wifi_credentials(void)
{
    nvs_handle_t nvs;
    if (nvs_open_from_partition("nvs", "chip-config", NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open chip-config namespace");
        return;
    }

    char existing_ssid[64] = {};
    char existing_pass[64] = {};
    size_t ssid_len = sizeof(existing_ssid);
    size_t pass_len = sizeof(existing_pass);
    bool need_write = !(nvs_get_blob(nvs, "wifi-ssid", existing_ssid, &ssid_len) == ESP_OK
                        && ssid_len == strlen(WIFI_SSID)
                        && memcmp(existing_ssid, WIFI_SSID, ssid_len) == 0
                        && nvs_get_blob(nvs, "wifi-pass", existing_pass, &pass_len) == ESP_OK
                        && pass_len == strlen(WIFI_PASSWORD)
                        && memcmp(existing_pass, WIFI_PASSWORD, pass_len) == 0);

    if (need_write) {
        nvs_set_blob(nvs, "wifi-ssid", WIFI_SSID, strlen(WIFI_SSID));
        nvs_set_blob(nvs, "wifi-pass", WIFI_PASSWORD, strlen(WIFI_PASSWORD));
        nvs_commit(nvs);
        ESP_LOGI(TAG, "WiFi credentials written to chip-config: %s", WIFI_SSID);
    } else {
        ESP_LOGI(TAG, "WiFi credentials unchanged: %s", WIFI_SSID);
    }
    nvs_close(nvs);
}

static void ensure_unique_id(void)
{
    nvs_handle_t nvs;
    if (nvs_open_from_partition("nvs", "chip-config", NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "ensure_unique_id: failed to open NVS");
        return;
    }
    char buf[48] = {};
    size_t len = sizeof(buf);
    bool missing = (nvs_get_str(nvs, "unique-id", buf, &len) != ESP_OK || strlen(buf) < 2);
    if (missing) {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(buf, sizeof(buf),
                 "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                 (uint8_t)(~mac[0]), (uint8_t)(~mac[1]),
                 (uint8_t)(~mac[2]), (uint8_t)(~mac[3]),
                 (uint8_t)(~mac[4]), (uint8_t)(~mac[5]),
                 mac[0] ^ 0x55u, mac[1] ^ 0xAAu,
                 mac[2] ^ 0x33u, mac[3] ^ 0xCCu);
        nvs_set_str(nvs, "unique-id", buf);
        nvs_commit(nvs);
        ESP_LOGI(TAG, "Generated UniqueID: %s", buf);
    } else {
        ESP_LOGI(TAG, "UniqueID exists: %s", buf);
    }
    nvs_close(nvs);
}

static void connect_wifi_station(void)
{
    wifi_config_t wifi_config = {};
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
    ESP_LOGI(TAG, "Initiated Wi-Fi connection to %s", WIFI_SSID);
}

extern "C" void app_main()
{
    nvs_flash_init();
    ensure_unique_id();
    store_wifi_credentials();

    // CdS センサ (ADC1_CH1 / GPIO1) の初期化
    if (cds_sensor_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize CdS sensor");
    }

    // CdS センサ周期計測タスクの作成 (優先度5, スタック 4096 バイト, 500ms 周期)
    xTaskCreate(cds_sensor_task, "cds_sensor_task", 4096, NULL, 5, NULL);

    // Matter ノードと Light Sensor エンドポイントの作成
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return;
    }

    light_sensor::config_t sensor_config;
    sensor_config.illuminance_measurement.measured_value = 1;
    sensor_config.illuminance_measurement.min_measured_value = 1;
    sensor_config.illuminance_measurement.max_measured_value = 65534;

    endpoint_t *endpoint = light_sensor::create(node, &sensor_config, ENDPOINT_FLAG_NONE, NULL);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to create Light Sensor endpoint");
        return;
    }

    s_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Matter Light Sensor Endpoint ID: %d", s_endpoint_id);

    esp_err_t err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter: %d", err);
        return;
    }
    s_system_ready = true;

    connect_wifi_station();

    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kOnNetwork));

    // ファクトリーリセット用タスクの作成
    xTaskCreate(factory_reset_task, "factory_reset", 4096, nullptr, 1, nullptr);
}

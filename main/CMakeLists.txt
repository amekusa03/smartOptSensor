idf_component_register(
    SRCS "app_main.cpp" "cds_sensor.c"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_matter nvs_flash esp_driver_gpio freertos esp_netif esp_wifi esp_driver_usb_serial_jtag esp_adc
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")

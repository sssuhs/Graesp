idf_component_register(
    SRCS
        "app_main.c"
        "src/alarm_controller.c"
        "src/command_udp.c"
        "src/current_estimator.c"
        "src/device_identity.c"
        "src/feature_extractor.c"
        "src/model_predictor.c"
        "src/mqtt_bridge.c"
        "${IDF_PATH}/components/lwip/lwip/src/apps/mqtt/mqtt.c"
        "src/ntc_sampler.c"
        "src/overload_detector.c"
        "src/runtime_stats.c"
        "src/telemetry_json.c"
        "src/telemetry_udp.c"
        "src/wifi_station.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_adc
        esp_driver_gpio
        esp_event
        esp_http_server
        esp_netif
        esp_rom
        esp_timer
        esp_wifi
        lwip
        nvs_flash
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE MQTT_OUTPUT_RINGBUF_SIZE=4096 MQTT_VAR_HEADER_BUFFER_LEN=512)




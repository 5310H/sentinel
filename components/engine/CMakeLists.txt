idf_component_register(
    SRCS "payload_manager.c" "ha_mqtt.c" "engine.c" "user_mgr.c" "system_monitoring.c" "otp.c" "net.c" "dispatcher.c" "logging.c" "monitor.c" "smtp.c" "telegram.c" "noonlight.c"
    INCLUDE_DIRS "."
    REQUIRES storage hardware mongoose w5500 security camera mqtt_app
    PRIV_REQUIRES esp_driver_gpio esp_timer nvs_flash esp_driver_i2c esp_eth esp_netif esp_event driver esp-tls esp_http_client
)
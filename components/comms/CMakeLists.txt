idf_component_register(
    SRCS
    INCLUDE_DIRS "."
    REQUIRES 
        engine            # <--- Added to fix user_mgr.h error
        storage           # Needed for config/users
        esp_eth           # For KC868 Ethernet
        w5500             # Ethernet Controller
        esp_timer 
        esp-tls 
        esp_hw_support 
        driver 
        mongoose 
        mqtt_app 
        esp_driver_gpio 
        esp_http_client 
        esp_netif 
        mbedtls
)
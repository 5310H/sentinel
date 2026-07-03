idf_component_register(SRCS "network_watchdog.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_timer esp_netif lwip storage)

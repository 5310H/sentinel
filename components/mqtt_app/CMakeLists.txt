idf_component_register(
    SRCS "mqtt_logic.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES storage mongoose cjson engine  # Added engine for ha_mqtt.h
)
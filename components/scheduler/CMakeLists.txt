idf_component_register(
    SRCS "scheduler.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer
    PRIV_REQUIRES mongoose
)

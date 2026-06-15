# JSON Component Wrapper
# ESP-Matter expects a 'json' component but ESP-IDF uses 'cjson'
# This wrapper provides compatibility

idf_component_register(
    INCLUDE_DIRS "."
    REQUIRES cjson
)

# Alias this component as 'json' for Matter SDK compatibility

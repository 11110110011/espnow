idf_component_register(
    SRCS "local_io.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store mqtt_bridge driver esp_timer
)

idf_component_register(
    SRCS "mqtt_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store mqtt esp_event
)

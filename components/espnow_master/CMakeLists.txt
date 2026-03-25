idf_component_register(
    SRCS "espnow_master.c"
    INCLUDE_DIRS "include"
    REQUIRES node_table mqtt_bridge config_store esp_wifi esp_timer
)

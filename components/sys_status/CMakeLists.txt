idf_component_register(
    SRCS "sys_status.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store mqtt_bridge node_table eth_mgr esp_timer esp_system
)

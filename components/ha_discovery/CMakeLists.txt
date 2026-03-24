idf_component_register(
    SRCS "ha_discovery.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store node_table mqtt_bridge
)

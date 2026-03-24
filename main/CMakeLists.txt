idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        config_store
        eth_mgr
        mqtt_bridge
        espnow_master
        node_table
        local_io
        ha_discovery
        web_cfg
        sys_status
        nvs_flash
        esp_event
)

idf_component_register(
    SRCS "web_cfg.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store node_table local_io espnow_master esp_http_server freertos
)

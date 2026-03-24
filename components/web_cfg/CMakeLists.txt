idf_component_register(
    SRCS "web_cfg.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store node_table local_io sys_status esp_http_server
)

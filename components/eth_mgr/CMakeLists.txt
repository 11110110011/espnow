idf_component_register(
    SRCS "eth_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store esp_eth esp_netif esp_event driver lwip
)

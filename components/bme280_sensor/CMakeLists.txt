idf_component_register(
    SRCS "bme280_sensor.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store mqtt_bridge driver freertos
)

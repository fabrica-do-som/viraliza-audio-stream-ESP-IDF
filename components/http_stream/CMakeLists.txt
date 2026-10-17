idf_component_register(
    SRCS "http_stream.c"
    INCLUDE_DIRS "include"
    REQUIRES "freertos" "esp_ringbuf" "esp_http_client" "esp-tls" "mbedtls" "esp_netif"
)

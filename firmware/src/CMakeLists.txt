idf_component_register(SRCS "main.cpp" "web.cpp" "ota.cpp" "live.cpp" "http_connections.cpp" "schema.cpp" "mqtt_bridge.cpp" "history.cpp" INCLUDE_DIRS "."
  REQUIRES esp_driver_uart esp_driver_gpio esp_ringbuf esp_wifi esp_event esp_netif
           mqtt mbedtls lwip nvs_flash esp_timer esp_hw_support esp_http_server json app_update esp_partition spi_flash)

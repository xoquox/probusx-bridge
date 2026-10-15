idf_component_register(SRCS "main.cpp" "web.cpp" "ota.cpp" "live.cpp" INCLUDE_DIRS "."
  REQUIRES esp_driver_uart esp_driver_gpio esp_ringbuf esp_wifi esp_event esp_netif
           nvs_flash esp_timer esp_hw_support esp_http_server json app_update)

#pragma once
#include "cJSON.h"
#include "esp_http_server.h"
void mqtt_bridge_start(httpd_handle_t server, const char *csrf);
void mqtt_bridge_status(cJSON *root);

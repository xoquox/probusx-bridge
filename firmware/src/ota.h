#pragma once
#include "esp_http_server.h"
#include "cJSON.h"
void ota_boot_start();
void ota_boot_ready();
void ota_register(httpd_handle_t server, const char *token);
void ota_add_status(cJSON *root);
// Während der Startprüfung oder eines Updates keinen manuellen Neustart zulassen.
bool ota_restart_allowed();

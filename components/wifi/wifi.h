/* 
 wifi.h - Wi-Fi setup routines

 This file is part of the ESP32 Everest Run project
 https://github.com/krzychb/esp32-everest-run

 Copyright (c) 2016 Krzysztof Budzynski <krzychb@gazeta.pl>
 This work is licensed under the Apache License, Version 2.0, January 2004
 See the file LICENSE for details.
*/
#ifndef WIFI_H
#define WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/event_groups.h"

extern EventGroupHandle_t wifi_event_group;
extern const int CONNECTED_BIT;

esp_err_t wifi_initialize(void);
bool network_is_alive(void);
bool network_init_done(void);

#ifdef __cplusplus
}
#endif

#endif

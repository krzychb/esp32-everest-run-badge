/*
 badge_bmp180.h - BMP180 pressure sensor I2C driver for ESP32

 This file is part of the ESP32 Everest Run project
 https://github.com/krzychb/esp32-everest-run

 Copyright (c) 2016 Krzysztof Budzynski <krzychb@gazeta.pl>
 This work is licensed under the Apache License, Version 2.0, January 2004
 See the file LICENSE for details.
*/

#ifndef BADGE_BMP180_H
#define BADGE_BMP180_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_ERR_BMP180_BASE                  0x30000
#define ESP_ERR_TOO_SLOW_TICK_RATE           (ESP_ERR_BMP180_BASE + 1)
#define ESP_ERR_BMP180_NOT_DETECTED          (ESP_ERR_BMP180_BASE + 2)
#define ESP_ERR_BMP180_CALIBRATION_FAILURE   (ESP_ERR_BMP180_BASE + 3)

esp_err_t badge_bmp180_init();
esp_err_t badge_bmp180_read_temperature(float* temperature);
esp_err_t badge_bmp180_read_pressure(unsigned long* pressure);
esp_err_t badge_bmp180_read_altitude(unsigned long reference_pressure, float* altitude);

#ifdef __cplusplus
}
#endif

#endif  // BADGE_BMP180_H

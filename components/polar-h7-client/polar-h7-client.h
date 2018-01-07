/*
 polar-h7-client.h - retrieve heart rate from Polar H7 Bluetooth sensor

 This file is part of the ESP32 Everest Run project
 https://github.com/krzychb/esp32-everest-run-badge

 Copyright (c) 2018 Krzysztof Budzynski <krzychb@gazeta.pl>
 This work is licensed under the Apache License, Version 2.0, January 2004
 See the file LICENSE for details.
*/

#ifndef POLAR_H7_CLIENT_H
#define POLAR_H7_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*heart_rate_data_callback)(uint32_t *args);

typedef struct {
    unsigned int heart_rate;
    heart_rate_data_callback data_retreived_cb;
} heart_rate_data;

void on_heart_rate_retrieval(heart_rate_data_callback data_retreived_cb);
esp_err_t initialise_heart_rate_retrieval(void);


#ifdef __cplusplus
}
#endif

#endif  // POLAR_H7_CLIENT_H

/*
 altimeter.c - Altitude recording and transmission using ESP32 and BMP180 sensor

 This file is part of the ESP32 Everest Run project
 https://github.com/krzychb/esp32-everest-run

 Copyright (c) 2016 Krzysztof Budzynski <krzychb@gazeta.pl>
 This work is licensed under the Apache License, Version 2.0, January 2004
 See the file LICENSE for details.
*/

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include <time.h>
#include <sys/time.h>
#include "esp_sleep.h"

#include "altimeter.h"
#include "wifi.h"
#include "weather.h"
#include "thingspeak.h"


static const char* TAG = "Main";

RTC_DATA_ATTR static unsigned long boot_count = 0l;

// Periods in seconds
#define DEEP_SLEEP_PERIOD                    2
#define DISPLAY_UPDATE_PERIOD                5
#define ALTITUDE_UPDATE_PERIOD               5
#define THINGSPEAK_UPDATE_PERIOD            15
#define REFERENCE_PRESSURE_UPDATE_PERIOD   120
#define NTP_UPDATE_PERIOD                  000


void app_main()
{
    ESP_LOGI(TAG, "Starting...");

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "Wakeup by timer");
    } else {
        ESP_LOGI(TAG, "First time boot");
        update_reference_pressure();
        //
        // ToDo: Fix initial altitude climbed reading
        // ToDo: Retrieve current time from NTP
        //
    }

    ESP_LOGI(TAG, "Module boot count: %lu", boot_count);
    boot_count++;

    while(1) {
        struct timeval module_time;
        gettimeofday(&module_time, NULL);
        ESP_LOGI(TAG, "Module time %lu s", module_time.tv_sec);

        // Update Time with NTP
        if (module_time.tv_sec > ntp_update.time + NTP_UPDATE_PERIOD) {
            ESP_LOGI(TAG, "Updating time with NTP");
            //
            // ToDo: Perform the action above
            //
            update_to_now(&ntp_update.time);
        }

        // Reference Pressure Update
        if (module_time.tv_sec > reference_pressure_update.time + REFERENCE_PRESSURE_UPDATE_PERIOD) {
            update_reference_pressure();
        }

        // ThingSpeak Update
        if (module_time.tv_sec > thingspeak_update.time + THINGSPEAK_UPDATE_PERIOD) {
            publish_altitude();
        }

        // Altitude Measurement
        if (module_time.tv_sec > altitude_update.time + ALTITUDE_UPDATE_PERIOD) {
            measure_altitude();
        }

        // Update Screen
        if (module_time.tv_sec > display_update.time + DISPLAY_UPDATE_PERIOD) {
            update_display();
        }

        // Handle Touch Pad Events
        //
        // ToDo: Perform the action above
        //
        break;
    }

    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", DEEP_SLEEP_PERIOD);
    esp_deep_sleep(1000000LL * DEEP_SLEEP_PERIOD);
}

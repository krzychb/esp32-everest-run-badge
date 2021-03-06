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

#include "badge_power.h"
#include "badge_leds.h"

static const char* TAG = "Main";

RTC_DATA_ATTR static unsigned long boot_count = 0l;

// Periods in seconds
#define DEEP_SLEEP_PERIOD                    5
#define DISPLAY_UPDATE_PERIOD                5
#define ALTITUDE_UPDATE_PERIOD               5
#define BATTERY_VOLTAGE_UPDATE_PERIOD        5
#define HEART_RATE_UPDATE_PERIOD            15
#define THINGSPEAK_UPDATE_PERIOD            15
#define REFERENCE_PRESSURE_UPDATE_PERIOD   120


void app_main()
{
    ESP_LOGI(TAG, "Starting...");

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "Wakeup by timer");
    } else {
        ESP_LOGI(TAG, "First time boot");
        show_welcome_screen();
        update_reference_pressure();
        initialize_altitude_measurement();
        measure_battery_voltage();
    }

    ESP_LOGI(TAG, "Module boot count: %lu", boot_count++);
    //
    // ToDo: Move leds task up to see first boot status
    //
    esp_err_t err = badge_leds_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize leds task, error: %d", err);
    }
    xTaskCreate(&leds_task, "leds_task", 4 * 1024, NULL, 5, NULL);

    while(1) {
        struct timeval module_time;
        gettimeofday(&module_time, NULL);
        ESP_LOGI(TAG, "Module time %lu s", module_time.tv_sec);

        // Altitude Measurement
        if (module_time.tv_sec > altitude_update.time + ALTITUDE_UPDATE_PERIOD) {
            measure_altitude();
        }

        // Measure battery voltage before Wi-Fi or BLE is on
        if (module_time.tv_sec > battery_voltage_update.time + BATTERY_VOLTAGE_UPDATE_PERIOD) {
            measure_battery_voltage();
        }

        // ThingSpeak Update
        if (module_time.tv_sec > thingspeak_update.time + THINGSPEAK_UPDATE_PERIOD) {
            publish_measurements();
        }

        // Heart Rate Update
        if (module_time.tv_sec > heart_rate_update.time + HEART_RATE_UPDATE_PERIOD) {
            update_heart_rate();
        }

        // Reference Pressure Update
        if (module_time.tv_sec > reference_pressure_update.time + REFERENCE_PRESSURE_UPDATE_PERIOD) {
            update_reference_pressure();
        }

        // Update Screen
        if (module_time.tv_sec > display_update.time + DISPLAY_UPDATE_PERIOD) {
            update_display(-1);
        }

        // Handle Touch Pad Events
        //
        // ToDo: Perform the action above
        //
        break;
    }

    //
    // ToDo: Introduce wakeup from touch
    //
    badge_power_leds_disable();
    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", DEEP_SLEEP_PERIOD);
    esp_deep_sleep(1000000LL * DEEP_SLEEP_PERIOD);
}

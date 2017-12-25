/*
 altimeter.h - Measure altitude with ESP32

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

#include "badge.h"
#include "badge_pins.h"
#include "badge_power.h"
#include "badge_input.h"
#include "badge_mpr121.h"
#include "badge_bmp180.h"

#include "altimeter.h"
#include "wifi.h"
#include "weather.h"
#include "thingspeak.h"

#include "epaper_fonts.h"
#include "epaper-29-dke.h"
#include "imagedata.h"
#include <string.h>

static const char* TAG = "Altimeter";

// Altitude measurement data to retain during deep sleep
RTC_DATA_ATTR update_status display_update = {0};
RTC_DATA_ATTR update_status altitude_update = {0};
RTC_DATA_ATTR update_status thingspeak_update = {0};
RTC_DATA_ATTR update_status reference_pressure_update = {0};
RTC_DATA_ATTR update_status ntp_update = {0};

// Discriminate of altitude changes to calculate cumulative altitude climbed
#define ALTITUDE_DISRIMINATION 1.6

RTC_DATA_ATTR static unsigned long reference_pressure = 0l;
RTC_DATA_ATTR static float altitude_climbed = 0.0;
RTC_DATA_ATTR static float altitude_last; // last measurement for cumulative calculations
RTC_DATA_ATTR altitude_data altitude_record = {0};

epaper_handle_t display_device = NULL;

epaper_conf_t epaper_conf = {
    .busy_pin = PIN_NUM_EPD_BUSY,
    .cs_pin = PIN_NUM_EPD_CS,
    .dc_pin = PIN_NUM_EPD_DATA,
    .miso_pin = -1, // pin not used
    .mosi_pin = PIN_NUM_EPD_MOSI,
    .reset_pin = PIN_NUM_EPD_RESET,
    .sck_pin = PIN_NUM_EPD_CLK,

    .rst_active_level = 0,
    .busy_active_level = 1,

    .dc_lev_data = 1,
    .dc_lev_cmd = 0,

    .clk_freq_hz = 20 * 1000 * 1000,
    .spi_host = HSPI_HOST,

    .width = EPD_WIDTH,
    .height = EPD_HEIGHT,
    .color_inv = 1,
};

void update_to_now(unsigned long* time)
{
    struct timeval module_time;
    gettimeofday(&module_time, NULL);
    *time = module_time.tv_sec;
}

static void weather_data_retreived(uint32_t *args)
{
    weather_data* weather = (weather_data*) args;

    reference_pressure = (unsigned long) (weather->pressure * 100);
    update_to_now(&reference_pressure_update.time);
    ESP_LOGI(TAG, "Reference pressure: %lu Pa", reference_pressure);
}

void update_reference_pressure(){

    unsigned long last_update = reference_pressure_update.time;

    ESP_LOGI(TAG, "Updating reference pressure");
    wifi_initialize();
    initialise_weather_data_retrieval(60000);
    /* Period above is meant for updates in background
     * In this case module would normally go into deep sleep
     * before the callback fires again
     */
    on_weather_data_retrieval(weather_data_retreived);
    ESP_LOGI(TAG, "Weather data retrieval initialized");

    int count_down = 5;
    ESP_LOGI(TAG, "Waiting for reference pressure update...");
    while (reference_pressure_update.time == last_update) {
        ESP_LOGI(TAG, "Waiting %d", count_down);
        vTaskDelay(1000 / portTICK_RATE_MS);
        if (reference_pressure_update.time != last_update) {
            ESP_LOGI(TAG, "Update received");
            break;
        }
        if (--count_down == 0) {
            if (reference_pressure == 0) {
                reference_pressure = 101325l;
                // thingspeak_update.time is set in callback
                ESP_LOGW(TAG, "Assumed standard pressure at the sea level");
            }
            reference_pressure_update.failures++;
            ESP_LOGW(TAG, "Exit waiting");
            break;
        }
    }
}

void publish_altitude()
{
    esp_err_t err;

    ESP_LOGI(TAG, "Publishing to ThingSpeak");
    wifi_initialize();
    thinkgspeak_initialise();
    if (network_is_alive() == true) {
        err = thinkgspeak_post_data(&altitude_record);
        update_to_now(&thingspeak_update.time);
        thingspeak_update.result = err;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed publishing altitude record to ThingSpeak, err = %d", err);
            thingspeak_update.failures++;
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi connection is missing");
    }
}

void measure_altitude()
{
    esp_err_t err;

    ESP_LOGI(TAG, "Measuring altitude");
    altitude_record.reference_pressure = reference_pressure;

    badge_init();
    err  = badge_power_sdcard_enable();
    err |= badge_bmp180_init();
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Altitude measurement init failed with error = %d", err);
        //
        // ToDo: report an error
        //
        badge_power_sdcard_disable();
        vTaskDelay(3000 / portTICK_RATE_MS);
        return;
    }

    err  = badge_bmp180_read_pressure(&altitude_record.pressure);
    err |= badge_bmp180_read_temperature(&altitude_record.temperature);
    err |= badge_bmp180_read_altitude(reference_pressure, &altitude_record.altitude);
    badge_power_sdcard_disable();

    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Altitude measurement failed with error = %d", err);
        altitude_update.failures++;
        //
        // ToDo: report an error
        //
        vTaskDelay(3000 / portTICK_RATE_MS);
        return;
    }

    update_to_now(&altitude_update.time);

    ESP_LOGI(TAG, "Altitude %0.1f m", altitude_record.altitude);

    float altitude_delta = altitude_record.altitude - altitude_last;
    if (altitude_delta > ALTITUDE_DISRIMINATION) {
        altitude_climbed += altitude_delta;
        altitude_last = altitude_record.altitude;
        ESP_LOGI(TAG, "Altitude climbed  %0.1f m", altitude_climbed);
    }
    altitude_record.altitude_climbed = altitude_climbed;
}

void update_display()
{
    char value_str[10];

    ESP_LOGI(TAG, "Updating display");

    badge_init();
    display_device = iot_epaper_create(NULL, &epaper_conf);
    iot_epaper_set_rotate(display_device, E_PAPER_ROTATE_270);

    iot_epaper_clean_paint(display_device, UNCOLORED);
    iot_epaper_draw_string(display_device, 15,  10, "Pressure", &epaper_font_16, COLORED);
    iot_epaper_draw_string(display_device, 15,  40, "Altitude", &epaper_font_16, COLORED);
    iot_epaper_draw_string(display_device, 15,  70, "Temperature", &epaper_font_16, COLORED);
    iot_epaper_draw_string(display_device, 15, 100, "Up Time", &epaper_font_16, COLORED);

    memset(value_str, 0x00, sizeof(value_str));
    sprintf(value_str, "%7lu Pa", altitude_record.pressure);
    iot_epaper_draw_string(display_device, 160,  10, value_str, &epaper_font_16, COLORED);
    memset(value_str, 0x00, sizeof(value_str));
    sprintf(value_str, "%7.1f m", altitude_record.altitude);
    iot_epaper_draw_string(display_device, 160,  40, value_str, &epaper_font_16, COLORED);

    memset(value_str, 0x00, sizeof(value_str));
    sprintf(value_str, "%7.1f C", altitude_record.temperature);
    iot_epaper_draw_string(display_device, 160,  70, value_str, &epaper_font_16, COLORED);

    memset(value_str, 0x00, sizeof(value_str));
    update_to_now(&altitude_record.up_time);
    int hours   = (int) (altitude_record.up_time / 3660);
    int minutes = (int) ((altitude_record.up_time / 60) % 60);
    int seconds = (int) (altitude_record.up_time % 60);
    sprintf(value_str, "%2d:%02d:%02d", hours, minutes, seconds);
    iot_epaper_draw_string(display_device, 180, 100, value_str, &epaper_font_16, COLORED);

    iot_epaper_draw_horizontal_line(display_device, 10, 33, 275, COLORED);
    iot_epaper_draw_horizontal_line(display_device, 10, 63, 275, COLORED);
    iot_epaper_draw_horizontal_line(display_device, 10, 93, 275, COLORED);
    iot_epaper_draw_vertical_line(display_device, 150,  3, 120, COLORED);
    iot_epaper_draw_rectangle(display_device, 10, 3, 285, 123, COLORED);

    iot_epaper_display_frame(display_device, NULL);
    iot_epaper_sleep(display_device);

    update_to_now(&display_update.time);
}

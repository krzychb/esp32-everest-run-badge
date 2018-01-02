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
#include "badge_leds.h"
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

led line[6] = {0};

// Screen currently selected to be displayed
RTC_DATA_ATTR static int active_screen;

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
RTC_DATA_ATTR static float altitude_descent = 0.0;
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
    .spi_host = VSPI_HOST,

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

void leds_task(void *pvParameter)
{
    esp_err_t err = badge_leds_init();

    led show_line[6] = {0};
    led led_off = {0};

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize leds task, error: %d", err);
        while(1);
    }

    while(1){
        for (int i=0; i<6; i++){
            show_line[i] = line[i];
            badge_leds_send_data((uint8_t*) &show_line, sizeof(show_line));
            vTaskDelay(10 / portTICK_PERIOD_MS);
            show_line[i] = led_off;
            badge_leds_send_data((uint8_t*) &show_line, sizeof(show_line));
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

static void weather_data_retreived(uint32_t *args)
{
    weather_data* weather = (weather_data*) args;

    reference_pressure = (unsigned long) (weather->pressure * 100);
    line[1].green = 0;
    update_to_now(&reference_pressure_update.time);
    ESP_LOGI(TAG, "Reference pressure: %lu Pa", reference_pressure);
}

void update_reference_pressure(){

    unsigned long last_update = reference_pressure_update.time;

    ESP_LOGI(TAG, "Updating reference pressure");
    wifi_initialize();
    line[1].green = 40;
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
            line[1].green = 0;
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
            line[1].green = 0;
            line[1].red = 40;
            break;
        }
    }
}

void publish_altitude()
{
    esp_err_t err;

    ESP_LOGI(TAG, "Publishing to ThingSpeak");
    wifi_initialize();
    line[2].green = 40;
    thinkgspeak_initialise();
    if (network_is_alive() == true) {
        err = thinkgspeak_post_data(&altitude_record);
        update_to_now(&thingspeak_update.time);
        thingspeak_update.result = err;
        if (err != ESP_OK) {
            line[2].red = 40;
            ESP_LOGE(TAG, "Failed publishing altitude record to ThingSpeak, err = %d", err);
            thingspeak_update.failures++;
        } else {
            line[2].green = 0;
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi connection is missing");
    }
}


void measure_altitude()
{
    esp_err_t err;

    ESP_LOGI(TAG, "Measuring altitude");
    line[4].green = 40;
    altitude_record.reference_pressure = reference_pressure;

    err = badge_bmp180_init();
    if(err != ESP_OK) {
        line[4].green = 0;
        line[4].red = 40;
        altitude_update.failures++;
        altitude_update.result = err;
        ESP_LOGE(TAG, "Altitude measurement init failed with error = %d", err);
        badge_power_sdcard_disable();
        return;
    }

    err  = badge_bmp180_read_pressure(&altitude_record.pressure);
    err |= badge_bmp180_read_temperature(&altitude_record.temperature);
    err |= badge_bmp180_read_altitude(reference_pressure, &altitude_record.altitude);
    badge_power_sdcard_disable();

    if(err != ESP_OK) {
        line[4].green = 0;
        line[4].red = 40;
        altitude_update.failures++;
        altitude_update.result = err;
        ESP_LOGE(TAG, "Altitude measurement failed with error = %d", err);
        return;
    }

    update_to_now(&altitude_update.time);

    ESP_LOGI(TAG, "Absolute altitude %0.1f m", altitude_record.altitude);

    float altitude_delta = altitude_record.altitude - altitude_last;
    if (altitude_delta > ALTITUDE_DISRIMINATION) {
        altitude_climbed += altitude_delta;
        ESP_LOGD(TAG, "Altitude climbed  %0.1f m", altitude_climbed);
    }
    if (altitude_delta < -ALTITUDE_DISRIMINATION) {
        altitude_descent += altitude_delta;
        ESP_LOGD(TAG, "Altitude descent  %0.1f m", altitude_descent);
    }
    altitude_last = altitude_record.altitude;
    altitude_record.altitude_climbed = altitude_climbed;
    altitude_record.altitude_descent = altitude_descent;
}

void check_battery_voltage()
{
    badge_power_init();
    bool bat_cs = badge_battery_charge_status();
    int v_bat_raw = badge_battery_volt_sense();
    int v_usb_raw = badge_usb_volt_sense();
    float v_bat = v_bat_raw / 1000.0;
    float v_usb = v_usb_raw / 1000.0;
    altitude_record.battery_voltage = v_bat;

    ESP_LOGI(TAG, "Charging: %s, Vusb %.3f V, Vbat %.3f V",
            bat_cs ? "Yes" : "No", v_usb, v_bat);
}

void update_display()
{
    char value_str[10];

    ESP_LOGI(TAG, "Updating display");

    //
    // ToDo: put battery voltage check in more obvious place
    //
    check_battery_voltage();

    display_device = iot_epaper_create(NULL, &epaper_conf);
    iot_epaper_set_rotate(display_device, E_PAPER_ROTATE_270);
    iot_epaper_clean_paint(display_device, UNCOLORED);

    bool failure_state;  // false == OK

    // Climbing Icon
    failure_state = altitude_update.result;
    if (failure_state) {
        iot_epaper_draw_filled_rectangle(display_device, 34, 0, 58, 17, COLORED);
    } else {
        iot_epaper_draw_rectangle(display_device, 34, 0, 58, 17, COLORED);
    }
    iot_epaper_draw_string(display_device,  35,  2, "/\\", &epaper_font_16, failure_state ? UNCOLORED : COLORED);

    //
    // ToDo: add error tracking
    //
    // Wi-Fi Icon
    failure_state = true;
    if (failure_state) {
        iot_epaper_draw_filled_rectangle(display_device, 82, 0, 122, 12, COLORED);
    } else {
        iot_epaper_draw_rectangle(display_device, 82, 0, 122, 12, COLORED);
    }
    iot_epaper_draw_string(display_device,  85,  1, "Wi-Fi", &epaper_font_12, failure_state ? UNCOLORED : COLORED);

    // Cloud Icon / ThingsSpeak
    failure_state = thingspeak_update.result;
    if (failure_state) {
        iot_epaper_draw_filled_rectangle(display_device, 138, 0, 177, 12, COLORED);
    } else {
        iot_epaper_draw_rectangle(display_device, 138, 0, 177, 12, COLORED);
    }
    iot_epaper_draw_string(display_device, 140,  1, "Cloud", &epaper_font_12, failure_state ? UNCOLORED : COLORED);

    // RefP Icon / OpenWeatherMap
    failure_state = reference_pressure_update.result;
    if (failure_state) {
        iot_epaper_draw_filled_rectangle(display_device, 202, 0, 234, 12, COLORED);
    } else {
        iot_epaper_draw_rectangle(display_device, 202, 0, 234, 12, COLORED);
    }
    iot_epaper_draw_string(display_device, 205,  1, "RefP", &epaper_font_12, failure_state ? UNCOLORED : COLORED);

    //
    // ToDo: add error tracking
    //
    // NTP Icon
    failure_state = true;
    if (failure_state) {
        iot_epaper_draw_filled_rectangle(display_device, 262, 0, 287, 12, COLORED);
    } else {
        iot_epaper_draw_rectangle(display_device, 262, 0, 287, 12, COLORED);
    }
    iot_epaper_draw_string(display_device, 265,  1, "NTP", &epaper_font_12, failure_state ? UNCOLORED : COLORED);

    switch (active_screen) {
        case 0:
            iot_epaper_draw_string(display_device, 10,  25, "Climbed", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 10,  52, "Descent", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 10,  79, "Battery", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 10, 106, "Up Time", &epaper_font_20, COLORED);

            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%7.1f m", altitude_record.altitude_climbed);
            iot_epaper_draw_string(display_device, 155,  25, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%7.1f m", altitude_record.altitude_descent);
            iot_epaper_draw_string(display_device, 155,  52, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%7.2f V", altitude_record.battery_voltage);
            iot_epaper_draw_string(display_device, 155,  79, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            update_to_now(&altitude_record.up_time);
            int hours   = (int) (altitude_record.up_time / 3660);
            int minutes = (int) ((altitude_record.up_time / 60) % 60);
            int seconds = (int) (altitude_record.up_time % 60);
            sprintf(value_str, "%2d:%02d:%02d", hours, minutes, seconds);
            iot_epaper_draw_string(display_device, 170, 106, value_str, &epaper_font_20, COLORED);

            iot_epaper_draw_horizontal_line(display_device, 0,  20, 296, COLORED);
            iot_epaper_draw_horizontal_line(display_device, 0,  47, 296, COLORED);
            iot_epaper_draw_horizontal_line(display_device, 0,  74, 296, COLORED);
            iot_epaper_draw_horizontal_line(display_device, 0, 101, 296, COLORED);
            break;
        case 1:
            iot_epaper_draw_string(display_device, 7,  25, "Pressure", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 7,  52, "Rf Pressure", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 7,  79, "Altitude", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 7, 106, "Temperature", &epaper_font_20, COLORED);

            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%7lu Pa", altitude_record.pressure);
            iot_epaper_draw_string(display_device, 155,  25, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%7lu Pa", altitude_record.reference_pressure);
            iot_epaper_draw_string(display_device, 155,  52, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%7.1f m", altitude_record.altitude);
            iot_epaper_draw_string(display_device, 155,  79, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%7.1f C", altitude_record.temperature);
            iot_epaper_draw_string(display_device, 155, 106, value_str, &epaper_font_20, COLORED);

            iot_epaper_draw_horizontal_line(display_device, 0,  20, 296, COLORED);
            iot_epaper_draw_horizontal_line(display_device, 0,  47, 296, COLORED);
            iot_epaper_draw_horizontal_line(display_device, 0,  74, 296, COLORED);
            iot_epaper_draw_horizontal_line(display_device, 0, 101, 296, COLORED);
            break;
        case 2:
            iot_epaper_draw_string(display_device, 80,  52, "Screen", &epaper_font_24, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%d", active_screen);
            iot_epaper_draw_string(display_device, 190,  52, value_str, &epaper_font_24, COLORED);
            break;
        default:
            iot_epaper_draw_string(display_device, 100,  40, "Screen", &epaper_font_24, COLORED);
            iot_epaper_draw_string(display_device, 20,  64, "Not Implemented", &epaper_font_24, COLORED);
            ESP_LOGW(TAG, "Screen %d is not implemented!", active_screen);
    }
    iot_epaper_display_frame(display_device, NULL);
    iot_epaper_sleep(display_device);

    active_screen++;
    if (active_screen > 1) {
        active_screen = 0;
    }

    update_to_now(&display_update.time);
}

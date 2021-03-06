/*
 altimeter.h - Measure altitude with ESP32

 This file is part of the ESP32 Everest Run project
 https://github.com/krzychb/esp32-everest-run-badge

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
#include "polar-h7-client.h"
#include "wifi.h"
#include "weather.h"
#include "thingspeak.h"

#include "epaper_fonts.h"
#include "epaper-29-dke.h"
#include "imagedata.h"
#include <string.h>

static const char* TAG = "Altimeter";

#define WEATHER_DATA_RETREIVAL_TIMEOUT      5
#define HEART_RATE_RETREIVAL_TIMEOUT        3

led line[6] = {0};

// Screen currently selected to be displayed
RTC_DATA_ATTR static int active_screen;

// Measurement data to retain during deep sleep
RTC_DATA_ATTR update_status battery_voltage_update = {0};
RTC_DATA_ATTR update_status display_update = {0};
RTC_DATA_ATTR update_status altitude_update = {0};
RTC_DATA_ATTR update_status heart_rate_update = {0};
RTC_DATA_ATTR update_status thingspeak_update = {0};
RTC_DATA_ATTR update_status reference_pressure_update = {0};
RTC_DATA_ATTR update_status wifi_connection = {0};

#define ALTITUDE_FOR_MER 136.5
#define FLOORS_FOR_MER 42


// Discriminate of altitude changes to calculate cumulative altitude climbed
#define ALTITUDE_DISRIMINATION 1.6
RTC_DATA_ATTR static float altitude_last; // last measurement for cumulative calculations
RTC_DATA_ATTR altitude_data altitude_record = {0};

// Discriminate of altitude changes for climb top/down counting
#define CLIMB_COUNT_ALTITUDE_DISRIMINATION 68

#define CLIMB_COUNT_STATE_START       0x00
#define CLIMB_COUNT_STATE_GOING_UP    0x10
#define CLIMB_COUNT_STATE_GOING_DOWN  0x20

RTC_DATA_ATTR int climb_count_state = CLIMB_COUNT_STATE_START;
RTC_DATA_ATTR static float altitude_last_for_climb_count; // last measurement for climb count calculation

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
    led show_line[6] = {0};
    led led_off = {0};

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

    altitude_record.reference_pressure = (unsigned long) (weather->pressure * 100);
    line[REF_PRESSURE_RETREIEVAL_LED_INDEX].green = LED_OFF;
    update_to_now(&reference_pressure_update.time);
    ESP_LOGI(TAG, "Reference pressure: %lu Pa", altitude_record.reference_pressure);
}

void update_reference_pressure(void){

    unsigned long last_update = reference_pressure_update.time;

    ESP_LOGI(TAG, "Updating reference pressure");
    wifi_initialize();
    line[REF_PRESSURE_RETREIEVAL_LED_INDEX].green = LED_ON_MED;
    initialise_weather_data_retrieval(60000);
    /* Period above is meant for updates in background
     * In this case module would normally go into deep sleep
     * before the callback fires again
     */
    on_weather_data_retrieval(weather_data_retreived);
    ESP_LOGI(TAG, "Weather data retrieval initialized");

    int count_down = WEATHER_DATA_RETREIVAL_TIMEOUT;
    ESP_LOGI(TAG, "Waiting for reference pressure update...");
    while (reference_pressure_update.time == last_update) {
        ESP_LOGI(TAG, "Waiting %d", count_down);
        vTaskDelay(1000 / portTICK_RATE_MS);
        if (reference_pressure_update.time != last_update) {
            ESP_LOGI(TAG, "Update received");
            reference_pressure_update.result = ESP_OK;
            line[REF_PRESSURE_RETREIEVAL_LED_INDEX].green = LED_OFF;
            break;
        }
        if (--count_down == 0) {
            if (altitude_record.reference_pressure == 0) {
                altitude_record.reference_pressure = 101325l;
                // thingspeak_update.time is set in callback
                ESP_LOGW(TAG, "Assumed standard pressure at the sea level");
            }
            reference_pressure_update.failures++;
            reference_pressure_update.result = ! ESP_OK;
            ESP_LOGW(TAG, "Exit waiting");
            line[REF_PRESSURE_RETREIEVAL_LED_INDEX].green = LED_OFF;
            line[REF_PRESSURE_RETREIEVAL_LED_INDEX].red = LED_ON_MED;
            break;
        }
    }
}

void publish_measurements(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Publishing to ThingSpeak");
    wifi_initialize();
    line[CLOUD_POSTING_LED_INDEX].green = LED_ON_MED;
    thinkgspeak_initialise();
    if (network_is_alive() == true) {
        update_to_now(&altitude_record.up_time);
        err = thinkgspeak_post_data(&altitude_record);
        update_to_now(&thingspeak_update.time);
        thingspeak_update.result = err;
        if (err != ESP_OK) {
            line[CLOUD_POSTING_LED_INDEX].red = LED_ON_MED;
            ESP_LOGE(TAG, "Failed publishing altitude record to ThingSpeak, err = %d", err);
            thingspeak_update.failures++;
        } else {
            line[CLOUD_POSTING_LED_INDEX].green = LED_OFF;
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi connection is missing");
    }
}

static void heart_rate_data_retreived(uint32_t *args)
{
    heart_rate_data* heart_rate_sensor = (heart_rate_data*) args;
    altitude_record.heart_rate = heart_rate_sensor->heart_rate;
    update_to_now(&heart_rate_update.time);
    ESP_LOGI(TAG, "Heart rate: %d BPM", altitude_record.heart_rate);
}

void update_heart_rate(void){

    ESP_LOGI(TAG, "Updating heart rate");

    // Do not turn BT on when the Wi-Fi radio is active
    // BT retrieval may take slower when Wi-FI is active as well
    if (network_init_done() != true) {

        unsigned long last_update = heart_rate_update.time;

        line[HEART_RATE_UPDATE_LED_INDEX].blue = LED_ON_MED;
        on_heart_rate_retrieval(heart_rate_data_retreived);
        esp_err_t ret = initialise_heart_rate_retrieval();
        if (ret){
            ESP_LOGE(TAG, "Failed to initialize HR retrieval, error code = %x", ret);
            line[HEART_RATE_UPDATE_LED_INDEX].blue = LED_OFF;
            line[HEART_RATE_UPDATE_LED_INDEX].red = LED_ON_MED;
        }

        int count_down = HEART_RATE_RETREIVAL_TIMEOUT;
        ESP_LOGI(TAG, "Waiting for heart rate update ...");
        while (1) {
            ESP_LOGI(TAG, "Waiting %d ...", count_down);
            vTaskDelay(1000 / portTICK_RATE_MS);
            if (heart_rate_update.time > last_update) {
                heart_rate_update.result = ESP_OK;
                line[HEART_RATE_UPDATE_LED_INDEX].blue = LED_OFF;
                ESP_LOGI(TAG, "Update received");
                break;
            }
            if (--count_down == 0) {
                heart_rate_update.failures++;
                heart_rate_update.result = ! ESP_OK;
                ESP_LOGW(TAG, "Exit waiting");
                line[HEART_RATE_UPDATE_LED_INDEX].blue = LED_OFF;
                line[HEART_RATE_UPDATE_LED_INDEX].red = LED_ON_MED;
                break;
            }
        }
    }
}

void measure_altitude(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Measuring altitude");
    line[ALTITUDE_MEASUREMENT_LED_INDEX].green = LED_ON_MED;

    err = badge_bmp180_init();

    if(err != ESP_OK) {
        line[ALTITUDE_MEASUREMENT_LED_INDEX].green = LED_OFF;
        line[ALTITUDE_MEASUREMENT_LED_INDEX].red = LED_ON_MED;
        altitude_update.failures++;
        altitude_update.result = err;
        ESP_LOGE(TAG, "Altitude measurement init failed with error = %d", err);
        badge_power_sdcard_disable();
        return;
    }

    unsigned long pressure;
    float temperature;
    float altitude;
    ESP_LOGI(TAG, "Reading BMP180");
    err  = badge_bmp180_read_pressure(&pressure);
    err |= badge_bmp180_read_altitude(altitude_record.reference_pressure, &altitude);
    err |= badge_bmp180_read_temperature(&temperature);
    badge_power_sdcard_disable();

    if(err != ESP_OK) {
        line[ALTITUDE_MEASUREMENT_LED_INDEX].green = LED_OFF;
        line[ALTITUDE_MEASUREMENT_LED_INDEX].red = LED_ON_MED;
        altitude_update.failures++;
        altitude_update.result = err;
        ESP_LOGE(TAG, "Altitude measurement failed with error = %d", err);
        return;
    }

    //
    // To Do: track potential issue with BMP180 measurement corruption
    //
    if (pressure < 92000 || pressure > 107000) {
        line[ALTITUDE_MEASUREMENT_LED_INDEX].green = LED_OFF;
        line[ALTITUDE_MEASUREMENT_LED_INDEX].red = LED_ON_MED;
        altitude_update.failures++;
        altitude_update.result = ! ESP_OK;
        ESP_LOGE(TAG, "Pressure range error! (%lu Pa)", pressure);
        return;
    }

    altitude_record.altitude = altitude;
    altitude_record.pressure = pressure;
    altitude_record.temperature = temperature;
    update_to_now(&altitude_update.time);

    ESP_LOGI(TAG, "Absolute altitude %0.1f m", altitude_record.altitude);

    float altitude_delta = altitude_record.altitude - altitude_last;
    if (altitude_delta > ALTITUDE_DISRIMINATION) {
        altitude_record.altitude_climbed += altitude_delta;
        altitude_last = altitude_record.altitude;
        ESP_LOGD(TAG, "Altitude climbed %0.1f m", altitude_record.altitude_climbed);
    }
    else if (altitude_delta < -ALTITUDE_DISRIMINATION) {
        altitude_record.altitude_descent += altitude_delta;
        altitude_last = altitude_record.altitude;
        ESP_LOGD(TAG, "Altitude descent %0.1f m", altitude_record.altitude_descent);
    } else {
        ESP_LOGD(TAG, "Altitude change is within +/- %0.1f from %0.1f m", ALTITUDE_DISRIMINATION, altitude_last);
    }

    altitude_delta = altitude_record.altitude - altitude_last_for_climb_count;
    if (altitude_delta > CLIMB_COUNT_ALTITUDE_DISRIMINATION) {
        altitude_last_for_climb_count = altitude_record.altitude;
        if (climb_count_state == CLIMB_COUNT_STATE_GOING_DOWN || climb_count_state == CLIMB_COUNT_STATE_START){
            climb_count_state = CLIMB_COUNT_STATE_GOING_UP;
            altitude_record.climb_count_top++;
        }
    }
    if (altitude_delta < -CLIMB_COUNT_ALTITUDE_DISRIMINATION) {
        altitude_last_for_climb_count = altitude_record.altitude;
        if (climb_count_state == CLIMB_COUNT_STATE_GOING_UP || climb_count_state == CLIMB_COUNT_STATE_START){
            climb_count_state = CLIMB_COUNT_STATE_GOING_DOWN;
            altitude_record.climb_count_down++;
        }
    }
    line[ALTITUDE_MEASUREMENT_LED_INDEX].green = LED_OFF;
}

void initialize_altitude_measurement(void)
{
    ESP_LOGI(TAG, "Initializing altitude measurement");
    measure_altitude();
    altitude_record.altitude_climbed = 0.0;
    altitude_record.altitude_descent = 0.0;
    altitude_last = altitude_record.altitude;
    altitude_record.climb_count_top = 0;
    altitude_record.climb_count_down = 0;
    altitude_last_for_climb_count = altitude_record.altitude;
    climb_count_state = CLIMB_COUNT_STATE_START;
}

void measure_battery_voltage(void)
{
    ESP_LOGI(TAG, "Measuring battery voltage");

    badge_power_init();
    altitude_record.battery_charging = badge_battery_charge_status();
    int v_bat_raw = badge_battery_volt_sense();
    int v_usb_raw = badge_usb_volt_sense();
    float v_bat = v_bat_raw / 1000.0;
    float v_usb = v_usb_raw / 1000.0;
    altitude_record.battery_voltage = v_bat;
    update_to_now(&battery_voltage_update.time);

    ESP_LOGI(TAG, "Charging: %s, Vusb %.3f V, Vbat %.3f V",
            altitude_record.battery_charging ? "Yes" : "No", v_usb, v_bat);
}

void init_display()
{
    static bool display_init_done = false;

    if (display_init_done == false) {
        display_device = iot_epaper_create(NULL, &epaper_conf);
        iot_epaper_set_rotate(display_device, E_PAPER_ROTATE_270);
        display_init_done = true;
    }
}

void show_status_line(){

    bool failure_state;  // false == OK

    // Climbing Icon
    failure_state = altitude_update.result;
    if (failure_state) {
        iot_epaper_draw_filled_rectangle(display_device, 34, 0, 58, 17, COLORED);
    } else {
        iot_epaper_draw_rectangle(display_device, 34, 0, 58, 17, COLORED);
    }
    iot_epaper_draw_string(display_device,  35,  2, "/\\", &epaper_font_16, failure_state ? UNCOLORED : COLORED);

    // Wi-Fi Icon
    failure_state = wifi_connection.result;
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
    // HRM Icon
    failure_state = heart_rate_update.result;
    if (failure_state) {
        iot_epaper_draw_filled_rectangle(display_device, 262, 0, 287, 12, COLORED);
    } else {
        iot_epaper_draw_rectangle(display_device, 262, 0, 287, 12, COLORED);
    }
    iot_epaper_draw_string(display_device, 265,  1, "HRM", &epaper_font_12, failure_state ? UNCOLORED : COLORED);

    // Draw separation lines
    iot_epaper_draw_horizontal_line(display_device, 0,  20, 296, COLORED);
    iot_epaper_draw_horizontal_line(display_device, 0,  47, 296, COLORED);
    iot_epaper_draw_horizontal_line(display_device, 0,  74, 296, COLORED);
    iot_epaper_draw_horizontal_line(display_device, 0, 101, 296, COLORED);

}

void update_display(int screen_number_to_show)
{
    char value_str[10];

    if (screen_number_to_show != -1){
        active_screen = screen_number_to_show;
    }
    ESP_LOGI(TAG, "Updating display to show screen %d", active_screen);

    init_display();

    iot_epaper_clean_paint(display_device, UNCOLORED);
    show_status_line();

    switch (active_screen) {
        case 0:
            iot_epaper_draw_string(display_device, 10,  25, "Climbed", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 10,  52, "Heart Rate", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 10,  79, "Battery", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 10, 106, "Up Time", &epaper_font_20, COLORED);

            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%6.0f m", altitude_record.altitude_climbed);
            iot_epaper_draw_string(display_device, 155,  25, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%6d BPM", altitude_record.heart_rate);
            iot_epaper_draw_string(display_device, 155,  52, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%6.2f V", altitude_record.battery_voltage);
            iot_epaper_draw_string(display_device, 155,  79, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            update_to_now(&altitude_record.up_time);
            int hours   = (int) (altitude_record.up_time / 3600);
            int minutes = (int) ((altitude_record.up_time / 60) % 60);
            int seconds = (int) (altitude_record.up_time % 60);
            sprintf(value_str, "%2d:%02d:%02d", hours, minutes, seconds);
            iot_epaper_draw_string(display_device, 127, 106, value_str, &epaper_font_20, COLORED);
            break;
        case 1:
            iot_epaper_draw_string(display_device, 7,  25, "Climb Top", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 7,  52, "Time/Climb", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 7,  79, "Floors Cnt", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 7, 106, "Meters Cnt", &epaper_font_20, COLORED);

            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%9d", altitude_record.climb_count_top);
            iot_epaper_draw_string(display_device, 155,  25, value_str, &epaper_font_20, COLORED);
            if (altitude_record.climb_count_top == 0) {
                strcpy(value_str,  "-");
                iot_epaper_draw_string(display_device, 267,  52, value_str, &epaper_font_20, COLORED);
                iot_epaper_draw_string(display_device, 267,  79, value_str, &epaper_font_20, COLORED);
                iot_epaper_draw_string(display_device, 267, 106, value_str, &epaper_font_20, COLORED);
            } else {
                memset(value_str, 0x00, sizeof(value_str));
                update_to_now(&altitude_record.up_time);
                unsigned long seconds_climb = altitude_record.up_time / altitude_record.climb_count_top;
                int hours   = (int) (seconds_climb / 3600);
                int minutes = (int) ((seconds_climb / 60) % 60);
                int seconds = (int) (seconds_climb % 60);
                sprintf(value_str, "%2d:%02d:%02d", hours, minutes, seconds);
                iot_epaper_draw_string(display_device, 170,  52, value_str, &epaper_font_20, COLORED);
                memset(value_str, 0x00, sizeof(value_str));
                sprintf(value_str, "%9d", altitude_record.climb_count_top * FLOORS_FOR_MER);
                iot_epaper_draw_string(display_device, 155,  79, value_str, &epaper_font_20, COLORED);
                memset(value_str, 0x00, sizeof(value_str));
                sprintf(value_str, "%9d", (int) (altitude_record.climb_count_top * ALTITUDE_FOR_MER));
                iot_epaper_draw_string(display_device, 155, 106, value_str, &epaper_font_20, COLORED);
            }
            break;
        case 2:
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
            break;
        case 3:
            iot_epaper_draw_string(display_device, 7,  25, "BMP180", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 7,  52, "Polar H7", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 7,  79, "ThingSpeak", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 7, 106, "OpenWeather", &epaper_font_20, COLORED);

            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%6lu Err", altitude_update.failures);
            iot_epaper_draw_string(display_device, 155,  25, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%6lu Err", heart_rate_update.failures);
            iot_epaper_draw_string(display_device, 155,  52, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%6lu Err", thingspeak_update.failures);
            iot_epaper_draw_string(display_device, 155,  79, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%6lu Err", reference_pressure_update.failures);
            iot_epaper_draw_string(display_device, 155, 106, value_str, &epaper_font_20, COLORED);
            break;
        case 4:
            iot_epaper_draw_string(display_device, 7,  25, "Wi-Fi Conn", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 7,  52, "Batt Charging", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 7,  79, "Descent", &epaper_font_20, COLORED);
            iot_epaper_draw_string(display_device, 7, 106, "Climb Down", &epaper_font_20, COLORED);

            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%6lu Err", wifi_connection.failures);
            iot_epaper_draw_string(display_device, 155,  25, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%s", altitude_record.battery_charging ? "Yes" : "No");
            iot_epaper_draw_string(display_device, 253,  52, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%6.0f m", altitude_record.altitude_descent);
            iot_epaper_draw_string(display_device, 155,  79, value_str, &epaper_font_20, COLORED);
            memset(value_str, 0x00, sizeof(value_str));
            sprintf(value_str, "%6d", altitude_record.climb_count_down);
            iot_epaper_draw_string(display_device, 155, 106, value_str, &epaper_font_20, COLORED);
            break;
        case 5:
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
    if (active_screen > 4) {
        active_screen = 0;
    }

    update_to_now(&display_update.time);
}

void show_welcome_screen(){

    ESP_LOGI(TAG, "Showing welcome screen");
    init_display();
    iot_epaper_display_frame(display_device, IMAGE_DATA);
    iot_epaper_display_frame(display_device, IMAGE_DATA);
}

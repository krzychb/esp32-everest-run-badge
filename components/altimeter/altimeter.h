/*
 altimeter.h - Measure altitude with ESP32

 This file is part of the ESP32 Everest Run project
 https://github.com/krzychb/esp32-everest-run

 Copyright (c) 2016 Krzysztof Budzynski <krzychb@gazeta.pl>
 This work is licensed under the Apache License, Version 2.0, January 2004
 See the file LICENSE for details.
*/

#ifndef ALTIMETER_H
#define ALTIMETER_H

#include "esp_err.h"
#include <time.h>
#include "esp_sleep.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t green;
    uint8_t red;
    uint8_t blue;
    uint8_t white;
} led;

#define ALTITUDE_MEASUREMENT_LED_INDEX      4
#define WIFI_ACTIVITY_LED_INDEX             3
#define CLOUD_POSTING_LED_INDEX             2
#define REF_PRESSURE_RETREIEVAL_LED_INDEX   1
#define HEART_RATE_UPDATE_LED_INDEX         0

extern led line[];

typedef struct
{
    unsigned long time;
    unsigned long failures;
    esp_err_t result;
} update_status;

extern RTC_DATA_ATTR update_status battery_voltage_update;
extern RTC_DATA_ATTR update_status display_update;
extern RTC_DATA_ATTR update_status altitude_update;
extern RTC_DATA_ATTR update_status heart_rate_update;
extern RTC_DATA_ATTR update_status thingspeak_update;
extern RTC_DATA_ATTR update_status reference_pressure_update;
extern RTC_DATA_ATTR update_status ntp_update;

typedef struct {
    unsigned long pressure;  /*!< Pressure [Pa] measured with BM180 */
    unsigned long reference_pressure;  /*!< Pressure [Pa] measured at the sea level */
    float altitude;  /*!< Altitude [meters] measured with BM180 and compensated to the sea level pressure */
    float altitude_climbed; /*!< Total altitude [meters] measured when climbing up */
    float altitude_descent; /*!< Total altitude [meters] measured when going down */
    int climb_count_top; /*!< Number of times reaching certain height when going up */
    int climb_count_down; /*!< Number of times reaching certain height when going down */
    int heart_rate; /*!< Heart rate obtained from a sensor */
    float temperature;  /*!< Temperature [deg C] measured with BM180 */
    float battery_voltage;  /*!< Battery voltage [V] of badge power supply */
    bool battery_charging;  /*!< Whether battery is being charged */
    bool logged;  /*!< This record has been saved to logger before posting */
    unsigned long up_time;  /*!< Time in seconds since last reboot of ESP32 */
    time_t timestamp;  /*!< Data and time the altitude measurement was taken */
} altitude_data;

void update_to_now(unsigned long* time);
void leds_task(void *pvParameter);
void measure_battery_voltage(void);
void update_reference_pressure(void);
void publish_measurements(void);
void update_heart_rate(void);
void measure_altitude(void);
void initialize_altitude_measurement(void);
void update_display(int screen_number_to_show);

#ifdef __cplusplus
}
#endif

#endif  // ALTIMETER_H

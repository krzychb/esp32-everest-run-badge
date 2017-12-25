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


/* Can run 'make menuconfig' to change defines below,
   or you can edit the following line and set the values.
*/
#define RED_BLINK_GPIO CONFIG_RED_BLINK_GPIO
#define GREEN_BLINK_GPIO CONFIG_GREEN_BLINK_GPIO
#define BLUE_BLINK_GPIO CONFIG_BLUE_BLINK_GPIO


#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    unsigned long time;
    unsigned long failures;
    esp_err_t result;
} update_status;

extern RTC_DATA_ATTR update_status display_update;
extern RTC_DATA_ATTR update_status altitude_update;
extern RTC_DATA_ATTR update_status thingspeak_update;
extern RTC_DATA_ATTR update_status reference_pressure_update;
extern RTC_DATA_ATTR update_status ntp_update;

typedef struct {
    unsigned long pressure;  /*!< Pressure [Pa] measured with BM180 */
    unsigned long reference_pressure;  /*!< Pressure [Pa] measured at the sea level */
    float altitude;  /*!< Altitude [meters] measured with BM180 and compensated to the sea level pressure */
    float altitude_climbed; /*!< Total altitude [meters] measured when climbing up (going down is not counted) */
    float temperature;  /*!< Temperature [deg C] measured with BM180 */
    bool logged;  /*!< This record has been saved to logger before posting */
    unsigned long up_time;  /*!< Time in seconds since last reboot of ESP32 */
    time_t timestamp;  /*!< Data and time the altitude measurement was taken */
} altitude_data;

void update_to_now(unsigned long* time);
void update_reference_pressure();
void publish_altitude();
void measure_altitude();
void update_display();

#ifdef __cplusplus
}
#endif

#endif  // ALTIMETER_H

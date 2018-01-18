/*
 badge_bmp180.c - BMP180 pressure sensor driver for ESP32

 This file is part of the ESP32 Everest Run project
 https://github.com/krzychb/esp32-everest-run

 Copyright (c) 2016 Krzysztof Budzynski <krzychb@gazeta.pl>
 This work is licensed under the Apache License, Version 2.0, January 2004
 See the file LICENSE for details.
 */

#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/i2c.h"
#include "esp_log.h"

#include "badge_base.h"
#include "badge_i2c.h"
#include "badge_mpr121.h"
#include "badge_power.h"
#include "badge_bmp180.h"

static const char* TAG = "BMP180 I2C Driver";

#define BMP180_ADDRESS 0x77     // I2C address of BMP180

#define BMP180_ULTRA_LOW_POWER  0
#define BMP180_STANDARD         1
#define BMP180_HIGH_RES         2
#define BMP180_ULTRA_HIGH_RES   3

#define BMP180_CAL_AC1          0xAA  // Calibration data (16 bits)
#define BMP180_CAL_AC2          0xAC  // Calibration data (16 bits)
#define BMP180_CAL_AC3          0xAE  // Calibration data (16 bits)
#define BMP180_CAL_AC4          0xB0  // Calibration data (16 bits)
#define BMP180_CAL_AC5          0xB2  // Calibration data (16 bits)
#define BMP180_CAL_AC6          0xB4  // Calibration data (16 bits)
#define BMP180_CAL_B1           0xB6  // Calibration data (16 bits)
#define BMP180_CAL_B2           0xB8  // Calibration data (16 bits)
#define BMP180_CAL_MB           0xBA  // Calibration data (16 bits)
#define BMP180_CAL_MC           0xBC  // Calibration data (16 bits)
#define BMP180_CAL_MD           0xBE  // Calibration data (16 bits)

#define BMP180_CONTROL             0xF4  // Control register
#define BMP180_DATA_TO_READ        0xF6  // Read results here
#define BMP180_READ_TEMP_CMD       0x2E  // Request temperature measurement
#define BMP180_READ_PRESSURE_CMD   0x34  // Request pressure measurement

// Calibration parameters
static int16_t ac1;
static int16_t ac2;
static int16_t ac3;
static uint16_t ac4;
static uint16_t ac5;
static uint16_t ac6;
static int16_t b1;
static int16_t b2;
static int16_t mb;
static int16_t mc;
static int16_t md;
static uint8_t oversampling = BMP180_ULTRA_HIGH_RES;


static esp_err_t bmp180_read_int16(uint8_t reg, int16_t* value)
{
    uint8_t data_rd[2] = {0};
    esp_err_t err = badge_i2c_read_reg(BMP180_ADDRESS, reg, data_rd, 2);

    if (err == ESP_OK) {
        *value = (int16_t) ((data_rd[0] << 8) | data_rd[1]);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read [0x%02x] int16 failed, err = %d", reg, err);
    }
    return err;
}


static esp_err_t bmp180_read_uint16(uint8_t reg, uint16_t* value)
{
    uint8_t data_rd[2] = {0};
    esp_err_t err = badge_i2c_read_reg(BMP180_ADDRESS, reg, data_rd, 2);

    if (err == ESP_OK) {
        *value = (uint16_t) ((data_rd[0] << 8) | data_rd[1]);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read [0x%02x] uint16 failed, err = %d", reg, err);
    }
    return err;
}


static esp_err_t bmp180_read_uint32(uint8_t reg, uint32_t* value)
{
    uint8_t data_rd[3] = {0};
    esp_err_t err = badge_i2c_read_reg(BMP180_ADDRESS, reg, data_rd, 3);
    if (err == ESP_OK) {
        *value = (uint32_t) ((data_rd[0] << 16) | (data_rd[1] << 8) | data_rd[2]);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read [0x%02x] uint16 failed, err = %d", reg, err);
    }
    return err;
}


static esp_err_t bmp180_read_uncompensated_temperature(int16_t* temp)
{
    esp_err_t err = badge_i2c_write_reg(BMP180_ADDRESS, BMP180_CONTROL, BMP180_READ_TEMP_CMD);
    if (err == ESP_OK) {
        TickType_t xDelay = 5 / portTICK_PERIOD_MS;
        if (xDelay == 0) {
            xDelay = 1;
        }
        vTaskDelay(xDelay);
        err = bmp180_read_int16(BMP180_DATA_TO_READ, temp);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read uncompensated temperature failed, err = %d", err);
    }
    return err;
}


static esp_err_t bmp180_calculate_b5(int32_t* b5)
{
    int16_t ut;
    int32_t x1, x2;

    esp_err_t err = bmp180_read_uncompensated_temperature(&ut);
    if (err == ESP_OK) {
        x1 = ((ut - (int32_t) ac6) * (int32_t) ac5) >> 15;
        x2 = ((int32_t) mc << 11) / (x1 + md);
        *b5 = x1 + x2;
    } else {
        ESP_LOGE(TAG, "Calculate b5 failed, err = %d", err);
    }
    return err;
}

static uint32_t bmp180_read_uncompensated_pressure(uint32_t* up)
{
    esp_err_t err = badge_i2c_write_reg(BMP180_ADDRESS, BMP180_CONTROL, BMP180_READ_PRESSURE_CMD + (oversampling << 6));
    if (err == ESP_OK) {
        TickType_t xDelay = (2 + (3 << oversampling)) / portTICK_PERIOD_MS;
        if (xDelay == 0) {
            xDelay = 1;
        }
        vTaskDelay(xDelay);
        err = bmp180_read_uint32(BMP180_DATA_TO_READ, up);
        if (err == ESP_OK) {
            *up >>= (8 - oversampling);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read uncompensated pressure failed, err = %d", err);
    }
    return err;
}


esp_err_t badge_bmp180_read_temperature(float* temperature)
{
    int32_t b5;

    esp_err_t err = bmp180_calculate_b5(&b5);
    if (err == ESP_OK) {
        *temperature = ((b5 + 8) >> 4) / 10.0;
    } else {
        ESP_LOGE(TAG, "Read temperature failed, err = %d", err);
    }
    return err;
}


esp_err_t badge_bmp180_read_pressure(unsigned long* pressure)
{
    int32_t b3, b5, b6, x1, x2, x3, p;
    uint32_t up, b4, b7;
    esp_err_t err;

    err = bmp180_calculate_b5(&b5);
    if (err == ESP_OK) {
        b6 = b5 - 4000;
        x1 = (b2 * (b6 * b6) >> 12) >> 11;
        x2 = (ac2 * b6) >> 11;
        x3 = x1 + x2;
        b3 = (((((int32_t)ac1) * 4 + x3) << oversampling) + 2) >> 2;

        x1 = (ac3 * b6) >> 13;
        x2 = (b1 * ((b6 * b6) >> 12)) >> 16;
        x3 = ((x1 + x2) + 2) >> 2;
        b4 = (ac4 * (uint32_t)(x3 + 32768)) >> 15;

        err  = bmp180_read_uncompensated_pressure(&up);
        if (err == ESP_OK) {
            b7 = ((uint32_t)(up - b3) * (50000 >> oversampling));
            if (b7 < 0x80000000) {
                p = (b7 << 1) / b4;
            } else {
                p = (b7 / b4) << 1;
            }

            x1 = (p >> 8) * (p >> 8);
            x1 = (x1 * 3038) >> 16;
            x2 = (-7357 * p) >> 16;
            p += (x1 + x2 + 3791) >> 4;
            *pressure = p;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Pressure compensation failed, err = %d", err);
    }

    return err;
}


esp_err_t badge_bmp180_read_altitude(unsigned long reference_pressure, float* altitude)
{
    unsigned long absolute_pressure;
    esp_err_t err = badge_bmp180_read_pressure(&absolute_pressure);
    if (err == ESP_OK) {
        *altitude =  44330 * (1.0 - powf(absolute_pressure / (float) reference_pressure, 0.190295));
    } else {
        ESP_LOGE(TAG, "Read altitude failed, err = %d", err);
    }
    return err;
}


esp_err_t badge_bmp180_init()
{
    static bool badge_bmp180_init_done = false;

    if (badge_bmp180_init_done)
        return ESP_OK;

    ESP_LOGD(TAG, "BMP180 init called");

    esp_err_t err = badge_mpr121_init();
    if (err != ESP_OK)
        return err;

    err = badge_power_sdcard_enable();
    if (err != ESP_OK)
        return err;

    err = badge_i2c_write_reg(BMP180_ADDRESS, 0x00, 0x00);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BMP180 sensor not found at 0x%02x", BMP180_ADDRESS);
        return ESP_ERR_BMP180_NOT_DETECTED;
    }

    ESP_LOGI(TAG, "BMP180 sensor found at 0x%02x", BMP180_ADDRESS);
    err  = bmp180_read_int16(BMP180_CAL_AC1, &ac1);
    err |= bmp180_read_int16(BMP180_CAL_AC2, &ac2);
    err |= bmp180_read_int16(BMP180_CAL_AC3, &ac3);
    err |= bmp180_read_uint16(BMP180_CAL_AC4, &ac4);
    err |= bmp180_read_uint16(BMP180_CAL_AC5, &ac5);
    err |= bmp180_read_uint16(BMP180_CAL_AC6, &ac6);
    err |= bmp180_read_int16(BMP180_CAL_B1, &b1);
    err |= bmp180_read_int16(BMP180_CAL_B2, &b2);
    err |= bmp180_read_int16(BMP180_CAL_MB, &mb);
    err |= bmp180_read_int16(BMP180_CAL_MC, &mc);
    err |= bmp180_read_int16(BMP180_CAL_MD, &md);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BMP180 sensor calibration read failure, err = %d", err);
        return ESP_ERR_BMP180_CALIBRATION_FAILURE;
    }
    ESP_LOGD(TAG, "AC1: %d, AC2: %d, AC3: %d, AC4: %d, AC5: %d, AC6: %d", ac1, ac2, ac3, ac4, ac5, ac6);
    ESP_LOGD(TAG, "B1: %d, B2: %d, MB: %d, MC: %d, MD: %d", b1, b2, mb, mc, md);

    badge_bmp180_init_done = true;

    return ESP_OK;
}

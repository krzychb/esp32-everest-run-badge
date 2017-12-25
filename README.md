# esp32-everest-run-badge

Internet connected altimeter for Everest Run - http://everestrun.pl/

This is continuation of similar project [esp32-everest-run](https://github.com/krzychb/esp32-everest-run) using new hardware.


## Components

* [SHA2017 Badge](https://twitter.com/sha2017badge), a great piece of hardware developed for SHA2017 hacker camp/conference that took place in The Netherlands in 2017
* [BMP180](https://www.bosch-sensortec.com/bst/products/all_products/bmp180) Barmoetric Pressure Sensor
* [esp-idf](https://github.com/espressif/esp-idf) Espressif IoT Development Framework for ESP32

## Build Status

[![Build Status](https://travis-ci.org/krzychb/esp32-everest-run-badge.svg?branch=master)](https://travis-ci.org/krzychb/esp32-everest-run-badge)

## Altitude Measurements On-line

Test data feed is available under https://thingspeak.com/channels/208884

## Installation Overview

Configure your PC according to [ESP32 Documentation](http://esp-idf.readthedocs.io/en/latest/?badge=latest). [Windows](http://esp-idf.readthedocs.io/en/latest/get-started/windows-setup.html), [Linux](http://esp-idf.readthedocs.io/en/latest/get-started/linux-setup.html) and [Mac OS](http://esp-idf.readthedocs.io/en/latest/get-started/macos-setup.html) are supported.

You can compile and upload code to ESP32 from command line with [make](http://esp-idf.readthedocs.io/en/latest/get-started/make-project.html) or using [Eclipse IDE](http://esp-idf.readthedocs.io/en/latest/get-started/eclipse-setup.html).

If this is you first exposure to ESP32 and [esp-idf](https://github.com/espressif/esp-idf), then get familiar with [hello_world](https://github.com/espressif/esp-idf/tree/master/examples/get-started/hello_world) and [blink](https://github.com/espressif/esp-idf/tree/master/examples/get-started/blink) examples. In next step check more advanced examples that have been specifically used to prepare this application: [http_request](https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_request) and [sntp](https://github.com/espressif/esp-idf/tree/master/examples/protocols/sntp).

Compilation and upload of this application is done in the same way like the above examples. To make testing more convenient you can use [ESP-WROVER-KIT](https://espressif.com/en/products/hardware/esp-wrover-kit/overview) that has micro-sd card slot installed.

## Acknowledgments

This repository has been prepared thanks to great hardware and software developed by the following teams and individuals:

* [SHA2017 Badge Team](https://twitter.com/sha2017badge), love the hardware and the software you have carefully designed and implemented for SHA2017!
* Espressif team that develops and maintains [esp-idf](https://github.com/espressif/esp-idf)  repository. With each update I am each time amazed, how stable this code works and how great functionality it provides.
* esp-iot-solution team that developed clean and easy to follow [epaper](https://github.com/espressif/esp-iot-solution/tree/master/components/spi_devices/epaper/test) component for similar type of ePaper display like on SHA2017 Badge. I reused it almost entirely used in this project.
* Waveshare that provides [great documentation](https://www.waveshare.com/wiki/2.9inch_e-Paper_Module) and [really user friendly demo code](https://www.waveshare.com/wiki/File:2.9inch_e-Paper_Module_code.7z). It was instrumental to help me understand how to control ePaper displays and adapt existing ESP-IDF code to control such displays.
* Serge Zaitsev who gave us a minimalistic JSON parser [jsmn](https://github.com/zserge/jsmn). In this repository jsmn is breaking down JSON strigns provided by api.openweathermap.org.
* [SpritesMods](http://spritesmods.com/), the [SPI Master driver](https://esp-idf.readthedocs.io/en/latest/api-reference/peripherals/spi_master.html) for ESP-IDF is a really nice piece of software:  versatile and fun to work with. And yes, it is driving ePaper displays with ease :) 
* Angus Gratton who's [http_request](https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_request) was the key ingredient of my own version of the http client that talks to varius cloud services.
* Ivan Grokhotkov, who architects and develops great and robust code and supports impossible number of users. This is really inspiring!

## License

[Apache License Version 2.0, January 2004](LICENSE)
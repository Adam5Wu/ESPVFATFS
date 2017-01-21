# ESPVFATFS

FATFS on internal flash for ESP8266 Arduino

## Purpose

ESP8266 is a nice little device, capable of running [nearly full featured Web server](https://github.com/me-no-dev/ESPAsyncWebServer), providing relatively rich user experiences.

Some versions of device has up to 32MB of internal flash storage, which can be used to hold almost all resources it needs to serve, making it a self-contained, ultra-portable Web server.

However, currently Arduino for ESP8266 provides only SPIFFS as backing file system, which lacks several important features:
- Real directories
- Long file names
- File modification time

Inspired by the ESP8266 [MicroPython project](https://github.com/Adam5Wu/ESPAsyncWebServer), this library provides a wrapper to use [FatFs](http://elm-chan.org/fsw/ff/00index_e.html) on the internal flash storage of ESP8266, which resolves the above mentioned limitations.

## How to use

Install the library - clone this repo into the "libraries" folder in your Arduino projects (found in your home / document directory).

(Re)start the Arduino, open up the example from menu [File] - [Examples] - [ESP VFATFS] - [FATFS_Init]

**WARNING**: Running the demo code, or more generally, calling `VFATFS.begin()`, will automatically reformat your designated flash storage area into FAT file system. All your pre-existing data WILL BE LOST! Please backup before proceed.

**WARNING 2**: Conversely, after you have mounted VFATFS, calling `SPIFFS.begin()` will automatically reformat your designated flash storage area into SPIFSS file system. All your pre-existing data WILL BE LOST! Please backup before proceed.

Current implementaiton has been tested on ESP-12 module with 4MB flash. It works with either "3MB SPIFFS" or "1MB SPIFFS", and you will get a FAT file system with corresponding sizes.

## Bugs / Suggestions?

No problem, just let me know by filing issues.

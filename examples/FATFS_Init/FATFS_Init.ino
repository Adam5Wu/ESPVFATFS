// This demo requires a modified ESP8266 Arduino, found here:
// https://github.com/Adam5Wu/Arduino

#define NO_GLOBAL_SPIFFS

#include <sys/time.h>
#include <ESP8266WiFi.h>

#include <FS.h>
#include <vfatfs_api.h>

#include <Units.h>

extern "C" {
  #include "lwip/sntp.h"
}

#define TIMEZONE    -(5*3600)
#define DSTOFFSET   0 // 3600
#define NTPSERVER   "pool.ntp.org"

const char* ssid = "Guest";
const char* password = "Welcome";

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    WiFi.disconnect(false);
    Serial.println("STA: Failed to connect, retrying...");
    delay(5000);
    WiFi.begin(ssid, password);
  }
  Serial.printf("Connected to %s\n", ssid);
  Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("Name Server: %s\n", WiFi.dnsIP().toString().c_str());

  delay(500);
  configTime(TIMEZONE,DSTOFFSET,NTPSERVER);
  delay(1000);
  while (!sntp_get_current_timestamp()) {
    Serial.println("Waiting for NTP time...");
    delay(2000);
  }

  {
    timeval curtime;
    gettimeofday(&curtime,NULL);
    Serial.printf("Current Time: %s", sntp_get_real_time(curtime.tv_sec));
  }

  while (!VFATFS.begin()) panic();

  FSInfo info;
  VFATFS.info(info);
  Serial.printf("FATFS: %s total, %s used (%.1f%%), block size %d\n",
    ToString(info.totalBytes, SizeUnit::BYTE, true).c_str(),
    ToString(info.usedBytes, SizeUnit::BYTE, true).c_str(),
    info.usedBytes*100.0/info.totalBytes,
    (unsigned int)info.blockSize);
}

void loop() {
  delay(5000);
  Serial.println("For more examples, please checkout:");
  Serial.println(" - https://github.com/Adam5Wu/ESPAsyncWebServer");
  Serial.println(" - https://github.com/Adam5Wu/esp8266FTPServer");
}


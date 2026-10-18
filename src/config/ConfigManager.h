#pragma once
#include <Arduino.h>
#include <EEPROM.h>

#define MAX_PWD_LENGTH 64
#define MAX_USR_LENGTH 100
#define MAX_URL_LENGTH 200
#define MAX_TOPIC_LENGTH 20

struct PowerConfig {
    bool deepSleepEnabled = false;
    uint32_t deepSleepThreshold = 300;
    uint16_t wakeDisplaySeconds = 10;
};

struct AccessPointConfiguration {
  char SSID[20] = "ESP8266";
  uint8_t IP[4] = {192,168,4,1};
  char password[MAX_PWD_LENGTH] = "ESP8266Test";
  uint8_t channel = 0;
};

struct StationConfiguration {
  char SSID[50] = "";
  char password[MAX_PWD_LENGTH] = "";
  char hostname[20] = "NodeMCU";
};

struct HttpServerConfiguration {
  int port = 80;
  bool requireAuthentication = true;
  char username[MAX_USR_LENGTH] = "admin";
  char password[MAX_PWD_LENGTH] = "admin";
};

struct HttpPostConfiguration {
  char url[MAX_URL_LENGTH] = "";
  char username[MAX_USR_LENGTH] = "";
  char password[MAX_PWD_LENGTH] = "";
};

struct MqttConfiguration {
  char broker[MAX_URL_LENGTH] = "";
  int  port = 1883;
  char producerTopic[MAX_TOPIC_LENGTH]  = "";
  char listenerTopic[MAX_TOPIC_LENGTH]  = "";
  char username[MAX_USR_LENGTH] = "";
  char password[MAX_PWD_LENGTH] = "";
};

struct NtpConfiguration {
  bool useNTP = true;
  char NTPPoolURL[MAX_URL_LENGTH] = "nl.pool.ntp.org";
  int NTPOffset = 3600;
};

struct MeasurementConfiguration {
  int sampleIntervalSeconds = 10;
  bool publishHttp = false;
  bool publishMqtt = false;
  bool humidity = true;
  bool pressure = true;
  bool temperature = true;
  bool gasResistance = true;
};

struct AppConfig {
  char mode[20] = "WiFiAccessPoint";  // WiFiAccessPoint or WiFiClient
  AccessPointConfiguration  accessPoint;
  StationConfiguration      station;
  HttpServerConfiguration   server;
  HttpPostConfiguration     httpPost;
  MqttConfiguration         mqtt;
  NtpConfiguration          ntp;
  PowerConfig               power;
  MeasurementConfiguration  measurement;
};

class ConfigManager {
public:
  static constexpr size_t EEPROM_SIZE=2084;
  static constexpr uint32_t MAGIC=0x32434D45UL; // EMC2
  static constexpr uint16_t VERSION=3;
  void begin();
  bool load(AppConfig& cfg);
  bool save(const AppConfig& cfg);
  void factoryDefaults(AppConfig& cfg) const { cfg = AppConfig{}; }
private:
  struct Header { uint32_t magic; uint16_t version; uint16_t size; uint32_t crc; uint32_t bootCounter; };
  static constexpr int CONFIG_OFFSET=16;
  uint32_t crc32(const uint8_t* data, size_t len) const;
};

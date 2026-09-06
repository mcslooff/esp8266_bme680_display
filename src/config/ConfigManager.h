#pragma once
#include <Arduino.h>
#include <EEPROM.h>

struct AppConfig {
  int serverPort = 80;
  char accessPointSSID[20] = "ESP8266";
  uint8_t accessPointIP[4] = {192,168,4,1};
  bool accessPointMode = true;
  char accessPointPassword[20] = "ESP8266Test";
  uint8_t accessPointChannel = 0;
  bool stationMode = false;
  char stationSSID[50] = "";
  char stationAccessPointPassword[20] = "";
  bool stationRequireAuthentication = true;
  char stationUsername[20] = "admin";
  char stationPassword[20] = "admin";
  int sensorSampleInterval = 10;
  char publishingPolicy[6] = "Poll";
  char publishingURL[100] = "";
  char publishingUsername[20] = "";
  char publishingPassword[20] = "";
  bool useNTP = true;
  char NTPPoolURL[50] = "nl.pool.ntp.org";
  int NTPOffset = 3600;
  char hostName[20] = "NodeMCU";
  bool domoticz = false;
  char temphumbaridx[20] = "0";
  char vocidx[20] = "0";
};

class ConfigManager {
public:
  static constexpr size_t EEPROM_SIZE=1024;
  static constexpr uint32_t MAGIC=0x32434D45UL; // EMC2
  static constexpr uint16_t VERSION=2;
  void begin();
  bool load(AppConfig& cfg);
  bool save(const AppConfig& cfg);
  void factoryDefaults(AppConfig& cfg) const { cfg = AppConfig{}; }
  bool migrateLegacy(AppConfig& cfg);
private:
  struct Header { uint32_t magic; uint16_t version; uint16_t size; uint32_t crc; uint32_t bootCounter; };
  static constexpr int CONFIG_OFFSET=16;
  uint32_t crc32(const uint8_t* data, size_t len) const;
};

#include "ConfigManager.h"
#include <string.h>

void ConfigManager::begin() { EEPROM.begin(EEPROM_SIZE); }
uint32_t ConfigManager::crc32(const uint8_t* data,size_t len) const {
  uint32_t crc=0xFFFFFFFFUL;
  while(len--){ crc^=*data++; for(int i=0;i<8;i++) crc=(crc>>1)^((crc&1)?0xEDB88320UL:0); }
  return ~crc;
}
bool ConfigManager::load(AppConfig& cfg) {
  Header h{}; EEPROM.get(0,h);
  if(h.magic==MAGIC && h.version==VERSION && h.size==sizeof(AppConfig) && CONFIG_OFFSET+(int)h.size <= EEPROM_SIZE){
    EEPROM.get(CONFIG_OFFSET,cfg);
    return crc32(reinterpret_cast<const uint8_t*>(&cfg),sizeof(cfg))==h.crc;
  }
  return migrateLegacy(cfg);
}
bool ConfigManager::migrateLegacy(AppConfig& cfg) {
  if(EEPROM.read(0)!=15) return false;
  // Legacy layout used the same field ordering as AppConfig, starting at byte 2.
  EEPROM.get(2,cfg);
  if(cfg.serverPort<1 || cfg.serverPort>65535 || cfg.sensorSampleInterval<1 || cfg.sensorSampleInterval>86400) return false;
  return save(cfg);
}
bool ConfigManager::save(const AppConfig& cfg) {
  Header h{}; h.magic=MAGIC; h.version=VERSION; h.size=sizeof(AppConfig);
  h.crc=crc32(reinterpret_cast<const uint8_t*>(&cfg),sizeof(cfg));
  Header old{}; EEPROM.get(0,old); h.bootCounter=old.bootCounter;
  EEPROM.put(0,h); EEPROM.put(CONFIG_OFFSET,cfg); return EEPROM.commit();
}

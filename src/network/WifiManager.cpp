#include "WifiManager.h"
void WifiManager::begin(const AppConfig& cfg){
  cfg_=&cfg;
  bool ap=cfg.accessPointMode, sta=cfg.stationMode && cfg.stationSSID[0];
  if(!ap && !sta) return;
  WiFi.persistent(false); WiFi.setAutoReconnect(true);
  WiFi.mode(ap&&sta?WIFI_AP_STA:(ap?WIFI_AP:WIFI_STA));
  if(ap){
    IPAddress ip(cfg.accessPointIP[0],cfg.accessPointIP[1],cfg.accessPointIP[2],cfg.accessPointIP[3]);
    WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0));
    WiFi.softAP(cfg.accessPointSSID,cfg.accessPointPassword,cfg.accessPointChannel,0);
  }
  if(sta){ WiFi.hostname(cfg.hostName); WiFi.begin(cfg.stationSSID,cfg.stationAccessPointPassword); retryAt_=millis()+15000; }
}
void WifiManager::update(){
  if(cfg_ && cfg_->stationMode && cfg_->stationSSID[0] && !connected() && (long)(millis()-retryAt_)>=0){
    WiFi.begin(cfg_->stationSSID,cfg_->stationAccessPointPassword); retryAt_=millis()+30000;
  }
}
int WifiManager::scan(){ networkCount_=WiFi.scanNetworks(); return networkCount_; }
String WifiManager::networksJson(const char* selected) const {
  String s="["; for(int i=0;i<networkCount_;++i){ if(i) s+=','; s+="{\"value\":\""; s+=WiFi.SSID(i); s+="\",\"text\":\""; s+=WiFi.SSID(i); s+="\",\"selected\":"; s+=(strcmp(selected,WiFi.SSID(i).c_str())==0?"true":"false"); s+='}'; } s+=']'; return s;
}

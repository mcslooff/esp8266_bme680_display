#include "WifiManager.h"
void WifiManager::begin(const AppConfig& cfg) {
  cfg_ = &cfg;
  bool sta = (strcmp(cfg.mode, "WiFiClient") == 0) && cfg.station.SSID[0];
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(sta ? WIFI_STA : WIFI_AP);
  if (sta) {
    WiFi.hostname(cfg.station.hostname);
    WiFi.begin(cfg.station.SSID, cfg.station.password);
    retryAt_ = millis() + 15000;
  } else {
    IPAddress ip(cfg.accessPoint.IP[0], cfg.accessPoint.IP[1], cfg.accessPoint.IP[2], cfg.accessPoint.IP[3]);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(cfg.accessPoint.SSID, cfg.accessPoint.password, cfg.accessPoint.channel, 0);
  }
}

void WifiManager::update() {
  bool sta = (strcmp(cfg_->mode, "WiFiClient") == 0) && cfg_->station.SSID[0];
  if (sta && cfg_->station.SSID[0] && !connected() && (long)(millis() - retryAt_) >= 0) {
    WiFi.begin(cfg_->station.SSID, cfg_->station.password);
    retryAt_ = millis() + 30000;
  }
}

int WifiManager::scan() {
  networkCount_ = WiFi.scanNetworks();
  return networkCount_;
}

String WifiManager::networksJson(const char* selected) const {
  String s = "[";
  for (int i = 0; i < networkCount_; ++i) {
    if (i) s += ',';
    s += "{\"value\":\"";
    s += WiFi.SSID(i);
    s += "\",\"text\":\"";
    s += WiFi.SSID(i);
    s += "\",\"selected\":";
    s += (strcmp(selected, WiFi.SSID(i).c_str()) == 0 ? "true" : "false");
    s += "}";
  }
  s += "]";
  return s;
}

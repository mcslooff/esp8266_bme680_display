#include "Publisher.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <base64.h>

String Publisher::json(const Measurement& m) const { String s="{\"stationName\":\""+String(cfg_.hostName)+"\",\"timestamp\":"+String((unsigned long)m.timestamp)+",\"temperature\":"+String(m.temperature,3)+",\"humidity\":"+String(m.humidity,3)+",\"airpressure\":"+String(m.pressureHpa(),3)+",\"voc\":"+String(m.gasResistanceKOhm(),3)+",\"battery\":"+String(m.batteryVoltage,3)+",\"dewpoint\":"+String(m.dewPoint(),3)+"}"; return s; }
bool Publisher::httpPost(const String& body){ if(!cfg_.publishingURL[0] || WiFi.status()!=WL_CONNECTED) return false; WiFiClient client; HTTPClient http; if(!http.begin(client,cfg_.publishingURL)) return false; http.setTimeout(5000); http.addHeader("Content-Type","application/json"); int code=http.POST(body); http.end(); return code>=200&&code<300; }
bool Publisher::domoticz(const Measurement& m){ if(!cfg_.publishingURL[0]||WiFi.status()!=WL_CONNECTED)return false; String u=base64::encode(cfg_.publishingUsername); String p=base64::encode(cfg_.publishingPassword); WiFiClient client; HTTPClient http; String url=String(cfg_.publishingURL)+"/json.htm?username="+u+"&password="+p+"&type=command&param=udevice&idx="+cfg_.temphumbaridx+"&nvalue=0&svalue="+String(m.temperature,2)+";"+String(m.humidity,2)+";0;"+String(m.pressureHpa(),2)+";0"; if(!http.begin(client,url))return false; http.setTimeout(5000); int code=http.GET(); http.end(); if(!(code>=200&&code<300))return false; url=String(cfg_.publishingURL)+"/json.htm?username="+u+"&password="+p+"&type=command&param=udevice&idx="+cfg_.vocidx+"&nvalue=0&svalue="+String(m.gasResistanceKOhm(),2); if(!http.begin(client,url))return false; http.setTimeout(5000); code=http.GET(); http.end(); return code>=200&&code<300; }
bool Publisher::publishBufferedJson(const String& body){ if(cfg_.domoticz || strcmp(cfg_.publishingPolicy,"Push")!=0) return false; return httpPost(body); }
bool Publisher::publish(const Measurement& m){ if(strcmp(cfg_.publishingPolicy,"Push")!=0)return true; bool ok=cfg_.domoticz?domoticz(m):httpPost(json(m)); if(!ok) log_.warn("Publish failed\n"); return ok; }
void Publisher::retryBuffered(){ /* Reserved for persistent replay; current v2 buffers JSONL in the application. */ }

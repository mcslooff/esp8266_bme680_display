#include "DisplayManager.h"
#include <FS.h>
#include <time.h>

static const uint8_t degree_icon[8]={0x40,0xA0,0x40,0x00,0x00,0x00,0x00,0x00};
static const uint8_t ohm_icon[8]={0x00,0x3C,0x42,0x42,0x24,0x24,0xC3,0x00};

void DisplayManager::begin(){
  display_.clearDisplay(); display_.setTextColor(SSD1306_WHITE); display_.setTextSize(1);
  on_=false; screen_=LAST;
}
void DisplayManager::wake(){ on_=true; turnedOnAt_=millis(); showLogo_=true; }
void DisplayManager::toggle(){ if(on_) { on_=false; display_.clearDisplay(); display_.display(); } else wake(); }
void DisplayManager::next(){ screen_=static_cast<Screen>((screen_+1)%SCREEN_COUNT); wake(); }
void DisplayManager::prev(){ screen_=static_cast<Screen>((screen_+SCREEN_COUNT-1)%SCREEN_COUNT); wake(); }
void DisplayManager::addConnectionScreen() {}
void DisplayManager::addDateTimeScreen() {}
void DisplayManager::addSpiffsScreen() {}
void DisplayManager::update(){
  if(!on_) return;
  const uint32_t now=millis();
  if(elapsed(now,turnedOnAt_,sleepTime_)){ on_=false; display_.clearDisplay(); display_.display(); return; }
  if(showLogo_ && !elapsed(now,turnedOnAt_,logoTime_)) { logo(); display_.display(); return; }
  showLogo_=false; render(); display_.display();
}
void DisplayManager::logo(){ display_.clearDisplay(); display_.setTextSize(2); display_.setCursor(16,8); display_.print(F("BME680")); display_.setTextSize(1); }
void DisplayManager::text(const char* s,int x,int y){display_.setCursor(x,y);display_.print(s);}
void DisplayManager::bar(int x,int y,int w,int h,float pct){ pct=constrain(pct,0,100); int fill=(w-4)*pct/100; display_.drawRect(x,y,w,h,SSD1306_WHITE); if(fill>0) display_.fillRect(x+2,y+2,fill,h-4,SSD1306_WHITE); }
void DisplayManager::grid(int h,int v){ int dh=HEIGHT/(h+1), dw=WIDTH/(v+1); for(int i=1;i<=h;i++) display_.drawLine(0,i*dh,WIDTH,i*dh,SSD1306_WHITE); for(int i=1;i<=v;i++) display_.drawLine(i*dw,0,i*dw,HEIGHT,SSD1306_WHITE); }
void DisplayManager::last(){
  Measurement m; display_.clearDisplay(); if(!store_.latest(m)){ text("No measurement",20,12); return; }
  display_.setTextSize(1); display_.setCursor(2,1); display_.printf("%.1f",m.temperature); display_.drawBitmap(54,1,degree_icon,8,8,1);
  display_.setCursor(66,1); display_.printf("%.1fhPa",m.pressureHpa());
  display_.setCursor(2,11); display_.printf("%.1f %%",m.humidity); display_.setCursor(66,11); display_.printf("%.1fk",m.gasResistanceKOhm()); display_.drawBitmap(117,10,ohm_icon,8,8,1);
  display_.setCursor(2,22); bar(2,21,30,9,m.batteryPercent()); display_.setCursor(35,22); display_.printf("%.0f%%",m.batteryPercent()); display_.setCursor(66,22); display_.printf("%.1f",m.dewPoint()); display_.drawBitmap(112,21,degree_icon,8,8,1); grid(2,1);
}
void DisplayManager::batteryScreen(){ Measurement m; display_.clearDisplay(); if(!store_.latest(m)){text("No measurement",20,12);return;} float p=m.batteryPercent(); display_.setCursor(24,0); display_.printf("%.2fV %.1f%%",m.batteryVoltage,p); bar(24,10,80,20,p); }
void DisplayManager::graph(uint8_t type){
  display_.clearDisplay(); size_t n=store_.size(); if(!n){text("No measurements",18,12);return;}
  float lo=1e30f,hi=-1e30f; Measurement m;
  for(size_t i=0;i<n;i++){store_.get(i,m); float v= type==0?m.temperature:type==1?m.humidity:type==2?m.gasResistanceKOhm():type==3?m.pressureHpa():m.batteryVoltage; lo=min(lo,v); hi=max(hi,v);}
  if(hi<=lo) hi=lo+1;
  const int usableW=127; const int top=8,bottom=31; int prevX=-1,prevY=-1;
  for(int x=0;x<usableW;x++){ size_t idx=(size_t)((uint32_t)x*n/usableW); if(idx>=n) idx=n-1; store_.get(idx,m); float v= type==0?m.temperature:type==1?m.humidity:type==2?m.gasResistanceKOhm():type==3?m.pressureHpa():m.batteryVoltage; int y=bottom-(int)((v-lo)/(hi-lo)*(bottom-top)); if(prevX>=0) display_.drawLine(prevX,prevY,x,y,SSD1306_WHITE); prevX=x;prevY=y; }
  display_.setCursor(0,0); const char* label=type==0?"Temp":type==1?"Hum":type==2?"Gas kOhm":type==3?"hPa":"Volt"; display_.print(label); display_.setCursor(72,0); display_.printf("%.1f",hi); display_.setCursor(104,24); display_.printf("%.1f",lo);
}
void DisplayManager::connection(){ display_.clearDisplay(); if(cfg_.accessPointMode){text("AP:",0,0);text(cfg_.accessPointSSID,24,0);} if(cfg_.stationMode){text("STA:",0,10);text(cfg_.stationSSID,30,10);} text("IP:",0,20); String ip=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString(); text(ip.c_str(),20,20); }
void DisplayManager::datetime(){ display_.clearDisplay(); time_t now=time(nullptr); struct tm* ti=localtime(&now); if(!ti){text("No time",40,12);return;} unsigned long sec=millis()/1000; display_.setCursor(2,1); display_.printf("Up %lu:%02lu:%02lu",sec/3600,(sec/60)%60,sec%60); display_.setCursor(2,11); display_.printf("%04d-%02d-%02d",ti->tm_year+1900,ti->tm_mon+1,ti->tm_mday); display_.setCursor(2,21); display_.printf("%02d:%02d:%02d",ti->tm_hour,ti->tm_min,ti->tm_sec); }
void DisplayManager::spiffs(){ display_.clearDisplay(); FSInfo f{}; if(!::SPIFFS.info(f)){text("SPIFFS error",30,12);return;} float p=f.totalBytes?100.0f*f.usedBytes/f.totalBytes:0; display_.setCursor(0,0); display_.printf("FS %lu/%lu",(unsigned long)f.usedBytes,(unsigned long)f.totalBytes); bar(1,12,126,12,p); display_.setCursor(50,26); display_.printf("%.0f%%",p); }
void DisplayManager::render(){ switch(screen_){case LAST:last();break;case BATTERY:batteryScreen();break;case TEMP:graph(0);break;case HUM:graph(1);break;case GAS:graph(2);break;case PRESSURE:graph(3);break;case BATGRAPH:graph(4);break;case CONNECTION:connection();break;case DATETIME:datetime();break;case SPIFFS:spiffs();break;default:last();} }

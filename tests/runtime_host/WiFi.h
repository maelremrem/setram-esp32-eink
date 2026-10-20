#pragma once
#include <Arduino.h>
#include <esp_wifi_types.h>
#define WL_CONNECTED 3
#define WIFI_STA 1
struct IPAddress {
 IPAddress(int=0,int=0,int=0,int=0){}
 String toString() const {return "192.0.2.1";}
};
struct HostWiFi {
 int connection=0, rssi=-65;
 wifi_ps_type_t sleep=WIFI_PS_MAX_MODEM;
 int RSSI()const{return rssi;}
 wifi_ps_type_t getSleep()const{return sleep;}
 bool setSleep(bool enabled){sleep=enabled?WIFI_PS_MIN_MODEM:WIFI_PS_NONE;return true;}
 bool setSleep(wifi_ps_type_t mode){sleep=mode;return true;}
 int status()const{return connection;}
 void mode(int){}
 void setAutoReconnect(bool){}
 IPAddress localIP()const{return {};}
};
extern HostWiFi WiFi;

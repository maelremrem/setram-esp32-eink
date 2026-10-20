#pragma once
#include <WiFi.h>
#include <functional>
extern std::function<void()> onAutoConnect;
class WiFiManagerParameter {
 std::string value;
public:
 WiFiManagerParameter(const char*,const char*,const char* v,int):value(v){}
 explicit WiFiManagerParameter(const char*){}
 void setValue(const char* v,int){value=v;}
 const char* getValue()const{return value.c_str();}
};
class WiFiManager {
public:
 bool active=false;
 bool getConfigPortalActive()const{return active;}
 void startConfigPortal(const char*,const char*){active=true;}
 void stopConfigPortal(){active=false;}
 void process(){}
 bool autoConnect(const char*,const char*){if(onAutoConnect) onAutoConnect(); return WiFi.status()==WL_CONNECTED;}
 void setDebugOutput(bool){}
 void setConfigPortalBlocking(bool){}
 void setCustomMenuHTML(const char*){}
 void setMenu(const char**,int){}
 void setWebServerCallback(std::function<void()>){}
 void setSaveParamsCallback(std::function<void()>){}
 void setRemoveDuplicateAPs(bool){}
 void setConnectTimeout(int){}
 void addParameter(WiFiManagerParameter*){}
 void setAPStaticIPConfig(IPAddress,IPAddress,IPAddress){}
};

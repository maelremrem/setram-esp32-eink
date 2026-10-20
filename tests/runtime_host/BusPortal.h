#pragma once
#include <WiFiManager.h>
#include <PortalSettings.h>
namespace busPortal {
inline uint32_t frequency(const char* s){return uint32_t(atoi(s))*1000;}
struct Portal {
 Portal(WiFiManager&,portalSettings::Settings&,uint32_t&,bool(*)(const portalSettings::Settings&,uint32_t)){}
 void attach(){}
};
}

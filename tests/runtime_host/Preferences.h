#pragma once
#include <Arduino.h>
#include <map>
#include <string>
// Controlled host fixture, not evidence of ESP32 flash persistence.
class Preferences {
  std::string space;
  bool opened=false, readOnly=true;
public:
 static std::map<std::string,unsigned>& values(){static std::map<std::string,unsigned> data;return data;}
 static std::map<std::string,std::string>& strings(){static std::map<std::string,std::string> data;return data;}
 static bool& writable(){static bool flag=false;return flag;}
 static bool& available(){static bool flag=true;return flag;}
 static bool& mismatch(){static bool flag=false;return flag;}
 static bool& stringMismatch(){static bool flag=false;return flag;}
 bool begin(const char* name,bool read){space=name;readOnly=read;opened=available();return opened;}
 void end(){opened=false;}
 String getString(const char* name,const char* fallback){
   if(!opened) return fallback;
   auto at=strings().find(space+"|"+name);
   if(at==strings().end()) return fallback;
   return stringMismatch() ? at->second+"x" : at->second;
 }
 unsigned getUInt(const char* name,unsigned fallback){
   if(!opened) return fallback;
   auto at=values().find(space+"|"+name);
   unsigned value=at==values().end() ? fallback : at->second;
   return !readOnly && mismatch() ? value^1u : value;
 }
 size_t putString(const char* name,const char* value){
   if(!opened || readOnly || !writable()) return 0;
   strings()[space+"|"+name]=value; return std::strlen(value)+1;
 }
 size_t putUInt(const char* name,unsigned value){
   if(!opened || readOnly || !writable()) return 0;
   values()[space+"|"+name]=value;return sizeof(value);
 }
};

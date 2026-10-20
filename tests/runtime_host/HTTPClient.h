#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <vector>
#include <functional>
#define HTTP_CODE_OK 200
extern std::vector<uint8_t> hostPayload;
extern int hostHTTPStatus, hostRequests;
extern std::function<void()> onRequest;
class HTTPClient {
public:
 void setConnectTimeout(int){}
 void setTimeout(int){}
 bool begin(WiFiClientSecure&,const char*){return true;}
 void addHeader(const char*,const char*){}
 void collectHeaders(const char**,int){}
 int GET(){++hostRequests; if(onRequest) onRequest(); return hostHTTPStatus;}
 String header(const char*){return "fixture Date";}
 int getSize(){return int(hostPayload.size());}
 int writeToStream(Stream* stream){return int(stream->write(hostPayload.data(),hostPayload.size()));}
 void end(){}
};

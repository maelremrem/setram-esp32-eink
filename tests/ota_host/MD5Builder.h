#pragma once
#include "Arduino.h"
#include <openssl/evp.h>
namespace otaFixture {
inline std::string md5(const std::string& input) {
  unsigned char hash[EVP_MAX_MD_SIZE]; unsigned size = 0;
  if (EVP_Digest(input.data(), input.size(), hash, &size, EVP_md5(), nullptr) != 1) std::abort();
  char hex[33];
  for (unsigned i=0; i<size; ++i) std::snprintf(hex+2*i,3,"%02x",hash[i]);
  return hex;
}
}
class MD5Builder {
 public:
  void begin() { data_.clear(); }
  void add(const char* s) { data_ += s; }
  void add(const String& s) { data_ += s.c_str(); }
  void calculate() { hash_=otaFixture::md5(data_); }
  String toString() const { return String(hash_); }
 private: std::string data_, hash_;
};

#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
class String {
 public:
  String() = default;
  String(const char* s): value_(s ? s : "") {}
  String(const std::string& s): value_(s) {}
  String(unsigned long n): value_(std::to_string(n)) {}
  unsigned length() const { return value_.size(); }
  const char* c_str() const { return value_.c_str(); }
  String& operator+=(char c) { value_ += c; return *this; }
  bool equals(const String& s) const { return value_ == s.value_; }
  void trim() {
    auto a = value_.find_first_not_of(" \r\n\t");
    if (a == std::string::npos) { value_.clear(); return; }
    value_ = value_.substr(a, value_.find_last_not_of(" \r\n\t") - a + 1);
  }
  friend String operator+(const String& a, const String& b) { return String(a.value_+b.value_); }
 private: std::string value_;
};
constexpr int LOW = 0;
namespace otaFixture {
extern std::vector<std::string> logs;
void log(const char*, ...);
extern unsigned restarts, delayCalls;
}
#define log_i(...) otaFixture::log(__VA_ARGS__)
#define log_w(...) otaFixture::log(__VA_ARGS__)
#define log_e(...) otaFixture::log(__VA_ARGS__)
#define log_d(...) otaFixture::log(__VA_ARGS__)
inline unsigned long micros() { return 123456UL; }
inline void delay(unsigned) { ++otaFixture::delayCalls; }
struct ESPClass { void restart() { ++otaFixture::restarts; } };
extern ESPClass ESP;

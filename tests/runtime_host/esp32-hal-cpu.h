#pragma once
#include <cstdint>
extern uint32_t hostCpuMhz;
inline uint32_t getCpuFrequencyMhz(){return hostCpuMhz;}
inline bool setCpuFrequencyMhz(uint32_t value){hostCpuMhz=value; return true;}

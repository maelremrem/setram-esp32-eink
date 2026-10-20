#pragma once
namespace battery {
// Approximate resting-voltage curve for a single 4.2V Li-ion cell ONLY.
// Reject implausible readings; voltage alone is not a calibrated fuel gauge.
inline int percent(int millivolts) {
  if(millivolts<3000 || millivolts>4300) return -1;
  const int volts[]={3300,3500,3650,3700,3750,3800,3850,3900,4000,4100,4200};
  const int levels[]={0,5,10,20,35,50,60,70,80,90,100};
  if(millivolts<=volts[0]) return 0;
  for(unsigned i=1;i<11;++i) if(millivolts<=volts[i])
    return levels[i-1]+(millivolts-volts[i-1])*(levels[i]-levels[i-1])/(volts[i]-volts[i-1]);
  return 100;
}
}

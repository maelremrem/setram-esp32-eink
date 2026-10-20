#include <Battery.h>
#include <cassert>
#include <iostream>
int main() {
  assert(battery::percent(4200)==100);
  assert(battery::percent(3300)==0);
  assert(battery::percent(3800)==50);
  assert(battery::percent(0)==-1 && battery::percent(5000)==-1);
  int previous=-1;
  for(int mv=3000;mv<=4300;++mv) { int p=battery::percent(mv); assert(p>=previous && p<=100); previous=p; }
  std::cout << "Battery estimate: OK\n";
}

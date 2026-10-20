#include <BusCatalog.h>
#include <BusPortalPolicy.h>
#include <iostream>
int main(int argc,char**argv) {
  if((argc!=6 && argc!=10) || !busPortal::frequency(argv[5])) return 2;
  const auto* e=busCatalog::find(argv[1],argv[2],argv[3],argv[4]);
  if(!e || !portalSettings::valid(busCatalog::settings(*e))) return 3;
  portalSettings::Settings first=busCatalog::settings(*e);
  if(argc==10) {
    const auto* e2=busCatalog::find(argv[6],argv[7],argv[8],argv[9]);
    if(!e2) return 4;
    portalSettings::Settings second=busCatalog::settings(*e2);
    if(first.stop==second.stop && first.route==second.route && first.direction==second.direction &&
       first.destinationLabel==second.destinationLabel) return 5;
    std::cout << portalSettings::serialize(first) << "\nSECOND\n" << portalSettings::serialize(second);
  } else {
    std::cout << portalSettings::serialize(first);
  }
  std::cout << '\n' << busPortal::frequency(argv[5]);
}

#include <cassert>
#include <BusCatalog.h>
#include <BusPortalPolicy.h>
int main() {
  auto &e = busCatalog::entries[0];
  for(const auto& row : busCatalog::entries) {
    assert(portalSettings::valid(busCatalog::settings(row)));
    assert(busCatalog::find(row.stop,row.route,row.direction,row.destination));
  }
  auto found = busCatalog::find(e.stop,e.route,e.direction,e.destination);
  assert(found && portalSettings::valid(busCatalog::settings(*found)));
  assert(!busCatalog::find("not-a-stop",e.route,e.direction,e.destination));
  assert(!busCatalog::find(e.stop,"not-a-route",e.direction,e.destination));
  assert(!busCatalog::find(e.stop,e.route,"-1",e.destination));
  assert(busPortal::frequency("30")==30000 && busPortal::frequency("60")==60000 && busPortal::frequency("120")==120000);
  assert(!busPortal::frequency("31") && !busPortal::frequency("60junk"));
  assert(busPortal::authorized(true,"abc","abc","http://192.168.4.1","192.168.4.1","192.168.4.1",""));
  assert(!busPortal::authorized(false,"abc","abc","","192.168.4.1","192.168.4.1",""));
  assert(!busPortal::authorized(true,"abc","bad","","192.168.4.1","192.168.4.1",""));
  assert(!busPortal::authorized(true,"abc","abc","https://evil.test","192.168.4.1","192.168.4.1",""));
  assert(!busPortal::authorized(true,"abc","abc","","evil.test","192.168.4.1",""));
}

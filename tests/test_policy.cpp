#include "DisplayPolicy.h"
#include <cassert>
#include <iostream>
int main() {
  assert(!displayPolicy::fresh(0, 1791316000));
  assert(displayPolicy::fresh(1791315970, 1791316000));
  assert(!displayPolicy::fresh(1791315600, 1791316000));
  assert(!displayPolicy::fresh(1791316100, 1791316000));
  assert(displayPolicy::fresh(1791316030, 1791316000));
  assert(displayPolicy::label("Arret tres long avec plein de caracteres", 10) == "Arret t...");
  assert(displayPolicy::label("Gare", 10) == "Gare");
  assert(displayPolicy::label("Universit\xc3\xa9", 18) == "Universite");
  assert(!displayPolicy::fresh(1791316120, 1791316000));
  assert(displayPolicy::usable(1791316120, 1791316000));
  assert(displayPolicy::classify(1791316120, 1791316000) == displayPolicy::FeedState::FutureTolerated);
  assert(displayPolicy::classify(1791316060, 1791316000) == displayPolicy::FeedState::Fresh);
  assert(displayPolicy::usable(1791316180, 1791316000));
  assert(!displayPolicy::usable(1791316181, 1791316000));
  assert(displayPolicy::classify(1791316181, 1791316000) == displayPolicy::FeedState::FutureRejected);
  assert(displayPolicy::aheadSeconds(1791316120, 1791316000) == 120);
  assert(displayPolicy::aheadSeconds(1791315970, 1791316000) == 0);
  assert(!displayPolicy::usable(0, 1791316000));
  assert(!displayPolicy::usable(1791316000, 0));
  assert(!displayPolicy::usable(1791315819, 1791316000));
  assert(displayPolicy::usable(1791315820, 1791316000));
  std::cout << "Display policy: OK\n";
}

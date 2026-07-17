#include <iostream>

#include "apgar/version.h"

int main() {
  std::cout << apgar::ProjectName() << ' ' << apgar::Version() << '\n';
  return 0;
}

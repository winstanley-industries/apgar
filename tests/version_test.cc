#include "apgar/version.h"

#include <iostream>

int main() {
  if (apgar::ProjectName() != "APGAR") {
    std::cerr << "unexpected project name: " << apgar::ProjectName() << '\n';
    return 1;
  }

  if (apgar::Version() != "0.0.0-dev") {
    std::cerr << "unexpected version: " << apgar::Version() << '\n';
    return 1;
  }

  return 0;
}

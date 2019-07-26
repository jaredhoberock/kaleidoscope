// clang -std=c++17 main.cpp -lstdc++ `llvm-config --cppflags --ldflags --system-libs --libs core`
#include <iostream>
#include "interpret.hpp"

int main()
{
  // XXX what's the best place for this?
  llvm::InitializeNativeTarget();

  // XXX what's the best place for this?
  llvm::InitializeNativeTargetAsmPrinter();

  // XXX what's this for, and why don't we need it?
  //InitializeNativeTargetAsmParser();

  interpret();

  return 0;
}


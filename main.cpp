// clang -std=c++17 main.cpp -lstdc++ -rdynamic `llvm-config --cppflags --ldflags --system-libs --libs core`
#include <iostream>
#include "interpret.hpp"

extern "C" double putchard(double x)
{
  std::cerr << static_cast<char>(x);
  return 0;
}

extern "C" double printd(double x)
{
  std::cerr << x << std::endl;
  return 0;
}

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


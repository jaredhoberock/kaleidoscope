// clang -std=c++17 main.cpp lexer.cpp -lstdc++ `llvm-config --cppflags --ldflags --system-libs --libs core`
#include <iostream>
#include "parser.hpp"
#include "generator.hpp"
#include "interpret.hpp"

int main()
{
  // XXX what's the best place for this?
  llvm::InitializeNativeTarget();

  // XXX what's the best place for this?
  llvm::InitializeNativeTargetAsmPrinter();
  //InitializeNativeTargetAsmParser();

  interpret();

  return 0;
}


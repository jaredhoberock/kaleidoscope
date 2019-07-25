// clang -std=c++17 main.cpp lexer.cpp -lstdc++ `llvm-config --cppflags --ldflags --system-libs --libs core`
#include <iostream>
#include "parser.hpp"
#include "generator.hpp"

int main()
{
  parser p;

  program prog = p.parse_program();

  generator g;

  // visit the program
  g.visitor()(prog);

  // print out the generated code
  g.module().print(llvm::errs(), nullptr);

  return 0;
}


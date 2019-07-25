#include <iostream>
#include "parser.hpp"

int main()
{
  parser p;

  program prog = p.parse_program();

  return 0;
}


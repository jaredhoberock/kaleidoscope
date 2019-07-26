#pragma once

#include "parser.hpp"
#include "jit_compiler.hpp"
#include "overloaded.hpp"

void handle_statement(generator& gen, jit_compiler& compiler, const top_level_statement& statement)
{
  std::visit(overloaded(
    [&](const function& f)
    {
      // generate IR
      auto& f_ir = gen.visitor()(f);

      // print IR
      f_ir.print(llvm::errs());

      // give the module to the compiler 
      compiler.add_module(gen.release_module());
    },
    [&](const function_prototype& fp)
    {
      // generate IR
      auto& f_ir = gen.visitor()(fp);

      // print IR
      f_ir.print(llvm::errs());
    },
    [&](const expression& e)
    {
      // create an anonymous function for this expression
      function f(function_prototype("__anon_expr", std::vector<std::string>()), e);

      auto& f_ir = gen.visitor()(f);
      f_ir.print(llvm::errs());

      // give the module to the compiler
      auto module_handle = compiler.add_module(gen.release_module());

      // ask the compiler for __anon_expr's symbol
      auto symbol = compiler.find_symbol("__anon_expr");

      if(!symbol)
      {
        // remove the module
        compiler.remove_module(module_handle);
        throw std::runtime_error("Function not found");
      }

      // form a pointer to the function
      double (*f_ptr)() = reinterpret_cast<double(*)()>(*symbol.getAddress());

      // evaluate the expression
      std::cout << "Evaluated to " << f_ptr() << std::endl;

      // remove the module
      compiler.remove_module(module_handle);
    }),
    statement
  );
}

void interpret()
{
  parser p;
  generator gen;
  jit_compiler compiler;

  while(p.current_token() != token(char(EOF)))
  {
    if(p.current_token() != token(';'))
    {
      // parse a statement
      top_level_statement statement = p.parse_top_level_statement();

      // handle it
      handle_statement(gen, compiler, statement);
    }
    else
    {
      // consume the semicolon
      p.parse_token(';');
    }
  }
}


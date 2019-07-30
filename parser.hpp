#pragma once

#include <vector>
#include <cassert>
#include <map>
#include "syntax.hpp"
#include "lexer.hpp"
#include "overloaded.hpp"

class parser
{
  public:
    parser()
      : current_token_(get_token()) // get the first token
    {}

    // XXX this really shouldn't be exposed
    const token& current_token() const
    {
      return current_token_;
    }

    // XXX this really shouldn't be exposed
    inline token parse_token(token expected)
    {
      if(current_token_ != expected)
      {
        visit([](const auto& expected, const auto& got)
        {
          std::cerr << "Expected '" << expected << "', got '" << got << "'" << std::endl;
          assert(false);
        },
        expected, current_token_);
      }

      current_token_ = get_token();
      return expected;
    }

    // program := { top_level_statement | ';' }*
    inline program parse_program()
    {
      std::vector<top_level_statement> statements;
    
      while(current_token_ != token(char(EOF)))
      {
        if(current_token_ != token(';'))
        {
          // consume a statement
          statements.push_back(parse_top_level_statement());
        }
        else
        {
          // consume the semicolon
          parse_token(';');
        }
      }
    
      return {statements};
    }

    // top_level_statement := function | extern | expression
    inline top_level_statement parse_top_level_statement()
    {
      if(current_token_ == token(keyword::def))
      {
        auto result = parse_function();
        std::cout << "parse_top_level_statement: parsed function" << std::endl;
        return result;
      }
      else if(current_token_ == token(keyword::extern_))
      {
        auto result = parse_extern();
        std::cout << "parse_top_level_statement: parsed extern" << std::endl;
        return result;
      }

      auto result = parse_expression();
      std::cout << "parse_top_level_statement: parsed expression" << std::endl;
      return result;
    }

  private:
    inline std::string parse_identifier()
    {
      std::string result = std::get<std::string>(current_token_);
      current_token_ = get_token();
      return result;
    }

    // number := number
    inline number parse_number()
    {
      double result = std::get<double>(current_token_);
      current_token_ = get_token();
      return {result};
    }

    inline expression parse_number_expression()
    {
      return parse_number();
    }

    // function_call_arguments := '(' (expression ',')* ');
    inline std::vector<expression> parse_function_call_arguments()
    {
      // swallow left parens
      parse_token('(');

      std::vector<expression> result;

      while(current_token_ != token(')'))
      {
        result.emplace_back(parse_expression());

        if(current_token_ == token(')'))
        {
          break;
        }

        // swallow comma
        parse_token(',');
      }

      // swallow right parens
      parse_token(')');

      return result;
    }

    // parens_expression := '(' expression ')'
    inline expression parse_parens_expression()
    {
      parse_token('(');

      expression result = parse_expression();

      parse_token(')');

      return result;
    }

    // identifier_expression := identifier | identifier '(' expression* ')'
    inline expression parse_identifier_expression()
    {
      std::string identifier = parse_identifier();

      // check for a function call
      if(current_token_ != token('('))
      {
        // just a variable
        return variable{identifier};
      }

      return call{identifier, parse_function_call_arguments()};
    }

    // primary_expression := if_expression | for_expression | identifier_expression | number | parens_expression
    inline expression parse_primary_expression()
    {
      // XXX we could really use a pattern match here

      return visit(overloaded(
        [this](const keyword& kw)
        {
          if(kw == keyword::if_)
          {
            return parse_if_expression();
          }

          return parse_for_expression();
        },
        [this](const std::string&)
        {
          return parse_identifier_expression();
        },
        [this](const double&)
        {
          return parse_number_expression();
        },
        [this](const char&)
        {
          return parse_parens_expression();
        }),
        current_token_
      );
    }

    inline int current_token_precedence()
    {
      static std::map<char, int> precedence;
      if(precedence.empty())
      {
        precedence['<'] = 10;
        precedence['+'] = 20;
        precedence['-'] = 20;
        precedence['*'] = 40;
      }

      return visit(overloaded(
        [&,this](char token)
        {
          auto found = precedence.find(token);
          return found == precedence.end() ? -1 : found->second; 
        },
        [](const auto&)
        {
          return -1;
        }),
        current_token_
      );
    }

    // binop_rhs := ('+' primary_expression)*
    inline expression parse_binop_rhs(expression lhs, int precedence)
    {
      while(true)
      {
        // if this is a binary operation, find its precedence
        int token_precedence = current_token_precedence();
        if(token_precedence < precedence)
        {
          break;
        }

        // get the operator
        char op = std::get<char>(current_token_);
        current_token_ = get_token();

        // parse the expression beyond the operator
        expression rhs = parse_primary_expression();

        // if the operator binds less tightly with rhs than the operator beyond rhs,
        // let the pending operator take rhs as its lhs
        int next_precedence = current_token_precedence();
        if(token_precedence < next_precedence)
        {
          rhs = parse_binop_rhs(std::move(rhs), token_precedence + 1);
        }

        // merge lhs & rhs
        lhs = binary_operation(op, std::move(lhs), std::move(rhs));
      }

      return lhs;
    }

    // expression := primary_expression binop_rhs
    inline expression parse_expression()
    {
      expression left_hand_expression = parse_primary_expression();
      return parse_binop_rhs(left_hand_expression, 0);
    }

    // function_prototype := identifier '(' identifier* ')'
    inline function_prototype parse_function_prototype()
    {
      std::string function_name = parse_identifier();

      // consume '('
      parse_token('(');

      std::vector<std::string> parameter_names;

      // parse parameters until we encounter ')'
      // XXX should introduce a parse_delimited_list() or some such
      while(current_token_ != token(')'))
      {
        parameter_names.push_back(parse_identifier());
      }

      // consume ')'
      parse_token(')');

      return {function_name, parameter_names};
    }

    // function := 'def' prototype expression
    inline function parse_function()
    {
      assert(current_token_ == token(keyword::def));
      current_token_ = get_token();
      return {parse_function_prototype(), parse_expression()};
    }

    // extern := 'extern' function_prototype
    inline function_prototype parse_extern()
    {
      assert(current_token_ == token(keyword::extern_));

      current_token_ = get_token();

      return parse_function_prototype();
    }

    // if := 'if' expression 'then' expression 'else' expression
    inline expression parse_if_expression()
    {
      parse_token(keyword::if_);

      expression condition = parse_expression();

      parse_token(keyword::then);

      expression then_branch = parse_expression();

      parse_token(keyword::else_);

      expression else_branch = parse_expression();

      return if_expression{condition, then_branch, else_branch};
    }

    // for := 'for' identifier '=' expression ',' expression (',' expression)? 'in' expression
    inline expression parse_for_expression()
    {
      parse_token(keyword::for_);

      std::string loop_variable_name = parse_identifier();

      parse_token('=');

      expression begin = parse_expression();

      parse_token(',');

      expression end = parse_expression();

      std::optional<expression> step;
      if(current_token_ == token(','))
      {
        parse_token(',');

        step = parse_expression();
      }

      parse_token(keyword::in);

      expression body = parse_expression();

      return for_expression{loop_variable_name, begin, end, step, body};
    }

    token current_token_;
};


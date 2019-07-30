#pragma once

#include <memory>
#include <vector>
#include <string>
#include "recursive_variant.hpp"

class number
{
  public:
    inline number(double value)
      : value_(value)
    {}

    inline double value() const
    {
      return value_;
    }

  private:
    double value_;
};

class variable
{
  public:
    inline variable(const std::string& name)
      : name_(name)
    {}

    inline const std::string& name() const
    {
      return name_;
    }

  private:
    std::string name_;
};

class binary_operation;

class call;

class if_expression;


using expression = recursive_variant<
  number,
  variable,
  binary_operation,
  call,
  if_expression
>;


class binary_operation
{
  public:
    binary_operation(char op, expression&& lhs, expression&& rhs)
      : operator_(op), lhs_(std::move(lhs)), rhs_(std::move(rhs))
    {}

    char op() const
    {
      return operator_;
    }

    const expression& lhs() const
    {
      return lhs_;
    }

    const expression& rhs() const
    {
      return rhs_;
    }

  private:
    char operator_;
    expression lhs_, rhs_;
};


class if_expression
{
  public:
    if_expression(expression condition, expression then_expression, expression else_expression)
      : condition_(condition),
        then_expression_(then_expression),
        else_expression_(else_expression)
    {}

    const expression& condition() const
    {
      return condition_;
    }

    const expression& then_expression() const
    {
      return then_expression_;
    }

    const expression& else_expression() const
    {
      return else_expression_;
    }

  private:
    expression condition_;
    expression then_expression_;
    expression else_expression_;
};


class call
{
  public:
    call(const std::string& callee_name, const std::vector<expression>& arguments)
      : callee_name_(callee_name), arguments_(arguments)
    {}

    const std::string& callee_name() const
    {
      return callee_name_;
    }

    const std::vector<expression>& arguments() const
    {
      return arguments_;
    }

  private:
    std::string callee_name_;
    std::vector<expression> arguments_;
};


class function_prototype
{
  public:
    function_prototype(const std::string& name, const std::vector<std::string>& parameters)
      : name_(name),
        parameters_(parameters)
    {}

    const std::string& name() const
    {
      return name_;
    }

    const std::vector<std::string>& parameters() const
    {
      return parameters_;
    }

  private:
    std::string name_;
    std::vector<std::string> parameters_;
};


class function
{
  public:
    function(const function_prototype& prototype, const expression& body)
      : prototype_(prototype), body_(body)
    {}

    const function_prototype& prototype() const
    {
      return prototype_;
    }

    const expression& body() const
    {
      return body_;
    }

  private:
    function_prototype prototype_;
    expression body_;
};


using top_level_statement = std::variant<function, function_prototype, expression>;


class program
{
  public:
    program(const std::vector<top_level_statement>& statements)
      : statements_(statements)
    {}

    const std::vector<top_level_statement>& statements() const
    {
      return statements_;
    }

  private:
    std::vector<top_level_statement> statements_;
};


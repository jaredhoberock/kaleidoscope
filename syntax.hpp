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


using expression = recursive_variant<
  number,
  variable,
  binary_operation,
  call
>;


class binary_operation
{
  public:
    binary_operation(char op, expression&& lhs, expression&& rhs)
      : operator_(op), lhs_(std::move(lhs)), rhs_(std::move(rhs))
    {}

  private:
    char operator_;
    expression lhs_, rhs_;
};


class call
{
  public:
    call(const std::string& callee, const std::vector<expression>& arguments)
      : callee_(callee), arguments_(arguments)
    {}

  private:
    std::string callee_;
    std::vector<expression> arguments_;
};


class function_prototype
{
  public:
    function_prototype(const std::string& name, const std::vector<std::string>& parameters)
      : name_(name),
        parameters_(parameters)
    {}

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

  private:
    std::vector<top_level_statement> statements_;
};


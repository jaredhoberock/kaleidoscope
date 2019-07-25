#pragma once

template<class... Types> struct overloaded : Types...
{
  overloaded(Types... overloads)
    : Types(overloads)...
  {}

  using Types::operator()...;
};

template<class... Types> overloaded(Types...) -> overloaded<Types...>;


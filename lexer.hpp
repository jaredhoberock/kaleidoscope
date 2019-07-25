#pragma once

#include <string>
#include <variant>
#include <iostream>

enum class keyword
{
  def,
  extern_,
};

inline std::ostream& operator<<(std::ostream& os, const keyword& kw)
{
  switch(kw)
  {
    case keyword::def:
    {
      os << "def";
      break;
    }

    case keyword::extern_:
    {
      os << "extern";
      break;
    }
  }

  return os;
}

using token = std::variant<keyword, std::string, double, char>;

namespace std
{


inline std::ostream& operator<<(std::ostream& os, const token& t)
{
  std::visit([&](const auto& t)
  {
    os << t;
  },
  t);

  return os;
}


} // end namespace std

token get_token();


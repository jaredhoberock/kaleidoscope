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


inline token get_token()
{
  char last_char = ' ';

  // skip whitespace
  while(isspace(last_char = getchar()))
  {
    ;
  }

  // get an identifier or keyword
  if(isalpha(last_char))
  {
    std::string word(1, last_char);
    while(isalpha((last_char = getchar())))
    {
      word += last_char;
    }

    // put back the last char
    ungetc(last_char, stdin);

    if(word == "def")
    {
      return keyword::def;
    }
    else if(word == "extern")
    {
      return keyword::extern_;
    }

    return word;
  }

  // get a number
  else if(isdigit(last_char) || last_char == '.')
  {
    std::string number(1, last_char);
    while(isdigit(last_char = getchar()) || last_char == '.')
    {
      number += last_char;
    }

    // put back the last char
    ungetc(last_char, stdin);

    // convert the string to a double
    return strtod(number.c_str(), 0);
  }

  // skip comments
  else if(last_char == '#')
  {
    while((last_char = getchar()) != EOF and last_char != '\n' and last_char != '\r')
    {
      ;
    }

    if(last_char != EOF)
    {
      return get_token();
    }
  }

  // check for EOF
  else if(last_char == EOF)
  {
    return token(char(EOF));
  }
  
  // just return the last character
  return last_char;
}


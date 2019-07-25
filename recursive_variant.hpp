#pragma once

#include <variant>
#include <memory>
#include <type_traits>
#include <utility>

namespace detail
{


template<class T>
class wrapped
{
  public:
    template<class... Args,
             class = std::enable_if_t<
               std::is_constructible_v<T,Args&&...>
             >>
    wrapped(Args&&... args)
      : value_(std::make_unique<T>(std::forward<Args>(args)...))
    {}

    wrapped(const T& value)
      : value_(std::make_unique<T>(value))
    {}

    wrapped(T&& value)
      : value_(std::make_unique<T>(std::move(value)))
    {}

    wrapped(wrapped&&) = default;

    wrapped(const wrapped& other)
      : value_(std::make_unique<T>(other.value()))
    {}

    wrapped& operator=(wrapped&&) = default;
    wrapped& operator=(wrapped&) = default;

    T& value()
    {
      return *value_;
    }

    const T& value() const
    {
      return *value_;
    }

  private:
    std::unique_ptr<T> value_;
};


// see https://stackoverflow.com/a/1956217/722294
namespace 
{


template<class T, int discriminator>
struct is_complete {  
  template<class U, int = sizeof(U)>
  static std::true_type test(int);

  template<class>
  static std::false_type test(...);

  static const bool value = decltype(test<T>(0))::value;
};


} // end anonymous namespace

#define IS_COMPLETE(X) is_complete<X,__COUNTER__>::value


template<class T>
using wrap_if_incomplete_t = std::conditional_t<IS_COMPLETE(T), T, wrapped<T>>;


template<class Function>
struct unwrap_and_call
{
  mutable Function f_;

  // forward unwrapped arguments along
  template<class Arg>
  static Arg&& unwrap_if(Arg&& arg)
  {
    return std::forward<Arg>(arg);
  }

  template<class Arg>
  static Arg& unwrap_if(detail::wrapped<Arg>& arg)
  {
    return arg.value();
  }

  template<class Arg>
  static const Arg& unwrap_if(const detail::wrapped<Arg>& arg)
  {
    return arg.value();
  }

  template<class Arg>
  static Arg&& unwrap_if(detail::wrapped<Arg>&& arg)
  {
    return std::move(arg.value());
  }

  // forward unwrapped arguments to f
  template<class... Args>
  decltype(auto) operator()(Args&&... args) const
  {
    return f_(unwrap_if(std::forward<Args>(args))...);
  }
};


template<class Result, class Function>
struct call_and_convert_to
{
  mutable Function f_;

  template<class... Args>
  Result operator()(Args&&... args) const
  {
    return f_(std::forward<Args>(args)...);
  }
};


} // end detail


template<class... Types>
class recursive_variant : public std::variant<detail::wrap_if_incomplete_t<Types>...>
{
  public:
    using super_t = std::variant<detail::wrap_if_incomplete_t<Types>...>;
    using super_t::super_t;

    template<class U,

             std::enable_if_t<
               std::is_constructible_v<super_t, U&&>
             >* = nullptr>
    recursive_variant(U&& value)
      : super_t(std::forward<U>(value))
    {}

    // add a wrapping constructor
    template<class U,

             std::enable_if_t<
               // if std::variant<Types...> is not directly constructible from U...
               !std::is_constructible_v<super_t, U&&> and

               // but std::variant<Types...> *is* constructible from a wrapped<U>...
               std::is_constructible_v<super_t, detail::wrapped<std::decay_t<U>>>
             >* = nullptr>
    recursive_variant(U&& value)
      : super_t(detail::wrapped<std::decay_t<U>>(std::forward<U>(value)))
    {}
};


namespace detail
{


template<class Variant>
constexpr Variant&& super(Variant&& var)
{
  return std::forward<Variant>(var);
}

template<class... Types>
constexpr typename recursive_variant<Types...>::super_t& super(recursive_variant<Types...>& var)
{
  return var;
}

template<class... Types>
constexpr const typename recursive_variant<Types...>::super_t& super(const recursive_variant<Types...>& var)
{
  return var;
}

template<class... Types>
constexpr typename recursive_variant<Types...>::super_t&& super(recursive_variant<Types...>&& var)
{
  return std::move(var);
}


} // end detail


template<class Visitor, class Variant, class... Variants>
constexpr decltype(auto) visit(Visitor&& visitor, Variant&& var, Variants&&... vars)
{
  // unwrap wrapped types before the visitor sees them
  detail::unwrap_and_call<std::decay_t<Visitor>> unwrapping_visitor{std::forward<Visitor>(visitor)};

  // unwrap recursive_variants into std::variant before passing to std::visit
  return std::visit(std::move(unwrapping_visitor), detail::super(std::forward<Variant>(var)), detail::super(std::forward<Variants>(vars))...);
}


template<class Result, class Visitor, class Variant, class... Variants>
constexpr Result visit(Visitor&& visitor, Variant&& var, Variants&&... vars)
{
  // convert this visitor's result type to Result
  detail::call_and_convert_to<Result, std::decay_t<Visitor>> converting_visitor{std::forward<Visitor>(visitor)};

  // forward along to the above overload of visit
  return ::visit(std::move(converting_visitor), std::forward<Variant>(var), std::forward<Variants>(vars)...);
}


// clean up after ourself
#undef IS_COMPLETE


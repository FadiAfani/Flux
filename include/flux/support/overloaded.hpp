#pragma once

namespace flux::support {

template <typename... Visitors> struct Overloaded : Visitors... {
  using Visitors::operator()...;
};

template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

} // namespace flux::support

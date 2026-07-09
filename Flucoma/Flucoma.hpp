#pragma once
#include <avnd/concepts/all.hpp>
#include <avnd/wrappers/controls.hpp>

namespace Flucoma
{

struct Flucoma
{
  static consteval auto name() { return "Flucoma"; }
  static consteval auto c_name() { return "flucoma"; }
  static consteval auto uuid() { return "04545931-4450-4466-AA77-56F174F79F01"; }

  struct inputs
  {
    // TODO: add FluCoMa parameters here
  };

  struct outputs
  {
    // TODO: add outputs here
  };

  void operator()(int frames) { }
};

}

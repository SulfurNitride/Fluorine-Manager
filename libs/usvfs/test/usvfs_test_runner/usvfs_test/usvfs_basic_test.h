#pragma once

#include "usvfs_test_base.h"

class usvfs_basic_test : public usvfs_test_base
{
public:
  static constexpr auto SCENARIO_NAME = "basic";

  usvfs_basic_test(const usvfs_test_options& options) : usvfs_test_base(options) {}

  virtual const char* scenario_name();
  virtual bool scenario_run();
};

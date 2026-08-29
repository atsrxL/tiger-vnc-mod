/* Copyright 2026 TigerVNC contributors
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#include <string>

#include <gtest/gtest.h>

#include "parameters.h"

namespace {

class TemporaryConfig
{
public:
  TemporaryConfig()
    : path(std::string(testing::TempDir()) +
           "tigervnc-viewerparameters.tigervnc")
  {
    remove(path.c_str());
  }

  ~TemporaryConfig()
  {
    remove(path.c_str());
  }

  std::string path;
};

}

TEST(ViewerParameters, settingsRoundTripThroughConfigurationFile)
{
  TemporaryConfig config;

  autoSelect.setParam(false);
  fullScreenScaleToFit.setParam(true);
  windowedScaleToFit.setParam(true);

  saveViewerParameters(config.path.c_str(), "example.test::5999");

  autoSelect.setParam(true);
  fullScreenScaleToFit.setParam(false);
  windowedScaleToFit.setParam(false);

  const char* serverName = loadViewerParameters(config.path.c_str());

  ASSERT_NE(serverName, nullptr);
  EXPECT_STREQ(serverName, "example.test::5999");
  EXPECT_FALSE(autoSelect);
  EXPECT_TRUE(fullScreenScaleToFit);
  EXPECT_TRUE(windowedScaleToFit);
}

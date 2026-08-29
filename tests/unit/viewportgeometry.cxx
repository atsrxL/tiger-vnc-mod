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

#include <gtest/gtest.h>

#include "ViewportGeometry.h"

TEST(ViewportGeometry, selectsModeSpecificScaleOption)
{
  EXPECT_FALSE(viewportgeometry::scaleToFitEnabled(false, false, false));
  EXPECT_TRUE(viewportgeometry::scaleToFitEnabled(false, false, true));
  EXPECT_FALSE(viewportgeometry::scaleToFitEnabled(true, false, true));
  EXPECT_TRUE(viewportgeometry::scaleToFitEnabled(true, true, false));
}

TEST(ViewportGeometry, downscalesAndPreservesAspectRatio)
{
  core::Point size = viewportgeometry::scaleToFit(
    3840, 2160, 1280, 800, false);
  EXPECT_EQ(size.x, 1280);
  EXPECT_EQ(size.y, 720);

  size = viewportgeometry::scaleToFit(1600, 1200, 1000, 500, false);
  EXPECT_EQ(size.x, 666);
  EXPECT_EQ(size.y, 500);
}

TEST(ViewportGeometry, windowedModeNeverUpscales)
{
  core::Point size = viewportgeometry::scaleToFit(
    1280, 720, 1920, 1080, false);
  EXPECT_EQ(size.x, 1280);
  EXPECT_EQ(size.y, 720);
}

TEST(ViewportGeometry, fullscreenModeCanUpscale)
{
  core::Point size = viewportgeometry::scaleToFit(
    1280, 720, 1920, 1200, true);
  EXPECT_EQ(size.x, 1920);
  EXPECT_EQ(size.y, 1080);
}

TEST(ViewportGeometry, fullscreenModeCanDownscale)
{
  core::Point size = viewportgeometry::scaleToFit(
    3840, 2160, 1920, 1080, true);
  EXPECT_EQ(size.x, 1920);
  EXPECT_EQ(size.y, 1080);
}

TEST(ViewportGeometry, downscalesWhenOnlyOneAxisExceedsWindow)
{
  core::Point size = viewportgeometry::scaleToFit(
    2000, 1000, 3000, 800, false);
  EXPECT_EQ(size.x, 1600);
  EXPECT_EQ(size.y, 800);
}

TEST(ViewportGeometry, usesWideScaleIntermediates)
{
  core::Point size = viewportgeometry::scaleToFit(
    2000000000, 1500000000, 1500000000, 1400000000, false);
  EXPECT_EQ(size.x, 1500000000);
  EXPECT_EQ(size.y, 1125000000);
}

TEST(ViewportGeometry, clampsTinyAvailableArea)
{
  core::Point size = viewportgeometry::scaleToFit(
    1920, 1080, 0, 0, false);
  EXPECT_EQ(size.x, 1);
  EXPECT_EQ(size.y, 1);
}

TEST(ViewportGeometry, mapsScaledPointerCoordinates)
{
  core::Point remote = viewportgeometry::localToRemote(
    {640, 360}, 1920, 1080, 1280, 720);
  EXPECT_EQ(remote.x, 960);
  EXPECT_EQ(remote.y, 540);

  core::Point local = viewportgeometry::remoteToLocal(
    {960, 540}, 1920, 1080, 1280, 720);
  EXPECT_EQ(local.x, 640);
  EXPECT_EQ(local.y, 360);
}

TEST(ViewportGeometry, clampsPointerOutsideDisplay)
{
  core::Point remote = viewportgeometry::localToRemote(
    {-10, 800}, 1920, 1080, 1280, 720);
  EXPECT_EQ(remote.x, 0);
  EXPECT_EQ(remote.y, 1079);
}

TEST(ViewportGeometry, handlesSinglePixelCoordinateRanges)
{
  core::Point remote = viewportgeometry::localToRemote(
    {0, 0}, 1920, 1080, 1, 1);
  EXPECT_EQ(remote.x, 0);
  EXPECT_EQ(remote.y, 0);

  core::Point local = viewportgeometry::remoteToLocal(
    {1919, 1079}, 1920, 1080, 1, 1);
  EXPECT_EQ(local.x, 0);
  EXPECT_EQ(local.y, 0);
}

TEST(ViewportGeometry, mapsBothDisplayEdgesToFramebufferEdges)
{
  core::Point remote = viewportgeometry::localToRemote(
    {1279, 719}, 3840, 2160, 1280, 720);
  EXPECT_EQ(remote.x, 3839);
  EXPECT_EQ(remote.y, 2159);

  core::Point local = viewportgeometry::remoteToLocal(
    {3839, 2159}, 3840, 2160, 1280, 720);
  EXPECT_EQ(local.x, 1279);
  EXPECT_EQ(local.y, 719);
}

TEST(ViewportGeometry, usesWideMappingIntermediates)
{
  core::Point remote = viewportgeometry::localToRemote(
    {900000000, 600000000},
    1500000000, 1000000000,
    1000000000, 800000000);
  EXPECT_EQ(remote.x, 1350000000);
  EXPECT_EQ(remote.y, 750000000);
}

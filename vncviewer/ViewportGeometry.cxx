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

#include "ViewportGeometry.h"

#include <algorithm>
#include <stdint.h>

namespace {

int mapCoordinate(int position, int sourceExtent, int destinationExtent)
{
  if (sourceExtent <= 1 || destinationExtent <= 1)
    return 0;
  if (position <= 0)
    return 0;
  if (position >= sourceExtent - 1)
    return destinationExtent - 1;

  // Map inclusive pixel ranges so both edges of a downscaled framebuffer
  // remain reachable by pointer input and server-driven cursor movement.
  return (int)(((int64_t)position * (destinationExtent - 1) +
                (sourceExtent - 1) / 2) /
               (sourceExtent - 1));
}

}

bool viewportgeometry::scaleToFitEnabled(bool fullscreen,
                                         bool fullScreenScale,
                                         bool windowedScale)
{
  return fullscreen ? fullScreenScale : windowedScale;
}

core::Point viewportgeometry::scaleToFit(int framebufferWidth,
                                         int framebufferHeight,
                                         int availableWidth,
                                         int availableHeight,
                                         bool allowUpscale)
{
  if (framebufferWidth <= 0 || framebufferHeight <= 0)
    return {1, 1};

  availableWidth = std::max(1, availableWidth);
  availableHeight = std::max(1, availableHeight);

  if (!allowUpscale && framebufferWidth <= availableWidth &&
      framebufferHeight <= availableHeight) {
    return {framebufferWidth, framebufferHeight};
  }

  core::Point result;
  if ((int64_t)availableWidth * framebufferHeight <=
      (int64_t)availableHeight * framebufferWidth) {
    result.x = availableWidth;
    result.y = (int)((int64_t)framebufferHeight * availableWidth /
                     framebufferWidth);
  } else {
    result.x = (int)((int64_t)framebufferWidth * availableHeight /
                     framebufferHeight);
    result.y = availableHeight;
  }

  result.x = std::max(1, result.x);
  result.y = std::max(1, result.y);
  return result;
}

core::Point viewportgeometry::remoteToLocal(const core::Point& position,
                                            int framebufferWidth,
                                            int framebufferHeight,
                                            int displayWidth,
                                            int displayHeight)
{
  if (framebufferWidth <= 0 || framebufferHeight <= 0 ||
      displayWidth <= 0 || displayHeight <= 0) {
    return {0, 0};
  }

  return {
    mapCoordinate(position.x, framebufferWidth, displayWidth),
    mapCoordinate(position.y, framebufferHeight, displayHeight)
  };
}

core::Point viewportgeometry::localToRemote(const core::Point& position,
                                            int framebufferWidth,
                                            int framebufferHeight,
                                            int displayWidth,
                                            int displayHeight)
{
  if (framebufferWidth <= 0 || framebufferHeight <= 0 ||
      displayWidth <= 0 || displayHeight <= 0) {
    return {0, 0};
  }

  return {
    mapCoordinate(position.x, displayWidth, framebufferWidth),
    mapCoordinate(position.y, displayHeight, framebufferHeight)
  };
}

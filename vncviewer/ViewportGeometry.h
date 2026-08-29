/* Copyright 2026 TigerVNC contributors
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __VIEWPORTGEOMETRY_H__
#define __VIEWPORTGEOMETRY_H__

#include <core/Rect.h>

namespace viewportgeometry {

  bool scaleToFitEnabled(bool fullscreen, bool fullScreenScale,
                         bool windowedScale);

  core::Point scaleToFit(int framebufferWidth, int framebufferHeight,
                         int availableWidth, int availableHeight,
                         bool allowUpscale);

  core::Point remoteToLocal(const core::Point& position,
                            int framebufferWidth, int framebufferHeight,
                            int displayWidth, int displayHeight);

  core::Point localToRemote(const core::Point& position,
                            int framebufferWidth, int framebufferHeight,
                            int displayWidth, int displayHeight);

}

#endif

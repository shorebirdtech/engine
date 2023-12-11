// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "display_list/dl_color.h"
#include "display_list/effects/dl_color_source.h"
#include "impeller/core/formats.h"
#include "impeller/geometry/color.h"
#include "impeller/geometry/path.h"
#include "impeller/geometry/path_builder.h"
#include "impeller/geometry/point.h"
#include "impeller/geometry/rect.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkColorType.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "third_party/skia/include/core/SkRSXform.h"
#include "third_party/skia/include/core/SkTextBlob.h"

namespace impeller {
namespace skia_conversions {

Rect ToRect(const SkRect& rect);

std::optional<Rect> ToRect(const SkRect* rect);

std::vector<Rect> ToRects(const SkRect tex[], int count);

std::vector<Point> ToPoints(const SkPoint points[], int count);

Point ToPoint(const SkPoint& point);

Color ToColor(const flutter::DlColor& color);

std::vector<Matrix> ToRSXForms(const SkRSXform xform[], int count);

PathBuilder::RoundingRadii ToRoundingRadii(const SkRRect& rrect);

Path ToPath(const SkPath& path, Point shift = Point(0, 0));

Path ToPath(const SkRRect& rrect);

Path PathDataFromTextBlob(const sk_sp<SkTextBlob>& blob,
                          Point shift = Point(0, 0));

std::optional<impeller::PixelFormat> ToPixelFormat(SkColorType type);

/// @brief Convert display list colors + stops into impeller colors and stops,
/// taking care to ensure that the stops monotonically increase from 0.0 to 1.0.
///
/// The general process is:
/// * Ensure that the first gradient stop value is 0.0. If not, insert a new
///   stop with a value of 0.0 and use the first gradient color as this new
///   stops color.
/// * Ensure the last gradient stop value is 1.0. If not, insert a new stop
///   with a value of 1.0 and use the last gradient color as this stops color.
/// * Clamp all gradient values between the values of 0.0 and 1.0.
/// * For all stop values, ensure that the values are monotonically increasing
///   by clamping each value to a minimum of the previous stop value and itself.
///   For example, with stop values of 0.0, 0.5, 0.4, 1.0, we would clamp such
///   that the values were 0.0, 0.5, 0.5, 1.0.
void ConvertStops(const flutter::DlGradientColorSourceBase* gradient,
                  std::vector<Color>& colors,
                  std::vector<float>& stops);

}  // namespace skia_conversions
}  // namespace impeller

// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "impeller/entity/contents/filters/inputs/filter_input.h"

namespace impeller {

class PlaceholderFilterInput final : public FilterInput {
 public:
  explicit PlaceholderFilterInput(Rect coverage);

  ~PlaceholderFilterInput() override;

  // |FilterInput|
  Variant GetInput() const override;

  // |FilterInput|
  std::optional<Snapshot> GetSnapshot(
      const std::string& label,
      const ContentContext& renderer,
      const Entity& entity,
      std::optional<Rect> coverage_limit) const override;

  // |FilterInput|
  std::optional<Rect> GetCoverage(const Entity& entity) const override;

  // |FilterInput|
  void PopulateGlyphAtlas(
      const std::shared_ptr<LazyGlyphAtlas>& lazy_glyph_atlas,
      Scalar scale) override;

 private:
  Rect coverage_rect_;

  friend FilterInput;
};

}  // namespace impeller

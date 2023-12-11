// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <optional>
#include <set>

#include "flutter/fml/macros.h"
#include "impeller/renderer/backend/vulkan/command_pool_vk.h"
#include "impeller/renderer/backend/vulkan/context_vk.h"
#include "impeller/renderer/backend/vulkan/descriptor_pool_vk.h"
#include "impeller/renderer/backend/vulkan/device_holder.h"
#include "impeller/renderer/backend/vulkan/queue_vk.h"
#include "impeller/renderer/backend/vulkan/shared_object_vk.h"
#include "impeller/renderer/backend/vulkan/vk.h"

namespace impeller {

class ContextVK;
class DeviceBuffer;
class Buffer;
class Texture;
class TextureSourceVK;
class TrackedObjectsVK;
class FenceWaiterVK;

class CommandEncoderFactoryVK {
 public:
  CommandEncoderFactoryVK(const std::weak_ptr<const ContextVK>& context);

  std::shared_ptr<CommandEncoderVK> Create();

  void SetLabel(const std::string& label);

 private:
  std::weak_ptr<const ContextVK> context_;
  std::optional<std::string> label_;

  FML_DISALLOW_COPY_AND_ASSIGN(CommandEncoderFactoryVK);
};

class CommandEncoderVK {
 public:
  using SubmitCallback = std::function<void(bool)>;

  // Visible for testing.
  CommandEncoderVK(std::weak_ptr<const DeviceHolder> device_holder,
                   std::shared_ptr<TrackedObjectsVK> tracked_objects,
                   const std::shared_ptr<QueueVK>& queue,
                   std::shared_ptr<FenceWaiterVK> fence_waiter);

  ~CommandEncoderVK();

  bool IsValid() const;

  bool Submit(SubmitCallback callback = {});

  bool Track(std::shared_ptr<SharedObjectVK> object);

  bool Track(std::shared_ptr<const Buffer> buffer);

  bool IsTracking(const std::shared_ptr<const Buffer>& texture) const;

  bool Track(const std::shared_ptr<const Texture>& texture);

  bool IsTracking(const std::shared_ptr<const Texture>& texture) const;

  bool Track(std::shared_ptr<const TextureSourceVK> texture);

  vk::CommandBuffer GetCommandBuffer() const;

  void PushDebugGroup(const char* label) const;

  void PopDebugGroup() const;

  void InsertDebugMarker(const char* label) const;

  std::optional<vk::DescriptorSet> AllocateDescriptorSet(
      const vk::DescriptorSetLayout& layout,
      size_t command_count);

 private:
  friend class ContextVK;

  std::weak_ptr<const DeviceHolder> device_holder_;
  std::shared_ptr<TrackedObjectsVK> tracked_objects_;
  std::shared_ptr<QueueVK> queue_;
  const std::shared_ptr<FenceWaiterVK> fence_waiter_;
  bool is_valid_ = true;

  void Reset();

  FML_DISALLOW_COPY_AND_ASSIGN(CommandEncoderVK);
};

}  // namespace impeller

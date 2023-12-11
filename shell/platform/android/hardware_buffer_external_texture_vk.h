// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_ANDROID_HARDWARE_BUFFER_EXTERNAL_TEXTURE_VK_H_
#define FLUTTER_SHELL_PLATFORM_ANDROID_HARDWARE_BUFFER_EXTERNAL_TEXTURE_VK_H_

#include "flutter/shell/platform/android/hardware_buffer_external_texture.h"

#include "flutter/impeller/renderer/backend/vulkan/android_hardware_buffer_texture_source_vk.h"
#include "flutter/impeller/renderer/backend/vulkan/context_vk.h"
#include "flutter/impeller/renderer/backend/vulkan/vk.h"
#include "flutter/shell/platform/android/android_context_vulkan_impeller.h"

namespace flutter {

class HardwareBufferExternalTextureVK : public HardwareBufferExternalTexture {
 public:
  HardwareBufferExternalTextureVK(
      const std::shared_ptr<impeller::ContextVK>& impeller_context,
      int64_t id,
      const fml::jni::ScopedJavaGlobalRef<jobject>&
          hardware_buffer_texture_entry,
      const std::shared_ptr<PlatformViewAndroidJNI>& jni_facade);

  ~HardwareBufferExternalTextureVK() override;

 private:
  void ProcessFrame(PaintContext& context, const SkRect& bounds) override;
  void Detach() override;

  const std::shared_ptr<impeller::ContextVK> impeller_context_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_ANDROID_HARDWARE_BUFFER_EXTERNAL_TEXTURE_VK_H_

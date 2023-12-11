// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/renderer/backend/vulkan/surface_context_vk.h"

#include "flutter/fml/trace_event.h"
#include "impeller/renderer/backend/vulkan/command_pool_vk.h"
#include "impeller/renderer/backend/vulkan/context_vk.h"
#include "impeller/renderer/backend/vulkan/swapchain_vk.h"

namespace impeller {

SurfaceContextVK::SurfaceContextVK(const std::shared_ptr<ContextVK>& parent)
    : parent_(parent) {}

SurfaceContextVK::~SurfaceContextVK() = default;

Context::BackendType SurfaceContextVK::GetBackendType() const {
  return parent_->GetBackendType();
}

std::string SurfaceContextVK::DescribeGpuModel() const {
  return parent_->DescribeGpuModel();
}

bool SurfaceContextVK::IsValid() const {
  return parent_->IsValid();
}

std::shared_ptr<Allocator> SurfaceContextVK::GetResourceAllocator() const {
  return parent_->GetResourceAllocator();
}

std::shared_ptr<ShaderLibrary> SurfaceContextVK::GetShaderLibrary() const {
  return parent_->GetShaderLibrary();
}

std::shared_ptr<SamplerLibrary> SurfaceContextVK::GetSamplerLibrary() const {
  return parent_->GetSamplerLibrary();
}

std::shared_ptr<PipelineLibrary> SurfaceContextVK::GetPipelineLibrary() const {
  return parent_->GetPipelineLibrary();
}

std::shared_ptr<CommandBuffer> SurfaceContextVK::CreateCommandBuffer() const {
  return parent_->CreateCommandBuffer();
}

const std::shared_ptr<const Capabilities>& SurfaceContextVK::GetCapabilities()
    const {
  return parent_->GetCapabilities();
}

void SurfaceContextVK::Shutdown() {
  parent_->Shutdown();
}

bool SurfaceContextVK::SetWindowSurface(vk::UniqueSurfaceKHR surface) {
  auto swapchain = SwapchainVK::Create(parent_, std::move(surface));
  if (!swapchain) {
    VALIDATION_LOG << "Could not create swapchain.";
    return false;
  }
  swapchain_ = std::move(swapchain);
  return true;
}

std::unique_ptr<Surface> SurfaceContextVK::AcquireNextSurface() {
  TRACE_EVENT0("impeller", __FUNCTION__);
  auto surface = swapchain_ ? swapchain_->AcquireNextDrawable() : nullptr;
  if (!surface) {
    return nullptr;
  }
  if (auto pipeline_library = parent_->GetPipelineLibrary()) {
    impeller::PipelineLibraryVK::Cast(*pipeline_library)
        .DidAcquireSurfaceFrame();
  }
  if (auto allocator = parent_->GetResourceAllocator()) {
    allocator->DidAcquireSurfaceFrame();
  }
  parent_->GetCommandPoolRecycler()->Dispose();
  return surface;
}

void SurfaceContextVK::SetSyncPresentation(bool value) {
  parent_->SetSyncPresentation(value);
}

#ifdef FML_OS_ANDROID

vk::UniqueSurfaceKHR SurfaceContextVK::CreateAndroidSurface(
    ANativeWindow* window) const {
  if (!parent_->GetInstance()) {
    return vk::UniqueSurfaceKHR{VK_NULL_HANDLE};
  }

  auto create_info = vk::AndroidSurfaceCreateInfoKHR().setWindow(window);
  auto surface_res =
      parent_->GetInstance().createAndroidSurfaceKHRUnique(create_info);

  if (surface_res.result != vk::Result::eSuccess) {
    VALIDATION_LOG << "Could not create Android surface, error: "
                   << vk::to_string(surface_res.result);
    return vk::UniqueSurfaceKHR{VK_NULL_HANDLE};
  }

  return std::move(surface_res.value);
}

#endif  // FML_OS_ANDROID

}  // namespace impeller

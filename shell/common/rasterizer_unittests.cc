// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define FML_USED_ON_EMBEDDER

#include "flutter/shell/common/rasterizer.h"

#include <memory>
#include <optional>

#include "flutter/flow/frame_timings.h"
#include "flutter/fml/synchronization/count_down_latch.h"
#include "flutter/fml/time/time_point.h"
#include "flutter/shell/common/thread_host.h"
#include "flutter/testing/testing.h"

#include "third_party/skia/include/core/SkColorSpace.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/gpu/GrTypes.h"
#include "third_party/skia/include/gpu/ganesh/SkSurfaceGanesh.h"

#include "gmock/gmock.h"

using testing::_;
using testing::ByMove;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace flutter {
namespace {

constexpr float kDevicePixelRatio = 2.0f;

class MockDelegate : public Rasterizer::Delegate {
 public:
  MOCK_METHOD(void,
              OnFrameRasterized,
              (const FrameTiming& frame_timing),
              (override));
  MOCK_METHOD(fml::Milliseconds, GetFrameBudget, (), (override));
  MOCK_METHOD(fml::TimePoint, GetLatestFrameTargetTime, (), (const, override));
  MOCK_METHOD(const TaskRunners&, GetTaskRunners, (), (const, override));
  MOCK_METHOD(const fml::RefPtr<fml::RasterThreadMerger>,
              GetParentRasterThreadMerger,
              (),
              (const, override));
  MOCK_METHOD(std::shared_ptr<const fml::SyncSwitch>,
              GetIsGpuDisabledSyncSwitch,
              (),
              (const, override));
  MOCK_METHOD(const Settings&, GetSettings, (), (const, override));
  MOCK_METHOD(bool,
              ShouldDiscardLayerTree,
              (int64_t, const flutter::LayerTree&),
              (override));
};

class MockSurface : public Surface {
 public:
  MOCK_METHOD(bool, IsValid, (), (override));
  MOCK_METHOD(std::unique_ptr<SurfaceFrame>,
              AcquireFrame,
              (const SkISize& size),
              (override));
  MOCK_METHOD(SkMatrix, GetRootTransformation, (), (const, override));
  MOCK_METHOD(GrDirectContext*, GetContext, (), (override));
  MOCK_METHOD(std::unique_ptr<GLContextResult>,
              MakeRenderContextCurrent,
              (),
              (override));
  MOCK_METHOD(bool, ClearRenderContext, (), (override));
  MOCK_METHOD(bool, AllowsDrawingWhenGpuDisabled, (), (const, override));
};

class MockExternalViewEmbedder : public ExternalViewEmbedder {
 public:
  MOCK_METHOD(DlCanvas*, GetRootCanvas, (), (override));
  MOCK_METHOD(void, CancelFrame, (), (override));
  MOCK_METHOD(void,
              BeginFrame,
              (SkISize frame_size,
               GrDirectContext* context,
               double device_pixel_ratio,
               fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger),
              (override));
  MOCK_METHOD(void,
              PrerollCompositeEmbeddedView,
              (int64_t view_id, std::unique_ptr<EmbeddedViewParams> params),
              (override));
  MOCK_METHOD(PostPrerollResult,
              PostPrerollAction,
              (fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger),
              (override));
  MOCK_METHOD(DlCanvas*, CompositeEmbeddedView, (int64_t view_id), (override));
  MOCK_METHOD(void,
              SubmitFrame,
              (GrDirectContext * context,
               const std::shared_ptr<impeller::AiksContext>& aiks_context,
               std::unique_ptr<SurfaceFrame> frame),
              (override));
  MOCK_METHOD(void,
              EndFrame,
              (bool should_resubmit_frame,
               fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger),
              (override));
  MOCK_METHOD(bool, SupportsDynamicThreadMerging, (), (override));
};
}  // namespace

TEST(RasterizerTest, create) {
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  EXPECT_TRUE(rasterizer != nullptr);
}

static std::unique_ptr<FrameTimingsRecorder> CreateFinishedBuildRecorder(
    fml::TimePoint timestamp) {
  std::unique_ptr<FrameTimingsRecorder> recorder =
      std::make_unique<FrameTimingsRecorder>();
  recorder->RecordVsync(timestamp, timestamp);
  recorder->RecordBuildStart(timestamp);
  recorder->RecordBuildEnd(timestamp);
  return recorder;
}

static std::unique_ptr<FrameTimingsRecorder> CreateFinishedBuildRecorder() {
  return CreateFinishedBuildRecorder(fml::TimePoint::Now());
}

TEST(RasterizerTest, drawEmptyPipeline) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  ON_CALL(delegate, GetTaskRunners()).WillByDefault(ReturnRef(task_runners));
  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();
  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillOnce(Return(ByMove(std::make_unique<GLContextDefaultResult>(true))));
  rasterizer->Setup(std::move(surface));
  fml::AutoResetWaitableEvent latch;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    rasterizer->Draw(pipeline);
    latch.Signal();
  });
  latch.Wait();
}

TEST(RasterizerTest,
     drawWithExternalViewEmbedderExternalViewEmbedderSubmitFrameCalled) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));
  EXPECT_CALL(delegate, OnFrameRasterized(_));
  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();

  std::shared_ptr<NiceMock<MockExternalViewEmbedder>> external_view_embedder =
      std::make_shared<NiceMock<MockExternalViewEmbedder>>();
  rasterizer->SetExternalViewEmbedder(external_view_embedder);

  SurfaceFrame::FramebufferInfo framebuffer_info;
  framebuffer_info.supports_readback = true;

  auto surface_frame = std::make_unique<SurfaceFrame>(
      /*surface=*/nullptr, framebuffer_info,
      /*submit_callback=*/[](const SurfaceFrame&, DlCanvas*) { return true; },
      /*frame_size=*/SkISize::Make(800, 600));
  EXPECT_CALL(*surface, AllowsDrawingWhenGpuDisabled()).WillOnce(Return(true));
  EXPECT_CALL(*surface, AcquireFrame(SkISize()))
      .WillOnce(Return(ByMove(std::move(surface_frame))));
  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillOnce(Return(ByMove(std::make_unique<GLContextDefaultResult>(true))));

  EXPECT_CALL(*external_view_embedder,
              BeginFrame(/*frame_size=*/SkISize(), /*context=*/nullptr,
                         /*device_pixel_ratio=*/2.0,
                         /*raster_thread_merger=*/
                         fml::RefPtr<fml::RasterThreadMerger>(nullptr)))
      .Times(1);
  EXPECT_CALL(*external_view_embedder, SubmitFrame).Times(1);
  EXPECT_CALL(
      *external_view_embedder,
      EndFrame(/*should_resubmit_frame=*/false,
               /*raster_thread_merger=*/fml::RefPtr<fml::RasterThreadMerger>(
                   nullptr)))
      .Times(1);

  rasterizer->Setup(std::move(surface));
  fml::AutoResetWaitableEvent latch;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    auto layer_tree =
        std::make_unique<LayerTree>(/*config=*/LayerTree::Config(),
                                    /*frame_size=*/SkISize());
    auto layer_tree_item = std::make_unique<LayerTreeItem>(
        std::move(layer_tree), CreateFinishedBuildRecorder(),
        kDevicePixelRatio);
    PipelineProduceResult result =
        pipeline->Produce().Complete(std::move(layer_tree_item));
    EXPECT_TRUE(result.success);
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    rasterizer->Draw(pipeline);
    latch.Signal();
  });
  latch.Wait();
}

TEST(
    RasterizerTest,
    drawWithExternalViewEmbedderAndThreadMergerNotMergedExternalViewEmbedderSubmitFrameNotCalled) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));
  EXPECT_CALL(delegate, OnFrameRasterized(_));
  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();
  std::shared_ptr<NiceMock<MockExternalViewEmbedder>> external_view_embedder =
      std::make_shared<NiceMock<MockExternalViewEmbedder>>();
  rasterizer->SetExternalViewEmbedder(external_view_embedder);
  EXPECT_CALL(*external_view_embedder, SupportsDynamicThreadMerging)
      .WillRepeatedly(Return(true));
  SurfaceFrame::FramebufferInfo framebuffer_info;
  framebuffer_info.supports_readback = true;
  auto surface_frame = std::make_unique<SurfaceFrame>(
      /*surface=*/nullptr, framebuffer_info,
      /*submit_callback=*/[](const SurfaceFrame&, DlCanvas*) { return true; },
      /*frame_size=*/SkISize::Make(800, 600));
  EXPECT_CALL(*surface, AllowsDrawingWhenGpuDisabled()).WillOnce(Return(true));
  EXPECT_CALL(*surface, AcquireFrame(SkISize()))
      .WillOnce(Return(ByMove(std::move(surface_frame))));
  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillOnce(Return(ByMove(std::make_unique<GLContextDefaultResult>(true))));

  EXPECT_CALL(*external_view_embedder,
              BeginFrame(/*frame_size=*/SkISize(), /*context=*/nullptr,
                         /*device_pixel_ratio=*/2.0,
                         /*raster_thread_merger=*/_))
      .Times(1);
  EXPECT_CALL(*external_view_embedder, SubmitFrame).Times(0);
  EXPECT_CALL(*external_view_embedder, EndFrame(/*should_resubmit_frame=*/false,
                                                /*raster_thread_merger=*/_))
      .Times(1);

  rasterizer->Setup(std::move(surface));
  fml::AutoResetWaitableEvent latch;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    auto layer_tree = std::make_unique<LayerTree>(
        /*config=*/LayerTree::Config(), /*frame_size=*/SkISize());
    auto layer_tree_item = std::make_unique<LayerTreeItem>(
        std::move(layer_tree), CreateFinishedBuildRecorder(),
        kDevicePixelRatio);
    PipelineProduceResult result =
        pipeline->Produce().Complete(std::move(layer_tree_item));
    EXPECT_TRUE(result.success);
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    rasterizer->Draw(pipeline);
    latch.Signal();
  });
  latch.Wait();
}

TEST(
    RasterizerTest,
    drawWithExternalViewEmbedderAndThreadsMergedExternalViewEmbedderSubmitFrameCalled) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  fml::MessageLoop::EnsureInitializedForCurrentThread();
  TaskRunners task_runners("test",
                           fml::MessageLoop::GetCurrent().GetTaskRunner(),
                           fml::MessageLoop::GetCurrent().GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());

  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));
  EXPECT_CALL(delegate, OnFrameRasterized(_));

  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();

  std::shared_ptr<NiceMock<MockExternalViewEmbedder>> external_view_embedder =
      std::make_shared<NiceMock<MockExternalViewEmbedder>>();
  rasterizer->SetExternalViewEmbedder(external_view_embedder);

  SurfaceFrame::FramebufferInfo framebuffer_info;
  framebuffer_info.supports_readback = true;

  auto surface_frame = std::make_unique<SurfaceFrame>(
      /*surface=*/nullptr, framebuffer_info,
      /*submit_callback=*/[](const SurfaceFrame&, DlCanvas*) { return true; },
      /*frame_size=*/SkISize::Make(800, 600));
  EXPECT_CALL(*surface, AllowsDrawingWhenGpuDisabled()).WillOnce(Return(true));
  EXPECT_CALL(*surface, AcquireFrame(SkISize()))
      .WillOnce(Return(ByMove(std::move(surface_frame))));
  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillOnce(Return(ByMove(std::make_unique<GLContextDefaultResult>(true))));
  EXPECT_CALL(*external_view_embedder, SupportsDynamicThreadMerging)
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*external_view_embedder,
              BeginFrame(/*frame_size=*/SkISize(), /*context=*/nullptr,
                         /*device_pixel_ratio=*/2.0,
                         /*raster_thread_merger=*/_))
      .Times(1);
  EXPECT_CALL(*external_view_embedder, SubmitFrame).Times(1);
  EXPECT_CALL(*external_view_embedder, EndFrame(/*should_resubmit_frame=*/false,
                                                /*raster_thread_merger=*/_))
      .Times(1);

  rasterizer->Setup(std::move(surface));

  auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
  auto layer_tree = std::make_unique<LayerTree>(/*config=*/LayerTree::Config(),
                                                /*frame_size=*/SkISize());
  auto layer_tree_item = std::make_unique<LayerTreeItem>(
      std::move(layer_tree), CreateFinishedBuildRecorder(), kDevicePixelRatio);
  PipelineProduceResult result =
      pipeline->Produce().Complete(std::move(layer_tree_item));
  EXPECT_TRUE(result.success);
  ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
  rasterizer->Draw(pipeline);
}

TEST(RasterizerTest,
     drawLastLayerTreeWithThreadsMergedExternalViewEmbedderAndEndFrameCalled) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  fml::MessageLoop::EnsureInitializedForCurrentThread();
  TaskRunners task_runners("test",
                           fml::MessageLoop::GetCurrent().GetTaskRunner(),
                           fml::MessageLoop::GetCurrent().GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());

  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));
  EXPECT_CALL(delegate, OnFrameRasterized(_));

  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();

  std::shared_ptr<NiceMock<MockExternalViewEmbedder>> external_view_embedder =
      std::make_shared<NiceMock<MockExternalViewEmbedder>>();
  rasterizer->SetExternalViewEmbedder(external_view_embedder);

  SurfaceFrame::FramebufferInfo framebuffer_info;
  framebuffer_info.supports_readback = true;

  auto surface_frame1 = std::make_unique<SurfaceFrame>(
      /*surface=*/nullptr, framebuffer_info,
      /*submit_callback=*/[](const SurfaceFrame&, DlCanvas*) { return true; },
      /*frame_size=*/SkISize::Make(800, 600));
  auto surface_frame2 = std::make_unique<SurfaceFrame>(
      /*surface=*/nullptr, framebuffer_info,
      /*submit_callback=*/[](const SurfaceFrame&, DlCanvas*) { return true; },
      /*frame_size=*/SkISize::Make(800, 600));
  EXPECT_CALL(*surface, AllowsDrawingWhenGpuDisabled())
      .WillRepeatedly(Return(true));
  // Prepare two frames for Draw() and DrawLastLayerTree().
  EXPECT_CALL(*surface, AcquireFrame(SkISize()))
      .WillOnce(Return(ByMove(std::move(surface_frame1))))
      .WillOnce(Return(ByMove(std::move(surface_frame2))));
  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillOnce(Return(ByMove(std::make_unique<GLContextDefaultResult>(true))));
  EXPECT_CALL(*external_view_embedder, SupportsDynamicThreadMerging)
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*external_view_embedder,
              BeginFrame(/*frame_size=*/SkISize(), /*context=*/nullptr,
                         /*device_pixel_ratio=*/2.0,
                         /*raster_thread_merger=*/_))
      .Times(2);
  EXPECT_CALL(*external_view_embedder, SubmitFrame).Times(2);
  EXPECT_CALL(*external_view_embedder, EndFrame(/*should_resubmit_frame=*/false,
                                                /*raster_thread_merger=*/_))
      .Times(2);

  rasterizer->Setup(std::move(surface));

  auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
  auto layer_tree = std::make_unique<LayerTree>(/*config=*/LayerTree::Config(),
                                                /*frame_size=*/SkISize());
  auto layer_tree_item = std::make_unique<LayerTreeItem>(
      std::move(layer_tree), CreateFinishedBuildRecorder(), kDevicePixelRatio);
  PipelineProduceResult result =
      pipeline->Produce().Complete(std::move(layer_tree_item));
  EXPECT_TRUE(result.success);

  // The Draw() will respectively call BeginFrame(), SubmitFrame() and
  // EndFrame() one time.
  ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
  rasterizer->Draw(pipeline);

  // The DrawLastLayerTree() will respectively call BeginFrame(), SubmitFrame()
  // and EndFrame() one more time, totally 2 times.
  rasterizer->DrawLastLayerTree(CreateFinishedBuildRecorder());
}

TEST(RasterizerTest, externalViewEmbedderDoesntEndFrameWhenNoSurfaceIsSet) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));
  auto rasterizer = std::make_unique<Rasterizer>(delegate);

  std::shared_ptr<NiceMock<MockExternalViewEmbedder>> external_view_embedder =
      std::make_shared<NiceMock<MockExternalViewEmbedder>>();
  rasterizer->SetExternalViewEmbedder(external_view_embedder);

  EXPECT_CALL(
      *external_view_embedder,
      EndFrame(/*should_resubmit_frame=*/false,
               /*raster_thread_merger=*/fml::RefPtr<fml::RasterThreadMerger>(
                   nullptr)))
      .Times(0);

  fml::AutoResetWaitableEvent latch;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    auto layer_tree = std::make_unique<LayerTree>(
        /*config=*/LayerTree::Config(), /*frame_size=*/SkISize());
    auto layer_tree_item = std::make_unique<LayerTreeItem>(
        std::move(layer_tree), CreateFinishedBuildRecorder(),
        kDevicePixelRatio);
    PipelineProduceResult result =
        pipeline->Produce().Complete(std::move(layer_tree_item));
    EXPECT_TRUE(result.success);
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    rasterizer->Draw(pipeline);
    latch.Signal();
  });
  latch.Wait();
}

TEST(RasterizerTest, externalViewEmbedderDoesntEndFrameWhenNotUsedThisFrame) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));

  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();
  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillOnce(Return(ByMove(std::make_unique<GLContextDefaultResult>(true))));

  std::shared_ptr<NiceMock<MockExternalViewEmbedder>> external_view_embedder =
      std::make_shared<NiceMock<MockExternalViewEmbedder>>();
  rasterizer->SetExternalViewEmbedder(external_view_embedder);
  rasterizer->Setup(std::move(surface));

  EXPECT_CALL(*external_view_embedder,
              BeginFrame(/*frame_size=*/SkISize(), /*context=*/nullptr,
                         /*device_pixel_ratio=*/2.0,
                         /*raster_thread_merger=*/_))
      .Times(0);
  EXPECT_CALL(
      *external_view_embedder,
      EndFrame(/*should_resubmit_frame=*/false,
               /*raster_thread_merger=*/fml::RefPtr<fml::RasterThreadMerger>(
                   nullptr)))
      .Times(0);

  fml::AutoResetWaitableEvent latch;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    auto layer_tree = std::make_unique<LayerTree>(
        /*config=*/LayerTree::Config(), /*frame_size=*/SkISize());
    auto layer_tree_item = std::make_unique<LayerTreeItem>(
        std::move(layer_tree), CreateFinishedBuildRecorder(),
        kDevicePixelRatio);
    PipelineProduceResult result =
        pipeline->Produce().Complete(std::move(layer_tree_item));
    EXPECT_TRUE(result.success);
    // Always discard the layer tree.
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(true));
    RasterStatus status = rasterizer->Draw(pipeline);
    EXPECT_EQ(status, RasterStatus::kDiscarded);
    latch.Signal();
  });
  latch.Wait();
}

TEST(RasterizerTest, externalViewEmbedderDoesntEndFrameWhenPipelineIsEmpty) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));

  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();
  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillOnce(Return(ByMove(std::make_unique<GLContextDefaultResult>(true))));

  std::shared_ptr<NiceMock<MockExternalViewEmbedder>> external_view_embedder =
      std::make_shared<NiceMock<MockExternalViewEmbedder>>();
  rasterizer->SetExternalViewEmbedder(external_view_embedder);
  rasterizer->Setup(std::move(surface));

  EXPECT_CALL(
      *external_view_embedder,
      EndFrame(/*should_resubmit_frame=*/false,
               /*raster_thread_merger=*/fml::RefPtr<fml::RasterThreadMerger>(
                   nullptr)))
      .Times(0);

  fml::AutoResetWaitableEvent latch;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    RasterStatus status = rasterizer->Draw(pipeline);
    EXPECT_EQ(status, RasterStatus::kFailed);
    latch.Signal();
  });
  latch.Wait();
}

TEST(RasterizerTest,
     drawWithGpuEnabledAndSurfaceAllowsDrawingWhenGpuDisabledDoesAcquireFrame) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));
  EXPECT_CALL(delegate, OnFrameRasterized(_));

  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();
  auto is_gpu_disabled_sync_switch =
      std::make_shared<const fml::SyncSwitch>(false);

  SurfaceFrame::FramebufferInfo framebuffer_info;
  framebuffer_info.supports_readback = true;
  auto surface_frame = std::make_unique<SurfaceFrame>(
      /*surface=*/nullptr, /*framebuffer_info=*/framebuffer_info,
      /*submit_callback=*/[](const SurfaceFrame&, DlCanvas*) { return true; },
      /*frame_size=*/SkISize::Make(800, 600));
  EXPECT_CALL(*surface, AllowsDrawingWhenGpuDisabled()).WillOnce(Return(true));
  ON_CALL(delegate, GetIsGpuDisabledSyncSwitch())
      .WillByDefault(Return(is_gpu_disabled_sync_switch));
  EXPECT_CALL(delegate, GetIsGpuDisabledSyncSwitch()).Times(0);
  EXPECT_CALL(*surface, AcquireFrame(SkISize()))
      .WillOnce(Return(ByMove(std::move(surface_frame))));
  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillOnce(Return(ByMove(std::make_unique<GLContextDefaultResult>(true))));

  rasterizer->Setup(std::move(surface));
  fml::AutoResetWaitableEvent latch;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    auto layer_tree = std::make_unique<LayerTree>(
        /*config=*/LayerTree::Config(), /*frame_size=*/SkISize());
    auto layer_tree_item = std::make_unique<LayerTreeItem>(
        std::move(layer_tree), CreateFinishedBuildRecorder(),
        kDevicePixelRatio);
    PipelineProduceResult result =
        pipeline->Produce().Complete(std::move(layer_tree_item));
    EXPECT_TRUE(result.success);
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    rasterizer->Draw(pipeline);
    latch.Signal();
  });
  latch.Wait();
}

TEST(
    RasterizerTest,
    drawWithGpuDisabledAndSurfaceAllowsDrawingWhenGpuDisabledDoesAcquireFrame) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));
  EXPECT_CALL(delegate, OnFrameRasterized(_));
  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();
  auto is_gpu_disabled_sync_switch =
      std::make_shared<const fml::SyncSwitch>(true);

  SurfaceFrame::FramebufferInfo framebuffer_info;
  framebuffer_info.supports_readback = true;

  auto surface_frame = std::make_unique<SurfaceFrame>(
      /*surface=*/nullptr, /*framebuffer_info=*/framebuffer_info,
      /*submit_callback=*/[](const SurfaceFrame&, DlCanvas*) { return true; },
      /*frame_size=*/SkISize::Make(800, 600));
  EXPECT_CALL(*surface, AllowsDrawingWhenGpuDisabled()).WillOnce(Return(true));
  ON_CALL(delegate, GetIsGpuDisabledSyncSwitch())
      .WillByDefault(Return(is_gpu_disabled_sync_switch));
  EXPECT_CALL(delegate, GetIsGpuDisabledSyncSwitch()).Times(0);
  EXPECT_CALL(*surface, AcquireFrame(SkISize()))
      .WillOnce(Return(ByMove(std::move(surface_frame))));
  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillOnce(Return(ByMove(std::make_unique<GLContextDefaultResult>(true))));

  rasterizer->Setup(std::move(surface));
  fml::AutoResetWaitableEvent latch;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    auto layer_tree = std::make_unique<LayerTree>(
        /*config=*/LayerTree::Config(), /*frame_size=*/SkISize());
    auto layer_tree_item = std::make_unique<LayerTreeItem>(
        std::move(layer_tree), CreateFinishedBuildRecorder(),
        kDevicePixelRatio);
    PipelineProduceResult result =
        pipeline->Produce().Complete(std::move(layer_tree_item));
    EXPECT_TRUE(result.success);
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    RasterStatus status = rasterizer->Draw(pipeline);
    EXPECT_EQ(status, RasterStatus::kSuccess);
    latch.Signal();
  });
  latch.Wait();
}

TEST(
    RasterizerTest,
    drawWithGpuEnabledAndSurfaceDisallowsDrawingWhenGpuDisabledDoesAcquireFrame) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));
  EXPECT_CALL(delegate, OnFrameRasterized(_));
  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();
  auto is_gpu_disabled_sync_switch =
      std::make_shared<const fml::SyncSwitch>(false);

  SurfaceFrame::FramebufferInfo framebuffer_info;
  framebuffer_info.supports_readback = true;

  auto surface_frame = std::make_unique<SurfaceFrame>(
      /*surface=*/nullptr, /*framebuffer_info=*/framebuffer_info,
      /*submit_callback=*/[](const SurfaceFrame&, DlCanvas*) { return true; },
      /*frame_size=*/SkISize::Make(800, 600));
  EXPECT_CALL(*surface, AllowsDrawingWhenGpuDisabled()).WillOnce(Return(false));
  EXPECT_CALL(delegate, GetIsGpuDisabledSyncSwitch())
      .WillOnce(Return(is_gpu_disabled_sync_switch));
  EXPECT_CALL(*surface, AcquireFrame(SkISize()))
      .WillOnce(Return(ByMove(std::move(surface_frame))));
  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillOnce(Return(ByMove(std::make_unique<GLContextDefaultResult>(true))));

  rasterizer->Setup(std::move(surface));
  fml::AutoResetWaitableEvent latch;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    auto layer_tree = std::make_unique<LayerTree>(
        /*config=*/LayerTree::Config(), /*frame_size=*/SkISize());
    auto layer_tree_item = std::make_unique<LayerTreeItem>(
        std::move(layer_tree), CreateFinishedBuildRecorder(),
        kDevicePixelRatio);
    PipelineProduceResult result =
        pipeline->Produce().Complete(std::move(layer_tree_item));
    EXPECT_TRUE(result.success);
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    RasterStatus status = rasterizer->Draw(pipeline);
    EXPECT_EQ(status, RasterStatus::kSuccess);
    latch.Signal();
  });
  latch.Wait();
}

TEST(
    RasterizerTest,
    drawWithGpuDisabledAndSurfaceDisallowsDrawingWhenGpuDisabledDoesntAcquireFrame) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));
  EXPECT_CALL(delegate, OnFrameRasterized(_)).Times(0);
  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();
  auto is_gpu_disabled_sync_switch =
      std::make_shared<const fml::SyncSwitch>(true);

  SurfaceFrame::FramebufferInfo framebuffer_info;
  framebuffer_info.supports_readback = true;

  auto surface_frame = std::make_unique<SurfaceFrame>(
      /*surface=*/nullptr, /*framebuffer_info=*/framebuffer_info,
      /*submit_callback=*/[](const SurfaceFrame&, DlCanvas*) { return true; },
      /*frame_size=*/SkISize::Make(800, 600));
  EXPECT_CALL(*surface, AllowsDrawingWhenGpuDisabled()).WillOnce(Return(false));
  EXPECT_CALL(delegate, GetIsGpuDisabledSyncSwitch())
      .WillOnce(Return(is_gpu_disabled_sync_switch));
  EXPECT_CALL(*surface, AcquireFrame(SkISize())).Times(0);
  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillOnce(Return(ByMove(std::make_unique<GLContextDefaultResult>(true))));

  rasterizer->Setup(std::move(surface));
  fml::AutoResetWaitableEvent latch;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    auto layer_tree = std::make_unique<LayerTree>(
        /*config=*/LayerTree::Config(), /*frame_size=*/SkISize());
    auto layer_tree_item = std::make_unique<LayerTreeItem>(
        std::move(layer_tree), CreateFinishedBuildRecorder(),
        kDevicePixelRatio);
    PipelineProduceResult result =
        pipeline->Produce().Complete(std::move(layer_tree_item));
    EXPECT_TRUE(result.success);
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    RasterStatus status = rasterizer->Draw(pipeline);
    EXPECT_EQ(status, RasterStatus::kDiscarded);
    latch.Signal();
  });
  latch.Wait();
}

TEST(
    RasterizerTest,
    FrameTimingRecorderShouldStartRecordingRasterTimeBeforeSurfaceAcquireFrame) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));
  EXPECT_CALL(delegate, OnFrameRasterized(_))
      .WillOnce([&](const FrameTiming& frame_timing) {
        fml::TimePoint now = fml::TimePoint::Now();
        fml::TimePoint raster_start =
            frame_timing.Get(FrameTiming::kRasterStart);
        EXPECT_TRUE(now - raster_start < fml::TimeDelta::FromSecondsF(1));
      });

  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();
  auto is_gpu_disabled_sync_switch =
      std::make_shared<const fml::SyncSwitch>(false);
  ON_CALL(delegate, GetIsGpuDisabledSyncSwitch())
      .WillByDefault(Return(is_gpu_disabled_sync_switch));
  ON_CALL(*surface, AcquireFrame(SkISize()))
      .WillByDefault(::testing::Invoke([] { return nullptr; }));
  EXPECT_CALL(*surface, AcquireFrame(SkISize()));
  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillOnce(Return(ByMove(std::make_unique<GLContextDefaultResult>(true))));
  rasterizer->Setup(std::move(surface));
  fml::AutoResetWaitableEvent latch;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    auto layer_tree = std::make_unique<LayerTree>(
        /*config=*/LayerTree::Config(), /*frame_size=*/SkISize());
    auto layer_tree_item = std::make_unique<LayerTreeItem>(
        std::move(layer_tree), CreateFinishedBuildRecorder(),
        kDevicePixelRatio);
    PipelineProduceResult result =
        pipeline->Produce().Complete(std::move(layer_tree_item));
    EXPECT_TRUE(result.success);
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    RasterStatus status = rasterizer->Draw(pipeline);
    EXPECT_EQ(status, RasterStatus::kFailed);
    latch.Signal();
  });
  latch.Wait();
}

TEST(RasterizerTest,
     drawLayerTreeWithCorrectFrameTimingWhenPipelineIsMoreAvailable) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());
  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  ON_CALL(delegate, GetTaskRunners()).WillByDefault(ReturnRef(task_runners));

  fml::AutoResetWaitableEvent latch;
  std::unique_ptr<Rasterizer> rasterizer;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    rasterizer = std::make_unique<Rasterizer>(delegate);
    latch.Signal();
  });
  latch.Wait();

  auto surface = std::make_unique<NiceMock<MockSurface>>();
  EXPECT_CALL(*surface, AllowsDrawingWhenGpuDisabled())
      .WillRepeatedly(Return(true));
  ON_CALL(*surface, AcquireFrame(SkISize()))
      .WillByDefault(::testing::Invoke([] {
        SurfaceFrame::FramebufferInfo framebuffer_info;
        framebuffer_info.supports_readback = true;
        return std::make_unique<SurfaceFrame>(
            /*surface=*/nullptr, framebuffer_info,
            /*submit_callback=*/
            [](const SurfaceFrame& frame, DlCanvas*) { return true; },
            /*frame_size=*/SkISize::Make(800, 600));
      }));
  ON_CALL(*surface, MakeRenderContextCurrent())
      .WillByDefault(::testing::Invoke(
          [] { return std::make_unique<GLContextDefaultResult>(true); }));

  fml::CountDownLatch count_down_latch(2);
  auto first_timestamp = fml::TimePoint::Now();
  auto second_timestamp = first_timestamp + fml::TimeDelta::FromMilliseconds(8);
  std::vector<fml::TimePoint> timestamps = {first_timestamp, second_timestamp};
  int frame_rasterized_count = 0;
  EXPECT_CALL(delegate, OnFrameRasterized(_))
      .Times(2)
      .WillRepeatedly([&](const FrameTiming& frame_timing) {
        EXPECT_EQ(timestamps[frame_rasterized_count],
                  frame_timing.Get(FrameTiming::kVsyncStart));
        EXPECT_EQ(timestamps[frame_rasterized_count],
                  frame_timing.Get(FrameTiming::kBuildStart));
        EXPECT_EQ(timestamps[frame_rasterized_count],
                  frame_timing.Get(FrameTiming::kBuildFinish));
        frame_rasterized_count++;
        count_down_latch.CountDown();
      });

  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    rasterizer->Setup(std::move(surface));
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    for (int i = 0; i < 2; i++) {
      auto layer_tree = std::make_unique<LayerTree>(
          /*config=*/LayerTree::Config(), /*frame_size=*/SkISize());
      auto layer_tree_item = std::make_unique<LayerTreeItem>(
          std::move(layer_tree), CreateFinishedBuildRecorder(timestamps[i]),
          kDevicePixelRatio);
      PipelineProduceResult result =
          pipeline->Produce().Complete(std::move(layer_tree_item));
      EXPECT_TRUE(result.success);
      EXPECT_EQ(result.is_first_item, i == 0);
    }
    // Although we only call 'Rasterizer::Draw' once, it will be called twice
    // finally because there are two items in the pipeline.
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    rasterizer->Draw(pipeline);
  });
  count_down_latch.Wait();
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    rasterizer.reset();
    latch.Signal();
  });
  latch.Wait();
}

TEST(RasterizerTest, TeardownFreesResourceCache) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());

  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));

  auto rasterizer = std::make_unique<Rasterizer>(delegate);
  auto surface = std::make_unique<NiceMock<MockSurface>>();
  auto context = GrDirectContext::MakeMock(nullptr);
  context->setResourceCacheLimit(0);

  EXPECT_CALL(*surface, MakeRenderContextCurrent())
      .WillRepeatedly([]() -> std::unique_ptr<GLContextResult> {
        return std::make_unique<GLContextDefaultResult>(true);
      });
  EXPECT_CALL(*surface, GetContext()).WillRepeatedly(Return(context.get()));

  rasterizer->Setup(std::move(surface));
  EXPECT_EQ(context->getResourceCacheLimit(), 0ul);

  rasterizer->SetResourceCacheMaxBytes(10000000, false);
  EXPECT_EQ(context->getResourceCacheLimit(), 10000000ul);
  EXPECT_EQ(context->getResourceCachePurgeableBytes(), 0ul);

  int count = 0;
  size_t bytes = 0;
  context->getResourceCacheUsage(&count, &bytes);
  EXPECT_EQ(bytes, 0ul);

  auto image_info =
      SkImageInfo::MakeN32Premul(500, 500, SkColorSpace::MakeSRGB());
  auto sk_surface = SkSurfaces::RenderTarget(context.get(),
                                             skgpu::Budgeted::kYes, image_info);
  EXPECT_TRUE(sk_surface);

  SkPaint paint;
  sk_surface->getCanvas()->drawPaint(paint);
  context->flushAndSubmit(GrSyncCpu::kYes);

  EXPECT_EQ(context->getResourceCachePurgeableBytes(), 0ul);

  sk_surface.reset();

  context->getResourceCacheUsage(&count, &bytes);
  EXPECT_GT(bytes, 0ul);
  EXPECT_GT(context->getResourceCachePurgeableBytes(), 0ul);

  rasterizer->Teardown();
  EXPECT_EQ(context->getResourceCachePurgeableBytes(), 0ul);
}

TEST(RasterizerTest, TeardownNoSurface) {
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());

  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  EXPECT_CALL(delegate, GetTaskRunners())
      .WillRepeatedly(ReturnRef(task_runners));

  auto rasterizer = std::make_unique<Rasterizer>(delegate);

  EXPECT_TRUE(rasterizer);
  rasterizer->Teardown();
}

TEST(RasterizerTest, presentationTimeSetWhenVsyncTargetInFuture) {
  GTEST_SKIP() << "eglPresentationTime is disabled due to "
                  "https://github.com/flutter/flutter/issues/112503";
#if false
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());

  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  ON_CALL(delegate, GetTaskRunners()).WillByDefault(ReturnRef(task_runners));

  fml::AutoResetWaitableEvent latch;
  std::unique_ptr<Rasterizer> rasterizer;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    rasterizer = std::make_unique<Rasterizer>(delegate);
    latch.Signal();
  });
  latch.Wait();

  const auto millis_16 = fml::TimeDelta::FromMilliseconds(16);
  const auto first_timestamp = fml::TimePoint::Now() + millis_16;
  auto second_timestamp = first_timestamp + millis_16;
  std::vector<fml::TimePoint> timestamps = {first_timestamp, second_timestamp};

  int frames_submitted = 0;
  fml::CountDownLatch submit_latch(2);
  auto surface = std::make_unique<MockSurface>();
  ON_CALL(*surface, AllowsDrawingWhenGpuDisabled()).WillByDefault(Return(true));
  ON_CALL(*surface, AcquireFrame(SkISize()))
      .WillByDefault(::testing::Invoke([&] {
        SurfaceFrame::FramebufferInfo framebuffer_info;
        framebuffer_info.supports_readback = true;
        return std::make_unique<SurfaceFrame>(
            /*surface=*/nullptr, framebuffer_info,
            /*submit_callback=*/
            [&](const SurfaceFrame& frame, DlCanvas*) {
              const auto pres_time = *frame.submit_info().presentation_time;
              const auto diff = pres_time - first_timestamp;
              int num_frames_submitted = frames_submitted++;
              EXPECT_EQ(diff.ToMilliseconds(),
                        num_frames_submitted * millis_16.ToMilliseconds());
              submit_latch.CountDown();
              return true;
            },
            /*frame_size=*/SkISize::Make(800, 600));
      }));

  ON_CALL(*surface, MakeRenderContextCurrent())
      .WillByDefault(::testing::Invoke(
          [] { return std::make_unique<GLContextDefaultResult>(true); }));

  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    rasterizer->Setup(std::move(surface));
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    for (int i = 0; i < 2; i++) {
      auto layer_tree = std::make_unique<LayerTree>(
          /*config=*/LayerTree::Config(), /*frame_size=*/SkISize());
      auto layer_tree_item = std::make_unique<LayerTreeItem>(
          std::move(layer_tree), CreateFinishedBuildRecorder(timestamps[i]),
          kDevicePixelRatio);
      PipelineProduceResult result =
          pipeline->Produce().Complete(std::move(layer_tree_item));
      EXPECT_TRUE(result.success);
      EXPECT_EQ(result.is_first_item, i == 0);
    }
    // Although we only call 'Rasterizer::Draw' once, it will be called twice
    // finally because there are two items in the pipeline.
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    rasterizer->Draw(pipeline);
  });

  submit_latch.Wait();
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    rasterizer.reset();
    latch.Signal();
  });
  latch.Wait();
#endif  // false
}

TEST(RasterizerTest, presentationTimeNotSetWhenVsyncTargetInPast) {
  GTEST_SKIP() << "eglPresentationTime is disabled due to "
                  "https://github.com/flutter/flutter/issues/112503";
#if false
  std::string test_name =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  ThreadHost thread_host("io.flutter.test." + test_name + ".",
                         ThreadHost::Type::Platform | ThreadHost::Type::RASTER |
                             ThreadHost::Type::IO | ThreadHost::Type::UI);
  TaskRunners task_runners("test", thread_host.platform_thread->GetTaskRunner(),
                           thread_host.raster_thread->GetTaskRunner(),
                           thread_host.ui_thread->GetTaskRunner(),
                           thread_host.io_thread->GetTaskRunner());

  NiceMock<MockDelegate> delegate;
  Settings settings;
  ON_CALL(delegate, GetSettings()).WillByDefault(ReturnRef(settings));
  ON_CALL(delegate, GetTaskRunners()).WillByDefault(ReturnRef(task_runners));

  fml::AutoResetWaitableEvent latch;
  std::unique_ptr<Rasterizer> rasterizer;
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    rasterizer = std::make_unique<Rasterizer>(delegate);
    latch.Signal();
  });
  latch.Wait();

  const auto millis_16 = fml::TimeDelta::FromMilliseconds(16);
  const auto first_timestamp = fml::TimePoint::Now() - millis_16;

  fml::CountDownLatch submit_latch(1);
  auto surface = std::make_unique<MockSurface>();
  ON_CALL(*surface, AllowsDrawingWhenGpuDisabled()).WillByDefault(Return(true));
  ON_CALL(*surface, AcquireFrame(SkISize()))
      .WillByDefault(::testing::Invoke([&] {
        SurfaceFrame::FramebufferInfo framebuffer_info;
        framebuffer_info.supports_readback = true;
        return std::make_unique<SurfaceFrame>(
            /*surface=*/nullptr, framebuffer_info,
            /*submit_callback=*/
            [&](const SurfaceFrame& frame, DlCanvas*) {
              const std::optional<fml::TimePoint> pres_time =
                  frame.submit_info().presentation_time;
              EXPECT_EQ(pres_time, std::nullopt);
              submit_latch.CountDown();
              return true;
            },
            /*frame_size=*/SkISize::Make(800, 600));
      }));

  ON_CALL(*surface, MakeRenderContextCurrent())
      .WillByDefault(::testing::Invoke(
          [] { return std::make_unique<GLContextDefaultResult>(true); }));

  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    rasterizer->Setup(std::move(surface));
    auto pipeline = std::make_shared<LayerTreePipeline>(/*depth=*/10);
    auto layer_tree = std::make_unique<LayerTree>(
        /*config=*/LayerTree::Config(), /*frame_size=*/SkISize());
    auto layer_tree_item = std::make_unique<LayerTreeItem>(
        std::move(layer_tree), CreateFinishedBuildRecorder(first_timestamp),
        kDevicePixelRatio);
    PipelineProduceResult result =
        pipeline->Produce().Complete(std::move(layer_tree_item));
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.is_first_item, true);
    ON_CALL(delegate, ShouldDiscardLayerTree).WillByDefault(Return(false));
    rasterizer->Draw(pipeline);
  });

  submit_latch.Wait();
  thread_host.raster_thread->GetTaskRunner()->PostTask([&] {
    rasterizer.reset();
    latch.Signal();
  });
  latch.Wait();
#endif  // false
}

}  // namespace flutter

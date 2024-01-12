#ifndef SHELL_COMMON_SHOREBIRD_BLOBS_HANDLE_H_
#define SHELL_COMMON_SHOREBIRD_BLOBS_HANDLE_H_

#include "flutter/fml/file.h"
#include "flutter/runtime/dart_snapshot.h"
#include "third_party/dart/runtime/include/dart_tools_api.h"

namespace flutter {

struct BlobsIndex {
  size_t blob;
  size_t offset;
};

// Implements a POSIX file I/O interface which allows us to provide the four
// data blobs of a Dart snapshot (vm_data, vm_instructions, isolate_data,
// isolate_instructions) to Rust as though it were a single piece of memory.
class BlobsHandle {
 public:
  // This would ideally be private, but we need to be able to call it from the
  // static createForSnapshots method.
  explicit BlobsHandle(std::vector<std::unique_ptr<fml::Mapping>> blobs)
      : blobs_(std::move(blobs)) {}

  static std::unique_ptr<BlobsHandle> createForSnapshots(
      const DartSnapshot& vm_snapshot,
      const DartSnapshot& isolate_snapshot);

  uintptr_t Read(uint8_t* buffer, uintptr_t length);
  int64_t Seek(int64_t offset, int32_t whence);
  size_t FullSize() const;

 private:
  size_t AbsoluteOffsetForIndex(BlobsIndex index);
  BlobsIndex IndexForAbsoluteOffset(size_t offset);
  BlobsIndex IndexFromAbsoluteOffset(int64_t offset, BlobsIndex startIndex);

  BlobsIndex current_index_;
  std::vector<std::unique_ptr<fml::Mapping>> blobs_;

  // friend class testing::BlobsHandleTest;
};

}  // namespace flutter

#endif  // SHELL_COMMON_SHOREBIRD_BLOBS_HANDLE_H_

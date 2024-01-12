#include "flutter/shell/common/shorebird/blobs_handle.h"

#include "third_party/dart/runtime/include/dart_native_api.h"

namespace flutter {

static std::unique_ptr<fml::Mapping> DataBlob(const DartSnapshot& snapshot) {
  auto ptr = snapshot.GetDataMapping();
  return std::make_unique<fml::NonOwnedMapping>(ptr,
                                                Dart_SnapshotDataSize(ptr));
}

static std::unique_ptr<fml::Mapping> InstrBlob(const DartSnapshot& snapshot) {
  auto ptr = snapshot.GetInstructionsMapping();
  return std::make_unique<fml::NonOwnedMapping>(ptr,
                                                Dart_SnapshotInstrSize(ptr));
}

size_t BlobsHandle::FullSize() const {
  size_t size = 0;
  for (const auto& blob : blobs_) {
    size += blob->GetSize();
  }
  return size;
}

size_t BlobsHandle::AbsoluteOffsetForIndex(BlobsIndex index) {
  if (index.blob >= blobs_.size()) {
    return FullSize();
  }
  if (index.offset > blobs_[index.blob]->GetSize()) {
    FML_LOG(ERROR) << "Bad state, offset for blob " << index.blob << " ("
                   << index.offset << ") is larger than the blob size ("
                   << blobs_[index.blob]->GetSize()
                   << "). Returning index start of next blob";
    return AbsoluteOffsetForIndex({index.blob + 1, 0});
  }
  size_t offset = 0;
  for (size_t i = 0; i < index.blob; i++) {
    offset += blobs_[i]->GetSize();
  }
  offset += index.offset;
  return offset;
}

BlobsIndex BlobsHandle::IndexForAbsoluteOffset(size_t offset) {
  if (offset < 0) {
    FML_LOG(ERROR) << "Asking for blobs index when absolute index is before "
                      "the beginning of the blobs (offset:"
                   << offset << ")";
    FML_LOG(ERROR) << "Returning {0, 0}";
    return {0, 0};
  }

  BlobsIndex index = {0, 0};
  for (const auto& blob : blobs_) {
    if (offset < blob->GetSize()) {
      // The remaining offset is within this blob.
      index.offset = offset;
      break;
    }

    index.blob++;
    offset -= blob->GetSize();
  }
  return index;
}

BlobsIndex BlobsHandle::IndexFromAbsoluteOffset(int64_t offset,
                                                BlobsIndex start_index) {
  size_t start_offset = AbsoluteOffsetForIndex(start_index);
  if (offset < 0 && (size_t)-offset > start_offset) {
    // Seeking before the beginning of the blobs.
    return {0, 0};
  }

  size_t dest_offset = start_offset + offset;
  if (dest_offset >= FullSize()) {
    // Seeking past the end of the blobs.
    return {blobs_.size(), blobs_.back()->GetSize()};
  }

  return IndexForAbsoluteOffset(dest_offset);
}

std::unique_ptr<BlobsHandle> BlobsHandle::createForSnapshots(
    const DartSnapshot& vm_snapshot,
    const DartSnapshot& isolate_snapshot) {
  // This needs to match the order in which the blobs are written out in
  // analyze_snapshot
  std::vector<std::unique_ptr<fml::Mapping>> blobs;
  blobs.push_back(DataBlob(vm_snapshot));
  blobs.push_back(DataBlob(isolate_snapshot));
  blobs.push_back(InstrBlob(vm_snapshot));
  blobs.push_back(InstrBlob(isolate_snapshot));
  return std::make_unique<BlobsHandle>(std::move(blobs));
}

uintptr_t BlobsHandle::Read(uint8_t* buffer, uintptr_t length) {
  uintptr_t bytes_read = 0;
  // Copy current blob from current offset and possibly into the next blob
  // until we have read length bytes.
  while (bytes_read < length) {
    if (current_index_.blob >= blobs_.size()) {
      // We have read all blobs.
      break;
    }
    intptr_t remaining_blob_length =
        blobs_[current_index_.blob]->GetSize() - current_index_.offset;
    if (remaining_blob_length == 0) {
      // We have read all bytes in this blob.
      current_index_.blob++;
      current_index_.offset = 0;
      continue;
    }
    intptr_t bytes_to_read = fmin(length - bytes_read, remaining_blob_length);
    memcpy(buffer + bytes_read,
           blobs_[current_index_.blob]->GetMapping() + current_index_.offset,
           bytes_to_read);
    bytes_read += bytes_to_read;
    current_index_.offset += bytes_to_read;
  }

  return bytes_read;
}

int64_t BlobsHandle::Seek(int64_t offset, int32_t whence) {
  BlobsIndex start_index;
  switch (whence) {
    case SEEK_CUR:
      start_index = current_index_;
      break;
    case SEEK_SET:
      start_index = {0, 0};
      break;
    case SEEK_END:
      start_index = {blobs_.size(), blobs_.back()->GetSize()};
      break;
    default:
      FML_CHECK(false) << "Unrecognized whence value in Seek: " << whence;
  }
  current_index_ = IndexFromAbsoluteOffset(offset, start_index);
  return current_index_.offset;
}

}  // namespace flutter
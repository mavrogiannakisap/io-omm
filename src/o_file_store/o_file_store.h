#ifndef FILEORAM_FILE_ORAM_FILE_ORAM_H_
#define FILEORAM_FILE_ORAM_FILE_ORAM_H_

#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "path_oram/path_oram.h"
#include "path_omap/path_omap.h"
#include "path_osegtree/path_osegtree.h"
#include "utils/assert.h"
#include "utils/bytes.h"

#define ceil_div(a, b) (((a)+(b)-1)/(b))

namespace file_oram::o_file_store {

// Same as file_oram::{path_oram,path_omap}::{Key,Val,OptVal}.
// TODO: refactor all of the common types to one place?
using Key = uint32_t;

class Val {
 public:
  std::unique_ptr<char[]> data_;
  size_t l_ = 0;

  Val() = default;
  Val(std::unique_ptr<char[]> data, size_t n) : data_(std::move(data)), l_(n) {}
  explicit Val(size_t n) : data_(std::make_unique<char[]>(n)), l_(n) {}
};
using OptVal = std::optional<Val>;

namespace internal {
using OSegTree = path_osegtree::OSegTree;

using ORam = path_oram::ORam;
using ORamPos = path_oram::Pos;
using ORamKey = path_oram::Key;

using OMap = path_omap::OMap;
using OMapVal = path_omap::Val;

class OMapVal32 {
  // TODO: Use Multi32 instead?
 public:
  uint32_t v_ = 0;

  OMapVal32() = default;
  explicit OMapVal32(char *data) { utils::FromBytes(data, v_); }
  explicit OMapVal32(uint32_t v) : v_(v) {}

  [[nodiscard]] OMapVal ToBytes() const { return utils::ToUniquePtrBytes(v_); }
  void ToBytes(char *to) const { utils::ToBytes(v_, to); }
};

template<int i>
class OMapValMulti32 {
 public:
  std::array<uint32_t, i> v_;

  OMapValMulti32() { for (auto &v : v_) v = 0; }
  explicit OMapValMulti32(char *data) { utils::FromBytes(data, v_); }
  explicit OMapValMulti32(std::array<uint32_t, i> v) : v_(std::move(v)) {}

  [[nodiscard]] OMapVal ToBytes() const { return utils::ToUniquePtrBytes(v_); }
  void ToBytes(char *to) const { utils::ToBytes(v_, to); }
};

using SKPOMapVal = OMapValMulti32<3>; // Size Key Pos --> SKP

class PartMeta {
 public:
  Key k_;
  size_t l_;

  PartMeta() = default;
  PartMeta(Key k, size_t l) : k_(k), l_(l) {}
  explicit PartMeta(char *data) { utils::FromBytes(data, *this); }

  void ToBytes(char *to) const { utils::ToBytes(*this, to); }
};

class Part {
 public:
  PartMeta meta_;
  std::unique_ptr<char[]> data_;

  Part() = default;
  Part(PartMeta m, std::unique_ptr<char[]> d) : meta_(m), data_(std::move(d)) {}
  Part(Key k, size_t l) : meta_(k, l), data_(std::make_unique<char[]>(l)) {}
};

using OptPart = std::optional<Part>;

class SuperBlockMeta {
 public:
  uint32_t part_count_ = 0;
  uint32_t parts_valid_ = 0;
  std::unique_ptr<PartMeta[]> parts_ = nullptr;

  SuperBlockMeta() = default;
  explicit SuperBlockMeta(uint32_t cnt, bool no_alloc_buffer = false)
      : part_count_(cnt),
        parts_valid_(0),
        parts_(cnt && !no_alloc_buffer
               ? std::make_unique<PartMeta[]>(cnt)
               : nullptr) {}
  explicit SuperBlockMeta(char *data);

  void ToBytes(char *to);

  static size_t SizeInBytes(uint32_t part_count) {
    return sizeof(part_count_) + sizeof(parts_valid_) + (uint64_t(part_count) * sizeof(PartMeta));
  }
};

class SuperBlock {
 public:
  size_t byte_size_ = 0;
  std::unique_ptr<char[]> data_ = nullptr;
  SuperBlockMeta meta_;

  SuperBlock() = default;
  SuperBlock(char *data, size_t byte_size)
      : byte_size_(byte_size),
        data_(data),
        meta_(data) {}
  SuperBlock(std::unique_ptr<char[]> data, size_t byte_size)
      : byte_size_(byte_size),
        data_(std::move(data)),
        meta_(data_.get()) {
    my_assert(IsSizeValid());
  }
  SuperBlock(size_t byte_size, uint32_t part_count, bool no_alloc_buffer = false)
      : byte_size_(byte_size),
        data_(no_alloc_buffer ? nullptr : std::make_unique<char[]>(byte_size)),
        meta_(part_count, no_alloc_buffer) {
    my_assert(IsSizeValid());
  }
  void RefreshMetaFromData() { meta_ = SuperBlockMeta(data_.get()); }

  std::unique_ptr<char[]> Release() { return std::move(data_); }
  [[nodiscard]] size_t UsedSpace() const;
  [[nodiscard]] size_t FreeSpace() const {
    return byte_size_ - SuperBlockMeta::SizeInBytes(meta_.part_count_) - UsedSpace();
  }
  bool Append(Key k, char *from, size_t n); // only for when has all levels
  OptPart Remove(Key k);
  OptPart Read(Key k);
  bool RemoveTo(Key k, char *to);
  void Add(Key k, Val &v);
  void Add(const Part &p);
  void Free();
  void EnsureAllocated();
  bool IsAllocated() { return data_ != nullptr; }

 private:
  bool IsSizeValid() { return SuperBlockMeta::SizeInBytes(meta_.part_count_) < byte_size_; }
};

static uint64_t ConcatKeyIdx(Key k, uint32_t idx) {
  uint64_t res = k;
  res <<= 32UL;
  res |= idx;
  return res;
}

static Key ExtractKey(uint64_t key_idx) {
  return Key(key_idx >> 32);
}

static uint32_t ExtractIdx(uint64_t key_idx) {
  return key_idx;
}

class PosPair {
 public:
  ORamPos old_, new_;
};

class NaiveOram {
 public:
  static std::optional<NaiveOram> Construct(
      size_t total_cap, uint32_t max_parts,
      utils::Key enc_key,
      std::shared_ptr<grpc::Channel> channel,
      storage::InitializeRequest_StoreType st,
      bool first_build = false);
  void Destroy();
  void Fetch();
  void Evict();
  OptVal ReadAndRemove(Key k);
  bool ReadAndRemoveTo(Key k, char *to);
  void Add(Key k, Val v);

  [[nodiscard]] size_t TotalSizeOfStore() const { return ctext_size_; }
  [[nodiscard]] size_t BytesMoved() const { return bytes_moved_; }
  [[nodiscard]] bool WasPrebuilt() const { return was_prebuilt_; }

 private:
  NaiveOram(size_t total_cap, uint32_t max_parts,
            utils::Key enc_key,
            std::shared_ptr<grpc::Channel> channel,
            storage::InitializeRequest_StoreType st,
            bool first_build = false);
  const utils::Key enc_key_;
  const size_t total_cap_;
  const uint32_t max_part_count_;
  const size_t ptext_size_;
  const size_t ctext_size_;
  bool setup_successful_ = false;
  bool was_prebuilt_ = false;
  uint32_t store_id_;
  std::unique_ptr<storage::RemoteStore::Stub> stub_;
  SuperBlock sb_;
  bool server_valid_ = false;

  size_t bytes_moved_ = 0;
};
} // namespace internal

static uint8_t lg(uint64_t n) { return lround(ceil(log2(n))); }

using ValUpdateFunc = std::function<Val(OptVal)>;

class OFileStore {
 public:
  static std::optional<OFileStore> Construct(uint32_t n, uint8_t s, size_t &lf, uint32_t base_block_size,
             utils::Key enc_key,
             const std::shared_ptr<grpc::Channel> &channel,
             storage::InitializeRequest_StoreType data_st,
             storage::InitializeRequest_StoreType aux_st,
             bool upload_stash = true,
             bool first_build = false, std::string storage_type_ = "RAM", uint8_t init_level_ = 10, std::string store_path = "");
  static std::optional<OFileStore> SConstruct(uint32_t n, uint8_t s, size_t &lf, uint32_t base_block_size,
    utils::Key enc_key,
    const std::shared_ptr<grpc::Channel> &channel,
    storage::InitializeRequest_StoreType data_st,
    storage::InitializeRequest_StoreType aux_st,
    bool upload_stash,
    bool first_build,
    std::string storage_type_, uint8_t num_runs, uint8_t init_level_ , std::string store_path="");

  void Append(Key k, Val v, bool optimized = false); // InsertOne, InsertMany
  void AppendSingleLevel(Key k, Val v);
  void PrebuildAppend(Key k, Val v); // Adds everything to the stash and evicts once.

  OptVal Delete(Key k);
  void ReadUpdate(Key k, const ValUpdateFunc &val_updater);
  void Search(Key k, const ValUpdateFunc &val_updater);
  [[nodiscard]] static ValUpdateFunc MakeReader(OptVal &to);
  void Destroy();
  [[nodiscard]] uint32_t Capacity() const { return capacity_; };
  [[nodiscard]] size_t TotalSizeOfStore() const;
  void SetAutoEvict() { manual_evict_ = false; }
  void EvictAll();
  void BatchEvict();
  bool SetupCheck() {return setup_successful_;}

  [[nodiscard]] size_t BytesMoved() const;
  [[nodiscard]] bool WasPrebuilt() const { return was_prebuilt_; }

  const std::vector<uint8_t> levels_{};

 private:
  OFileStore(
    uint32_t n, uint8_t s, size_t &lf, uint32_t base_block_size,
    utils::Key enc_key,
    const std::shared_ptr<grpc::Channel> &channel,
    storage::InitializeRequest_StoreType data_st,
    storage::InitializeRequest_StoreType aux_st,
    bool upload_stash,
    bool first_build,
    std::string storage_type_, uint8_t init_level_, std::string store_path_);

  OFileStore(uint32_t n, uint8_t s, uint32_t base_block_size,
    utils::Key enc_key,
    const std::shared_ptr<grpc::Channel> &channel,
    storage::InitializeRequest_StoreType data_st,
    storage::InitializeRequest_StoreType aux_st,
    bool upload_stash,
    bool first_build,
    std::string storage_type_, uint8_t init_level_, std::string store_path_);

  const uint32_t capacity_; // Number of base blocks
  const bool has_all_levels_;
  const uint32_t locality_factor_;
  const size_t base_block_size_;
  bool manual_evict_ = true;

  std::unique_ptr<internal::OMap> part_oram_key_map_ = nullptr;
  std::unique_ptr<internal::OMap> size_map_ = nullptr;
  std::unique_ptr<internal::OMap> size_key_pos_map_ = nullptr; // when all levels present.
  std::unique_ptr<internal::OMap> counter_map_ = nullptr;
  std::unique_ptr<internal::NaiveOram> naive_oram_ = nullptr; // One is really enough.

  std::vector<std::unique_ptr<internal::ORam>> orams_{};
  std::vector<std::unique_ptr<internal::OSegTree>> allocators_{};
  std::vector<std::unique_ptr<internal::OMap>> pos_maps_{};

  bool setup_successful_ = false;
  bool was_prebuilt_ = true; // Unless at least one child was not prebuilt

  bool AllLevelsAppend(Key k, Val v);
  OptVal AllLevelsDelete(Key k);
  void AllLevelsReadUpdate(Key k, const ValUpdateFunc &val_updater);

  [[nodiscard]] static std::vector<uint8_t> MakeUniformLevels(size_t n, uint8_t s, size_t &lf);
  [[nodiscard]] static std::vector<uint8_t> MakeLevels(size_t n, uint8_t s, std::string storage_type_, uint32_t base_block_size_,  uint8_t init_level_);
  [[nodiscard]] size_t NumBlocks(size_t n) const { return ceil_div(n, base_block_size_); }
  [[nodiscard]] uint8_t LevelIdx(uint32_t n) const;
  [[nodiscard]] size_t SuperBlockMaxBaseBlocksInVal(uint8_t l) const; // times base_block_size_
  [[nodiscard]] size_t SuperBlockNumBaseBlocks(uint8_t l) const;
  [[nodiscard]] size_t SuperBlockSizeWithMetaInBytes(uint8_t l) const;
  [[nodiscard]] size_t MaxPartCount(uint8_t l) const;
//  size_t GetAndUpdateSize(Key k, uint32_t n, bool append = false, bool hide_if_exists = true);
  [[nodiscard]] internal::Part ExtractPartOrFail(Key k, uint32_t i, uint8_t l);
  [[nodiscard]] internal::ORamKey ReadAndRemoveOramKeyOrFail(Key k, uint32_t i);
  [[nodiscard]] internal::PosPair GetOldPosAndReposition(internal::ORamKey ok, uint8_t l, bool must_exist = false);
  [[nodiscard]] bool IsLevelNaive(uint8_t l) const;
  void InsertPart(Key k, uint32_t i, internal::Part part, uint8_t l, bool prebuild);
  void SetOramKey(Key k, uint32_t i, internal::ORamKey ok);
  [[nodiscard]] size_t NaiveORamMaxPartCount(uint8_t l) const;
};
} // namespace file_oram::o_file_store

#endif //FILEORAM_FILE_ORAM_FILE_ORAM_H_

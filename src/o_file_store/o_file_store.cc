#include "o_file_store.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <grpcpp/channel.h>
#include <openssl/rand.h>

#include "remote_store/server.h" // TODO: Extract common? (kMax...)
#include "utils/assert.h"
#include "utils/bytes.h"
#include "utils/namegen.h"
#include "utils/trace.h"

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

namespace file_oram::o_file_store {

const std::string kIdKey = "id";
const google::protobuf::Empty empty_req;
google::protobuf::Empty empty_res;

using namespace internal;
using klock = std::chrono::high_resolution_clock;

SuperBlockMeta::SuperBlockMeta(char *data) {
  utils::FromBytes(data, part_count_);
  data += sizeof(part_count_);
  utils::FromBytes(data, parts_valid_);
  data += sizeof(parts_valid_);
  parts_ = std::make_unique<PartMeta[]>(part_count_);
  for (int i = 0; i < part_count_; ++i) {
    parts_[i] = PartMeta(data);
    data += sizeof(PartMeta);
  }
}

void SuperBlockMeta::ToBytes(char *to) {
  utils::ToBytes(part_count_, to);
  to += sizeof(part_count_);
  utils::ToBytes(parts_valid_, to);
  to += sizeof(parts_valid_);
  for (int i = 0; i < parts_valid_; ++i) {
    parts_[i].ToBytes(to);
    to += sizeof(PartMeta);
  }
}

size_t SuperBlock::UsedSpace() const {
  size_t res = 0;
  for (int i = 0; i < meta_.parts_valid_; ++i) {
    res += meta_.parts_[i].l_;
  }
  return res;
}

OptPart SuperBlock::Read(Key k) {
  bool found = false;
  uint32_t i = 0;
  size_t base = 0;
  while (i < meta_.parts_valid_) {
    if (k == meta_.parts_[i].k_) {
      found = true;
      break;
    }
    base += meta_.parts_[i].l_;
    ++i;
  }
  if (!found) {
    return std::nullopt;
  }

  Part res(meta_.parts_[i], std::make_unique<char[]>(meta_.parts_[i].l_));
  auto res_start =
      data_.get() + SuperBlockMeta::SizeInBytes(meta_.part_count_) + base;
  std::copy_n(res_start, res.meta_.l_, res.data_.get());

  return res;
}

OptPart SuperBlock::Remove(Key k) {
  bool found = false;
  uint32_t i = 0;
  size_t base = 0;
  while (i < meta_.parts_valid_) {
    if (k == meta_.parts_[i].k_) {
      found = true;
      break;
    }
    base += meta_.parts_[i].l_;
    ++i;
  }
  if (!found) {
    return std::nullopt;
  }

  // Build res
  Part res(meta_.parts_[i], std::make_unique<char[]>(meta_.parts_[i].l_));
  auto res_start =
      data_.get() + SuperBlockMeta::SizeInBytes(meta_.part_count_) + base;
  std::copy_n(res_start, res.meta_.l_, res.data_.get());

  // Adjust parts
  auto to_copy_back = UsedSpace() - base - res.meta_.l_;
  std::copy_n(res_start + res.meta_.l_, to_copy_back, res_start);
  std::copy_n(meta_.parts_.get() + i + 1, meta_.parts_valid_ - i - 1,
              meta_.parts_.get() + i);
  --meta_.parts_valid_;
  meta_.ToBytes(data_.get());

  return res;
}

inline static size_t BucketSize(size_t val_len) {
  return sizeof(path_oram::internal::BucketMetadata) + (4 * (8 + val_len));
}

inline static size_t EncryptedBucketSize(size_t val_len) {
  return utils::CiphertextLen(BucketSize(val_len));
}

bool SuperBlock::Append(Key k, char *from, size_t n) {
  if (meta_.part_count_ != 1 ||
      (meta_.parts_valid_ == 1 && meta_.parts_[0].k_ != k) || FreeSpace() < n) {
    return false;
  }
  auto to = data_.get() + SuperBlockMeta::SizeInBytes(1) +
            (meta_.parts_valid_ == 1 ? meta_.parts_[0].l_ : 0);
  meta_.parts_[0].k_ = k;
  meta_.parts_[0].l_ = (meta_.parts_valid_ == 1 ? meta_.parts_[0].l_ : 0) + n;
  meta_.parts_valid_ = 1;
  std::copy_n(from, n, to);
  meta_.ToBytes(data_.get());
  return true;
}

bool SuperBlock::RemoveTo(Key k, char *to) {
  bool found = false;
  uint32_t i = 0;
  size_t base = 0;
  while (i < meta_.parts_valid_) {
    if (k == meta_.parts_[i].k_) {
      found = true;
      break;
    }
    base += meta_.parts_[i].l_;
    ++i;
  }
  if (!found) {
    return false;
  }

  // Build res
  Part res(meta_.parts_[i], std::make_unique<char[]>(meta_.parts_[i].l_));
  auto res_start =
      data_.get() + SuperBlockMeta::SizeInBytes(meta_.part_count_) + base;
  if (to) { // Discard if to==nullptr
    std::copy_n(res_start, meta_.parts_[i].l_, to);
  }

  // Adjust parts
  auto to_copy_back = UsedSpace() - base - res.meta_.l_;
  std::copy_n(res_start + res.meta_.l_, to_copy_back, res_start);
  std::copy_n(meta_.parts_.get() + i + 1, meta_.parts_valid_ - i - 1,
              meta_.parts_.get() + i);
  --meta_.parts_valid_;
  meta_.ToBytes(data_.get());

  return true;
}

void SuperBlock::Add(Key k, Val &v) {
  my_assert(FreeSpace() >= v.l_);
  my_assert(meta_.parts_valid_ < meta_.part_count_);
  auto write_base = data_.get() +
                    SuperBlockMeta::SizeInBytes(meta_.part_count_) +
                    UsedSpace();
  meta_.parts_[meta_.parts_valid_] = PartMeta(k, v.l_);
  ++meta_.parts_valid_;
  std::copy_n(v.data_.get(), v.l_, write_base);
  meta_.ToBytes(data_.get());
}

void SuperBlock::Add(const Part &p) {
  my_assert(FreeSpace() >= p.meta_.l_);
  my_assert(meta_.parts_valid_ < meta_.part_count_);
  auto write_base = data_.get() +
                    SuperBlockMeta::SizeInBytes(meta_.part_count_) +
                    UsedSpace();
  meta_.parts_[meta_.parts_valid_] = p.meta_;
  ++meta_.parts_valid_;
  std::copy_n(p.data_.get(), p.meta_.l_, write_base);
  meta_.ToBytes(data_.get());
}

void SuperBlock::Free() {
  data_.reset();
  meta_.parts_.reset();
}

void SuperBlock::EnsureAllocated() {
  if (!data_) {
    data_ = std::make_unique<char[]>(byte_size_);
  }
  if (!meta_.parts_) {
    meta_.parts_ = std::make_unique<PartMeta[]>(meta_.part_count_);
  }
}

std::optional<NaiveOram>
NaiveOram::Construct(size_t total_cap, uint32_t max_parts, utils::Key enc_key,
                     std::shared_ptr<grpc::Channel> channel,
                     storage::InitializeRequest_StoreType st,
                     bool first_build) {
  NaiveOram o(total_cap, max_parts, enc_key, std::move(channel), st,
              first_build);
  if (o.setup_successful_)
    return std::move(o);
  return std::nullopt;
}

NaiveOram::NaiveOram(size_t total_cap, uint32_t max_parts, utils::Key enc_key,
                     std::shared_ptr<grpc::Channel> channel,
                     storage::InitializeRequest_StoreType st, bool first_build)
    : enc_key_(enc_key), total_cap_(total_cap), max_part_count_(max_parts),
      ptext_size_(SuperBlockMeta::SizeInBytes(max_parts) + (total_cap)),
      ctext_size_(utils::CiphertextLen(ptext_size_)),
      stub_(storage::RemoteStore::NewStub(std::move(channel))),
      sb_(ptext_size_, max_part_count_, true) {

  grpc::ClientContext ctx;
  storage::InitializeRequest req;
  req.set_n(1);
  req.set_entry_size(ctext_size_);
  req.set_store_type(st);
  req.set_name(utils::GenName({
      "naive",
      "",
      "n",
      std::to_string(ctext_size_),
  }));
  // Only to not recreate with different IDs (the server is not initialized)
  // when pre-building
  storage::InitializeResponse res;
  auto status = stub_->Initialize(&ctx, req, &res);
  if (!status.ok()) {
    std::clog << "Failed to construct NaiveORAM; "
              << "Status error code: " << status.error_code()
              << ", error message: " << status.error_message() << std::endl;
    return;
  }
  auto it = ctx.GetServerInitialMetadata().find(kIdKey);
  if (it != ctx.GetServerInitialMetadata().end()) {
    store_id_ = std::stoul(std::string(it->second.begin(), it->second.end()));
  }

  if (res.found_prebuilt()) {
    server_valid_ = true;
    was_prebuilt_ = true;
  } else {
    if (first_build) {
      Evict();
    }
  }

  setup_successful_ = true;
}

void NaiveOram::Destroy() {
  if (!setup_successful_)
    return;
  grpc::ClientContext ctx;
  ctx.AddMetadata(kIdKey, std::to_string(store_id_));
  auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
  ctx.set_deadline(deadline);
  stub_->Destroy(&ctx, empty_req, &empty_res);
}

void NaiveOram::Fetch() {
  if (!server_valid_)
    return;
  sb_.EnsureAllocated();

  server_valid_ = false;
  bytes_moved_ += ctext_size_;
  storage::ReadManyRequest req;
  req.add_indexes(0);
  grpc::ClientContext ctx;
  ctx.AddMetadata(kIdKey, std::to_string(store_id_));
  storage::EntryPart ep;
  std::unique_ptr<grpc::ClientReader<storage::EntryPart>> reader(
      stub_->ReadMany(&ctx, req));
  std::vector<std::string *> ctext_parts{};
  while (reader->Read(&ep)) {
    my_assert(ep.index() == 0);
    my_assert(ep.data().size() <= storage::kMaxEntryPartSize);
    ctext_parts.push_back(ep.release_data());
  }
  auto plen = utils::DecryptStrArray(ctext_parts, 0, ctext_size_, enc_key_,
                                     sb_.data_.get());
  my_assert(plen == ptext_size_);
  for (auto ctext_part : ctext_parts)
    delete ctext_part;
  sb_.RefreshMetaFromData();
}

void NaiveOram::Evict() {
  if (server_valid_ || !sb_.IsAllocated())
    return;
  server_valid_ = true;
  bytes_moved_ += ctext_size_;

  grpc::ClientContext ctx;
  ctx.AddMetadata(kIdKey, std::to_string(store_id_));
  auto ctext = new char[ctext_size_];
  auto enc_success =
      utils::Encrypt(sb_.data_.get(), ptext_size_, enc_key_, ctext);
  my_assert(enc_success);
  sb_.Free();
  std::unique_ptr<grpc::ClientWriter<storage::EntryPart>> writer(
      stub_->WriteMany(&ctx, &empty_res));
  storage::EntryPart ep;
  bool write_success = true;
  size_t offset = 0;
  while (offset < ctext_size_) {
    ep.set_index(0);
    ep.set_offset(offset);
    auto b = ctext + offset;
    auto to_send = min(ctext_size_ - offset, storage::kMaxEntryPartSize);
    ep.set_data({b, b + to_send});
    write_success &= writer->Write(ep);
    my_assert(write_success);
    if (!write_success) {
      std::clog << "Write failed; data loss!" << std::endl;
      return;
    }
    offset += to_send;
    ep.Clear();
  }
  delete[] ctext;
  write_success &= writer->WritesDone();
  my_assert(write_success);
  if (!write_success) {
    std::clog << "Write failed; data loss!" << std::endl;
    return;
  }

  auto status = writer->Finish();
  my_assert(status.ok());
  if (!status.ok()) {
    std::clog << "NaiveORAM EvictAll failed to write!"
              << " status error code: " << status.error_code()
              << "; status error message: " << status.error_message()
              << std::endl;
    return;
  }
}

OptVal NaiveOram::ReadAndRemove(Key k) {
  if (server_valid_ || !sb_.IsAllocated())
    return std::nullopt;
  auto p = sb_.Remove(k);
  if (!p)
    return std::nullopt;
  return Val{std::move(p->data_), p->meta_.l_};
}

bool NaiveOram::ReadAndRemoveTo(Key k, char *to) {
  if (server_valid_ || !sb_.IsAllocated())
    return false;
  return sb_.RemoveTo(k, to);
}

void NaiveOram::Add(Key k, Val v) {
  if (server_valid_)
    return;
  if (!server_valid_ && !sb_.IsAllocated())
    sb_.EnsureAllocated();
  my_assert(sb_.FreeSpace() > v.l_);
  sb_.Add(k, v);
}

std::optional<OFileStore> OFileStore::Construct(
    uint32_t n, uint8_t s, size_t &lf, uint32_t base_block_size,
    utils::Key enc_key, const std::shared_ptr<grpc::Channel> &channel,
    storage::InitializeRequest_StoreType data_st,
    storage::InitializeRequest_StoreType aux_st, bool upload_stash,
    bool first_build, std::string storage_type_, uint8_t init_level_) {
  auto o =
      OFileStore(n, s, lf, base_block_size, enc_key, channel, data_st, aux_st,
                 upload_stash, first_build, storage_type_, init_level_);
  if (o.setup_successful_) {
    return o;
  }
  return std::nullopt;
}

std::optional<OFileStore> OFileStore::SConstruct(
    uint32_t n, uint8_t s, size_t &lf, uint32_t base_block_size,
    utils::Key enc_key, const std::shared_ptr<grpc::Channel> &channel,
    storage::InitializeRequest_StoreType data_st,
    storage::InitializeRequest_StoreType aux_st, bool upload_stash,
    bool first_build, std::string storage_type_, uint8_t num_runs,
    uint8_t init_level_) {

  if (s > 1) {
    return OFileStore(n, s, lf, base_block_size, enc_key, channel, data_st,
                      aux_st, upload_stash, first_build, storage_type_,
                      init_level_);
  } else {
    return OFileStore(n, s, base_block_size, enc_key, channel, data_st, aux_st,
                      upload_stash, first_build, storage_type_, init_level_);
  }
  return std::nullopt;
}

// MultiFileStore
// aka. multiple levels
OFileStore::OFileStore(uint32_t n, uint8_t s, size_t &lf,
                       uint32_t base_block_size, utils::Key enc_key,
                       const std::shared_ptr<grpc::Channel> &channel,
                       storage::InitializeRequest_StoreType data_st,
                       storage::InitializeRequest_StoreType aux_st,
                       bool upload_stash, bool first_build,
                       std::string storage_type_, uint8_t init_level_)
    : capacity_(n),
      levels_(MakeLevels(n, s, storage_type_, base_block_size, init_level_)),
      has_all_levels_(levels_.size() == lg(n) + 1), locality_factor_(lf),
      base_block_size_(base_block_size) {
  if ((n & (n - 1)) || levels_.empty()) { // Not a power of 2 or bad config
    return;
  }
  // Since all levels are dummy-filled, evictions will have
  // already happened once constructed.
  // SetAutoEvict();
  for (uint8_t l = 0; l < uint8_t(levels_.size()); ++l) {
    if (IsLevelNaive(l)) {
      std::clog << "OFS: levels: ";
      for (const auto &level : levels_) {
        if (level == levels_[l])
          std::clog << "[";
        std::clog << int(level);
        if (level == levels_[l])
          std::clog << "]";
        std::clog << " ";
      }
      std::clog << "; first naive level: " << int(levels_[l]) << std::endl;

      auto opt_oram =
          NaiveOram::Construct(uint64_t(n) * uint64_t(base_block_size), n,
                               enc_key, channel, data_st, first_build);
      if (!opt_oram) {
        Destroy();
        return;
      }
      was_prebuilt_ &= opt_oram->WasPrebuilt();
      naive_oram_ = std::make_unique<NaiveOram>(std::move(opt_oram.value()));
      break;
    }

    // N/2^i

    auto level_cap = capacity_ >> levels_[l];
    std::clog << "level_cap" << level_cap << std::endl;
    auto opt_ost =
        OSegTree::Construct(level_cap, SuperBlockNumBaseBlocks(l), enc_key,
                            channel, aux_st, aux_st, upload_stash, first_build);
    if (!opt_ost) {
      Destroy();
      return;
    }
    was_prebuilt_ &= opt_ost->WasPrebuilt();
    allocators_.push_back(
        std::make_unique<OSegTree>(std::move(opt_ost.value())));

    auto opt_oram = ORam::Construct(level_cap, SuperBlockSizeWithMetaInBytes(l),
                                    enc_key, channel, data_st, aux_st,
                                    upload_stash, "", first_build);
    if (!opt_oram) {
      Destroy();
      return;
    }

    was_prebuilt_ &= opt_oram.value()->WasPrebuilt();
    opt_oram.value()->FillWithDummies();
    orams_.push_back(std::unique_ptr<ORam>(opt_oram.value()));
    if (has_all_levels_) {
      continue;
    }

    auto opt_omap =
        OMap::Construct(level_cap, sizeof(uint32_t), enc_key, channel, aux_st,
                        aux_st, upload_stash, first_build);
    if (!opt_omap) {
      Destroy();
      return;
    }
    was_prebuilt_ &= opt_omap->WasPrebuilt();
    opt_omap->FillWithDummies();
    pos_maps_.push_back(std::make_unique<OMap>(std::move(opt_omap.value())));
  }

  if (has_all_levels_) {
    auto opt_omap =
        OMap::Construct(capacity_, sizeof(SKPOMapVal), enc_key, channel, aux_st,
                        aux_st, upload_stash, first_build);
    if (!opt_omap) {
      Destroy();
      return;
    }
    was_prebuilt_ &= opt_omap->WasPrebuilt();
    opt_omap->FillWithDummies();
    size_key_pos_map_ = std::make_unique<OMap>(std::move(opt_omap.value()));

    setup_successful_ = true;
    return;
  }

  auto opt_omap = OMap::Construct(capacity_, sizeof(uint32_t), enc_key, channel,
                                  aux_st, aux_st, upload_stash, first_build);
  if (!opt_omap) {
    Destroy();
    return;
  }
  was_prebuilt_ &= opt_omap->WasPrebuilt();
  opt_omap->FillWithDummies();
  size_map_ = std::make_unique<OMap>(std::move(opt_omap.value())); // length
                                                                   // omap

  auto opt_omap2 =
      OMap::Construct(capacity_, sizeof(uint32_t), enc_key, channel, aux_st,
                      aux_st, upload_stash, first_build);
  if (!opt_omap2) {
    Destroy();
    return;
  }
  was_prebuilt_ &= opt_omap2->WasPrebuilt();
  opt_omap2->FillWithDummies();
  part_oram_key_map_ =
      std::make_unique<OMap>(std::move(opt_omap2.value())); // block id omap

  auto opt_omap3 =
      OMap::Construct(capacity_, sizeof(uint32_t), enc_key, channel, aux_st,
                      aux_st, upload_stash, first_build);
  if (!opt_omap3) {
    Destroy();
    return;
  }
  was_prebuilt_ &= opt_omap3->WasPrebuilt();
  opt_omap3->FillWithDummies();
  counter_map_ = std::make_unique<OMap>(std::move(opt_omap3.value()));

  setup_successful_ = true;
}

OFileStore::OFileStore(uint32_t n, uint8_t s, uint32_t base_block_size,
                       utils::Key enc_key,
                       const std::shared_ptr<grpc::Channel> &channel,
                       storage::InitializeRequest_StoreType data_st,
                       storage::InitializeRequest_StoreType aux_st,
                       bool upload_stash, bool first_build,
                       std::string storage_type_, uint8_t init_level_)
    : capacity_(n), levels_(MakeLevels(n, s, storage_type_, base_block_size,
                                       init_level_)), // TODO: Fix
      has_all_levels_(levels_.size() == lg(n) + 1), locality_factor_(1),
      base_block_size_(base_block_size) {
  if ((n & (n - 1)) || levels_.empty()) { // Not a power of 2 or bad config
    return;
  }
  // Since all levels are dummy-filled, evictions will have
  // already happened once constructed.
  SetAutoEvict();
  for (uint8_t l = 0; l < uint8_t(levels_.size()); ++l) {
    // N/2^i
    auto level_cap = capacity_ >> levels_[l];
    auto opt_ost =
        OSegTree::Construct(level_cap, SuperBlockNumBaseBlocks(l), enc_key,
                            channel, aux_st, aux_st, upload_stash, first_build);
    if (!opt_ost) {
      Destroy();
      return;
    }
    was_prebuilt_ &= opt_ost->WasPrebuilt();
    allocators_.push_back(
        std::make_unique<OSegTree>(std::move(opt_ost.value())));

    auto opt_oram = ORam::Construct(level_cap, SuperBlockSizeWithMetaInBytes(l),
                                    enc_key, channel, data_st, aux_st,
                                    upload_stash, "", first_build);
    if (!opt_oram) {
      Destroy();
      return;
    }
    was_prebuilt_ &= opt_oram.value()->WasPrebuilt();
    opt_oram.value()->FillWithDummies();
    orams_.push_back(std::unique_ptr<ORam>(opt_oram.value()));

    if (has_all_levels_) {
      continue;
    }

    auto opt_omap =
        OMap::Construct(level_cap, sizeof(uint32_t), enc_key, channel, aux_st,
                        aux_st, upload_stash, first_build);
    if (!opt_omap) {
      Destroy();
      return;
    }
    was_prebuilt_ &= opt_omap->WasPrebuilt();
    opt_omap->FillWithDummies();
    pos_maps_.push_back(std::make_unique<OMap>(std::move(opt_omap.value())));
  }

  if (has_all_levels_) {
    auto opt_omap =
        OMap::Construct(capacity_, sizeof(SKPOMapVal), enc_key, channel, aux_st,
                        aux_st, upload_stash, first_build);
    if (!opt_omap) {
      Destroy();
      return;
    }
    was_prebuilt_ &= opt_omap->WasPrebuilt();
    opt_omap->FillWithDummies();
    size_key_pos_map_ = std::make_unique<OMap>(std::move(opt_omap.value()));

    setup_successful_ = true;
    return;
  }

  auto opt_omap = OMap::Construct(capacity_, sizeof(uint32_t), enc_key, channel,
                                  aux_st, aux_st, upload_stash, first_build);
  if (!opt_omap) {
    Destroy();
    return;
  }
  was_prebuilt_ &= opt_omap->WasPrebuilt();
  opt_omap->FillWithDummies();
  size_map_ = std::make_unique<OMap>(std::move(opt_omap.value())); // length
                                                                   // omap

  auto opt_omap2 =
      OMap::Construct(capacity_, sizeof(uint32_t), enc_key, channel, aux_st,
                      aux_st, upload_stash, first_build);
  if (!opt_omap2) {
    Destroy();
    return;
  }
  was_prebuilt_ &= opt_omap2->WasPrebuilt();
  opt_omap2->FillWithDummies();
  part_oram_key_map_ =
      std::make_unique<OMap>(std::move(opt_omap2.value())); // block id omap

  // Added by TOLIS:
  auto opt_omap3 =
      OMap::Construct(capacity_, sizeof(uint32_t), enc_key, channel, aux_st,
                      aux_st, upload_stash, first_build);
  if (!opt_omap3) {
    Destroy();
    return;
  }
  was_prebuilt_ &= opt_omap3->WasPrebuilt();
  opt_omap3->FillWithDummies();
  counter_map_ = std::make_unique<OMap>(std::move(opt_omap3.value()));

  setup_successful_ = true;
}

// In my setting: Returns a vector of size 1. V = {12} -> N/2^12 , oram_b = 2^12
std::vector<uint8_t> OFileStore::MakeLevels(size_t n, uint8_t s,
                                            std::string storage_type_,
                                            uint32_t base_block_size_,
                                            uint8_t init_level_) {
  std::vector<uint8_t> res;

  std::clog << "Encrypted Bucket Size: "
            << EncryptedBucketSize(base_block_size_) << std::endl;
  // int step = log2(n)/(2*s);
  int step = 4;

  if (!storage_type_.compare("RAM")) {

    for (int i = 0; i < s; i++) {
      res.push_back(init_level_);
      init_level_ += step;
    }
  } else if (!storage_type_.compare("SSD")) {
    // TODO
    // uint32_t bucket_size_ = EncryptedBucketSize(base_block_size_);
    // auto ceil = ceil_div(bucket_size , 4096); // 4096 -- size of a page
    for (int i = 0; i < s; i++) {
      res.push_back(
          floor(log2(int(4096 / EncryptedBucketSize(base_block_size_)))) +
          step);
    }
  } else if (!storage_type_.compare("HDD")) {
    for (int i = 0; i < s; i++) {
      res.push_back(init_level_ + step);
    }
  }

  return res;
}

std::vector<uint8_t> OFileStore::MakeUniformLevels(size_t n, uint8_t s,
                                                   size_t &lf) {
  std::vector<uint8_t> res;
  uint8_t l;
  int p;
  my_assert(s <= lg(n) + 1);
  if ((lg(n) + 1) % s == 0) {
    // scenario 1 (or default): Keep level l and not level 0 unless s=l+1
    l = lg(n);
    p = (l + 1) / s;
  } else if (lg(n / lf) % (s - 1) == 0) {
    // scenario 2: Keep level l and 0 unless s=1, and divide the range in
    // between
    l = lg(n / lf);
    p = l / (s - 1);
  } else {
    std::clog << "Bad config; could not make levels. n=" << n << ", lf=" << lf
              << ", s=" << int(s) << std::endl;
    return {};
  }

  for (auto curr = l; res.size() < s; curr -= p) {
    res.push_back(curr);
  }

  int min_diff = l;
  for (int i = 0; i < res.size() - 1; ++i) {
    my_assert(res[i] > res[i + 1]);
    if (res[i + 1] - res[i] < min_diff) {
      min_diff = res[i + 1] - res[i];
    }
  }
  size_t max_lf = 1ULL << min_diff;
  if (lf >= max_lf) {
    std::clog << "Locality factor (" << lf << ") too small for levels: ";
    for (const auto &level : res)
      std::clog << level << " ";
    std::clog << "(max acceptable lf: " << max_lf << ")" << std::endl;
    return {};
  }

  std::reverse(res.begin(), res.end());
  return res;
}

uint8_t OFileStore::LevelIdx(uint32_t n) const { // n in base blocks
  if (n == 0 || levels_.size() == 1) {
    return 0;
  }
  if (has_all_levels_) {
    return lg(n);
  }
  for (int i = 0; i < levels_.size() - 1; ++i) {
    if (n <= uint64_t(locality_factor_) *
                 uint64_t(SuperBlockMaxBaseBlocksInVal(i))) {
      return i;
    }
  }
  return levels_.size() - 1;
}

size_t OFileStore::SuperBlockMaxBaseBlocksInVal(
    uint8_t l) const { // times base_block_size_
  return 1 << levels_[l];
}

size_t OFileStore::SuperBlockNumBaseBlocks(uint8_t l) const {
  if (has_all_levels_ || levels_[l] == 0) {
    return SuperBlockMaxBaseBlocksInVal(l);
  }
  return SuperBlockMaxBaseBlocksInVal(l) << 1;
}

size_t OFileStore::SuperBlockSizeWithMetaInBytes(uint8_t l) const {
  uint32_t part_count = MaxPartCount(l);
  size_t meta_size = SuperBlockMeta::SizeInBytes(part_count);
  uint32_t num_base_blocks = SuperBlockNumBaseBlocks(l);
  size_t data_size = uint64_t(num_base_blocks) * base_block_size_;
  return meta_size + data_size;
}

size_t OFileStore::MaxPartCount(uint8_t l) const {
  if (has_all_levels_) {
    return 1;
  }
  return SuperBlockMaxBaseBlocksInVal(l);
}

void OFileStore::Destroy() {
  if (part_oram_key_map_)
    part_oram_key_map_->Destroy();
  if (size_map_)
    size_map_->Destroy();
  if (naive_oram_)
    naive_oram_->Destroy();
  if (size_key_pos_map_)
    size_key_pos_map_->Destroy();
  if (counter_map_)
    counter_map_->Destroy();
  for (auto &o : orams_)
    if (o)
      o->Destroy();
  for (auto &o : allocators_)
    if (o)
      o->Destroy();
  for (auto &o : pos_maps_)
    if (o)
      o->Destroy();
}

// size_t OFileStore::GetAndUpdateSize(Key k, uint32_t n, bool append, bool
// hide_if_exists) {
//   auto opt_curr_size = hide_if_exists ? size_map_->ReadAndRemove(k)
//                                       : size_map_->Read(k);
//   if (hide_if_exists) {
//     if (!manual_evict_) {
//       size_map_->EvictAll();
//     }
//   }
//   auto curr_size = opt_curr_size ? OMapVal32(opt_curr_size->get())
//                                  : OMapVal32(uint32_t(0));
//   auto new_size = OMapVal32(append ? curr_size.v_ + n : n);
//   if (opt_curr_size && !hide_if_exists) {
//     // Update in-place
//     new_size.ToBytes(opt_curr_size->get());
//   } else {
//     size_map_->Insert(k, new_size.ToBytes());
//   }
//   if (!manual_evict_) {
//     size_map_->EvictAll();
//   }
//   return curr_size.v_;
// }

ORamKey OFileStore::ReadAndRemoveOramKeyOrFail(Key k, uint32_t i) {
  // auto opt_key = part_oram_key_map_->ReadAndRemove(ConcatKeyIdx(k, i));
  auto opt_key = part_oram_key_map_->Read(ConcatKeyIdx(k, i));
  my_assert(opt_key);
  if (!manual_evict_) {
    part_oram_key_map_->EvictAll();
  }
  return OMapVal32(opt_key.value().get()).v_;
}

PosPair OFileStore::GetOldPosAndReposition(ORamKey ok, uint8_t l,
                                           bool must_exist) {
  auto opt_pos =
      must_exist ? pos_maps_[l]->Read(ok) : pos_maps_[l]->ReadAndRemove(ok);
  my_assert(opt_pos || !must_exist);
  if (!must_exist) {
    if (!manual_evict_) {
      pos_maps_[l]->EvictAll();
    }
  }

  auto pmv = opt_pos ? OMapVal32(opt_pos.value().get())
                     : OMapVal32(orams_[l]->GeneratePos());
  PosPair res{.old_ = pmv.v_, .new_ = orams_[l]->GeneratePos()};
  pmv.v_ = res.new_;
  if (must_exist) {
    pmv.ToBytes(opt_pos.value().get());
  } else {
    pos_maps_[l]->Insert(ok, std::move(pmv.ToBytes()));
  }
  if (!manual_evict_) {
    pos_maps_[l]->EvictAll();
  }
  return res;
}

Part OFileStore::ExtractPartOrFail(Key k, uint32_t i, uint8_t l) {

  auto ok = ReadAndRemoveOramKeyOrFail(k, i);
  auto pp = GetOldPosAndReposition(ok, l, true);
  bool suc = orams_[l]->FetchPath(pp.old_);
  if (!suc) 
      already_fetched_++;
  else 
      actually_fetched_++;

  auto opt_sb = orams_[l]->ReadAndRemoveFromStash(ok);
  my_assert(opt_sb.has_value());
  auto sb =
      SuperBlock(opt_sb.value().release(), SuperBlockSizeWithMetaInBytes(l));
  auto opt_part = sb.Remove(k);
  my_assert(opt_part);
  auto res = std::move(opt_part.value());
  orams_[l]->AddToStash(pp.new_, ok, sb.Release());
  if (!manual_evict_) {
    orams_[l]->EvictAll();
  }
  auto to_free = NumBlocks(res.meta_.l_);
  if (has_all_levels_) {
    to_free = 1UL << lg(to_free);
  } else if (levels_[l] > 0) {
    to_free += to_free % 2;
  }
  allocators_[l]->Free(ok, to_free);
  return std::move(res);
}

void OFileStore::SetOramKey(Key k, uint32_t i, ORamKey ok) {
  part_oram_key_map_->Insert(ConcatKeyIdx(k, i), OMapVal32(ok).ToBytes());
  if (!manual_evict_) {
    part_oram_key_map_->EvictAll();
  }
  // counter_map_->Insert(ConcatKeyIdx(k, ok), OMapVal32(i).ToBytes());
}

void OFileStore::InsertPart(Key k, uint32_t i, Part part, uint8_t l,
                            bool prebuild) {
  auto bl_count = NumBlocks(part.meta_.l_);
  uint32_t to_alloc = bl_count;
  if (has_all_levels_) {
    to_alloc = 1UL << lg(bl_count);
  } else if (levels_[l] > 0) {
    to_alloc = bl_count + (bl_count % 2); // alloc in multiples of 2
  }
  auto opt_ok = allocators_[l]->Alloc(to_alloc);
  my_assert(opt_ok.has_value());
  auto ok = opt_ok.value();
  SetOramKey(k, i, ok);

  auto pp = GetOldPosAndReposition(ok, l, false);

  auto sb_byte_size = SuperBlockSizeWithMetaInBytes(l);

  if (!prebuild) {
    bool suc = orams_[l]->FetchPath(pp.old_);
    if(!suc) 
      already_fetched_++;
    else
      actually_fetched_++;
    
    auto opt_sb = orams_[l]->ReadAndRemoveFromStash(ok);

    auto sb = opt_sb.has_value() ? SuperBlock(opt_sb->release(), sb_byte_size)
                                 : SuperBlock(sb_byte_size, MaxPartCount(l));
    my_assert(sb.FreeSpace() >= part.meta_.l_);
    sb.Add(part);
    orams_[l]->AddToStash(pp.new_, ok, sb.Release());
  } else {
    auto sb = SuperBlock(sb_byte_size, MaxPartCount(l));
    sb.Add(part);
    orams_[l]->AddToStash(pp.new_, ok, sb.Release());
  }

  if (!manual_evict_) {
    orams_[l]->EvictAll();
  }
}

// Need to fix this
void OFileStore::PrebuildAppend(Key k, Val v) {
  my_assert(v.l_ > 0);
  my_assert((v.l_ % base_block_size_) == 0);
  auto bl_count = NumBlocks(v.l_);

  // auto opt_curr_size = size_map_->ReadAndRemove(k);

  uint8_t level = 0; // only level kept.
  size_t new_bytes;
  size_t new_num_parts;
  size_t new_size;
  new_num_parts = ceil_div(bl_count, SuperBlockMaxBaseBlocksInVal(level));
  new_bytes = size_t(bl_count) * size_t(base_block_size_);
  new_size = new_bytes / base_block_size_;
  size_t offset = 0;
  size_t new_max_val_bytes =
      size_t(SuperBlockMaxBaseBlocksInVal(level)) * (base_block_size_);
  for (uint32_t i = 0; i < new_num_parts; i++) {
    auto part_size = min(new_max_val_bytes, new_bytes - offset); // 4
    Part new_part(k, part_size);
    auto new_part_ptr = new_part.data_.get();
    const auto new_part_last = new_part_ptr + part_size; // 384
    while (new_part_ptr < new_part_last)                 // 0 < 4
    {
      size_t to_move = 0;
      auto v_ptr = v.data_.get() + offset; // 0
      auto v_last = v.data_.get() + v.l_;  // 0 + 128
      if (v_ptr == v_last) {
        break;
      }
      my_assert(v_ptr < v_last);
      to_move = min(new_part_last - new_part_ptr, v_last - v_ptr);
      std::copy_n(v_ptr, to_move, new_part_ptr);
      my_assert(to_move > 0);
      offset += to_move;
      new_part_ptr += to_move;
    }

    InsertPart(k, i, std::move(new_part), level, true);
  }
  size_map_->Insert(k, OMapVal32(new_size).ToBytes());
  if (!manual_evict_) {
    size_map_->EvictAll();
  }
}

// Appending a list v to a single level ORAM, i.e., s = 1
void OFileStore::AppendSingleLevel(Key k, Val v) {
  my_assert(v.l_ > 0);
  my_assert((v.l_ % base_block_size_) == 0);
  auto bl_count = NumBlocks(v.l_);
  auto opt_curr_size = size_map_->ReadAndRemove(k);
  manual_evict_ = true;
  if (!manual_evict_) {
    size_map_->EvictAll();
  }

  /*
    curr_size: l * base_block_size
    curr_num_parts: ceil(l * bbs / B * bbs), if float(l/B) == 0, then lists are
    full In case that's true perform a dummy access
  */

  uint8_t level = 0;

  auto curr_size = opt_curr_size ? OMapVal32(opt_curr_size->get()).v_ : 0;
  auto curr_num_parts =
      ceil_div(curr_size, SuperBlockMaxBaseBlocksInVal(level));
  auto full_block = curr_size % SuperBlockMaxBaseBlocksInVal(level);
  size_t new_bytes;
  size_t new_num_parts;
  size_t new_size;
  if (full_block != 0 && curr_size > 0) {
    Part last_part;
    last_part = ExtractPartOrFail(k, curr_num_parts - 1, level);
    new_bytes = last_part.meta_.l_ + bl_count * size_t(base_block_size_);
    new_size = new_bytes / base_block_size_;
    new_num_parts = ceil_div(new_size, SuperBlockMaxBaseBlocksInVal(level));
  } else {
    new_num_parts = ceil_div(bl_count, SuperBlockMaxBaseBlocksInVal(level));
    new_bytes = size_t(bl_count) * size_t(base_block_size_);
    new_size = new_bytes / base_block_size_;

    // dummy access
    auto dummy_ = part_oram_key_map_->ReadAndRemove(0);
  }
  size_t offset = 0;
  size_t new_max_val_bytes =
      size_t(SuperBlockMaxBaseBlocksInVal(level)) * (base_block_size_);
  for (uint32_t i = 0; i < new_num_parts; i++) {
    auto part_size = min(new_max_val_bytes, new_bytes - offset); // 4
    Part new_part(k, part_size);
    auto new_part_ptr = new_part.data_.get();
    const auto new_part_last = new_part_ptr + part_size; // 384
    while (new_part_ptr < new_part_last)                 // 0 < 4
    {
      size_t to_move = 0;
      auto v_ptr = v.data_.get() + offset; // 0
      auto v_last = v.data_.get() + v.l_;  // 0 + 128
      if (v_ptr == v_last) {
        break;
      }
      my_assert(v_ptr < v_last);
      to_move = min(new_part_last - new_part_ptr, v_last - v_ptr);
      std::copy_n(v_ptr, to_move, new_part_ptr);
      my_assert(to_move > 0);
      offset += to_move;
      new_part_ptr += to_move;
    }

    InsertPart(k, curr_num_parts + i, std::move(new_part), level, false);
  }
  size_map_->Insert(k, OMapVal32(curr_size + new_size).ToBytes());
  if (!manual_evict_) {
    size_map_->EvictAll();
  }
}

void OFileStore::Append(Key k, Val v, bool optimized) {
  my_assert(v.l_ > 0);
  my_assert((v.l_ % base_block_size_) == 0);
  auto bl_count = NumBlocks(v.l_);
  my_assert(!optimized); // TODO: optimize

  if (has_all_levels_) {
    auto success = AllLevelsAppend(k, std::move(v));
    my_assert(success);
    return;
  }

  auto opt_curr_size = size_map_->ReadAndRemove(k);
  if (!manual_evict_) {
    size_map_->EvictAll();
  }
  auto curr_size = opt_curr_size ? OMapVal32(opt_curr_size->get()).v_ : 0;
  auto curr_level = LevelIdx(curr_size);
  auto curr_num_parts =
      ceil_div(curr_size, SuperBlockMaxBaseBlocksInVal(curr_level));
  // my_assert(curr_num_parts <= locality_factor_);
  auto new_size = curr_size + bl_count;
  auto new_level = LevelIdx(new_size);
  auto new_num_parts =
      ceil_div(new_size, SuperBlockMaxBaseBlocksInVal(new_level));
  // my_assert(new_num_parts <= locality_factor_);
  size_t curr_bytes = size_t(curr_size) * size_t(base_block_size_);
  size_t new_bytes = size_t(new_size) * size_t(base_block_size_);

  std::vector<Part> curr_parts{curr_num_parts};
  if (curr_size > 0 && IsLevelNaive(curr_level)) {
    curr_parts.resize(1);
    naive_oram_->Fetch();
    auto opt_v = naive_oram_->ReadAndRemove(k);
    my_assert(opt_v.has_value());
    curr_parts.push_back({{k, opt_v->l_}, std::move(opt_v->data_)});
  } else if (curr_size > 0) {
    for (uint32_t i = 0; i < curr_num_parts; ++i) {
      curr_parts[i] = ExtractPartOrFail(k, i, curr_level);
      my_assert(curr_parts[i].meta_.k_ == k);
    }
  }

  if (IsLevelNaive(new_level)) {
    if (!IsLevelNaive(curr_level) || curr_size == 0) {
      naive_oram_->Fetch();
    }
    Val new_v(new_bytes);
    size_t offset = 0;
    for (auto &p : curr_parts) {
      std::copy_n(p.data_.get(), p.meta_.l_, new_v.data_.get() + offset);
      offset += p.meta_.l_;
    }
    std::copy_n(v.data_.get(), v.l_, new_v.data_.get() + offset);
    naive_oram_->Add(k, std::move(new_v));
    if (!manual_evict_) {
      naive_oram_->Evict();
    }
    size_map_->Insert(k, OMapVal32(new_size).ToBytes());
    if (!manual_evict_) {
      size_map_->EvictAll();
    }
    return;
  }

  if (IsLevelNaive(curr_level)) {
    if (!manual_evict_) {
      naive_oram_->Evict();
    }
  }

  size_t curr_max_val_bytes =
      size_t(SuperBlockMaxBaseBlocksInVal(curr_level)) * (base_block_size_);
  size_t new_max_val_bytes =
      size_t(SuperBlockMaxBaseBlocksInVal(new_level)) * (base_block_size_);
  size_t offset = 0;
  for (uint32_t i = 0; i < new_num_parts; ++i) {
    if (offset < curr_bytes && curr_level == new_level &&
        (curr_bytes - offset) >= curr_max_val_bytes) {
      // Reuse parts when possible
      InsertPart(k, i, std::move(curr_parts[i]), curr_level, false);
      offset += curr_max_val_bytes;
      continue;
    }

    auto part_size = min(new_max_val_bytes, new_bytes - offset);
    Part new_part(k, part_size);
    auto new_part_ptr = new_part.data_.get();
    const auto new_part_last = new_part_ptr + part_size;
    while (new_part_ptr < new_part_last) {
      size_t to_move = 0;
      if (offset < curr_bytes) {
        auto &curr_part = curr_parts[offset / curr_max_val_bytes];
        auto curr_part_ptr =
            curr_part.data_.get() + (offset % curr_max_val_bytes);
        auto curr_part_last = curr_part.data_.get() + curr_part.meta_.l_;
        my_assert(curr_part_ptr < curr_part_last);
        to_move =
            min(new_part_last - new_part_ptr, curr_part_last - curr_part_ptr);
        std::copy_n(curr_part_ptr, to_move, new_part_ptr);
      } else {
        auto v_ptr = v.data_.get() + offset - curr_bytes;
        auto v_last = v.data_.get() + v.l_;
        my_assert(v_ptr < v_last);
        to_move = min(new_part_last - new_part_ptr, v_last - v_ptr);
        std::copy_n(v_ptr, to_move, new_part_ptr);
      }
      my_assert(to_move > 0);
      offset += to_move;
      new_part_ptr += to_move;
    }
    InsertPart(k, i, std::move(new_part), new_level, false);
  }

  size_map_->Insert(k, OMapVal32(new_size).ToBytes());
  if (!manual_evict_) {
    size_map_->EvictAll();
  }
}

bool OFileStore::AllLevelsAppend(Key k, Val v) {
  auto opt_skp = size_key_pos_map_->ReadAndRemove(k);
  if (!manual_evict_) {
    size_key_pos_map_->EvictAll();
  }
  auto skp = opt_skp ? SKPOMapVal(opt_skp->get()) : SKPOMapVal();
  auto curr_size = skp.v_[0];
  auto ok = skp.v_[1];
  auto op = skp.v_[2];
  auto curr_level = LevelIdx(curr_size);
  auto new_size = curr_size + NumBlocks(v.l_);
  auto new_level = LevelIdx(new_size);

  Val new_v(new_size * base_block_size_);
  if (curr_size > 0 && IsLevelNaive(curr_level)) {
    naive_oram_->Fetch();
    auto removed = naive_oram_->ReadAndRemoveTo(k, new_v.data_.get());
    my_assert(removed);
  } else if (curr_size > 0) {
    allocators_[curr_level]->Free(ok, SuperBlockMaxBaseBlocksInVal(curr_level));
    orams_[curr_level]->FetchPath(op);
    auto opt_ov = orams_[curr_level]->ReadAndRemoveFromStash(ok);
    my_assert(opt_ov.has_value());
    if (!manual_evict_) {
      orams_[curr_level]->EvictAll();
    }
    auto sb = SuperBlock(opt_ov->release(),
                         SuperBlockSizeWithMetaInBytes(curr_level));
    auto removed = sb.RemoveTo(k, new_v.data_.get());
    my_assert(removed);
  }

  std::copy_n(v.data_.get(), v.l_,
              new_v.data_.get() + (curr_size * base_block_size_));

  if (!IsLevelNaive(new_level)) {
    if (IsLevelNaive(curr_level)) {
      if (!manual_evict_) {
        naive_oram_->Evict();
      }
    }
    auto opt_ok =
        allocators_[new_level]->Alloc(SuperBlockMaxBaseBlocksInVal(new_level));
    my_assert(opt_ok.has_value());
    ok = opt_ok.value();
    SuperBlock sb = SuperBlock(SuperBlockSizeWithMetaInBytes(new_level), 1);
    sb.Add(k, new_v);
    orams_[new_level]->FetchDummyPath();
    op = orams_[new_level]->AddToStash(ok, std::move(sb.data_));
    if (!manual_evict_) {
      orams_[new_level]->EvictAll();
    }
  } else {
    if (!IsLevelNaive(curr_level) || curr_size == 0) {
      naive_oram_->Fetch();
    }
    naive_oram_->Add(k, std::move(new_v));
    if (!manual_evict_) {
      naive_oram_->Evict();
    }
  }

  skp.v_[0] = new_size;
  skp.v_[1] = ok;
  skp.v_[2] = op;
  size_key_pos_map_->Insert(k, skp.ToBytes());
  if (!manual_evict_) {
    size_key_pos_map_->EvictAll();
  }
  return true;
}

// NEED TO DO:
OptVal OFileStore::Delete(Key k) {
  if (has_all_levels_) {
    return AllLevelsDelete(k);
  }
  auto opt_curr_size = size_map_->ReadAndRemove(k);
  if (!manual_evict_) {
    size_map_->EvictAll();
  }
  auto curr_size = opt_curr_size ? OMapVal32(opt_curr_size->get()).v_ : 0;
  if (curr_size == 0) {
    return std::nullopt;
  }
  auto curr_level = LevelIdx(curr_size);

  if (IsLevelNaive(curr_level)) {
    naive_oram_->Fetch();
    auto ov = naive_oram_->ReadAndRemove(k);
    my_assert(ov.has_value());
    if (!manual_evict_) {
      naive_oram_->Evict();
    }
    return std::move(ov);
  }

  auto curr_num_parts =
      ceil_div(curr_size, SuperBlockMaxBaseBlocksInVal(curr_level));
  // my_assert(curr_num_parts <= locality_factor_);
  Val res(curr_size * base_block_size_);
  auto val_ptr = res.data_.get();
  const auto val_last = val_ptr + res.l_;
  for (uint32_t i = 0; i < curr_num_parts; ++i) {
    auto part = ExtractPartOrFail(k, i, curr_level);
    my_assert(part.meta_.k_ == k);
    my_assert(val_ptr + part.meta_.l_ <= val_last);
    std::copy_n(part.data_.get(), part.meta_.l_, val_ptr);
    val_ptr += part.meta_.l_;
  }
  return std::move(res);
}

OptVal OFileStore::AllLevelsDelete(Key k) {
  auto opt_skp = size_key_pos_map_->ReadAndRemove(k);
  if (!manual_evict_) {
    size_key_pos_map_->EvictAll();
  }
  if (!opt_skp) {
    return std::nullopt;
  }

  auto skp = SKPOMapVal(opt_skp->get());
  auto curr_size = skp.v_[0];
  my_assert(curr_size > 0);
  auto ok = skp.v_[1];
  auto op = skp.v_[2];
  auto curr_level = LevelIdx(curr_size);

  if (IsLevelNaive(curr_level)) {
    naive_oram_->Fetch();
    auto opt_v = naive_oram_->ReadAndRemove(k);
    my_assert(opt_v.has_value());
    if (!manual_evict_) {
      naive_oram_->Evict();
    }
    return std::move(opt_v);
  }

  allocators_[curr_level]->Free(ok, SuperBlockMaxBaseBlocksInVal(curr_level));
  orams_[curr_level]->FetchPath(op);
  auto opt_ov = orams_[curr_level]->ReadAndRemoveFromStash(ok);
  my_assert(opt_ov.has_value());
  if (!manual_evict_) {
    orams_[curr_level]->EvictAll();
  }

  SuperBlock sb(opt_ov->release(), SuperBlockSizeWithMetaInBytes(curr_level));
  auto opt_part = sb.Remove(k);
  my_assert(opt_part.has_value());
  return Val(std::move(opt_part.value().data_), opt_part.value().meta_.l_);
}

void OFileStore::ReadUpdate(Key k, const ValUpdateFunc &val_updater) {
  if (has_all_levels_) {
    AllLevelsReadUpdate(k, val_updater);
    return;
  }

  manual_evict_ = true;
  auto opt_curr_size = size_map_->ReadAndRemove(k);
  if (!manual_evict_) {
    size_map_->EvictAll();
  }

  auto curr_size = opt_curr_size ? OMapVal32(opt_curr_size->get()).v_ : 0;
  auto curr_level = LevelIdx(curr_size);
  auto curr_num_parts =
      ceil_div(curr_size, SuperBlockMaxBaseBlocksInVal(curr_level));

  OptVal curr_v = std::nullopt;

  if (curr_size > 0) {
    curr_v = Val(curr_size * base_block_size_);
    auto write_ptr = curr_v->data_.get();
    const auto last = write_ptr + curr_v->l_;
    for (uint32_t i = 0; i < curr_num_parts; ++i) {
      auto part = ExtractPartOrFail(k, i, curr_level);
      my_assert(part.meta_.k_ == k);
      my_assert(write_ptr + part.meta_.l_ <= last);
      std::copy_n(part.data_.get(), part.meta_.l_, write_ptr);
      write_ptr += part.meta_.l_;
    }
  }
  
  auto new_val = val_updater(std::move(curr_v));
  if (new_val.l_ == 0) {
    return;
  }
  auto new_size = NumBlocks(new_val.l_);
  auto new_level = LevelIdx(new_size);
  auto new_num_parts =
      ceil_div(new_size, SuperBlockMaxBaseBlocksInVal(new_level));
  // my_assert(new_num_parts <= locality_factor_);
  size_t new_sb_bytes =
      SuperBlockMaxBaseBlocksInVal(new_level) * base_block_size_;
  auto new_val_ptr = new_val.data_.get();
  const auto new_val_last = new_val_ptr + new_val.l_;
  for (uint32_t i = 0; i < new_num_parts; ++i) {
    my_assert(new_val_ptr < new_val_last);
    auto new_part_size = min(new_sb_bytes, new_val_last - new_val_ptr);
    Part new_part(k, new_part_size);
    std::copy_n(new_val_ptr, new_part_size, new_part.data_.get());
    new_val_ptr += new_part_size;
    InsertPart(k, i, std::move(new_part), curr_level, false);
  }

  size_map_->Insert(k, OMapVal32(new_size).ToBytes());
  if (!manual_evict_) {
    size_map_->EvictAll();
  }
}

// Only for Single level OMM
void OFileStore::Search(Key k, const ValUpdateFunc &val_updater) {
  manual_evict_ = true;
  // Step 1: Read the size of the list

  auto opt_curr_size = size_map_->Read(k);
  if (!manual_evict_) {
    size_map_->EvictAll();
  }
  // std::clog << "\tStep 1: " << res.count() << std::endl;

  // Step 2: Calculate partitions
  auto curr_size = opt_curr_size ? OMapVal32(opt_curr_size->get()).v_ : 0;
  auto curr_level = LevelIdx(curr_size);
  auto curr_num_parts =
      ceil_div(curr_size, SuperBlockMaxBaseBlocksInVal(curr_level));

  if (!curr_size)
    return;

  // Step 3: retrieve the list
  OptVal curr_v = Val(curr_size * base_block_size_);
  auto write_ptr = curr_v->data_.get();
  const auto last = write_ptr + curr_v->l_;
  for (uint32_t i = 0; i < curr_num_parts; ++i) {
    // Step 3a: Retrieve list's bin ID (oram block ID)
    auto ok = ReadAndRemoveOramKeyOrFail(k, i);

    // Step 3b: Retrieve path for the specific bin Id
    auto pp = GetOldPosAndReposition(ok, curr_level, true);

    // Step 3c: Retrieve the actual block from the ORAM
    orams_[curr_level]->FetchPath(pp.old_);
    auto opt_sb = orams_[curr_level]->ReadAndRemoveFromStash(ok);
    my_assert(opt_sb.has_value());
    auto sb = SuperBlock(opt_sb.value().release(),
                         SuperBlockSizeWithMetaInBytes(curr_level));
    auto opt_part = sb.Read(k);
    my_assert(opt_part);
    auto res = std::move(opt_part.value());
    orams_[curr_level]->AddToStash(pp.new_, ok, sb.Release());
    if (!manual_evict_) {
      orams_[curr_level]->EvictAll();
    }
    auto part = std::move(res);
    my_assert(part.meta_.k_ == k);
    my_assert(write_ptr + part.meta_.l_ <= last);
    std::copy_n(part.data_.get(), part.meta_.l_, write_ptr);
    write_ptr += part.meta_.l_;
  }

  // std::clog << "\tStep 3a (BINOM): " << binom_time.count() << std::endl
  //           << "\tStep 3b (PM): " << pm_time.count() << std::endl
  //           << "\tStep 3c (ORAM): " << oram_time.count() << std::endl;
  auto new_val = val_updater(std::move(curr_v));
  size_t new_sb_bytes =
      SuperBlockMaxBaseBlocksInVal(curr_level) * base_block_size_;
  auto new_val_ptr = new_val.data_.get();
  const auto new_val_last = new_val_ptr + new_val.l_;
  for (uint32_t i = 0; i < curr_num_parts; ++i) {
    my_assert(new_val_ptr < new_val_last);
    auto new_part_size = min(new_sb_bytes, new_val_last - new_val_ptr);
    Part new_part(k, new_part_size);
    std::copy_n(new_val_ptr, new_part_size, new_part.data_.get());
    new_val_ptr += new_part_size;
    InsertPart(k, i, std::move(new_part), curr_level, false);
  }
  if (!manual_evict_) {
    size_map_->EvictAll();
    orams_[curr_level]->EvictAll();
    pos_maps_[curr_level]->EvictAll();
    part_oram_key_map_->EvictAll();
  }
}

void OFileStore::AllLevelsReadUpdate(Key k, const ValUpdateFunc &val_updater) {
  auto opt_skp = size_key_pos_map_->ReadAndRemove(k);
  if (!manual_evict_) {
    size_key_pos_map_->EvictAll();
  }
  auto skp = opt_skp.has_value() ? SKPOMapVal(opt_skp->get()) : SKPOMapVal();
  uint32_t curr_size = skp.v_[0];
  ORamKey ok = skp.v_[1];
  ORamPos op = skp.v_[2];
  uint8_t curr_level = LevelIdx(curr_size);

  OptVal curr_v = std::nullopt;
  if (curr_size > 0 && IsLevelNaive(curr_level)) {
    naive_oram_->Fetch();
    if (!manual_evict_) {
      curr_v = naive_oram_->ReadAndRemove(k);
    }
    my_assert(curr_v.has_value());
  } else if (curr_size > 0) {
    allocators_[curr_level]->Free(ok, SuperBlockMaxBaseBlocksInVal(curr_level));
    orams_[curr_level]->FetchPath(op);
    auto opt_ov = orams_[curr_level]->ReadAndRemoveFromStash(ok);
    if (!manual_evict_) {
      orams_[curr_level]->EvictAll();
    }
    my_assert(opt_ov.has_value());
    SuperBlock sb(opt_ov.value().release(),
                  SuperBlockSizeWithMetaInBytes(curr_level));
    auto opt_part = sb.Remove(k);
    my_assert(opt_part);
    curr_v = Val(std::move(opt_part.value().data_), opt_part.value().meta_.l_);
  }

  auto new_v = val_updater(std::move(curr_v));
  if (new_v.l_ == 0) {
    return;
  }
  auto new_size = NumBlocks(new_v.l_);
  auto new_level = LevelIdx(new_size);
  if (!IsLevelNaive(new_level)) {
    if (IsLevelNaive(curr_level)) {
      if (!manual_evict_) {
        naive_oram_->Evict();
      }
    }
    auto opt_ok =
        allocators_[new_level]->Alloc(SuperBlockMaxBaseBlocksInVal(new_level));
    my_assert(opt_ok.has_value());
    ok = opt_ok.value();
    SuperBlock sb = SuperBlock(SuperBlockSizeWithMetaInBytes(new_level), 1);
    sb.Add(k, new_v);
    orams_[new_level]->FetchDummyPath();
    op = orams_[new_level]->AddToStash(ok, std::move(sb.data_));
    if (!manual_evict_) {
      orams_[new_level]->EvictAll();
    }
  } else {
    if (!IsLevelNaive(curr_level) || curr_size == 0) {
      naive_oram_->Fetch();
    }
    naive_oram_->Add(k, std::move(new_v));
    if (!manual_evict_) {
      naive_oram_->Evict();
    }
  }

  skp.v_[0] = new_size;
  skp.v_[1] = ok;
  skp.v_[2] = op;
  size_key_pos_map_->Insert(k, skp.ToBytes());
  if (!manual_evict_) {
    size_key_pos_map_->EvictAll();
  }
}

ValUpdateFunc OFileStore::MakeReader(OptVal &to) {
  return [&to](OptVal ov) -> Val {
    if (ov) {
      to = Val(ov->l_);
      std::copy_n(ov->data_.get(), ov->l_, to->data_.get());
      return std::move(ov.value());
    }
    return {};
  };
}

size_t OFileStore::TotalSizeOfStore() const {
  size_t res = 0;
  if (part_oram_key_map_)
    res += part_oram_key_map_->TotalSizeOfStore();
  if (size_map_)
    res += size_map_->TotalSizeOfStore();
  if (size_key_pos_map_)
    res += size_key_pos_map_->TotalSizeOfStore();
  if (naive_oram_)
    res += naive_oram_->TotalSizeOfStore();
  for (auto &o : orams_)
    if (o)
      res += o->TotalSizeOfStore();
  for (auto &o : allocators_)
    if (o)
      res += o->TotalSizeOfStore();
  for (auto &o : pos_maps_)
    if (o)
      res += o->TotalSizeOfStore();
  return res;
}

bool OFileStore::IsLevelNaive(uint8_t l) const {
  auto path_oram_access = (1ULL + lg(capacity_ >> levels_[l])) * 4ULL *
                          SuperBlockSizeWithMetaInBytes(l);
  auto naive_oram_access =
      SuperBlockMeta::SizeInBytes(NaiveORamMaxPartCount(l)) +
      (uint64_t(capacity_) * uint64_t(base_block_size_));
  return naive_oram_access <= path_oram_access;
}

size_t OFileStore::NaiveORamMaxPartCount(uint8_t l) const {
  auto smallest_size_for_level =
      l > 0 ? (uint64_t(locality_factor_) *
               uint64_t(SuperBlockMaxBaseBlocksInVal(l - 1))) +
                  1ULL
            : uint64_t(capacity_);
  smallest_size_for_level = min(smallest_size_for_level, capacity_);
  return uint64_t(capacity_) / smallest_size_for_level;
}

size_t OFileStore::BytesMoved() const {
  size_t res = 0;
  if (part_oram_key_map_)
    res += part_oram_key_map_->BytesMoved();
  if (size_map_)
    res += size_map_->BytesMoved();
  if (size_key_pos_map_)
    res += size_key_pos_map_->BytesMoved();
  if (naive_oram_)
    res += naive_oram_->BytesMoved();
  for (auto &o : orams_)
    if (o)
      res += o->BytesMoved();
  for (auto &o : allocators_)
    if (o)
      res += o->BytesMoved();
  for (auto &o : pos_maps_)
    if (o)
      res += o->BytesMoved();
  return res;
}

void OFileStore::EvictAll() {
  if (part_oram_key_map_)
    part_oram_key_map_->EvictAll();
  if (size_map_)
    size_map_->EvictAll();
  if (size_key_pos_map_)
    size_key_pos_map_->EvictAll();
  if (naive_oram_)
    naive_oram_->Evict();
  for (auto &o : pos_maps_)
    if (o)
      o->EvictAll();
  for (auto &o : orams_) {
    if (o) {
      for (int i = 0; i < already_fetched_; i++) {
        o->FetchDummyPath();
      }
      o->EvictAll();
    }
  }
  already_fetched_ = 0;
  actually_fetched_ = 0;
}
} // namespace file_oram::o_file_store

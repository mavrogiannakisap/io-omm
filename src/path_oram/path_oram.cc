#include "path_oram.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <openssl/rand.h>

#include "remote_store.grpc.pb.h"
#include "remote_store/server.h" // TODO: Extract common? (kMax...)
#include "utils/assert.h"
#include "utils/backoff.h"
#include "utils/bytes.h"
#include "utils/crypto.h"
#include "utils/namegen.h"
#include "utils/trace.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

using namespace file_oram;
using namespace file_oram::path_oram;
using namespace file_oram::path_oram::internal;

namespace {
// Hack, but better than nothing. (A better solution needs an overhaul.)
const std::string kIdKey = "id";
} // namespace

BlockMetadata::BlockMetadata(bool zero_fill) {
  if (zero_fill) {
    pos_ = 0;
    key_ = 0;
  }
}

BlockMetadata::BlockMetadata(Pos p, Key k) : pos_(p), key_(k) {}

inline static size_t BlockSize(size_t val_len) {
  return sizeof(BlockMetadata) + val_len;
}

Block::Block(bool zero_fill) : meta_(zero_fill) {}
Block::Block(Pos p, Key k, Val v) : meta_(p, k), val_(std::move(v)) {}
Block::Block(char *data, size_t val_len) {
  utils::FromBytes(data, meta_);
  val_ = std::make_unique<char[]>(val_len);
  std::copy(data + sizeof(BlockMetadata),
            data + sizeof(BlockMetadata) + val_len, val_.get());
}

void Block::ToBytes(size_t val_len, char *out) {
  const auto meta_f = reinterpret_cast<const char *>(std::addressof(meta_));
  const auto meta_l = meta_f + sizeof(BlockMetadata);
  std::copy(meta_f, meta_l, out);
  if (val_)
    std::copy(val_.get(), val_.get() + val_len, out + sizeof(BlockMetadata));
}

inline static size_t BucketSize(size_t val_len) {
  return sizeof(BucketMetadata) + (kBlocksPerBucket * BlockSize(val_len));
}

inline static size_t EncryptedBucketSize(size_t val_len) {
  return utils::CiphertextLen(BucketSize(val_len));
}

Bucket::Bucket(char *data, size_t val_len) {
  utils::FromBytes(data, meta_);
  size_t offset = sizeof(BucketMetadata);
  for (int i = 0; i < kBlocksPerBucket; ++i) {
    blocks_[i] = Block(data + offset, val_len);
    offset += BlockSize(val_len);
  }
}

std::unique_ptr<char[]> Bucket::ToBytes(size_t val_len) {
  auto res = std::make_unique<char[]>(BucketSize(val_len));
  const auto meta_f = reinterpret_cast<const char *>(std::addressof(meta_));
  size_t offset = sizeof(BucketMetadata);
  const auto meta_l = meta_f + offset;
  std::copy(meta_f, meta_l, res.get());
  for (int i = 0; i < kBlocksPerBucket; ++i) {
    blocks_[i].ToBytes(val_len, res.get() + offset);
    offset += BlockSize(val_len);
  }
  return std::move(res);
}

const google::protobuf::Empty empty_req;
google::protobuf::Empty empty_res;

// Assumes power-of-two sizes.
std::optional<ORam *>
ORam::Construct(size_t n, size_t val_len, utils::Key enc_key,
                std::shared_ptr<grpc::Channel> channel,
                storage::InitializeRequest_StoreType data_st,
                storage::InitializeRequest_StoreType aux_st, bool upload_stash,
                const std::string &name, bool first_build) {
  if (n & (n - 1)) {     // Not a power of 2.
    return std::nullopt; // Checked here to avoid initializing stub_.
  }
  auto o = new ORam(n, val_len, enc_key, std::move(channel), data_st, aux_st,
                    upload_stash, name, first_build);
  if (o->setup_successful_) {
    return o;
  }
  return std::nullopt;
}

void ORam::Destroy() {
  if (!setup_successful_)
    return;
  grpc::ClientContext ctx;
  ctx.AddMetadata(kIdKey, std::to_string(data_store_id_));
  auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
  ctx.set_deadline(deadline);
  stub_->Destroy(&ctx, empty_req, &empty_res);

  grpc::ClientContext ctx2;
  ctx2.AddMetadata(kIdKey, std::to_string(aux_store_id_));
  deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
  ctx2.set_deadline(deadline);
  stub_->Destroy(&ctx2, empty_req, &empty_res);
}

static const size_t kRemoteStashMaxBlocks = 150;

ORam::ORam(size_t n, size_t val_len, utils::Key enc_key,
           std::shared_ptr<grpc::Channel> channel,
           storage::InitializeRequest_StoreType data_st,
           storage::InitializeRequest_StoreType aux_st, bool upload_stash,
           std::string name, bool first_build)
    : capacity_(n), val_len_(val_len), min_pos_(n - 1), max_pos_((2 * n) - 2),
      store_size_(max(1, n - 1)),
      store_entry_size_(EncryptedBucketSize(val_len)),
      depth_(max(0, ceil(log2(n)) - 1)), enc_key_(enc_key),
      stub_(storage::RemoteStore::NewStub(std::move(channel))),
      data_store_type_(data_st), aux_store_type_(aux_st),
      upload_stash_(upload_stash),
      remote_stash_max_blocks_(min(n, kRemoteStashMaxBlocks)),
      remote_stash_entry_size_(
          min(sizeof(blocks_in_remote_stash_) +
                  utils::CiphertextLen(remote_stash_max_blocks_ *
                                       BlockSize(val_len)),
              storage::kMaxEntryPartSize)),
      remote_stash_entry_count_(
          ((sizeof(blocks_in_remote_stash_) +
            utils::CiphertextLen(remote_stash_max_blocks_ *
                                 BlockSize(val_len))) +
           remote_stash_entry_size_ - 1) /
          remote_stash_entry_size_),
      name_(!name.empty() ? name
                          : utils::GenName({
                                "oram",
                                "",
                                "n",
                                std::to_string(n),
                                "v",
                                std::to_string(val_len),
                            })) {

  // Data store
  grpc::ClientContext ctx;
  storage::InitializeRequest req;
  req.set_n(store_size_);
  req.set_entry_size(store_entry_size_);
  req.set_store_type(data_store_type_);
  req.set_name(name_);
  req.set_first_build(first_build);
  storage::InitializeResponse res;
  auto status = stub_->Initialize(&ctx, req, &res);
  if (!status.ok()) {
    std::clog << "Failed to construct PathORAM data store; "
              << "Status error code: " << status.error_code()
              << ", error message: " << status.error_message() << std::endl;
    return;
  }
  auto it = ctx.GetServerInitialMetadata().find(kIdKey);
  if (it != ctx.GetServerInitialMetadata().end()) {
    data_store_id_ =
        std::stoul(std::string(it->second.begin(), it->second.end()));
  }

  // Stash store
  grpc::ClientContext ctx2;
  req.Clear();
  req.set_n(remote_stash_entry_count_);
  req.set_entry_size(remote_stash_entry_size_);
  req.set_store_type(aux_store_type_);
  req.set_name(name_ + "-stash");
  req.set_first_build(first_build);
  res.Clear();
  status = stub_->Initialize(&ctx2, req, &res);
  if (!status.ok()) {
    std::clog << "Failed to construct PathORAM aux store; "
              << "Status error code: " << status.error_code()
              << ", error message: " << status.error_message() << std::endl;
    return;
  }
  it = ctx2.GetServerInitialMetadata().find(kIdKey);
  if (it != ctx2.GetServerInitialMetadata().end()) {
    aux_store_id_ =
        std::stoul(std::string(it->second.begin(), it->second.end()));
  }

  if (res.found_prebuilt()) {
    was_prebuilt_ = true;
    node_valid_[0] = true;
    inserted_into_ = true;

    local_stash_valid_ = false;
    FetchStash();
  }
  ResetAvailablePaths();
  setup_successful_ = true;
}

size_t ORam::Capacity() const { return capacity_; }
inline Pos ORam::Parent(Pos p) { return p > 0 ? ((p - 1) / 2) : 0; }
inline Pos ORam::LChild(Pos p) { return (2 * p) + 1; }
inline Pos ORam::RChild(Pos p) { return (2 * p) + 2; }

bool ORam::FetchPath(Pos p) {
  // my_assert(node_valid_[0]); // Fill with dummies or batch setup.
  my_assert(p >= min_pos_ && p <= max_pos_); // a leaf index.
  if (cached_nodes_.find(Parent(p)) != cached_nodes_.end()) {
    already_fetched_++;
    return false; // path already fetched.
  }

  available_paths_.erase(p);
  auto path = Path(p);
  if (!node_valid_[0]) { // root invalid.
    cached_nodes_.insert(path.begin(), path.end());
    return false;
  }
  std::vector<Pos> to_fetch;
  for (auto it = path.crbegin(); it < path.crend(); ++it) {
    Pos idx = *it;
    if (cached_nodes_.find(idx) != cached_nodes_.end()) {
      continue; // node already fetched.
    }
    to_fetch.push_back(idx);
    cached_nodes_.insert(idx);
  }
  if (!node_valid_[to_fetch[0]]) {
    return false; // first node invalid.
  }
  bytes_moved_ += to_fetch.size() * EncryptedBucketSize(val_len_);
  AsyncFetch(to_fetch);
  return true;
}

void ORam::DecryptAndAddBucket(Pos p, const std::vector<std::string *> &data) {
  my_assert(!data.empty());
  FetchStash();
  auto ptext = std::make_unique<char[]>(store_entry_size_);
  auto plen =
      utils::DecryptStrArray(data, 0, store_entry_size_, enc_key_, ptext.get());
  my_assert(plen == BucketSize(val_len_));
  auto bu = Bucket(ptext.get(), val_len_);
  std::lock_guard<std::mutex> l{mux_};
  AddBucket(p, bu);
}

void ORam::AddBucket(Pos p, Bucket &bu) {
  node_valid_[LChild(p)] = bu.meta_.flags_ & kLeftChildValid;
  node_valid_[RChild(p)] = bu.meta_.flags_ & kRightChildValid;
  for (int i = 0; i < kBlocksPerBucket; ++i) {
    if (bu.meta_.flags_ & (1 << (2 + i)))
      stash_[bu.blocks_[i].meta_.key_] = std::move(bu.blocks_[i]);
  }
}

void ORam::FetchDummyPath() {
  Pos rd_idx_;
  std::random_device rd;
  std::mt19937 gen(rd());

  std::uniform_int_distribution<> distrib(0, available_paths_.size() - 1);

  rd_idx_ = distrib(gen);
  assert(!available_paths_.empty());
  auto it = available_paths_.begin();
  std::cout << "Random position chosen: " << rd_idx_ << "that correlates to "
            << *it << std::endl;
  std::advance(it, rd_idx_);
  FetchPath(*it);
}

// Called after eviction.
void ORam::ResetAvailablePaths() {
  if (available_paths_.size() == capacity_)
    return;
  for (Pos p = min_pos_; p <= max_pos_; ++p) {
    available_paths_.insert(p);
  }
}

OptVal ORam::ReadAndRemoveFromStash(Key k) {
  FetchStash();
  auto it = stash_.find(k);
  if (it == stash_.end())
    return std::nullopt;
  auto res = std::move((*it).second.val_);
  stash_.erase(it);
  return std::move(res);
}

Pos ORam::AddToStash(Key k, Val v) {
  Pos p = GeneratePos();
  AddToStash(p, k, std::move(v));
  return p;
}

void ORam::AddToStash(Pos p, Key k, Val v) {
  FetchStash();
  inserted_into_ = true;
  my_assert(p >= min_pos_ && p <= max_pos_); // a leaf index.
  stash_[k] = {p, k, std::move(v)};
}

void ORam::EvictAll() {
  my_assert(node_valid_[0]); // Fill with dummies or batch setup.
  if (cached_nodes_.empty()) {
    return;
  }
  FetchStash();

  std::vector<std::unique_ptr<BucketUploader>> uploaders;
  size_t uploaded = 0;
  bool root_done = false;
  for (uint32_t level = depth_; !root_done; --level) {
    std::map<Pos, Bucket> to_send;

    uint64_t min_of_level = (1UL << level) - 1;
    auto it_l = cached_nodes_.lower_bound(min_of_level);
    uint64_t max_of_level = min_of_level << 1;
    const auto &it_h = cached_nodes_.upper_bound(max_of_level);
    while (it_l != it_h) {
      Pos p = *it_l;
      to_send[p] = Bucket();
      ++it_l;
    }

    auto it = stash_.cbegin();
    while (it != stash_.cend()) {
      auto p = (*it).second.meta_.pos_;
      auto p_at_level = PathAtLevel(p, level);
      if (cached_nodes_.find(p_at_level) == cached_nodes_.end()) {
        ++it;
        continue;
      }
      auto &bu = to_send[p_at_level];
      // Use flags field as counter; correct it before encrypting/sending.
      if (bu.meta_.flags_ == kBlocksPerBucket) {
        ++it;
        continue; // bucket full.
      }
      Val &v = const_cast<Val &>((*it).second.val_);
      bu.blocks_[bu.meta_.flags_].meta_ = (*it).second.meta_;
      bu.blocks_[bu.meta_.flags_].val_ = std::move(v);
      ++bu.meta_.flags_;
      it = stash_.erase(it);
    }

    for (auto &item : to_send) {
      auto &[p, bu] = item;
      int num_real_blocks = bu.meta_.flags_;
      bu.meta_.flags_ = 0;
      if (node_valid_[LChild(p)] ||
          (cached_nodes_.find(LChild(p)) != cached_nodes_.end()))
        bu.meta_.flags_ |= kLeftChildValid;
      if (node_valid_[RChild(p)] ||
          (cached_nodes_.find(RChild(p)) != cached_nodes_.end()))
        bu.meta_.flags_ |= kRightChildValid;
      for (int i = 0; i < num_real_blocks; ++i) {
        bu.meta_.flags_ |= (1 << (2 + i));
      }

      uploaders.push_back(AsyncUpload(p, std::move(bu)));
      ++uploaded;
    }
    if (level == 0)
      root_done = true;
    already_fetched_ = 0;
  }

  UploadStash();
  FlushUploaders(uploaders);

  cached_nodes_.clear();
  node_valid_.clear();
  node_valid_[0] = true;
}

void ORam::BatchSetupEvictAll() {
  if (node_valid_[0]) { // not a fresh instance
    return;
  }
  size_t total_bl_count = 0;
  bool root_done = false;
  std::vector<std::unique_ptr<BucketUploader>> uploaders;
  for (uint32_t level = depth_; !root_done; --level) {
    size_t level_bl_count = 0; // for debugging, will be optimized out otherwise
    std::map<Pos, Bucket> to_send;

    uint64_t min_of_level = (1UL << level) - 1;
    uint64_t max_of_level = min_of_level << 1;
    for (auto it = min_of_level; it <= max_of_level; ++it) {
      to_send[it] = Bucket();
    }

    auto it = stash_.cbegin();
    while (it != stash_.cend()) {
      auto pos = (*it).second.meta_.pos_;
      auto p_at_level = PathAtLevel(pos, level);
      auto &bu = to_send[p_at_level];
      // Use flags field as counter; correct it before encrypting/sending.
      if (bu.meta_.flags_ == kBlocksPerBucket) {
        ++it;
        continue; // bucket full.
      }
      Val &v = const_cast<Val &>((*it).second.val_);
      bu.blocks_[bu.meta_.flags_].meta_ = (*it).second.meta_;
      bu.blocks_[bu.meta_.flags_].val_ = std::move(v);
      ++bu.meta_.flags_;
      it = stash_.erase(it);
      ++level_bl_count;
    }

    for (auto &item : to_send) {
      auto &[p, bu] = item;
      int num_real_blocks = bu.meta_.flags_;
      bu.meta_.flags_ = 0;
      bu.meta_.flags_ |= kLeftChildValid;
      bu.meta_.flags_ |= kRightChildValid;
      for (int i = 0; i < num_real_blocks; ++i) {
        bu.meta_.flags_ |= (1 << (2 + i));
      }
      uploaders.push_back(AsyncUpload(p, std::move(bu)));
      ++total_bl_count;
    }
    if (level == 0)
      root_done = true;
  }

  UploadStash();
  FlushUploaders(uploaders);
  node_valid_[0] = true;
  cached_nodes_.clear();
}

void ORam::FillWithDummies() {
  if (node_valid_[0] || inserted_into_ || was_prebuilt_) {
    return;
  }
  std::vector<std::unique_ptr<BucketUploader>> uploaders;
  for (size_t idx = 0; idx < store_size_; ++idx) {
    uploaders.push_back(AsyncUpload(idx, Bucket(true)));
  }
  UploadStash();
  FlushUploaders(uploaders);
  node_valid_[0] = true;
}

Pos ORam::GeneratePos() const {
  Pos res;
  RAND_bytes(reinterpret_cast<unsigned char *>(&res), sizeof(Pos));
  // This RNG is fine because the number of leaves is already a power of two.
  res = (res % (max_pos_ - min_pos_ + 1)) + min_pos_;
  return res;
}

Pos ORam::PathAtLevel(Pos p, uint32_t level) const {
  my_assert(p >= min_pos_ && p <= max_pos_); // a leaf index.
  p = Parent(p);                             // Skip last level
  uint32_t level_diff = depth_ - level;
  uint64_t divider = 1UL << level_diff;
  return ((p - divider + 1UL) / divider);
}

std::vector<Pos> ORam::Path(Pos p) const {
  my_assert(p >= min_pos_ && p <= max_pos_); // a leaf index.
  std::vector<Pos> res;
  p = Parent(p); // Skip last level
  while (p > 0) {
    res.push_back(p);
    p = Parent(p);
  }
  res.push_back(0);
  return res;
}

void ORam::UploadStash() {
  if (!local_stash_valid_ || !upload_stash_) {
    return;
  }
  local_stash_valid_ = false;
  blocks_in_remote_stash_ = min(stash_.size(), remote_stash_max_blocks_);
  most_blocks_in_remote_stash_yet_ =
      max(most_blocks_in_remote_stash_yet_, blocks_in_remote_stash_);

  size_t ptext_size = blocks_in_remote_stash_ * BlockSize(val_len_);
  if (!ptext_size) {
    return;
  }
  auto ptext = new char[ptext_size];
  size_t ptext_offset = 0;
  size_t ctext_size =
      (sizeof(blocks_in_remote_stash_) + utils::CiphertextLen(ptext_size) +
       remote_stash_entry_size_ - 1) /
      remote_stash_entry_size_ * remote_stash_entry_size_; // pad to entry
  auto ctext = new char[ctext_size];
  auto it = stash_.begin();
  for (size_t i = 0; i < blocks_in_remote_stash_; ++i) {
    const auto meta_f =
        reinterpret_cast<const char *>(std::addressof(it->second.meta_));
    size_t to_copy = sizeof(BlockMetadata);
    std::copy_n(meta_f, to_copy, ptext + ptext_offset);
    ptext_offset += to_copy;
    to_copy = val_len_;
    if (it->second.val_) {
      std::copy_n(it->second.val_.get(), to_copy, ptext + ptext_offset);
    }
    ptext_offset += to_copy;
    ++it;
  }
  stash_.erase(stash_.begin(), it);
  utils::ToBytes(blocks_in_remote_stash_, ctext); // Hack for loading prebuilt
  auto enc_success = utils::Encrypt(ptext, ptext_size, enc_key_,
                                    ctext + sizeof(blocks_in_remote_stash_));
  assert(enc_success);
  delete[] ptext;

  grpc::ClientContext ctx;
  ctx.AddMetadata(kIdKey, std::to_string(aux_store_id_));
  std::unique_ptr<grpc::ClientWriter<storage::EntryPart>> writer(
      stub_->WriteMany(&ctx, &empty_res));
  storage::EntryPart ep;
  bool write_success = true;

  size_t idx = 0;
  size_t offset = 0;
  while (offset < ctext_size) {
    size_t to_send = min(remote_stash_entry_size_, ctext_size - offset);
    ep.set_index(idx);
    ep.set_offset(0);
    ep.set_data({ctext + offset, ctext + offset + to_send});
    bytes_moved_ += to_send;
    write_success &= writer->Write(ep);
    ep.Clear();
    if (!write_success) {
      std::clog << "Write failed; data loss!" << std::endl;
    }
    my_assert(write_success);
    ++idx;
    offset += to_send;
  }
  write_success &= writer->WritesDone();
  if (!write_success) {
    std::clog << "Write failed; data loss!" << std::endl;
  }

  auto status = writer->Finish();
  if (!status.ok()) {
    std::clog << "PathORAM UploadStash failed to write!"
              << " status error code: " << status.error_code()
              << "; status error message: " << status.error_message()
              << std::endl;
  }
  my_assert(status.ok());
  delete[] ctext;
}

void ORam::FetchStash() {
  if (local_stash_valid_ || !upload_stash_) {
    return;
  }
  local_stash_valid_ = true;

  // First fetch after constructing prebuild
  size_t blocks_to_fetch = was_prebuilt_ && !setup_successful_
                               ? remote_stash_max_blocks_
                               : blocks_in_remote_stash_;
  if (!blocks_to_fetch) {
    return;
  }

  size_t ptext_size = blocks_to_fetch * BlockSize(val_len_);
  size_t ctext_size =
      sizeof(blocks_in_remote_stash_) + utils::CiphertextLen(ptext_size);
  size_t entries_to_fetch =
      (ctext_size + remote_stash_entry_size_ - 1) / remote_stash_entry_size_;

  storage::ReadManyRequest req;
  for (size_t i = 0; i < entries_to_fetch; ++i) {
    req.add_indexes(i);
  }
  grpc::ClientContext ctx;
  ctx.AddMetadata(kIdKey, std::to_string(aux_store_id_));
  storage::EntryPart se;
  std::unique_ptr<grpc::ClientReader<storage::EntryPart>> reader(
      stub_->ReadMany(&ctx, req));
  bool cancelled = false;
  std::vector<std::string *> ctext_parts{};
  while (reader->Read(&se)) {
    bytes_moved_ += se.data().size();
    my_assert(se.data().size() <= storage::kMaxEntryPartSize);
    my_assert(se.index() < capacity_);
    auto idx = se.index();
    if (idx == 0) {
      utils::FromBytes(se.data().data(), blocks_in_remote_stash_);
      ptext_size = blocks_in_remote_stash_ * BlockSize(val_len_);
      ctext_size =
          sizeof(blocks_in_remote_stash_) + utils::CiphertextLen(ptext_size);
      entries_to_fetch = (ctext_size + remote_stash_entry_size_ - 1) /
                         remote_stash_entry_size_;
    }

    if (idx >= entries_to_fetch) {
      ctx.TryCancel();
      cancelled = true;
      break;
    }
    ctext_parts.push_back(se.release_data());
  }

  auto status = reader->Finish();
  if (!cancelled && !status.ok()) {
    std::clog << "ORAM ReadPath failed to read!"
              << " status error code: " << status.error_code()
              << "; status error message: " << status.error_message()
              << std::endl;
  }
  my_assert(cancelled || status.ok());

  if (!ptext_size) {
    return;
  }

  auto ptext = new char[ptext_size];
  auto plen =
      utils::DecryptStrArray(ctext_parts, sizeof(blocks_in_remote_stash_),
                             utils::CiphertextLen(ptext_size), enc_key_, ptext);
  my_assert(plen == ptext_size);
  for (auto ctext_part : ctext_parts)
    delete ctext_part;
  for (size_t i = 0; i < blocks_in_remote_stash_; ++i) {
    Block bl(ptext + (i * BlockSize(val_len_)), val_len_);
    stash_[bl.meta_.key_] = std::move(bl);
  }
  delete[] ptext;
}

void ORam::AsyncFetch(const std::vector<Pos> &nodes) {
  class Reader : public grpc::ClientReadReactor<storage::EntryPart> {
  public:
    Reader(storage::RemoteStore::Stub *stub, uint32_t data_store_id, Pos p,
           ORam *oram)
        : p_(p), oram_(oram) {
      ctx_.AddMetadata(kIdKey, std::to_string(data_store_id));
      req_.add_indexes(p);
      stub->async()->ReadMany(&ctx_, &req_, this);
      StartRead(&ep_);
      StartCall();
    }

    void OnReadDone(bool ok) override {
      if (ok) {
        std::lock_guard l{mux_};
        ctext_parts_.push_back(ep_.release_data());
        StartRead(&ep_);
      }
    }

    void OnDone(const grpc::Status &s) override {
      if (!s.ok()) {
        std::clog << "PathORAM AsyncFetch::Reader failed to read!"
                  << " status error code: " << s.error_code()
                  << "; status error message: " << s.error_message()
                  << std::endl;
      }
      my_assert(s.ok());
      my_assert(!ctext_parts_.empty());
      {
        std::unique_lock<std::mutex> l{mux_};
        oram_->DecryptAndAddBucket(p_, ctext_parts_);
        for (auto &sp : ctext_parts_)
          delete sp;
        done_ = true;
        // oram_->running_workers_semaphore_->release();
        cv_.notify_one();
      }
    }

    void Await() {
      std::unique_lock<std::mutex> l{mux_};
      while (!done_)
        cv_.wait(l);
    }

  private:
    storage::ReadManyRequest req_;
    Pos p_;
    grpc::ClientContext ctx_;
    storage::EntryPart ep_;
    std::vector<std::string *> ctext_parts_;
    ORam *oram_;
    std::mutex mux_;
    std::condition_variable cv_;
    bool done_ = false;
  };

  std::vector<Reader *> readers;
  auto FlushReaders = [&]() {
    for (auto &r : readers) {
      if (r) {
        r->Await();
        delete r;
      }
    }
    readers.clear();
  };

  for (const auto &p : nodes) {
    running_workers_semaphore_->acquire();
    auto reader = new Reader(stub_.get(), data_store_id_, p, this);
    running_workers_semaphore_->release();

    readers.push_back(reader);
  }
  FlushReaders();
}

BucketUploader::BucketUploader(storage::RemoteStore::Stub *stub,
                               uint32_t store_id, file_oram::path_oram::Pos p,
                               std::unique_ptr<char[]> val, size_t len,
                               std::shared_ptr<AsyncSem> done_sem)
    : p_(p), val_(std::move(val)), store_id_(store_id), len_(len),
      ctx_(std::make_unique<grpc::ClientContext>()), stub_(stub),
      done_sem_(std::move(done_sem)) {

  Start();
}

void BucketUploader::Restart() {
  offset_ = 0;
  ctx_ = std::make_unique<grpc::ClientContext>();
  status_ = grpc::Status();
  done_ = false;
  Start();
}

void BucketUploader::OnWriteDone(bool ok) {
  my_assert(ok); // todo: needed?
  NextWrite();
}

void BucketUploader::OnDone(const grpc::Status &s) {
  if (!s.ok()) {
    fprintf(stderr,
            "PathORAM BucketUploader failed to write! p=%d, try=%d, offset=%zu,"
            " len=%zu status error code=%d; status error message: %s\n",
            p_, try_, offset_, len_, s.error_code(), s.error_message().c_str());
  }
  std::unique_lock<std::mutex> l{mux_};
  done_ = true;
  status_ = s;
  done_sem_->release();
  cv_.notify_one();
}

grpc::Status BucketUploader::Await() {
  std::unique_lock<std::mutex> l{mux_};
  while (!done_)
    cv_.wait(l);
  return std::move(status_);
}

void BucketUploader::Start() {
  ++try_;
  ctx_->AddMetadata(kIdKey, std::to_string(store_id_));
  stub_->async()->WriteMany(ctx_.get(), &empty_res, this);
  NextWrite();
  StartCall();
}

void BucketUploader::NextWrite() {
  if (offset_ < len_) {
    size_t to_send = min(storage::kMaxEntryPartSize, len_ - offset_);
    ep_.set_index(p_);
    ep_.set_offset(offset_);
    auto beginning = val_.get() + offset_;
    if (ep_allocated_len_ == to_send) {
      std::copy_n(beginning, to_send, ep_.mutable_data()->begin());
    } else {
      ep_.set_data({beginning, beginning + to_send});
      ep_allocated_len_ = to_send;
    }
    StartWrite(&ep_);
    offset_ += to_send;
    return;
  }
  StartWritesDone();
}

std::unique_ptr<BucketUploader> ORam::AsyncUpload(Pos p, Bucket bu) {
  running_workers_semaphore_->acquire();
  auto ptext = bu.ToBytes(val_len_);
  auto ctext = std::make_unique<char[]>(EncryptedBucketSize(val_len_));
  bool enc_success =
      utils::Encrypt(ptext.get(), BucketSize(val_len_), enc_key_, ctext.get());
  my_assert(enc_success);
  bytes_moved_ += EncryptedBucketSize(val_len_);
  return std::make_unique<BucketUploader>(
      stub_.get(), data_store_id_, p, std::move(ctext),
      EncryptedBucketSize(val_len_), running_workers_semaphore_);
  // running_workers_semaphore_->release();
}

void ORam::FlushUploaders(
    std::vector<std::unique_ptr<BucketUploader>> &uploaders) {
  for (auto &u : uploaders) {
    if (u) {
      auto s = u->Await();
      my_assert(s.ok());
    }
  }
  uploaders.clear();
}

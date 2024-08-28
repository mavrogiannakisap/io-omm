#include "enigmap.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <utility>
#include <vector>

#include "path_oram/path_oram.h"
#include "path_osm/path_osm.h"
#include "utils/assert.h"
#include "utils/bytes.h"
#include "utils/crypto.h"
#include "utils/trace.h"

using namespace file_oram::path_osm;
using namespace file_oram::path_osm::internal;
using namespace file_oram::path_em;

std::optional<EM> EM::Construct(size_t n, size_t val_len, utils::Key enc_key,
                                std::shared_ptr<grpc::Channel> channel,
                                storage::InitializeRequest_StoreType st) {

  auto em = EM(n, val_len, enc_key, channel, st);
  if (em.setup_successful_) {
    return em;
  }
  return std::nullopt;
}

void EM::Initialization(
    std::vector<std::pair<path_osm::Key, path_osm::Val>> arr_) {
  bool success = Initialize(arr_);
  if (!success) {
    std::cerr << "EM Initialization failed" << std::endl;
    return;
  }
  osm_->SetNextKey(next_key); // sync the next key in enigmap and osm
}

void EM::Destroy() { osm_->Destroy(); }

void EM::InOrderTraversal(std::vector<path_osm::internal::Block> &b_,
                          std::map<ORKey, ORPos> &bps_, size_t l, size_t r) {
  if (l > r || l < 0 || r >= n) {
    return;
  }

  size_t m = (l + r) / 2;
  size_t new_l = (l + m - 1) / 2;
  size_t new_r = (m + r + 1) / 2;

  InOrderTraversal(b_, bps_, l, m - 1);
  InOrderTraversal(b_, bps_, m + 1, r);
  if (m > l) {
    b_[m].meta_.l_ = BlockPointer(new_l, bps_[new_l]);
    if (b_[new_l].meta_.key_ == b_[m].meta_.key_) {
      b_[m].meta_.l_count_ =
          1 + b_[new_l].meta_.l_count_ + b_[new_l].meta_.r_count_;
    }
  }
  if (m < r) {
    b_[m].meta_.r_ = BlockPointer(new_r, bps_[new_r]);
    if (b_[new_r].meta_.key_ == b_[m].meta_.key_) {
      b_[m].meta_.r_count_ =
          1 + b_[new_r].meta_.l_count_ + b_[new_r].meta_.r_count_;
    }
  }
}

bool EM::Initialize(std::vector<std::pair<path_osm::Key, path_osm::Val>> arr_) {
  if (arr_.empty()) {
    return false;
  }

  assert(arr_.size() == n);
  std::vector<path_osm::internal::Block> b_(n);
  std::map<ORKey, ORPos> bps_; // block pointers, BP = { ORKey, ORPos }

  /* We can skip this since the experiments are on synthesized data, i.e., input
   * is already sorted. */
  /*std::sort(arr_.begin(), arr_.end(), [](const std::pair<path_osm::Key,
     path_osm::Val>& a, const std::pair<path_osm::Key, path_osm::Val>& b) {
      return a.first < b.first;
      }); */
  for (int i = 0; i < n; i++) {
    b_[i] = path_osm::internal::Block();
  }

  for (auto &elem : arr_) {
    auto &[k, v] = elem;
    path_osm::internal::Block block(k, v, 1);
    // b_.push_back(block);
    b_[next_key] = block;
    auto pos = osm_->GeneratePos();
    bps_[next_key++] = pos;
  }

  InOrderTraversal(b_, bps_, 0, n - 1);

  osm_->PrebuildEvict(bps_, b_);

  return true;
}

EM::EM(size_t n, size_t val_len, utils::Key enc_key,
       std::shared_ptr<grpc::Channel> channel,
       storage::InitializeRequest_StoreType st)
    : n(n), val_len(val_len), enc_key(enc_key), channel(channel), st(st) {
  auto opt_osm = path_osm::OSM::Construct(n, val_len, enc_key, channel, st);
  if (!opt_osm.has_value()) {
    setup_successful_ = false;
    return;
  }
  osm_ = std::make_unique<path_osm::OSM>(std::move(opt_osm.value()));
  setup_successful_ = true;
}

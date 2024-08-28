#include "o_file_store.h"

#include <cstddef>
#include <iostream>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "remote_store/common.h"
#include "utils/assert.h"
#include "utils/bench.h"
#include "utils/crypto.h"

using namespace file_oram;
using namespace file_oram::o_file_store;
using namespace file_oram::storage;
using namespace file_oram::utils;

int main() {
  auto args = grpc::ChannelArguments();
  args.SetMaxReceiveMessageSize(INT_MAX);
  args.SetMaxSendMessageSize(INT_MAX);
  auto channel = grpc::CreateCustomChannel("localhost:50052",
                                           grpc::InsecureChannelCredentials(),
                                           args);
  auto ek = DumbKey();
  constexpr uint8_t nlg = 17;
  constexpr uint32_t n = 1UL << nlg;
  constexpr uint8_t bslg = 4;
  constexpr uint32_t bs = 1UL << bslg;

  for (const auto &s : utils::SList(n)) {
    for (const auto &lf_loop : utils::LfList(n, s)) {
      auto lf = lf_loop;
      std::clog << "Creating OFileStore with lf=" << lf << ", s=" << int(s) << std::endl;
      auto opt_ofs = file_oram::o_file_store::OFileStore::Construct(
          n, s, lf, bs, ek, channel, kFileStore, kRamStore);
      my_assert(opt_ofs.has_value());
      auto &ofs = opt_ofs.value();
      std::clog << "OFileStore created with lf=" << lf << ", s=" << int(s) << ", levels=[";
      for (auto &l : ofs.levels_) {
        std::clog << int(l) << " ";
      }
      std::clog << "]" << std::endl;

      Val v1(bs);
      std::fill(v1.data_.get(), v1.data_.get() + bs, static_cast<char>(12));
      Val v2(bs);
      std::fill(v2.data_.get(), v2.data_.get() + bs, static_cast<char>(11));
      ofs.AppendSingleLevel(1, std::move(v1));
      ofs.AppendSingleLevel(1, std::move(v2));

      for (uint32_t i = 1UL; i < n; i <<= 2) {
        auto len = i * bs;
        Val v(len);
        std::fill(v.data_.get(), v.data_.get() + len, static_cast<char>(i));
        ofs.Append(i, std::move(v));
      }

      for (uint32_t i = 1UL; i < n; i <<= 2) {
        OptVal opt_v;
        ofs.ReadUpdate(i, OFileStore::MakeReader(opt_v));
        my_assert(opt_v.has_value());
        auto &v = opt_v.value();
        my_assert(v.l_ == i * bs);
        for (size_t j = v.l_; j > 0; --j) {
          my_assert(v.data_.get()[j - 1] == static_cast<char>(i));
        }
      }

      for (uint32_t i = 1UL; i < n; i <<= 2) {
        auto opt_v = ofs.Delete(i);
        my_assert(opt_v.has_value());
        auto &v = opt_v.value();
        my_assert(v.l_ == i * bs);
        for (size_t j = v.l_; j > 0; --j) {
          my_assert(v.data_.get()[j - 1] == static_cast<char>(i));
        }
      }

      ofs.Destroy();
    }
  }
}

int main() { return 0; }

//#include <algorithm>
//#include <chrono>
//#include <cstddef>
//#include <cstdint>
//#include <iostream>
//#include <memory>
//#include <string>
//
//#include <grpc/grpc.h>
//#include <grpcpp/channel.h>
//#include <grpcpp/client_context.h>
//#include <grpcpp/create_channel.h>
//#include <grpcpp/security/credentials.h>
//
//#include "remote_store.grpc.pb.h"
//
//namespace file_oram::storage {
//
//const static std::string kIdKey = "id";
//
//class RemoteStoreClient {
// public:
//  explicit RemoteStoreClient(size_t n, size_t entry_size,
//                             const std::shared_ptr<grpc::Channel> &channel,
//                             InitializeRequest_StoreType store_type)
//      : n_(n),
//        entry_size_(entry_size),
//        stub_(RemoteStore::NewStub(channel)),
//        store_type_(store_type) {
//    grpc::ClientContext ctx;
//    InitializeRequest req;
//    req.set_n(n_);
//    req.set_entry_size(entry_size_);
//    req.set_store_type(store_type_);
//    InitializeResponse rsp;
//    auto status = stub_->Initialize(&ctx, req, &rsp);
//    if (!status.ok()) {
//      std::clog << "Status error code: " << status.error_code()
//                << ", error message: " << status.error_message() << std::endl;
//      return;
//    }
//    std::clog << "Initialization successful for n=" << n_ << std::endl;
//    auto it = ctx.GetServerInitialMetadata().find(kIdKey);
//    if (it != ctx.GetServerInitialMetadata().end()) {
//      store_id_ = std::stoul(std::string(it->second.begin(), it->second.end()));
//      std::clog << "Store id is: " << store_id_ << std::endl;
//    }
//  }
//
//  ~RemoteStoreClient() {
//    grpc::ClientContext ctx;
//    ctx.AddMetadata(kIdKey, std::to_string(store_id_));
//    DestroyRequest req;
//    DestroyResponse rsp;
//    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
//    ctx.set_deadline(deadline);
//    stub_->Destroy(&ctx, req, &rsp);
//  }
//
//  void ReadMany(const std::vector<uint64_t> indexes) {
//    grpc::ClientContext ctx;
//    ctx.AddMetadata(kIdKey, std::to_string(store_id_));
//    ReadManyRequest req;
//    for (const auto &idx : indexes)
//      req.add_indexes(idx);
//    Entry e;
//    std::unique_ptr<grpc::ClientReader<Entry>> reader(
//        stub_->ReadMany(&ctx, req));
//    while (reader->Read(&e)) {
//      std::clog << "Entry at index=" << e.index() << " has data:";
//      for (const auto &item : e.data()) std::clog << " " << (int) item;
//      std::clog << std::endl;
//    }
//    auto status = reader->Finish();
//    if (!status.ok()) {
//      std::clog << "Status error code: " << status.error_code()
//                << ", error message: " << status.error_message() << std::endl;
//      return;
//    }
//    std::clog << "ReadPath successful" << std::endl;
//  }
//
//  void WriteMany(const std::vector<Entry> &data) {
//    grpc::ClientContext ctx;
//    ctx.AddMetadata(kIdKey, std::to_string(store_id_));
//    WriteManyResponse rsp;
//
//    std::unique_ptr<grpc::ClientWriter<Entry>> writer(
//        stub_->WriteMany(&ctx, &rsp));
//    bool write_success = true;
//    for (const auto &e : data) {
//      write_success &= writer->Write(e);
//      if (!write_success) {
//        std::clog << "Write error!" << std::endl;
//        break;
//      }
//    }
//    write_success &= writer->WritesDone();
//    auto status = writer->Finish();
//
//    std::clog << "WriteBuckets done; write_success="
//              << ((write_success && status.ok()) ? "true" : "false")
//              << std::endl;
//    if (!status.ok()) {
//      std::clog << "Status error code: " << status.error_code()
//                << ", error message: " << status.error_message() << std::endl;
//    }
//  }
//
// private:
//  size_t n_;
//  size_t entry_size_;
//  uint32_t store_id_ = 0;
//  InitializeRequest_StoreType store_type_;
//  std::unique_ptr<RemoteStore::Stub> stub_;
//};
//
//} // namespace file_oram::storage
//
//int main() {
//  constexpr size_t n = 7;
//  constexpr size_t es = 1ULL << 2;
////  constexpr int num_accesses = 3;
//  constexpr auto store_type =
//      file_oram::storage::InitializeRequest_StoreType_MMAP_FILE;
//
//  auto args = grpc::ChannelArguments();
//  args.SetMaxReceiveMessageSize(INT_MAX);
//  args.SetMaxSendMessageSize(INT_MAX);
//  auto channel = grpc::CreateCustomChannel(
//      "localhost:50052", grpc::InsecureChannelCredentials(), args);
//
//  file_oram::storage::RemoteStoreClient client(n, es, channel, store_type);
//
//  {
//    // Write
//    std::string v(n, '\0');
//    for (char &c : v) { v = 0x01; }
//    for ()
//  }
//
//  auto indexes = std::vector<uint64_t>{5, 2, 0};
//  client.ReadMany(indexes);
//  std::vector<std::string> res;
//  for (int i = 0; i < 3; ++i) {
//    file_oram::storage::Entry e;
//    e.set_index(indexes[i]);
//    char data[4];
//    for (char &j : data) { j = i; }
//    e.set_data(std::string(data, data + 4));
//    wp.push_back(e);
//  }
//  client.WriteMany(wp);
//  client.ReadMany({6, 2, 0});
//  return 0;
//}

#pragma once
#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <semaphore>
#include <set>
#include <vector>

namespace Storage {
#define F_ST 0

template <class T> class RAMServer {
private:
  std::vector<T> mem;
  uint32_t n;

public:
  RAMServer(std::vector<T> &blocks, uint32_t n) { mem.swap(blocks); }
  RAMServer() {}
  RAMServer(uint32_t n) { mem.reserve(n); }
  ~RAMServer() { mem.clear(); }

  void WriteMany(std::map<uint32_t, T> &to_write) {
    assert(to_write.size());
    for (auto &it : to_write) {
      auto &[idx, b] = it;
      mem.insert(mem.begin() + idx, b);
    }

    to_write.clear();
  }

  void Write(T &b, uint32_t idx) { mem.insert(mem.begin() + idx, b); }

  void ReadAll(std::vector<T> &res) {
    assert(mem.size() > 0);
    assert(res.capacity() > 0);
    res = mem;
  }

  void ReadMany(std::vector<uint32_t> indexes, std::vector<T> &res) {
    assert(res.capacity() > 0);
    for (auto &idx : indexes) {
      assert(idx < n && idx >= 0);
      res.push_back(mem[idx]);
    }
  }

  void Read(uint32_t index, T &res) {
    assert(index < n && index >= 0);
    res.Copy(mem[index]);
  }
};

template <class T> class Server {
private:
  std::fstream file_;
  uint32_t n;
  uint32_t entry_size_;
  std::string name;

public:
  Server(std::string file_path, uint32_t n, uint32_t entry_size,
         std::string name)
      : n(n), entry_size_(entry_size) {
    file_.open(file_path, std::ios::binary | std::ios::in | std::ios::out);
    std::clog << "Initializing ORam, name=" << name << " n= " << n
              << " bucket_size=" << entry_size_ << " path= " << file_path
              << std::endl;
    if (!file_) {
      std::clog << "[LOCAL SERVER] ERROR: Initialization - cannot open '"
                << file_path << "'" << std::endl;
      std::exit(1);
    }
  }

  ~Server() { file_.close(); }
  void WriteAll(const char *bytes, uint32_t size) {
    file_.seekp(0);
    file_.write(bytes, size);
  }

  void WriteAll(std::vector<T> &mem, uint32_t index) {
    file_.seekp(index * entry_size_);
    std::string to_write_;
    for (auto &b : mem) {
      char *b_ser = (char *)malloc(entry_size_);
      b.ToBytes(entry_size_, b_ser);
      to_write_.append(b_ser, entry_size_);
      delete[] b_ser;
    }
    file_.write(to_write_.c_str(), entry_size_ * mem.size());
  }

  void Write(char *bytes, uint32_t index) {
    file_.seekp(index * entry_size_);
    file_.write(bytes, entry_size_);
    if (file_.fail()) {
      std::clog << "[LOCAL SERVER] ERROR: failed to write in Write(char "
                   "*bytes, uint32_t index\n";
    }
  }

  void WriteMany(const char *bytes, std::vector<uint32_t> idxs) {
    size_t offset = 0;
    for (auto &idx : idxs) {
      auto beginning = bytes + offset;
      file_.seekp(idx * entry_size_);
      char *out = (char *)malloc(entry_size_);
      std::copy_n(beginning, entry_size_, out);
      file_.write(out, entry_size_);

      if (file_.fail()) {
        std::clog << "[LOCAL SERVER] ERROR: failed to write in WriteMany(char "
                     "*bytes, vector<> indexes)\n";
      }
      delete[] out;
      offset += entry_size_;
    }
  }

  void WriteMany(std::unique_ptr<char[]> bytes, std::vector<uint32_t> idxs) {
    size_t offset = 0;
    for (auto &idx : idxs) {
      auto beginning = bytes.get() + offset;
      file_.seekp(idx * entry_size_);
      char *out = (char *)malloc(entry_size_);
      std::copy_n(beginning, entry_size_, out);
      file_.write(out, entry_size_);

      if (file_.fail()) {
        std::clog << "[LOCAL SERVER] ERROR: failed to write in WriteMany(char "
                     "*bytes, vector<> indexes)\n";
      }
      delete[] out;
      offset += entry_size_;
    }
  }

  /* void WriteMany(std::map<uint32_t, T> &to_write) {
      assert(to_write.size());
      for (auto &it : to_write) {
          auto &[idx, b] = it;
          file_.seekp(idx * entry_size_);
          char *b_ser = (char *)malloc(entry_size_);
          b.ToBytes(entry_size_, b_ser);
          file_.write(b_ser, entry_size_);

          delete[] b_ser;
      }
  }


  void WriteMany(std::vector<T> &mem, std::vector<uint32_t> &index) {
    assert(mem.size() == index.size());
    auto idx = index.begin();
    for(auto &b : mem) {
        file_.seekp(*idx * entry_size_);
        char *b_ser = (char *)malloc(entry_size_);
        b.ToBytes(entry_size_, b_ser);
        file_.write(b_ser, entry_size_);

        delete[] b_ser;
        idx++;
    }
  }*/

  void WriteBytes(char *bytes, size_t count, uint32_t idx) {
    assert(count);
    assert(bytes);
    file_.seekp(idx * entry_size_);
    file_.write(bytes, count * entry_size_);
  }

  void WriteBytes(std::unique_ptr<char[]> bytes, size_t count, uint32_t idx) {
    assert(count);
    assert(bytes);
    file_.seekp(idx * entry_size_);
    file_.write(bytes.get(), entry_size_);

    if (file_.fail()) {
      std::clog << "[LOCAL SERVER] ERROR: failed to write in WriteMany(char "
                   "*bytes, vector<> indexes)\n";
      std::exit(1);
    }
  }

  void Write(T *b, uint32_t ind) {
    assert(b != NULL);
    file_.seekp(ind * entry_size_);

    char *to_write_ = (char *)malloc(entry_size_ * 1);
    b->ToBytes(entry_size_, to_write_);
    file_.write(to_write_, entry_size_);
    delete[] to_write_;
  }

  void ReadAll(uint32_t count, std::vector<T> &res) {
    uint32_t bs_ = entry_size_;
    file_.seekg(F_ST);
    char *in = (char *)malloc(bs_ * count);
    file_.read(in, bs_ * count);
    for (int i = 0; i < bs_ * count; i += bs_) {
      char *b_ser = (char *)malloc(bs_);
      std::copy(in + i, in + i + bs_, b_ser);
      res.push_back(std::move(b_ser));
      delete[] b_ser;
    }
    delete[] in;
  }

  void Read(uint32_t idx, std::vector<std::string> &res) {
    file_.seekg(idx * entry_size_);
    char *in = (char *)malloc(entry_size_);
    file_.read(in, entry_size_);
    res.push_back(in);
    free(in);
  }

  void Read(uint32_t idx, std::string &res) {
    file_.seekg(idx * entry_size_);
    res.resize(entry_size_);
    file_.read(&res[0], entry_size_);
  }

  void ReadMany(std::vector<uint32_t> idxs, std::vector<char *> res) {
    for (auto &idx : idxs) {
      file_.seekg(idx * entry_size_);
      char *in = (char *)malloc(entry_size_);
      file_.read(in, entry_size_);
      res.push_back(std::move(in));
    }
  }

  void ReadMany(std::vector<uint32_t> idxs, std::string &res) {
    std::clog << "IN HERE\n";
    for (auto &idx : idxs) {
      assert(idx < n * entry_size_);
      file_.seekg(idx * entry_size_);
      char *in = (char *)malloc(entry_size_);
      assert(in);
      file_.read(in, entry_size_);
      res.append(in, entry_size_);
      free(in);
    }
  }
};
} // namespace Storage

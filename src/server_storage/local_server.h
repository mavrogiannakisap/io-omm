#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <semaphore>
#include <set>
#include <vector>
#include <iostream>
#include <fstream>
#include <iterator>
#include <assert.h>
#include <numeric>

namespace Storage {
#define F_ST 0

using Pos = uint32_t;
using Key = uint32_t;
using Val = std::unique_ptr<char[]>;

template<class T>
class RAMServer {
private:
    std::vector<Block> mem;
    uint32_t n;
public:
    RAMServer(std::vector<Block> &blocks, uint32_t n);
    RAMServer(){};
    RAMServer(uint32_t n);
    ~RAMServer();
    void WriteMany(std::vector<T> &to_write, std::vector<uint32_t> &index);
    void Write(Block &b, uint32_t ind);
    void ReadAll(std::vector<T> &res);
    void ReadMany(std::vector<uint32_t> indexes, std::vector<T> &res);
    void Read(uint32_t index, T &res);
};

template<class T>
class Server
{
private:
    std::fstream file_;
    uint32_t n;
    /* data */
public:
    Server(std::string file_path, uint32_t n);
    ~Server();
    void WriteAll(std::vector<T> &mem, uint32_t index, uint32_t val_len);
    void WriteMany(std::vector<T> &mem, std::vector<uint32_t> &index, uint32_t val_len);
    void Write(T *b, uint32_t ind, uint32_t val_len);
    void ReadAll(uint32_t val_len, uint32_t count, std::vector<T> &res);
    void ReadMany(std::vector<uint32_t> indexes, uint32_t val_len, std::vector<T> &res);
    void Read(uint32_t index, uint32_t val_len, T &res);
};
} // namespace Storage::Server

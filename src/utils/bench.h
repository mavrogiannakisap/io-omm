#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <cxxopts.hpp>
#include <utility>

#include "trace.h"

namespace file_oram::utils {
using klock = std::chrono::high_resolution_clock; // name clock exists in time.h

// Returns (S, L) pairs to bench for N=2^23
static inline std::vector<std::pair<uint64_t, uint64_t>> BenchSizes() {
  std::vector<std::pair<uint64_t, uint64_t >> res{};
  res.emplace_back(24, 1);
  res.emplace_back(3, 128);
  res.emplace_back(3, 16);
  res.emplace_back(3, 2);
  res.emplace_back(2, 1);
  return res;
}

static inline std::vector<uint64_t> AppendCaps() {
  std::vector<uint64_t> res{};
  for (uint64_t n = 5; n < 24; n += 3) res.emplace_back(n);
  return res;
}

static inline std::vector<uint64_t> AppendValueSizes() {
  std::vector<uint64_t> res{};
  res.emplace_back(128);
  res.emplace_back(512);
  res.emplace_back(1024);
  res.emplace_back(4096);
  return res;
}

static inline std::vector<uint64_t> InsertValueSizes() {
  std::vector<uint64_t> res{};
  res.emplace_back(128);
  res.emplace_back(512);
  res.emplace_back(1024);
  res.emplace_back(4096);
  res.emplace_back(16384);
//  res.emplace_back(65535);
  return res;
}

static inline std::vector<size_t> SList(size_t n) {
  size_t nl = lround(ceil(log2(n)));
  std::vector<size_t> res{};
  for (size_t i = 1; i <= nl + 1; ++i)
    if ((nl + 1) % i == 0)
      res.push_back(i);
  return res;
}

static inline std::vector<size_t> LfList(size_t n, size_t s) {
  std::vector<size_t> res{};
  res.push_back(1);
  if (s == 1) {
    return res;
  }
  size_t nl = lround(ceil(log2(n)));
  size_t level_every = (nl + 1) / s;
  for (size_t i = 2; i < (1UL << level_every); i <<= 1) {
    res.push_back(i);
  }
  return res;
}

std::string header_prefix = "test,data_mem,aux_mem,ssd,cached,bbs,n,s,lf,id,";
std::string header_suffix = "total_time";

static void PrintCsvHeaders(const std::string &ops) {
  std::cout << header_prefix << ops << "," << header_suffix << std::endl;
}

class Config {
 public:
  uint8_t num_runs_ = 0;
  uint32_t capacity_ = 0;
  uint8_t num_levels_ = 0;
  uint32_t locality_factor_ = 0;
  uint32_t base_block_size_ = 0;
  std::string store_path_ = "";
  uint8_t min_val_len_ = 0;
  uint8_t max_val_len_ = 0;
  bool is_ssd_ = false;
  bool is_cached_ = true;
  bool is_data_mem_ = true;
  bool is_aux_mem_ = true;
  bool print_csv_headers_ = false;
  std::string data_file_ = "";
  std::string storage_type_ = "";
  uint8_t initial_level_ = 10;
  bool full_init_ = false;

  Config() = default;
  Config(int argc, char **argv) {
    cxxopts::Options opts("Benchmark");
    opts.add_options()
        ("r,num_runs", "Number of runs to average",
         cxxopts::value<uint8_t>()->default_value("1"))
        ("N,capacity", "Capacity of the instance to run (power of two)",
         cxxopts::value<uint8_t>()->default_value("10"))
        ("s,num_levels", "Number of levels to keep",
         cxxopts::value<uint8_t>()->default_value("0"))
        ("L,locality_factor", "Maximum locality factor fo accesses",
         cxxopts::value<uint32_t>()->default_value("1"))
        ("v,base_block_size", "Base size of values",
         cxxopts::value<uint32_t>()->default_value("4096"))
        ("p,store_path",
         "Path to store data - empty for ram; if includes \"ssd\" in path, "
         "that will be recorded; if includes \"nocache\" in path, all accesses "
         "will be followed by a flush",
         cxxopts::value<std::string>()->default_value(""))
        ("m,min_val_len", "Minimum value length to measure (power of two)",
         cxxopts::value<uint8_t>()->default_value("0"))
        ("M,max_val_len", "Maximum value length to measure (power of two)",
         cxxopts::value<uint8_t>()->default_value("0"))
        ("d,data_on_disk", "Store the main data (ORAMs) on disk") // default type is bool
        ("a,aux_on_disk", "Store the auxiliary data (OMaps/OSTs) on disk") // default type is bool
        ("print_csv_headers", "Print CSV headers") // default type is bool
        ("h,help", "Print usage") // default type is bool
        ("full_init", "Initialize scheme with real values") // default type is bool
        ("f,data_file", "Path to crimes of chicago data preprocessed file",
         cxxopts::value<std::string>()->default_value(""))
        ("t,storage_type","Define the type of storage {RAM, HDD, SSD}",
        cxxopts::value<std::string>()->default_value("RAM"))
        ("i,initial_level","The first level that will be selected (selection goes upwards)",
        cxxopts::value<uint8_t>()->default_value("12"));
    auto parsed = opts.parse(argc, argv);

    if (parsed.count("help")) {
      std::cerr << opts.help() << std::endl;
      exit(0);
    }


    num_runs_ = parsed["num_runs"].as<uint8_t>();
    capacity_ = 1ULL << parsed["capacity"].as<uint8_t>();
    num_levels_ = parsed["num_levels"].as<uint8_t>();
    if (num_levels_ == 0) {
      num_levels_ = parsed["capacity"].as<uint8_t>() + 1;
    }
    locality_factor_ = parsed["locality_factor"].as<uint32_t>();
    base_block_size_ = parsed["base_block_size"].as<uint32_t>();
    store_path_ = parsed["store_path"].as<std::string>();
    min_val_len_ = parsed["min_val_len"].as<uint8_t>();
    max_val_len_ = parsed["max_val_len"].as<uint8_t>();
    is_data_mem_ = store_path_.empty() || !parsed.count("data_on_disk");
    is_aux_mem_ = store_path_.empty() || !parsed.count("aux_on_disk");
    is_cached_ = store_path_.find("nocache") == std::string::npos;
    is_ssd_ = store_path_.find("ssd") != std::string::npos;
    print_csv_headers_ = parsed.count("print_csv_headers");
    data_file_ = parsed["data_file"].as<std::string>();
    storage_type_ = parsed["storage_type"].as<std::string>();

    initial_level_ = parsed["initial_level"].as<uint8_t>();

  }

  friend std::ostream &operator<<(std::ostream &os, const Config &config) {
    os << "num_runs_: " << int(config.num_runs_)
       << " capacity_: " << config.capacity_
       << " num_levels_: " << int(config.num_levels_)
       << " locality_factor_: " << config.locality_factor_
       << " base_block_size_: " << config.base_block_size_
       << " store_path_: " << config.store_path_
       << " min_val_len_: " << int(config.min_val_len_)
       << " max_val_len_: " << int(config.max_val_len_)
       << " is_data_mem_: " << int(config.is_data_mem_)
       << " is_aux_mem_: " << int(config.is_aux_mem_)
       << " is_ssd_: " << int(config.is_ssd_)
       << " is_cached_: " << int(config.is_cached_)
       << " data_file_: " << config.data_file_;
    return os;
  }
};

class Measurement {
 public:
  const std::string name_;
  Config config_;
  std::map<std::pair<std::string, uint32_t>, double> numbers_;
  const std::chrono::time_point<klock> start_ = klock::now();
  std::chrono::time_point<klock> last_read_ = start_;

  Measurement() = default;

  Measurement(std::string name, Config c) : name_(std::move(name)), config_(std::move(c)) {}

  friend Measurement operator+(const Measurement &m1, const Measurement &m2) {
    Measurement res{m1.name_, m1.config_};
    for (const auto &time : m1.numbers_) {
      res.numbers_[time.first] = m1.numbers_.find(time.first)->second
          + m2.numbers_.find(time.first)->second;
    }
    return res;
  }

  Measurement &operator+=(const Measurement &m) {
    for (const auto &time : m.numbers_) {
      numbers_[time.first] += m.numbers_.find(time.first)->second;
    }
    return *this;
  }

  friend Measurement operator/(const Measurement &m, const int &d) {
    Measurement res{m.name_, m.config_};
    for (const auto &time : m.numbers_) {
      res.numbers_[time.first] = m.numbers_.find(time.first)->second / d;
    }
    return res;
  }

  Measurement &operator/=(const int &d) {
    for (const auto &time : numbers_) {
      numbers_[time.first] /= d;
    }
    return *this;
  }

  // Returns time since Measurement started or Took was last called.
  double Took() {
    auto now = klock::now();
    std::chrono::duration<double> res = now - last_read_;
    last_read_ = now;
    return res.count();
  }

  friend std::ostream &operator<<(std::ostream &os, const Measurement &m) {
    std::chrono::duration<double> total_time = klock::now() - m.start_;
    std::set < std::string > keys;
    std::set < uint32_t > ids;
    for (const auto &time : m.numbers_) {
      auto &key = time.first.first;
      auto &id = time.first.second;
      keys.insert(key);
      ids.insert(id);
    }

    auto header = header_prefix;
    for (auto &key : keys) {
      header += key + ",";
    }
    header += header_suffix;
    if (m.config_.print_csv_headers_) {
      os << header << std::endl;
    }

    auto row_prefix = m.name_ + ","
        + std::to_string(int(m.config_.is_data_mem_)) + ","
        + std::to_string(int(m.config_.is_aux_mem_)) + ","
        + std::to_string(int(m.config_.is_ssd_)) + ","
        + std::to_string(int(m.config_.is_cached_)) + ","
        + std::to_string(m.config_.base_block_size_) + ","
        + std::to_string(m.config_.capacity_) + ","
        + std::to_string(int(m.config_.num_levels_)) + ","
        + std::to_string(m.config_.locality_factor_) + ",";
    auto row_suffix = std::to_string(total_time.count());

    for (const auto id : ids) {
      auto row = row_prefix + std::to_string(id) + ",";
      for (auto &key : keys) {
        auto f_it = m.numbers_.find({key, id});
        if (f_it == m.numbers_.end()) {
          row += "0,";
          continue;
        }
        row += std::to_string(f_it->second) + ",";
      }
      row += row_suffix;
      std::cout << row << std::endl;
    }
    return os;
  }
};

} // namespace file_oram::utils

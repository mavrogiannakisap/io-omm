#ifndef FILEORAM_UTILS_BYTES_H_
#define FILEORAM_UTILS_BYTES_H_

#include <algorithm>
#include <array>
#include <memory>
#include <string>

namespace file_oram::utils {

//template<typename T>
//inline std::string ToBytes(const T &object) {
//  const auto begin = reinterpret_cast<const char *> (std::addressof(object));
//  const auto end = begin + sizeof(T);
//  return {begin, end};
//}
//
template<typename T>
inline std::array<char, sizeof(T)> ToBytes(const T &object) {
  std::array<char, sizeof(T)> bytes;

  const auto begin = reinterpret_cast<const char *> (std::addressof(object));
  const auto end = begin + sizeof(T);
  std::copy(begin, end, bytes.begin());

  return bytes;
}

template<typename T>
inline void ToBytes(const T &object, char *to) {
  const auto begin = reinterpret_cast<const char *> (std::addressof(object));
  const auto end = begin + sizeof(T);
  std::copy(begin, end, to);
}

template<typename T>
inline std::unique_ptr<char[]> ToUniquePtrBytes(const T &object) {
  auto res = std::make_unique<char[]>(sizeof(T));
  const auto begin = reinterpret_cast<const char *> (std::addressof(object));
  std::copy_n(begin, sizeof(T), res.get());
  return std::move(res);
}

//template<typename T>
//T &FromBytes(const std::string &data, T &object) {
//  auto begin_object = reinterpret_cast<char *> (std::addressof(object));
//  std::copy(std::begin(data), std::end(data), begin_object);
//
//  return object;
//}
//
template<typename T>
T &FromBytes(const char *data, T &object) {
  auto begin_object =
      reinterpret_cast<unsigned char *> (std::addressof(object));
  std::copy(data, data + sizeof(T), begin_object);

  return object;
}

template<typename T>
T &FromBytes(const std::array<char, sizeof(T)> &bytes, T &object) {
  auto begin_object = reinterpret_cast<char *> (std::addressof(object));
  std::copy(std::begin(bytes), std::end(bytes), begin_object);

  return object;
}

} // namespace file_oram::utils

#endif //FILEORAM_UTILS_BYTES_H_

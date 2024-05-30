#ifndef FILEORAM_UTILS_BACKOFF_H_
#define FILEORAM_UTILS_BACKOFF_H_

#include <chrono>
#include <random>
#include <thread>

namespace file_oram::utils {

using std::chrono::milliseconds;

#define min(a, b) ((a)<(b)?(a):(b))
#define next(last, max) (2 * (last) + (max))

static const auto kMinBackoff = milliseconds(50);
static const auto kMaxBackoff = milliseconds(5000);

inline static void JitterySleep(milliseconds max) {
  std::default_random_engine generator;
  std::uniform_int_distribution<milliseconds::rep>
      distribution(0, max.count());
  std::this_thread::sleep_for(milliseconds(distribution(generator)));
}

// returns the duration it slept for, to be given as last in the next call.
inline static auto ExponentialBackoff(milliseconds last = kMinBackoff,
                                      milliseconds max = kMaxBackoff) {
  auto next = min(2 * last, max);
  std::this_thread::sleep_for(next);
  return next;
}
} // namespace file_oram::utils

#endif //FILEORAM_UTILS_BACKOFF_H_

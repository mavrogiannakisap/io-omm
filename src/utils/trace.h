/*
 * Source: https://codereview.stackexchange.com/a/67400/272659
 */
#ifndef FILEORAM_SRC_UTILS_TRACE_H_
#define FILEORAM_SRC_UTILS_TRACE_H_

//#define MY_TRACE_ON 1

#ifdef MY_TRACE_ON

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

const static bool kTracePossible = true;
static std::atomic<bool> do_trace = false;

using klock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double>;
using TimePoint = std::chrono::time_point<klock, Duration>;

class TimeLogger {
 public:
  TimeLogger(std::string x) : name_(std::move(x)) {}
  ~TimeLogger() {
    if (do_trace) {
      Duration took = klock::now() - start_;
      std::fprintf(::stderr, ">>> \"%s\",%f\n", name_.c_str(), took.count());
    }
  }
 private:
  std::string name_;
  TimePoint start_ = klock::now();
};

#define MY_TRACE_FOR(x) TimeLogger __my_trace_time_logger(x);
#define MY_TRACE        MY_TRACE_FOR(__PRETTY_FUNCTION__)

#else
const static bool kTracePossible = false;
const static bool do_trace = false;

#define MY_TRACE_FOR(x)
#define MY_TRACE
#endif

#endif //FILEORAM_SRC_UTILS_TRACE_H_

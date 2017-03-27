#pragma once

#include <stdio.h>
#include <ctime>

namespace fluorine {

class Timer {
public:
  Timer() : start_(0) {}
  Timer(const Timer &) = delete;
  void operator=(const Timer &) = delete;

  void start() { start_ = clock(); }

  double elapsed_milliseconds() const {
    return static_cast<double>(clock() - start_) / CLOCKS_PER_SEC * 1000;
  }

private:
  clock_t start_;
};

class TimerGuard {
public:
  TimerGuard() : timer_() {
    timer_.start();
    fprintf(stderr, "Timer started\n");
  }
  ~TimerGuard() {
    fprintf(stderr, "elapsed %.2f milliseconds.\n",
            timer_.elapsed_milliseconds());
  }

private:
  Timer timer_;
};

} // namespace fluorine

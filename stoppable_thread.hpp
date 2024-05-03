#pragma once

#include <fmt/core.h>
#include <memory>
#include <thread>

// a base case of thread
class StoppableThread {
  std::unique_ptr<std::thread> thread_;
  std::atomic_bool stop_;

public:
  explicit StoppableThread() : thread_(nullptr), stop_(false) {}
  virtual ~StoppableThread() = default;
  void start() {
    fmt::println("{} started.", name());
    thread_ = std::make_unique<std::thread>(&StoppableThread::main, this);
  }
  void main() {
    while (!stop_.load()) {
      this->run();
    }
  }
  virtual void run() = 0;
  virtual std::string name() = 0;
  void stop() { stop_.store(true); }
  void wait() {
    if (thread_ && thread_->joinable()) {
      thread_->join();
      fmt::println("{} finished.", name());
    }
  }
};

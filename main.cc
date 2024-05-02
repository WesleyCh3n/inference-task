#include "concurrentqueue.h"

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>

#include <iostream>
#include <memory>
#include <vector>

class StoppableThread {
  std::unique_ptr<std::thread> thread_;
  std::atomic_bool stop_;

public:
  explicit StoppableThread() : thread_(nullptr), stop_(false) {}
  void start() {
    std::cout << "start" << std::endl;
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
      std::cout << "finish" << std::endl;
    }
  }
};

struct Frame {
  int producer_id;
  int id;
  int data;
};
std::ostream &operator<<(std::ostream &os, const Frame &x) {
  return os << fmt::format("[p: {}, i: {}, d: {}]", x.producer_id, x.id,
                           x.data);
}
template <> struct fmt::formatter<Frame> : ostream_formatter {};

class Consumer : public StoppableThread {
  moodycamel::ConcurrentQueue<Frame> &q_;
  std::unordered_map<int, moodycamel::ConcurrentQueue<Frame>> &q_out_;

public:
  Consumer(moodycamel::ConcurrentQueue<Frame> &q,
           std::unordered_map<int, moodycamel::ConcurrentQueue<Frame>> &q_out)
      : q_(q), q_out_(q_out) {}
  std::string name() final { return "Consumer"; }
  void run() final {
    std::vector<Frame> v(20);
    std::size_t count = q_.try_dequeue_bulk(v.begin(), 20);
    if (count > 0) {
      fmt::println("{} dequeued {}.", name(),
                   std::vector<Frame>(v.begin(), v.begin() + count));

      for (int i = 0; i < count; ++i) {
        q_out_[v[i].producer_id].try_enqueue(v[i]);
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  }
};

class Producer : public StoppableThread {
  moodycamel::ConcurrentQueue<Frame> &q_;
  int id_;
  int frame_ = 0;

public:
  Producer(moodycamel::ConcurrentQueue<Frame> &q, int id) : q_(q), id_(id) {}
  std::string name() final { return fmt::format("Producer: {}", id_); }

  void run() final {
    Frame frame{id_, frame_, frame_};
    bool success = q_.try_enqueue(frame);
    if (success) {
      fmt::println("{} enqueue {}.", name(), frame_);
    } else {
      fmt::println("{} failed enqueue.", name());
    }
    frame_ += 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
};

class PostConsumer : public StoppableThread {
  moodycamel::ConcurrentQueue<Frame> &q_;
  int id_;

public:
  PostConsumer(moodycamel::ConcurrentQueue<Frame> &q_map, int id)
      : q_(q_map), id_(id) {}
  void run() final {
    Frame frame;
    bool success = q_.try_dequeue(frame);
    if (success) {
      fmt::println("{} dequeued {}.", name(), frame);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  std::string name() final { return fmt::format("PostConsumer: {}", id_); }
};

int main(int argc, char *argv[]) {
  moodycamel::ConcurrentQueue<Frame> q(32 * 10);
  std::unordered_map<int, moodycamel::ConcurrentQueue<Frame>> q_out;
  Consumer consumer(q, q_out);
  Producer p1(q, 1);
  Producer p2(q, 2);
  q_out.insert({1, moodycamel::ConcurrentQueue<Frame>()});
  q_out.insert({2, moodycamel::ConcurrentQueue<Frame>()});
  PostConsumer pc1(q_out[1], 1);
  PostConsumer pc2(q_out[2], 2);
  consumer.start();

  std::vector<StoppableThread *> producers = {&p1, &p2, &pc1, &pc2};
  for (auto &p : producers)
    p->start();

  std::cin.get();

  for (auto &p : producers)
    p->stop();
  for (auto &p : producers)
    p->wait();

  consumer.stop();
  consumer.wait();
  return 0;
}

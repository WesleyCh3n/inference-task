#include <concurrentqueue.h>
#include <fmt/core.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>

#include <csignal>
#include <sys/signal.h>

#include <iostream>
#include <memory>
#include <vector>

#include "noncopyable.hpp"
#include "stoppable_thread.hpp"

volatile sig_atomic_t g_stop = 0;

void sigint_handler(int) {
  fmt::println("SIGINT received.");
  g_stop = 1;
}

const uint32_t FPS = 5;
const uint32_t nProducers = 5;

// data between producer and consumer. make it moveable but not copyable
class Frame : Noncopyable {
public:
  int producer_id;
  int id;
  int data;

  Frame() = default;
  Frame(int producer_id, int id, int data)
      : producer_id(producer_id), id(id), data(data) {}
  // move constructor
  Frame(Frame &&other) noexcept : producer_id(other.producer_id), id(other.id) {
    data = other.data; // this can be moved
  }
  // move assignment
  Frame &operator=(Frame &&other) noexcept {
    producer_id = other.producer_id;
    id = other.id;
    data = other.data; // this can be moved
    return *this;
  }
  // for easy printing
  friend std::ostream &operator<<(std::ostream &os, const Frame &x) {
    return os << fmt::format("[p: {}, i: {}, d: {}]", x.producer_id, x.id,
                             x.data);
  }
};
template <> struct fmt::formatter<Frame> : ostream_formatter {};

// consumer thread. take input from producer and output to post-consumer based
// on id. Use case: model inference
class Consumer : public StoppableThread {
  moodycamel::ConcurrentQueue<Frame> &q_;
  std::unordered_map<int, moodycamel::ConcurrentQueue<Frame>> &q_out_;

public:
  Consumer(moodycamel::ConcurrentQueue<Frame> &q,
           std::unordered_map<int, moodycamel::ConcurrentQueue<Frame>> &q_out)
      : q_(q), q_out_(q_out) {}
  std::string name() final { return "Consumer"; }
  void run() final {
    std::vector<Frame> v(nProducers * 2);
    std::size_t count = q_.try_dequeue_bulk(v.begin(), v.size());
    if (count > 0) {
      std::vector<Frame> result(std::make_move_iterator(v.begin()),
                                std::make_move_iterator(v.begin() + count));
      fmt::println("{} dequeued {}.", name(), result);

      for (int i = 0; i < count; ++i) {
        q_out_[result[i].producer_id].try_enqueue(std::move(result[i]));
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000 / FPS));
    }
  }
};

// producer thread. put input into queue. Use case: stream decoding
class Producer : public StoppableThread {
  moodycamel::ConcurrentQueue<Frame> &q_;
  int id_;
  int frame_ = 0;

public:
  Producer(moodycamel::ConcurrentQueue<Frame> &q, int id) : q_(q), id_(id) {}
  std::string name() final { return fmt::format("Producer: {}", id_); }

  void run() final {
    Frame frame{id_, frame_, frame_};
    bool success = q_.try_enqueue(std::move(frame));
    if (success) {
      fmt::println("{} enqueue {}.", name(), frame_);
    } else {
      fmt::println("{} failed enqueue.", name());
    }
    frame_ += 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 / FPS));
  }
};

// post-consumer thread. take output from consumer. Use case: post-processing
class PostConsumer : public StoppableThread {
  moodycamel::ConcurrentQueue<Frame> &q_;
  int id_;

public:
  PostConsumer(moodycamel::ConcurrentQueue<Frame> &q_map, int id)
      : q_(q_map), id_(id) {}
  void run() final {
    std::vector<Frame> frames(FPS);
    int count = q_.try_dequeue_bulk(frames.begin(), frames.size());
    if (count > 0) {
      std::vector<Frame> result(
          std::make_move_iterator(frames.begin()),
          std::make_move_iterator(frames.begin() + count));
      fmt::println("{} dequeued {}.", name(), result);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000 / FPS));
    }
  }
  std::string name() final { return fmt::format("PostConsumer: {}", id_); }
};

int main(int argc, char *argv[]) {
  // register signal handler
  {
    struct sigaction sig_int_act;
    sig_int_act.sa_handler = sigint_handler;
    sigemptyset(&sig_int_act.sa_mask);
    sig_int_act.sa_flags = 0;
    sigaction(SIGINT, &sig_int_act, nullptr);
  }

  // collect all threads
  std::vector<std::unique_ptr<StoppableThread>> threads;

  moodycamel::ConcurrentQueue<Frame> q_in(32 * 10);
  std::unordered_map<int, moodycamel::ConcurrentQueue<Frame>> q_out;

  std::unique_ptr<Consumer> consumer = std::make_unique<Consumer>(q_in, q_out);
  threads.push_back(std::move(consumer));

  for (int i = 0; i < nProducers; i++) {
    std::unique_ptr<Producer> p = std::make_unique<Producer>(q_in, i);
    q_out.insert({i, moodycamel::ConcurrentQueue<Frame>()});
    std::unique_ptr<PostConsumer> pc =
        std::make_unique<PostConsumer>(q_out[i], i);
    threads.push_back(std::move(p));
    threads.push_back(std::move(pc));
  }

  // start all threads
  for (auto &t : threads)
    t->start();

  // signal handler
  while (!g_stop) { // press ctrl-c to stop
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // graceful stop all threads
  for (auto &p : threads)
    p->stop();
  for (auto &p : threads)
    p->wait();

  return 0;
}

#include <concurrentqueue.h>
#include <fmt/core.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>

#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "noncopyable.hpp"
#include "signal_hanlder.hpp"
#include "stoppable_thread.hpp"

volatile sig_atomic_t g_stop = 0;

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
  moodycamel::ConcurrentQueue<Frame> &q_in_;
  std::unique_ptr<moodycamel::ConsumerToken> in_tok_;
  moodycamel::ConcurrentQueue<Frame> &q_out_;
  std::unordered_map<int, std::shared_ptr<moodycamel::ProducerToken>>
      &out_toks_;

public:
  Consumer(moodycamel::ConcurrentQueue<Frame> &q_in,
           moodycamel::ConcurrentQueue<Frame> &q_out,
           std::unordered_map<int, std::shared_ptr<moodycamel::ProducerToken>>
               &tokens)
      : q_in_(q_in), in_tok_(std::make_unique<moodycamel::ConsumerToken>(q_in)),
        q_out_(q_out), out_toks_(tokens) {}
  std::string name() final { return "Consumer"; }
  void run() final {
    std::vector<Frame> v(nProducers * 2);
    std::size_t count = q_in_.try_dequeue_bulk(*in_tok_, v.begin(), v.size());
    if (count > 0) {
      std::vector<Frame> result(std::make_move_iterator(v.begin()),
                                std::make_move_iterator(v.begin() + count));
      fmt::println("{} <- {}.", name(), result);

      for (int i = 0; i < count; ++i) {
        q_out_.try_enqueue(*out_toks_[result[i].producer_id],
                           std::move(result[i]));
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000 / FPS));
    }
  }
};

// producer thread. put input into queue. Use case: stream decoding
class Producer : public StoppableThread {
  moodycamel::ConcurrentQueue<Frame> &q_;
  std::unique_ptr<moodycamel::ProducerToken> token_;
  int id_;
  int frame_ = 0;

public:
  Producer(moodycamel::ConcurrentQueue<Frame> &q, int id)
      : q_(q), token_(std::make_unique<moodycamel::ProducerToken>(q)), id_(id) {
  }
  std::string name() final { return fmt::format("Producer[{}]", id_); }

  void run() final {
    Frame frame{id_, frame_, frame_};
    bool success = q_.try_enqueue(*token_, std::move(frame));
    if (success) {
      fmt::println("{} -> {}.", name(), frame_);
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
  std::shared_ptr<moodycamel::ProducerToken> token_;
  int id_;

public:
  PostConsumer(moodycamel::ConcurrentQueue<Frame> &q_map, int id,
               std::shared_ptr<moodycamel::ProducerToken> &token)
      : q_(q_map), token_(token), id_(id) {}
  void run() final {
    std::vector<Frame> frames(FPS);
    int count = q_.try_dequeue_bulk_from_producer(*token_, frames.begin(),
                                                  frames.size());
    if (count > 0) {
      std::vector<Frame> result(
          std::make_move_iterator(frames.begin()),
          std::make_move_iterator(frames.begin() + count));
      fmt::println("{} <- {}.", name(), result);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000 / FPS));
    }
  }
  std::string name() final { return fmt::format("PostConsumer[{}]", id_); }
};

int main(int argc, char *argv[]) {
  // register signal handler
  register_signal_handler();

  // collect all threads
  std::vector<std::unique_ptr<StoppableThread>> threads;

  // TODO: determine how many block should be reserved
  moodycamel::ConcurrentQueue<Frame> q_in(32 * 10);
  moodycamel::ConcurrentQueue<Frame> q_out(32 * 10);

  std::unordered_map<int, std::shared_ptr<moodycamel::ProducerToken>> ptoks;
  for (int i = 0; i < nProducers; i++) {
    // using emplace to avoid copy
    ptoks.emplace(
        std::make_pair(i, std::make_shared<moodycamel::ProducerToken>(q_out)));
    threads.emplace_back(std::make_unique<Producer>(q_in, i));
    threads.emplace_back(std::make_unique<PostConsumer>(q_out, i, ptoks.at(i)));
  }

  threads.emplace_back(std::make_unique<Consumer>(q_in, q_out, ptoks));

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

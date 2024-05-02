#include "concurrentqueue.h"

#include <iostream>
#include <vector>

int main(int argc, char *argv[]) {
  moodycamel::ConcurrentQueue<int> q;
  std::atomic_bool done = false;
  std::thread consumer([&]() {
    while (!done.load()) {
      std::vector<int> v(20);
      std::size_t count = q.try_dequeue_bulk(v.begin(), 20);
      if (count > 0) {
        std::cout << "Consumer dequeued " << count << " elements." << std::endl;
        for (int i = 0; i < count; ++i) {
          std::cout << "q: " << v[i] << std::endl;
        }
      } else {
        std::cout << "Consumer dequeued nothing." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      }
    }
  });

  while (true) {
    auto c = std::tolower(std::cin.get());
    if (c == 'q') {
      done.store(true);
      break;
    } else {
      q.try_enqueue(c);
    }
  }
  consumer.join();
  return 0;
}

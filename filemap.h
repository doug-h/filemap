#include <cassert>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <list>
#include <thread>
#include <vector>


// TODO - SWITCH multithreading based on HDD vs SSD, cores etc

namespace fs = std::filesystem;

struct FormatSize {
  std::uintmax_t s;

  friend std::ostream &operator<<(std::ostream &os, FormatSize f)
  {
    int unit = 0;
    float head = f.s;
    while (head >= 1024) {
      ++unit;
      head /= 1024;
    }
    head = std::ceil(head * 10.0f) / 10.0f;
    os << head;

    if (unit) { os << "KMGTPE"[unit - 1]; }

    return os << 'B';
  }
};

std::uintmax_t decend(fs::path start)
{
  std::uintmax_t size = 0;
  for (const fs::directory_entry &dir_entry :
       fs::recursive_directory_iterator(start)) {
    if (dir_entry.is_regular_file()) { size += dir_entry.file_size(); }
  }

  return size;
}

template <typename F> class debug_task_wrapper {
  F f;

public:
  debug_task_wrapper(F f) : f(f) {}

  using return_type = typename decltype(std::function{f})::result_type;
  template <typename... Args> return_type operator()(Args... args)
  {
    auto start = std::chrono::high_resolution_clock::now();
    auto rv = f(args...);
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Task f(" << (args << ...) << ") \n Returned "
              << FormatSize{rv} << " and took "
              << (std::chrono::duration_cast<std::chrono::microseconds>(end -
                                                                        start)
                      .count() *
                  1e-6)
              << "s." << '\n';
    ;
    return std::move(rv);
  };
};


void Run(fs::path p)
{

  std::cout << p << '\n';


  std::uintmax_t size = 0;
  const auto start = std::chrono::system_clock::now();

  // First, slow way of doing it
  const unsigned int max_threads = std::thread::hardware_concurrency() - 1;
  unsigned int max_threads_obtained = 0;
  unsigned int num_threads = 0;

  std::list<std::future<std::uintmax_t>> futures;


  // Master decends, but also makes threads
  auto master_it = fs::recursive_directory_iterator(p);

  // Don't create a thread for the first directory we find
  bool first_dir_found = false;
  int prev_depth = 0;
  for (auto it = fs::begin(master_it); it != fs::end(master_it); ++it) {
    const fs::directory_entry &dir_entry = *it;

    if (dir_entry.is_regular_file()) {
      size += dir_entry.file_size();
    } else if (dir_entry.is_directory()) {
      if (it.depth() != prev_depth) {
        first_dir_found = false;
        prev_depth = it.depth();
      }
      if (!first_dir_found) {
        first_dir_found = true;
      } else {
        if (num_threads < max_threads) {
          assert(futures.size() == num_threads);

          futures.push_back(std::async(std::launch::async,
                                       debug_task_wrapper(decend),
                                       dir_entry.path()));

          ++num_threads;

          // Thread will handle this dir so master does not descend
          it.disable_recursion_pending();
        }
      }
    } else {
      // Skip files we don't understand
    }


    // Reset threads that finish early
    for (auto lit = futures.begin(); lit != futures.end();) {
      if (lit->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::uintmax_t s = lit->get();
        size += s;
        lit = futures.erase(lit);
        --num_threads;
      } else {
        ++lit;
      }
    }
    max_threads_obtained = std::max(max_threads_obtained, num_threads);
  }


  // Wait for remaining
  for (auto lit = futures.begin(); lit != futures.end(); ++lit) {
    size += lit->get();
  }
  const auto end = std::chrono::system_clock::now();
  float dur_s =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count() /
      1000.0f;

  std::cout << FormatSize{size} << ' ' << size << '\n';
  std::cout << dur_s << "s" << '\n';
  std::cout << FormatSize{(std::uintmax_t)(size / dur_s)} << "/s" << '\n';
  std::cout << "Max threads used " << max_threads_obtained << '\n';
}

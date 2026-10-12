// Native PS5 capability probe for the OpenGOAL port: the C++ runtime features
// the game runtime cannot be built without. Compiled the way OpenGOAL's own
// sources are (exceptions and RTTI on), unlike the template's sources.
//
// The exception test has its own entry point so the caller can run it last: if
// unwinding does not work the title aborts, and every earlier line is already
// in the log.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using Say = void (*)(const char* format, ...);

thread_local int t_value = 7;

struct Base {
  virtual ~Base() = default;
};
struct Derived : Base {
  int value = 42;
};

[[gnu::noinline]] void thrower(int depth) {
  std::string keep_a_destructor_on_the_stack = "unwind";
  if (depth == 0)
    throw std::runtime_error("thrown " + keep_a_destructor_on_the_stack);
  thrower(depth - 1);
}

void probe_threads(Say say) {
  std::atomic<int> sum{0};
  std::mutex mutex;
  std::condition_variable ready;
  bool go = false;
  int worker_tls = -1;
  std::thread worker([&] {
    std::unique_lock<std::mutex> lock(mutex);
    ready.wait(lock, [&] { return go; });
    t_value = 11;
    worker_tls = t_value;
    sum += 5;
  });
  {
    std::lock_guard<std::mutex> lock(mutex);
    go = true;
  }
  ready.notify_one();
  worker.join();
  say("[probe] cxx thread=%s sum=%d tls-main=%d tls-worker=%d hardware_concurrency=%u",
      sum == 5 && t_value == 7 && worker_tls == 11 ? "ok" : "wrong-result", sum.load(), t_value,
      worker_tls, std::thread::hardware_concurrency());

  auto before = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::steady_clock::now() - before)
                     .count();
  say("[probe] cxx sleep_for(20ms) elapsed_us=%lld", static_cast<long long>(elapsed));
}

void probe_filesystem(Say say) {
  namespace fs = std::filesystem;
  std::error_code error;
  for (const char* path : {"/app0", "/app0/eboot.bin", "/app0/sce_sys/param.json", "/data",
                           "/data/opengoal", "/download0"}) {
    bool exists = fs::exists(path, error);
    int exists_error = error.value();
    bool directory = fs::is_directory(path, error);
    say("[probe] cxx fs path=%s exists=%d errno=%d is_directory=%d", path, exists, exists_error,
        directory);
  }
  for (const char* path : {"/app0", "/data/opengoal"}) {
    int count = 0;
    fs::directory_iterator it(path, error);
    int open_error = error.value();
    if (!error) {
      for (fs::directory_iterator end; it != end && !error; it.increment(error))
        ++count;
    }
    say("[probe] cxx fs list=%s open-errno=%d entries=%d last-errno=%d", path, open_error, count,
        error.value());
  }
  // fs::current_path is deliberately not probed again: the title died inside it on hardware
  // (2026-10-12, results/PPSA99005-20261012T205700Z).

  if (std::FILE* file = std::fopen("/app0/sce_sys/param.json", "rb")) {
    char buffer[16] = {};
    size_t count = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    say("[probe] cxx fs read=/app0/sce_sys/param.json bytes=%zu", count);
  } else {
    say("[probe] cxx fs read=/app0/sce_sys/param.json failed");
  }
}
}  // namespace

extern "C" void og_runtime_probe(Say say) {
  probe_threads(say);
  probe_filesystem(say);

  Base* base = new Derived;
  Derived* derived = dynamic_cast<Derived*>(base);
  say("[probe] cxx rtti=%s", derived && derived->value == 42 ? "ok" : "wrong-result");
  delete base;
}

extern "C" void og_exception_probe(Say say) {
  say("[probe] cxx exception about-to-throw");
  try {
    thrower(3);
    say("[probe] cxx exception=not-thrown");
  } catch (const std::exception& e) {
    say("[probe] cxx exception=ok what=%s", e.what());
  }
}

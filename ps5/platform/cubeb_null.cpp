// Null audio output for the PS5 bring-up.
//
// Implements the part of the cubeb API that snd::Player uses. A thread asks the
// player's data callback for PCM at the stream's sample rate, as a real backend
// would, and discards it: the mixer, its tick and everything timed off it behave
// as on desktop, and nothing reaches the console's audio hardware.
//
// Replace with a real AudioOut backend behind the same interface.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "common/log/log.h"

#include "third-party/cubeb/cubeb/include/cubeb/cubeb.h"

struct cubeb {};

struct cubeb_stream {
  cubeb_data_callback data_callback = nullptr;
  cubeb_state_callback state_callback = nullptr;
  void* user = nullptr;
  uint32_t rate = 48000;
  uint32_t frames = 480;
  std::vector<uint8_t> buffer;
  std::thread thread;
  std::atomic<bool> running{false};
};

namespace {
// 10 ms at 48 kHz: the order of latency desktop backends report.
constexpr uint32_t kPeriodFrames = 480;

void run(cubeb_stream* stream) {
  using clock = std::chrono::steady_clock;
  const auto period = std::chrono::duration_cast<clock::duration>(
      std::chrono::duration<double>(double(stream->frames) / double(stream->rate)));
  auto deadline = clock::now();
  while (stream->running.load(std::memory_order_acquire)) {
    stream->data_callback(stream, stream->user, nullptr, stream->buffer.data(), stream->frames);
    // A fixed schedule rather than a fixed sleep, so the mixer runs at the stream's
    // rate on average. After a long stall, catch up from now instead of bursting.
    deadline += period;
    const auto now = clock::now();
    if (deadline < now - 10 * period) {
      deadline = now;
    }
    std::this_thread::sleep_until(deadline);
  }
}
}  // namespace

int cubeb_init(cubeb** context, char const* /*context_name*/, char const* /*backend_name*/) {
  *context = new cubeb;
  return CUBEB_OK;
}

int cubeb_get_min_latency(cubeb* /*context*/,
                          cubeb_stream_params* /*params*/,
                          uint32_t* latency_frames) {
  *latency_frames = kPeriodFrames;
  return CUBEB_OK;
}

int cubeb_stream_init(cubeb* /*context*/,
                      cubeb_stream** stream,
                      char const* /*stream_name*/,
                      cubeb_devid /*input_device*/,
                      cubeb_stream_params* /*input_stream_params*/,
                      cubeb_devid /*output_device*/,
                      cubeb_stream_params* output_stream_params,
                      uint32_t latency_frames,
                      cubeb_data_callback data_callback,
                      cubeb_state_callback state_callback,
                      void* user_ptr) {
  if (!output_stream_params || !data_callback || output_stream_params->rate == 0) {
    return CUBEB_ERROR_INVALID_PARAMETER;
  }
  auto* result = new cubeb_stream;
  result->data_callback = data_callback;
  result->state_callback = state_callback;
  result->user = user_ptr;
  result->rate = output_stream_params->rate;
  result->frames = latency_frames ? latency_frames : kPeriodFrames;
  // Large enough for any cubeb sample format (at most 4 bytes per sample).
  result->buffer.resize(size_t(result->frames) * output_stream_params->channels * 4);
  *stream = result;
  lg::info("[ps5] audio: null output, {} Hz, {} channels, {} frames per callback (PCM discarded)",
           result->rate, output_stream_params->channels, result->frames);
  return CUBEB_OK;
}

int cubeb_stream_start(cubeb_stream* stream) {
  if (!stream) {
    return CUBEB_ERROR_INVALID_PARAMETER;
  }
  if (!stream->running.exchange(true)) {
    stream->thread = std::thread(run, stream);
    if (stream->state_callback) {
      stream->state_callback(stream, stream->user, CUBEB_STATE_STARTED);
    }
  }
  return CUBEB_OK;
}

int cubeb_stream_stop(cubeb_stream* stream) {
  if (!stream) {
    return CUBEB_ERROR_INVALID_PARAMETER;
  }
  if (stream->running.exchange(false)) {
    stream->thread.join();
    if (stream->state_callback) {
      stream->state_callback(stream, stream->user, CUBEB_STATE_STOPPED);
    }
  }
  return CUBEB_OK;
}

void cubeb_stream_destroy(cubeb_stream* stream) {
  if (stream) {
    cubeb_stream_stop(stream);
    delete stream;
  }
}

void cubeb_destroy(cubeb* context) {
  delete context;
}

#pragma once

#include <string>
#include <utility>

#include "common/log/log.h"

namespace lg {
/*!
 * Record that startup reached a stage.
 *
 * A PS5 title has no terminal and may fail before it shows anything, so there these are
 * info-level lines in a log that is flushed on every write: a start that fails names the last
 * stage it completed. Elsewhere they are debug-level.
 */
template <typename... Args>
void startup(const std::string& format, Args&&... args) {
#ifdef __PROSPERO__
  log(level::info, "[startup] " + format, std::forward<Args>(args)...);
#else
  log(level::debug, "[startup] " + format, std::forward<Args>(args)...);
#endif
}
}  // namespace lg

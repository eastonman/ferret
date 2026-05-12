#pragma once

#include <spdlog/spdlog.h>

#include <string>
#include <utility>

namespace ferret::log {

enum class Level {
  Trace,
  Debug,
  Info,
  Warn,
  Error,
  Critical,
  Off,
};

void init();
void set_level(Level);
Level parse_level(const std::string& s);

template <typename... Args>
inline void trace(spdlog::format_string_t<Args...> fmt, Args&&... args) {
  spdlog::trace(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void debug(spdlog::format_string_t<Args...> fmt, Args&&... args) {
  spdlog::debug(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
  spdlog::info(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
  spdlog::warn(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
  spdlog::error(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void critical(spdlog::format_string_t<Args...> fmt, Args&&... args) {
  spdlog::critical(fmt, std::forward<Args>(args)...);
}

}  // namespace ferret::log

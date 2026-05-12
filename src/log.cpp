#include "ferret/log.hpp"

#include <spdlog/sinks/stdout_sinks.h>

#include <stdexcept>

namespace ferret::log {

namespace {

spdlog::level::level_enum to_spdlog(Level l) {
  switch (l) {
    case Level::Trace:
      return spdlog::level::trace;
    case Level::Debug:
      return spdlog::level::debug;
    case Level::Info:
      return spdlog::level::info;
    case Level::Warn:
      return spdlog::level::warn;
    case Level::Error:
      return spdlog::level::err;
    case Level::Critical:
      return spdlog::level::critical;
    case Level::Off:
      return spdlog::level::off;
  }
  return spdlog::level::warn;
}

}  // namespace

void init() {
  auto logger = spdlog::stderr_logger_mt("ferret");
  logger->set_pattern("ferret: %l: %v");
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::warn);
}

void set_level(Level l) { spdlog::set_level(to_spdlog(l)); }

Level parse_level(const std::string& s) {
  if (s == "trace") {
    return Level::Trace;
  }
  if (s == "debug") {
    return Level::Debug;
  }
  if (s == "info") {
    return Level::Info;
  }
  if (s == "warn" || s == "warning") {
    return Level::Warn;
  }
  if (s == "error") {
    return Level::Error;
  }
  if (s == "critical") {
    return Level::Critical;
  }
  if (s == "off") {
    return Level::Off;
  }
  throw std::invalid_argument("unknown log level: " + s);
}

}  // namespace ferret::log

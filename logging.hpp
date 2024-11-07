#pragma once
#include <iostream>
#include <string>
// maybe use a compile -D define to tell which level we're at? or a run time
// parameter
// honestly a runtime parameter

enum LOG_LEVELS {
  LOG_FATAL,
  LOG_ERROR,
  LOG_WARNING,
  LOG_INFO,
  LOG_DEBUG,
  LOG_TRACE
};

constexpr std::string LOG_LEVELS_STR[] = {"FATAL", "ERROR", "WARNING",
                                          "INFO",  "DEBUG", "TRACE"};
class Logging {
public:
  static Logging &GetInstance() {
    static Logging instance;
    return instance;
  }

  void SetLogLevel(LOG_LEVELS level) { log_level = level; }
  LOG_LEVELS GetLogLevel() { return log_level; }

private:
  Logging() {}
  LOG_LEVELS log_level = LOG_DEBUG;
};

#define CERR_WHEN(level, message)                                              \
  if (Logging::GetInstance().GetLogLevel() >= level) {                         \
    std::cerr << __FILE__ << ":" << __LINE__ << ":" << LOG_LEVELS_STR[level]   \
              << ": " << message << std::endl;                                 \
  }

#define CERR_ALWAYS(message) CERR_WHEN(LOG_FATAL, message)

#define CERR_OUT(message) std::cerr << message << std::endl;

#pragma once
#include <functional>
#include <unordered_map>

struct cmd_parser {

  void add_opt(
      const std::string &opt,
      std::function<void(const std::string &)> f = [](auto) {}) {
    m_opts[opt] = f;
  }

  void parse(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
      if (m_opts.find(argv[i]) != m_opts.end()) {
        m_opts[argv[i]](argv[i + 1]);
        ++i;
      }
      if (m_flags.find(argv[i]) != m_flags.end()) {
        m_flags[argv[i]]();
      }
    }
  }

  void add_flag(
      const std::string &flag, std::function<void()> f = []() {}) {
    m_flags[flag] = f;
  }

  std::unordered_map<std::string, std::function<void(const std::string &)>>
      m_opts;
  std::unordered_map<std::string, std::function<void()>> m_flags;
};

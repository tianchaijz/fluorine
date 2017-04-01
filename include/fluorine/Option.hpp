#pragma once

#include <string>
#include <boost/program_options.hpp>

namespace fluorine {
struct Option {
  std::string config_path_;
  std::string log_path_;
  std::string ip_db_path_;
  std::string redis_address_;
  std::string redis_queue_;
  bool tcp_input_ = false;

  std::string frontend_ip_;
  unsigned short frontend_port_;

  std::string backend_ip_;
  unsigned short backend_port_;

  inline bool IsTcpInput() { return tcp_input_; }
  inline bool IsRedisInput() { return redis_address_.size() > 0; }

  std::pair<std::string, int> GetRedisAddress() {
    size_t pos = redis_address_.find(':');
    if (pos == std::string::npos) {
      return std::pair<std::string, int>(redis_address_, 6379);
    } else {
      return std::pair<std::string, int>(
          redis_address_.substr(0, pos),
          std::atoi(redis_address_.substr(pos + 1).c_str()));
    }
  }
};

void ParseOption(int argc, char *argv[], Option &opt);
} // namespace fluorne

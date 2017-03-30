#pragma once

#include <string>
#include <boost/program_options.hpp>

namespace fluorine {
struct Option {
  std::string config_path_;
  std::string log_path_;
  std::string ip_db_path_;
  std::string redis_input_;
  bool tcp_input_ = false;

  std::string frontend_ip_;
  unsigned short frontend_port_;

  std::string backend_ip_;
  unsigned short backend_port_;

  bool IsTcpInput() { return tcp_input_; }
};

void ParseOption(int argc, char *argv[], Option &opt);
} // namespace fluorne

#include <iostream>
#include "fluorine/Option.hpp"

namespace fluorine {
namespace po = boost::program_options;

static void conflictingOptions(const po::variables_map &vm, const char *x,
                               const char *y) {
  if (vm.count(x) && !vm[x].defaulted() && vm.count(y) && !vm[y].defaulted()) {
    throw std::logic_error(std::string("Conflicting options '") + x +
                           "' and '" + y + "'.");
  }
}

static void optionDependency(const po::variables_map &vm, const char *x,
                             const char *y) {
  if (vm.count(x) && !vm[x].defaulted() &&
      (vm.count(y) == 0 || vm[x].defaulted())) {
    throw std::logic_error(std::string("Option '") + x + "' requires option '" +
                           y + "'.");
  }
}

// https://github.com/boostorg/program_options/tree/develop/example
void ParseOption(int argc, char *argv[], Option &opt) {
  using namespace boost::program_options;
  try {
    options_description desc("Usage");
    // clang-format off
    desc.add_options()
      ("help,h", "print usage message")
      ("config,c", value(&opt.config_path_), "config file path")
      ("log,l", value(&opt.log_path_), "log file path")
      ("db,d", value(&opt.ip_db_path_)->default_value("/opt/17monipdb.dat"), "ip database path")
      ("redis,r", value(&opt.redis_address_), "redis input(host:port)")
      ("redis-queue", value(&opt.redis_queue_), "redis job queue")
      ("tcp,t", bool_switch(&opt.tcp_input_), "tcp input")
      ("listen-ip", value(&opt.frontend_ip_)->default_value("127.0.0.1"), "listen ip")
      ("listen-port", value(&opt.frontend_port_)->default_value(5565), "listen port")
      ("server-ip", value(&opt.backend_ip_)->default_value("127.0.0.1"), "server ip")
      ("server-port", value(&opt.backend_port_)->default_value(5566), "server port");
    // clang-format on

    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);

    if (vm.count("help")) {
      std::cout << desc << std::endl;
      exit(0);
    }

    notify(vm);
    conflictingOptions(vm, "log", "tcp");
    conflictingOptions(vm, "log", "redis");
    conflictingOptions(vm, "tcp", "redis");
    optionDependency(vm, "redis", "redis-queue");

    if (!vm.count("redis") && !vm.count("config")) {
      std::cerr << "config required" << std::endl;
      exit(1);
    }
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    exit(1);
  }
}

} // namespace fluorne

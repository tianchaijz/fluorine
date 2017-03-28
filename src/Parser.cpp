#include <fstream>
#include <iostream>

#include "spdlog/spdlog.h"
#include "fluorine/log/Parser.hpp"
#include "fluorine/config/Parser.hpp"

static auto logger = spdlog::stdout_color_st("Parser");

namespace fluorine {
namespace log {
bool ParseLog(std::string &line, Log &log, unsigned int field_number,
              unsigned int time_index) {
  Grammar<iterator_type> g(field_number, time_index);
  iterator_type begin = line.begin();
  iterator_type end   = line.end();
  bool ok             = qi::phrase_parse(begin, end, g, qi::space, log);
  if (!ok || begin != end) {
    logger->warn("log parse failed, remaining unparsed: {}",
                 std::string(begin, end));
    return false;
  }

  return true;
}

} // namespace log

namespace config {
const std::string Attribute::IGNORE = "0";
const std::string Attribute::STORE  = "1";
const std::string Attribute::ADD    = "2";

bool ParseConfig(const std::string &path, Config &cfg) {
  std::ifstream is(path);
  std::stringstream buffer;

  if (is.is_open()) {
    buffer << is.rdbuf();
    is.close();
  } else {
    logger->error("unable to open: {}", path);
    return false;
  }

  Grammar<iterator_type> g;
  Skipper<iterator_type> skip;
  std::string content = buffer.str();
  iterator_type begin = content.begin(), end = content.end();

  bool ok = qi::phrase_parse(begin, end, g, skip, cfg);
  if (!ok || begin != end) {
    logger->error("config parse failed, remaining unparsed: {}",
                  std::string(begin, end));
    return false;
  }

  return true;
}

} // namespace config
} // namespace fluorine

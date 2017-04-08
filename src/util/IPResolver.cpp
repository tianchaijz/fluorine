#include <stdlib.h>
#include <iostream>

#include <boost/spirit/include/qi.hpp>

#include "spdlog/spdlog.h"
#include "fluorine/util/IPResolver.hpp"

static auto logger = spdlog::stdout_color_st("IP Resolver");

namespace fluorine {
namespace util {
static std::unique_ptr<IPResolver> resolver(nullptr);

void InitIPResolver(const std::string &db_path) {
  if (!resolver) {
    resolver.reset(new IPResolver(db_path.c_str()));
  }
}

bool ResolveIP(const std::string &ip, char **result) {
  return resolver->Resolve(ip, result);
}

#define B2IL(b)                                                               \
  (((b)[0] & 0xFF) | (((b)[1] << 8) & 0xFF00) | (((b)[2] << 16) & 0xFF0000) | \
   (((b)[3] << 24) & 0xFF000000))

#define B2IU(b)                                                               \
  (((b)[3] & 0xFF) | (((b)[2] << 8) & 0xFF00) | (((b)[1] << 16) & 0xFF0000) | \
   (((b)[0] << 24) & 0xFF000000))

IPResolver::IPResolver(const char *db_path) {
  FILE *fd = fopen(db_path, "rb");

  fseek(fd, 0, SEEK_END);
  size_t size = ftell(fd);
  ASSERT(size > 0);
  fseek(fd, 0, SEEK_SET);

  data_        = (byte *)malloc(size * sizeof(byte));
  size_t count = fread(data_, sizeof(byte), size, fd);
  fclose(fd);

  ASSERT(count > 512 * sizeof(uint));

  Init();
}

IPResolver::IPResolver(const char *db_data, const size_t db_size) {
  data_ = (byte *)malloc(db_size * sizeof(byte));
  memcpy(data_, db_data, db_size);
  Init();
}

IPResolver::~IPResolver() {
  if (offset_) {
    offset_ = 0;
    free(flag_);
    free(index_);
    free(data_);
  }
}

template <typename Iterator = std::string::const_iterator>
struct IPGrammar : boost::spirit::qi::grammar<Iterator, std::vector<uint>()> {
  IPGrammar() : IPGrammar::base_type(ip) {
    using namespace boost::spirit::qi;
    ip = uint_ >> '.' >> uint_ >> '.' >> uint_ >> '.' >> uint_;
  }

private:
  boost::spirit::qi::rule<Iterator, std::vector<uint>()> ip;
};

bool IPResolver::Resolve(const std::string &ip, char **result) {
  auto res = lru_.get(ip);
  if (res) {
    *result = res->get();
    return true;
  }

  IPGrammar<> g;
  std::vector<uint> ips;
  ips.reserve(4);
  bool ok = boost::spirit::qi::parse(ip.begin(), ip.end(), g, ips);
  if (!ok) {
    logger->error("invalid ip: {}", ip);
    return false;
  }

  uint ip_prefix    = ips[0];
  uint ip_long      = B2IU(ips);
  uint start        = flag_[ip_prefix];
  uint max_comp_len = offset_ - 1028;
  uint index_offset = 0;
  uint index_length = 0;
  for (start = start * 8 + 1024; start < max_comp_len; start += 8) {
    if (B2IU(index_ + start) >= ip_long) {
      index_offset = B2IL(index_ + start + 4) & 0x00FFFFFF;
      index_length = index_[start + 7];
      break;
    }
  }

  if (index_length > ResultLengthMax) {
    logger->error("index length too big: {}", index_length);
    return false;
  }

  std::shared_ptr<char> data(new char[index_length + 1],
                             std::default_delete<char[]>());
  *result = data.get();
  memcpy(*result, data_ + offset_ + index_offset - 1024, index_length);
  (*result)[index_length] = '\0';

  lru_.insert(ip, std::move(data));

  return true;
}

void IPResolver::Init() {
  uint length = B2IU(data_);
  ASSERT(length > 0 && length < 16777216);

  index_ = (byte *)malloc(length * sizeof(byte));
  memcpy(index_, data_ + 4, length);

  offset_ = length;

  flag_ = (uint *)malloc(256 * sizeof(uint));
  memcpy(flag_, index_, 256 * sizeof(uint));
}

} // namespace util
} // namespace fluorine

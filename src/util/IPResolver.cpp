#include <stdlib.h>
#include <iostream>

#include <boost/spirit/include/qi.hpp>

#include "spdlog/spdlog.h"
#include "fluorine/Macros.hpp"
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

bool ResolveIP(const std::string &ip, IPResolver::ResultType **result) {
  return resolver->Resolve(ip, result);
}

#define B2IL(b)                                                               \
  (((b)[0] & 0xFF) | (((b)[1] << 8) & 0xFF00) | (((b)[2] << 16) & 0xFF0000) | \
   (((b)[3] << 24) & 0xFF000000))

#define B2IU(b)                                                               \
  (((b)[3] & 0xFF) | (((b)[2] << 8) & 0xFF00) | (((b)[1] << 16) & 0xFF0000) | \
   (((b)[0] << 24) & 0xFF000000))

IPResolver::ResultType IPResolver::UnknownResult =
    IPResolver::ResultType(IPResolver::FieldNumber, "unknown");

IPResolver::IPResolver(const char *db_path) {
  FILE *fd = fopen(db_path, "rb");
  if (fd == nullptr) {
    perror(db_path);
    exit(1);
  }

  fseek(fd, 0, SEEK_END);
  size_t size = ftell(fd);
  ASSERT(size > 0);
  fseek(fd, 0, SEEK_SET);

  data_        = (byte *)malloc(size);
  size_t count = fread(data_, 1, size, fd);
  fclose(fd);

  ASSERT(count > 512 * sizeof(uint));

  Init();
}

IPResolver::IPResolver(const char *db_data, const size_t db_size) {
  data_ = (byte *)malloc(db_size);
  memcpy(data_, db_data, db_size);
  Init();
}

IPResolver::~IPResolver() {
  if (offset_) {
    offset_ = 0;
    free(data_);
  }
}

template <typename Iterator = std::string::const_iterator>
struct IPGrammar
    : boost::spirit::qi::grammar<Iterator, std::vector<uint8_t>()> {
  IPGrammar() : IPGrammar::base_type(ip) {
    using namespace boost::spirit::qi;

    uint_parser<uint8_t, 10, 1, 3> uint8_p;
    ip = uint8_p >> '.' >> uint8_p >> '.' >> uint8_p >> '.' >> uint8_p;
  }

private:
  boost::spirit::qi::rule<Iterator, std::vector<uint8_t>()> ip;
};

bool IPResolver::Resolve(const std::string &ip, ResultType **result) {
  static char buf[ResultLengthMax + 1];
  static ResultType ipv6(FieldNumber, "IPv6");
  static IPGrammar<> g;

  if (ip.find(':') != std::string::npos) {
    *result = &ipv6;
    return true;
  }

  auto res = lru_.get(ip);
  if (res) {
    *result = res->get();
    return true;
  }

  std::vector<uint8_t> ips;
  ips.reserve(4);
  bool ok = boost::spirit::qi::parse(ip.begin(), ip.end(), g, ips);
  if (!ok) {
    return false;
  }

  uint32_t ip_int       = B2IU(ips);
  uint32_t start        = flag_[ips[0]];
  uint32_t max_comp_len = offset_ - 1028;
  uint32_t index_offset = 0;
  uint32_t index_length = 0;

  uint32_t lo = start * 8 + 1024;
  uint32_t hi = max_comp_len;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 16 * 8;
    if (B2IU(index_ + mid) < ip_int) {
      lo = mid + 8;
    } else {
      hi = mid;
    }
  }

  index_offset = B2IL(index_ + lo + 4) & 0x00FFFFFF;
  index_length = index_[lo + 7];

  memcpy(buf, data_ + offset_ + index_offset - 1024, index_length);
  buf[index_length] = '\0';

  LRUValueType fields(new ResultType());
  fields->reserve(FieldNumber);

  int n   = 0;
  char *s = buf, *e = buf;
  while (*e && n < FieldNumber) {
    if (*e == '\t') {
      fields->emplace_back(s, e);
      s = e + 1;
      ++n;
    }
    ++e;
  }

  if (n < FieldNumber) {
    fields->emplace_back(s, e);
  }

  *result = fields.get();
  lru_.insert(ip, std::move(fields));

  return true;
}

void IPResolver::Init() {
  uint32_t length = B2IU(data_);
  ASSERT(length > 0 && length < 16777216);

  offset_ = length;
  index_  = data_ + 4;
  flag_   = reinterpret_cast<uint32_t *>(index_);
}

} // namespace util
} // namespace fluorine

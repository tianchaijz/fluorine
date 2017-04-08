#pragma once

#include <stddef.h>
#include <string>
#include <memory>

#include "fluorine/Macros.hpp"
#include "fluorine/util/LRUCache.hpp"

namespace fluorine {
namespace util {
void InitIPResolver(const std::string &db_path);
bool ResolveIP(const std::string &ip, char **result);
bool ResolveIP(const std::string &ip, char **result);

// IP resolver(ipip.net)
class IPResolver {
public:
  using LRUType = LRUCache<std::string, std::shared_ptr<char>>;

  typedef unsigned char byte;
  typedef unsigned int uint;
  typedef unsigned char uchar;

  static const int ResultLengthMax = 256;
  static const int FieldNumber     = 5;
  static const size_t LRUCapacity  = 32768;

  IPResolver(const char *db_path);
  IPResolver(const char *db_data, const size_t db_size);
  ~IPResolver();
  bool Resolve(const std::string &ip, char **result);

private:
  DISALLOW_COPY_AND_ASSIGN(IPResolver);
  void Init();

  byte *data_  = nullptr;
  byte *index_ = nullptr;
  uint *flag_  = nullptr;
  uint offset_ = 0;
  LRUType lru_ = LRUType(LRUCapacity);
};

} // namespace util
} // namespace fluorine

#pragma once

#include <stddef.h>
#include <string>
#include <vector>
#include <memory>

#include "fluorine/util/LRUCache.hpp"

namespace fluorine {
namespace util {
// IP resolver(ipip.net)
class IPResolver {
public:
  using LRUValueType = std::shared_ptr<std::vector<std::string>>;
  using LRUType      = LRUCache<std::string, LRUValueType>;
  using ResultType   = std::vector<std::string>;

  typedef unsigned char byte;

  static const int ResultLengthMax = UCHAR_MAX;
  static const int FieldNumber     = 5;
  static const size_t LRUCapacity  = 32768;
  static ResultType UnknownResult;

  static std::string &GetCountry(ResultType &result) { return result[0]; }
  static std::string &GetProvince(ResultType &result) { return result[1]; }
  static std::string &GetCity(ResultType &result) { return result[2]; }
  static std::string &GetISP(ResultType &result) { return result[4]; }

  IPResolver(const char *db_path);
  IPResolver(const char *db_data, const size_t db_size);
  ~IPResolver();
  bool Resolve(const std::string &ip, ResultType **result);

private:
  DISALLOW_COPY_AND_ASSIGN(IPResolver);
  void Init();

  byte *data_      = nullptr;
  byte *index_     = nullptr;
  uint32_t *flag_  = nullptr;
  uint32_t offset_ = 0;
  LRUType lru_     = LRUType(LRUCapacity);
};

void InitIPResolver(const std::string &db_path);
bool ResolveIP(const std::string &ip, IPResolver::ResultType **result);

} // namespace util
} // namespace fluorine

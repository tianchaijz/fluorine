#pragma once

#include <stddef.h>
#include <string>
#include "fluorine/Macros.hpp"

namespace fluorine {
namespace util {
void InitIPResolver(const std::string &db_path);
bool ResolveIP(const std::string &ip, char *result);

// IP resolver(ipip.net)
class IPResolver {
public:
  typedef unsigned char byte;
  typedef unsigned int uint;
  typedef unsigned char uchar;

  static const int ResultLengthMax = 256;
  static const int FieldNumber = 5;

  IPResolver(const char *db_path);
  IPResolver(const char *db_data, const size_t db_size);
  ~IPResolver();
  bool Resolve(const char *ip, char *result);

private:
  DISALLOW_COPY_AND_ASSIGN(IPResolver);
  void Init();

  byte *data_  = nullptr;
  byte *index_ = nullptr;
  uint *flag_  = nullptr;
  uint offset_ = 0;
};

} // namespace util
} // namespace fluorine

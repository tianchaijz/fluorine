#pragma once

#include <stdint.h>
#include <string>
#include <memory>
#include <cstdarg>

#include "hiredis/hiredis.h"
#include "fluorine/Macros.hpp"

namespace fluorine {
namespace util {
namespace redis {

struct RedisContextDeleter {
  void operator()(redisContext *ptr) {
    if (ptr != nullptr) { // hiredis 0.11 does not check.
      redisFree(ptr);
    }
  }
};

struct RedisReplyDeleter {
  void operator()(redisReply *ptr) {
    if (ptr != nullptr) { // hiredis 0.11 does not check.
      freeReplyObject(ptr);
    }
  }
};

typedef std::unique_ptr<redisReply, RedisReplyDeleter> RedisReply;
typedef std::unique_ptr<redisContext, RedisContextDeleter> RedisContext;

class Connection {
public:
  Connection(std::string host, int port)
      : host_(host), port_(port), state_(kDisconnected), timeout_us_(1000000),
        reconnect_interval_ms_(1000) {}

  ~Connection() { ShutDown(); }

  void StartUp();
  void ShutDown();

  RedisReply RedisCommand(const std::string &cmd);

private:
  enum State { kShutDown, kDisconnected, kConnected };
  RedisContext TryConnect();
  void EnsureConnection();
  void UpdateState();
  std::string ToString() { return host_ + ":" + std::to_string(port_); }

  RedisContext redis_;

  const std::string host_;
  const int port_;
  State state_;

  const int64_t timeout_us_;
  const int64_t reconnect_interval_ms_;

  DISALLOW_COPY_AND_ASSIGN(Connection);
};

typedef std::unique_ptr<Connection> Redis;

} // namespace redis
} // namespace util
} // namespace fluorine

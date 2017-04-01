#include <sys/time.h>
#include <boost/thread/thread.hpp>

#include "spdlog/spdlog.h"
#include "fluorine/util/Redis.hpp"

static auto logger = spdlog::stdout_color_st("Redis");

namespace fluorine {
namespace util {
namespace redis {

void RedisConnection::StartUp() { EnsureRedisConnection(); }

void RedisConnection::ShutDown() {
  if (state_ == kShutDown)
    return;

  redis_.reset();
  state_ = kShutDown;
}

RedisContext RedisConnection::TryConnect() {
  struct timeval timeout;
  timeout.tv_sec  = timeout_us_ / 1000000;
  timeout.tv_usec = timeout_us_ % 1000000;

  RedisContext rc(redisConnectWithTimeout(host_.c_str(), port_, timeout));
  if (rc == nullptr) {
    logger->error("cannot allocate redis context");
  } else if (rc->err) {
    logger->error("error while connecting to redis: {}", rc->errstr);
  } else if (redisSetTimeout(rc.get(), timeout) != REDIS_OK) {
    logger->error("error while setting timeout on redis context");
  } else {
    logger->info("connect redis success: {}", ToString());
    return rc;
  }

  return nullptr;
}

void RedisConnection::EnsureRedisConnection() {
  if (state_ == kConnected) {
    return;
  }

  RedisContext rc;
  for (;;) {
    rc = TryConnect();
    if (rc) {
      redis_ = std::move(rc);
      state_ = kConnected;
      return;
    }
    boost::this_thread::sleep(
        boost::posix_time::milliseconds(reconnect_interval_ms_));
  }
}

void RedisConnection::UpdateState() {
  // Quoting hireds documentation: "once an error is returned the context cannot
  // be reused and you should set up a new connection".
  if (redis_ != nullptr && redis_->err == REDIS_OK) {
    state_ = kConnected;
  } else {
    logger->error("redis disconnected");
    state_ = kDisconnected;
    redis_.reset();
  }
}

RedisReply RedisConnection::RedisCommand(const std::string &cmd) {
  EnsureRedisConnection();

  void *result = redisCommand(redis_.get(), cmd.c_str());
  auto reply   = RedisReply(static_cast<redisReply *>(result));

  if (!reply) {
    logger->error(
        "no reply returned from redis, while executing: {}, error: {}", cmd,
        redis_->errstr);
  } else if (reply->type == REDIS_REPLY_ERROR) {
    logger->error("redis returned error: {}, while executing: {}",
                  std::string(reply->str, reply->len), cmd);
  } else {
    logger->info("redis reply ok, while executing: {}", cmd);
  }

  UpdateState();
  return reply;
}

} // namespace redis
} // namespace util
} // namespace fluorine

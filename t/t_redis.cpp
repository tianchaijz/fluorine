#include <memory>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>

#include "fmt/format.h"
#include "fluorine/Macros.hpp"
#include "fluorine/util/Redis.hpp"

using namespace fluorine::util::redis;

void outputReply(RedisReply &reply) {
  if (!reply) {
    std::cout << "no reply" << std::endl;
    return;
  }

  switch (reply->type) {
  case REDIS_REPLY_STRING:
    std::cout << "string: " << reply->str << std::endl;
    break;
  case REDIS_REPLY_INTEGER:
    std::cout << "integer: " << reply->integer << std::endl;
    break;
  case REDIS_REPLY_STATUS:
    std::cout << "status: " << reply->str << std::endl;
    break;
  case REDIS_REPLY_NIL:
    std::cout << "nil" << std::endl;
    break;
  case REDIS_REPLY_ARRAY:
    for (size_t i = 0; i < reply->elements; ++i) {
      std::cout << i << ") " << reply->element[i]->str << std::endl;
    }
    break;
  case REDIS_REPLY_ERROR:
    std::cout << "error: " << std::string(reply->str, reply->len) << std::endl;
    break;
  default:
    std::cout << "unknown type: " << reply->type << std::endl;
  }
}

int main(int argc, char **argv) {
  Redis redis(new RedisConnection("127.0.0.1", 6379));

  auto reply = redis->RedisCommand("INFO");
  std::cout << reply->str << std::endl;

  reply = redis->RedisCommand(fmt::format("LRANGE list0 {} {}", "0", "-1"));
  outputReply(reply);

  std::getchar();

  reply = redis->RedisCommand(fmt::format("SET foo 3"));
  outputReply(reply);

  reply = redis->RedisCommand(fmt::format("GET foo"));
  outputReply(reply);

  for (;;) {
    reply = redis->RedisCommand(fmt::format("RPOP queue1"));
    if (reply && reply->type == REDIS_REPLY_NIL) {
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    outputReply(reply);
  }

  return 0;
}

#include <signal.h>
#include <stdint.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <fstream>
#include <algorithm>
#include <functional>
#include <boost/functional/hash.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "fmt/format.h"
#include "spdlog/spdlog.h"
#include "snet/EventLoop.h"
#include "snet/Timer.h"
#include "gzstream/gzstream.h"

#include "fluorine/Macros.hpp"
#include "fluorine/Option.hpp"
#include "fluorine/Timer.hpp"
#include "fluorine/Forwarder.hpp"
#include "fluorine/log/Parser.hpp"
#include "fluorine/log/Json.hpp"
#include "fluorine/config/Parser.hpp"
#include "fluorine/util/LRUCache.hpp"
#include "fluorine/util/IPResolver.hpp"
#include "fluorine/util/Redis.hpp"

using namespace fluorine;
using namespace fluorine::log;
using namespace fluorine::util;
using namespace fluorine::json;
using namespace fluorine::config;
using namespace fluorine::forwarder;
using namespace fluorine::util::redis;
using Value    = rapidjson::Value;
using Document = rapidjson::Document;

namespace lockfree = boost::lockfree;

using LRUType = LRUCache<size_t, std::unique_ptr<Document>>;

static auto logger = spdlog::stdout_color_st("F");
static lockfree::spsc_queue<std::string, lockfree::capacity<32768>> queue;
static unsigned long long lines = 0;
static unsigned long long total = 0;
static unsigned long long aggre = 0;
static std::atomic<bool> done(false);

void loop(snet::EventLoop *event_loop, Frontend *frontend, std::string path,
          const Config &config) {
  auto handler = [&frontend, &config, path]() {
    std::string line;
    while (frontend->CanSend() && queue.pop(line)) {
      Log log;
      if (!ParseLog(line, log, config.field_number_, config.time_index_)) {
        continue;
      }

      std::unique_ptr<rapidjson::Document> doc(new rapidjson::Document());
      if (!PopulateJsonDoc(doc.get(), log, config)) {
        continue;
      }

      if (!doc->HasMember("path")) {
        doc->AddMember("path", Value(path.c_str(), doc->GetAllocator()),
                       doc->GetAllocator());
      }

      std::string json = JsonDocToString(doc.get());
      char *ch         = new char[json.size() + 1];
      ch[json.size()]  = '\n';
      std::copy(json.begin(), json.end(), ch);
      std::unique_ptr<snet::Buffer> data(
          new snet::Buffer(ch, json.size() + 1, snet::OpDeleter));
      frontend->Send(std::move(data));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  };

  snet::TimerList timer_list;
  snet::Timer send_timer(&timer_list);
  snet::TimerDriver timer_driver(timer_list);

  auto callback = [&event_loop, &send_timer, &timer_driver, &handler,
                   &frontend]() {
    if (done && queue.empty()) {
      if (frontend->SendComplete()) {
        event_loop->Stop();
        event_loop->DelLoopHandler(&timer_driver);
        return;
      }
    } else {
      handler();
    }
    send_timer.ExpireFromNow(snet::Milliseconds(1));
  };

  send_timer.ExpireFromNow(snet::Milliseconds(0));
  send_timer.SetOnTimeout(callback);
  event_loop->AddLoopHandler(&timer_driver);
  event_loop->Loop();
}

template <class T>
inline void hash_combine(std::size_t &seed, const T &v) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

void agg(snet::EventLoop *event_loop, Frontend *frontend, std::string path,
         const Config &config) {
  auto aggregation = config.aggregation_;
  auto agg_keys    = aggregation->keys_;

  snet::TimerList timer_list;
  snet::Timer send_timer(&timer_list);
  snet::TimerDriver timer_driver(timer_list);

  std::vector<std::string> diff;
  std::set<std::string> store_set;
  std::set<std::string> ignore_set;

  if (aggregation->terms_) {
    for (auto &key : agg_keys) {
      store_set.insert(key);
    }
    store_set.insert(aggregation->time_);
    for (auto &term : *aggregation->terms_) {
      store_set.insert(term);
    }
  }

  for (auto it = store_set.begin(); it != store_set.end(); ++it) {
    logger->info("store filed: {}", *it);
  }

  auto ignore = [&store_set, &ignore_set](const Attribute &attr,
                                          std::string name) {
    if (attr.attribute_[1] == Attribute::STORE &&
        store_set.find(name) == store_set.end()) {
      ignore_set.insert(name);
    }
  };

  for (const auto &attr : config.attributes_) {
    if (attr.attribute_[0] == "ip") {
      ignore(attr, attr.name_);
      for (auto field : IPFields) {
        ignore(attr, attr.name_ + "@" + field);
      }
    } else if (attr.attribute_[0] == "request") {
      for (auto field : RequestFields) {
        ignore(attr, field);
      }
    } else {
      ignore(attr, attr.name_);
    }
  }

  for (auto it = ignore_set.begin(); it != ignore_set.end(); ++it) {
    logger->info("ignore filed: {}", *it);
  }

  auto clean_doc = [&ignore_set](std::unique_ptr<Document> &doc) {
    for (auto it = ignore_set.begin(); it != ignore_set.end(); ++it) {
      if (doc->HasMember(it->c_str())) {
        doc->RemoveMember(it->c_str());
      }
    }
  };

  auto send = [&frontend](std::unique_ptr<Document> &doc) {
    std::string json = JsonDocToString(doc.get());
    char *ch         = new char[json.size() + 1];
    ch[json.size()]  = '\n';
    std::copy(json.begin(), json.end(), ch);
    std::unique_ptr<snet::Buffer> data(
        new snet::Buffer(ch, json.size() + 1, snet::OpDeleter));
    frontend->Send(std::move(data));
  };

  LRUType::OnInsert oi = [path](std::unique_ptr<Document> &doc) {
    if (!doc->HasMember("count")) {
      doc->AddMember("count", int64_t(1), doc->GetAllocator());
    }
    if (!doc->HasMember("path")) {
      doc->AddMember("path", Value(path.c_str(), doc->GetAllocator()),
                     doc->GetAllocator());
    }
  };

  LRUType::OnAggregation oa = [agg_keys](std::unique_ptr<Document> &lhs,
                                         std::unique_ptr<Document> &rhs) {
    rapidjson::Value &count = (*lhs)["count"];
    count.SetInt64(count.GetInt64() + 1);

    for (auto &key : agg_keys) {
      auto k = key.c_str();

      rapidjson::Value &l = (*lhs)[k];
      rapidjson::Value &r = (*rhs)[k];

      // XXX: only support aggregation on int64 and double type
      if (l.IsNumber() && l.GetType() == r.GetType()) {
        if (l.IsInt64()) {
          l.SetInt64(l.GetInt64() + r.GetInt64());
        } else if (l.IsDouble()) {
          l.SetDouble(l.GetDouble() + r.GetDouble());
        }
      }
    }
  };

  LRUType::OnEvict oe = [&send](std::unique_ptr<Document> &doc) {
    rapidjson::Value &count = (*doc)["count"];
    total += count.GetInt64();
    ++aggre;
    send(doc);
  };

  LRUType::OnClear oc = [&send](LRUType::map_type &m) {
    for (auto &p : m) {
      rapidjson::Value &count = (*p.second.first)["count"];
      total += count.GetInt64();
      ++aggre;
      send(p.second.first);
    }
  };

  auto hash = [&aggregation](size_t &seed, std::unique_ptr<Document> &doc) {
    if (aggregation->terms_) {
      for (auto term : *aggregation->terms_) {
        Value &v = (*doc)[term.c_str()];
        if (v.IsString())
          hash_combine(seed, std::string(v.GetString()));
        else if (v.IsInt())
          hash_combine(seed, v.GetInt());
        else if (v.IsInt64())
          hash_combine(seed, v.GetInt64());
        else if (v.IsDouble())
          hash_combine(seed, v.GetDouble());
        else {
          logger->error("unexpected value type: {}, term: {}", v.GetType(),
                        term);
          return false;
        }
      }
    }

    return true;
  };

  LRUType lru(3600, oi, oa, oe, oc);
  auto handler = [&frontend, &config, &hash, &lru, &clean_doc, &path]() {
    std::string line;
    int interval = config.aggregation_->interval_;
    while (frontend->CanSend() && queue.pop(line)) {
      Log log;
      if (!ParseLog(line, log, config.field_number_, config.time_index_)) {
        logger->warn("{}, bad log: {}", path, line);
        continue;
      }

      std::unique_ptr<rapidjson::Document> doc(new rapidjson::Document());
      if (!PopulateJsonDoc(doc.get(), log, config)) {
        logger->warn("{}, json error: {}", path, line);
        continue;
      }

      clean_doc(doc);
      size_t timestamp;
      if (interval) {
        rapidjson::Value &tm = (*doc)[config.aggregation_->time_.c_str()];

        timestamp = tm.GetInt64();
        timestamp = timestamp - (timestamp % interval);
        tm.SetInt64(timestamp);
      } else {
        timestamp = 0;
      }

      if (hash(timestamp, doc)) {
        lru.insert(timestamp, std::move(doc));
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  };

  auto callback = [&event_loop, &send_timer, &timer_driver, &handler, &lru,
                   &frontend]() {
    if (done && queue.empty()) {
      lru.clear();
      if (frontend->SendComplete()) {
        event_loop->Stop();
        event_loop->DelLoopHandler(&timer_driver);
        logger->info("event loop stopped");
        return;
      }
    } else {
      handler();
    }
    send_timer.ExpireFromNow(snet::Milliseconds(1));
  };

  send_timer.ExpireFromNow(snet::Milliseconds(0));
  send_timer.SetOnTimeout(callback);
  event_loop->AddLoopHandler(&timer_driver);
  event_loop->Loop();
}

template <typename T>
inline void produce(T &is) {
  std::string line;
  while (std::getline(is, line)) {
    ++lines;
    if (lines % 100000 == 0) {
      logger->info("input lines: {}", lines);
    }
    while (!queue.push(std::move(line)))
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void producer(std::string path) {
  std::ifstream is(path);
  if (!is.is_open()) {
    logger->error("cannot open: {}", path);
    return;
  }

  produce(is);
}

void gzip_producer(std::string path) {
  igzstream is(path.c_str(), std::ios_base::in | std::ios_base::binary);
  if (!is.good()) {
    logger->error("cannot open: {}", path);
    return;
  }

  produce(is);
}

void cycle(snet::EventLoop *event_loop, Frontend *frontend, std::string path,
           Config &cfg) {
  TimerGuard tg;
  std::thread loop_thread;
  if (cfg.aggregation_) {
    loop_thread =
        std::thread(std::bind(agg, event_loop, frontend, path, std::cref(cfg)));
  } else {
    loop_thread = std::thread(
        std::bind(loop, event_loop, frontend, path, std::cref(cfg)));
  }

  if (boost::algorithm::ends_with(path, ".gz")) {
    gzip_producer(path);
  } else {
    producer(path);
  }

  done = true;
  loop_thread.join();

  logger->info("input: {}, handle: {}, aggregation: {}, {}%", lines, total,
               aggre, total == 0 ? 0 : aggre * 100.0 / total);
}

void fix_config(Config &cfg, bool show = false) {
  if (!cfg.aggregation_) {
    return;
  }

  auto agg = cfg.aggregation_;
  for (auto &attr : cfg.attributes_) {
    if (attr.name_ == agg->time_) {
      attr.attribute_[1] = Attribute::STORE;
    }
  }

  if (!show) {
    return;
  }

  std::stringstream ss;
  ss << "[";
  for (size_t i = 0; i < agg->keys_.size(); ++i) {
    if (i != 0) {
      ss << ",";
    }
    ss << agg->keys_[i];
  }
  ss << "]";

  auto info = fmt::format("aggregation: {}, {}, {}", ss.str(), agg->time_,
                          agg->interval_);
  std::cout << info << std::endl;
  if (agg->terms_) {
    for (auto f : *agg->terms_) {
      std::cout << fmt::format("{}, ", f);
    }
    std::cout << std::endl;
  }
}

int main(int argc, char *argv[]) {
  signal(SIGPIPE, SIG_IGN);

  Option opt;
  ParseOption(argc, argv, opt);

  InitIPResolver(opt.ip_db_path_);

  auto event_loop = snet::CreateEventLoop(1000000);
  snet::TimerList timer_list;
  Frontend frontend(opt.backend_ip_, opt.backend_port_, event_loop.get(),
                    timer_list);

  if (opt.IsTcpInput()) {
    snet::TimerList timer_list;
    snet::TimerDriver timer_driver(timer_list);
    FrontendTcp ft(opt.frontend_ip_, opt.frontend_port_, opt.backend_ip_,
                   opt.backend_port_, event_loop.get(), timer_list);
    event_loop->AddLoopHandler(&timer_driver);
    event_loop->Loop();
  } else if (opt.IsRedisInput()) {
    auto address = opt.GetRedisAddress();
    auto redis   = Redis(new RedisConnection(address.first, address.second));
    for (;;) {
      auto reply = redis->RedisCommand("GET Log:Stop");
      if (reply && reply->type == REDIS_REPLY_STRING) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        continue;
      }

      reply = redis->RedisCommand(fmt::format("LPOP {}", opt.redis_queue_));
      if (reply && reply->type == REDIS_REPLY_STRING) {
        rapidjson::Document doc;
        doc.Parse(reply->str);

        Value &path = doc[0];
        Value &slot = doc[1];

        logger->info("input file: {}", path.GetString());

        reply = redis->RedisCommand(
            fmt::format("HGET Log:Config {}", slot.GetString()));

        if (!reply || reply->type != REDIS_REPLY_STRING) {
          continue;
        }

        Config cfg;
        if (!ParseConfig(reply->str, cfg)) {
          logger->error("invalid config got from redis");
          continue;
        }

        fix_config(cfg);
        done = false;
        cycle(event_loop.get(), &frontend, path.GetString(), cfg);
      } else {
        std::this_thread::sleep_for(std::chrono::seconds(2));
      }
    }
  } else {
    Config cfg;
    if (!ParseConfig(opt.config_path_, cfg)) {
      return 1;
    }
    fix_config(cfg);
    cycle(event_loop.get(), &frontend, opt.log_path_, cfg);
  }

  return 0;
}

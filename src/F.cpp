#include <signal.h>
#include <stdint.h>
#include <fstream>
#include <algorithm>
#include <functional>
#include <boost/functional/hash.hpp>
#include <boost/thread/thread.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include "spdlog/spdlog.h"
#include "snet/EventLoop.h"
#include "snet/Timer.h"

#include "fluorine/Macros.hpp"
#include "fluorine/Option.hpp"
#include "fluorine/Timer.hpp"
#include "fluorine/Forwarder.hpp"
#include "fluorine/log/Parser.hpp"
#include "fluorine/log/Json.hpp"
#include "fluorine/config/Parser.hpp"
#include "fluorine/util/LRUCache.hpp"
#include "fluorine/util/IPResolver.hpp"

using namespace fluorine;
using namespace fluorine::log;
using namespace fluorine::util;
using namespace fluorine::json;
using namespace fluorine::config;
using namespace fluorine::forwarder;
using Value    = rapidjson::Value;
using Document = rapidjson::Document;

namespace lockfree = boost::lockfree;

using LRUType = LRUCache<size_t, std::unique_ptr<Document>>;

static auto logger = spdlog::stdout_color_st("F");
static lockfree::spsc_queue<std::string, lockfree::capacity<8192>> queue;
static unsigned long long total = 0;
static unsigned long long aggre = 0;
static bool done                = false;

void loop(std::string backend_ip, unsigned short backend_port,
          const Config &config) {
  auto event_loop = snet::CreateEventLoop();
  snet::TimerList timer_list;
  Frontend frontend(backend_ip, backend_port, event_loop.get(), &timer_list);

  auto handler = [&frontend, &config]() {
    std::string line;
    while (frontend.CanSend() && queue.pop(line)) {
      std::string json;
      Log log;
      if (ParseLog(line, log, config.field_number_, config.time_index_)) {
        if (ToJsonString(log, json, config)) {
          char *ch        = new char[json.size() + 1];
          ch[json.size()] = '\n';
          std::copy(json.begin(), json.end(), ch);
          std::unique_ptr<snet::Buffer> data(
              new snet::Buffer(ch, json.size() + 1, snet::OpDeleter));
          frontend.Send(std::move(data));
        }
      }
    }
  };

  snet::Timer send_timer(&timer_list);
  auto callback = [&event_loop, &send_timer, &handler]() {
    if (done && queue.empty()) {
      event_loop->Stop();
      return;
    }
    handler();
    send_timer.ExpireFromNow(snet::Milliseconds(0));
  };
  send_timer.ExpireFromNow(snet::Milliseconds(0));
  send_timer.SetOnTimeout(callback);
  snet::TimerDriver timer_driver(timer_list);
  event_loop->AddLoopHandler(&timer_driver);
  event_loop->Loop();
}

template <class T>
inline void hash_combine(std::size_t &seed, const T &v) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

void agg(std::string backend_ip, unsigned short backend_port,
         const Config &config) {
  auto aggregation = config.aggregation_;
  auto agg_key     = aggregation->key_.c_str();
  auto event_loop  = snet::CreateEventLoop();
  snet::TimerList timer_list;
  Frontend frontend(backend_ip, backend_port, event_loop.get(), &timer_list);

  std::vector<std::string> diff;
  std::set<std::string> store_set;
  std::set<std::string> ignore_set;

  if (aggregation->fields_) {
    store_set.insert(aggregation->key_);
    store_set.insert(aggregation->time_);
    for (auto &f : *aggregation->fields_) {
      store_set.insert(f);
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
      for (auto field : IPFields) {
        ignore(attr, attr.name_ + "." + field);
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
    std::string json;
    DocToString(doc.get(), json);
    char *ch        = new char[json.size() + 1];
    ch[json.size()] = '\n';
    std::copy(json.begin(), json.end(), ch);
    std::unique_ptr<snet::Buffer> data(
        new snet::Buffer(ch, json.size() + 1, snet::OpDeleter));
    frontend.Send(std::move(data));
  };

  LRUType::OnInsert oi = [](std::unique_ptr<Document> &doc) {
    if (!doc->HasMember("count")) {
      doc->AddMember("count", int64_t(1), doc->GetAllocator());
    }
  };

  LRUType::OnAggregation oa = [agg_key](std::unique_ptr<Document> &lhs,
                                        std::unique_ptr<Document> &rhs) {
    rapidjson::Value &count = (*lhs)["count"];
    count.SetInt64(count.GetInt64() + 1);

    rapidjson::Value &v = (*lhs)[agg_key];
    v.SetInt64(v.GetInt64() + (*rhs)[agg_key].GetInt64());
  };

  LRUType::OnEvict oe = [&frontend, &send](std::unique_ptr<Document> &doc) {
    while (!frontend.CanSend()) {
      boost::this_thread::sleep(boost::posix_time::milliseconds(1));
    }
    rapidjson::Value &count = (*doc)["count"];
    total += count.GetInt64();
    ++aggre;
    send(doc);
  };

  LRUType::OnClear oc = [&frontend, &send](LRUType::map_type &m) {
    for (auto &p : m) {
      while (!frontend.CanSend()) {
        boost::this_thread::sleep(boost::posix_time::milliseconds(1));
      }

      rapidjson::Value &count = (*p.second.first)["count"];
      total += count.GetInt64();
      ++aggre;
      send(p.second.first);
    }
  };

  auto hash = [&aggregation](size_t seed, std::unique_ptr<Document> &doc) {
    if (aggregation->fields_) {
      for (auto f : *aggregation->fields_) {
        Value &v = (*doc)[f.c_str()];
        if (v.IsString())
          hash_combine(seed, std::string(v.GetString()));
        else if (v.IsInt())
          hash_combine(seed, v.GetInt());
        else if (v.IsInt64())
          hash_combine(seed, v.GetInt64());
        else if (v.IsDouble())
          hash_combine(seed, v.GetDouble());
        else {
          logger->error("unexpected type");
          assert(false);
        }
      }
    }

    return seed;
  };

  LRUType lru(300, oi, oa, oe, oc);
  auto handler = [&frontend, &config, &hash, &lru, &clean_doc]() {
    std::string line;
    int interval = config.aggregation_->interval_;
    while (frontend.CanSend() && queue.pop(line)) {
      std::string json;
      Log log;
      if (ParseLog(line, log, config.field_number_, config.time_index_)) {
        std::unique_ptr<rapidjson::Document> doc(new rapidjson::Document());
        if (PopulateJsonDoc(doc.get(), log, config)) {
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
          size_t key = hash(timestamp, doc);
          lru.insert(key, std::move(doc));
        }
      }
    }
  };

  snet::Timer send_timer(&timer_list);
  auto callback = [&event_loop, &send_timer, &handler, &lru]() {
    if (done && queue.empty()) {
      lru.clear();
      event_loop->Stop();
      return;
    }
    handler();
    send_timer.ExpireFromNow(snet::Milliseconds(0));
  };
  send_timer.ExpireFromNow(snet::Milliseconds(0));
  send_timer.SetOnTimeout(callback);
  snet::TimerDriver timer_driver(timer_list);
  event_loop->AddLoopHandler(&timer_driver);
  event_loop->Loop();
}

int main(int argc, char *argv[]) {
  signal(SIGPIPE, SIG_IGN);

  Option opt;
  ParseOption(argc, argv, opt);

  Config cfg;
  if (!ParseConfig(opt.config_path_, cfg)) {
    return 1;
  }

  if (cfg.aggregation_) {
    auto agg = cfg.aggregation_;
    for (auto &attr : cfg.attributes_) {
      if (attr.name_ == agg->time_) {
        attr.attribute_[1] = Attribute::STORE;
      }
    }

    std::cout << agg->key_ << "," << agg->time_ << "," << agg->interval_
              << std::endl;
    if (agg->fields_) {
      for (auto f : *agg->fields_) {
        std::cout << f << std::endl;
      }
    }
  }

  InitIPResolver(opt.ip_db_path_);

  if (opt.IsTcpInput()) {
    auto event_loop = snet::CreateEventLoop(2000);
    snet::TimerList timer_list;
    snet::TimerDriver timer_driver(timer_list);
    FrontendTcp ft(opt.frontend_ip_, opt.frontend_port_, opt.backend_ip_,
                   opt.backend_port_, event_loop.get(), &timer_list);
    event_loop->AddLoopHandler(&timer_driver);
    event_loop->Loop();
  } else {
    TimerGuard tg;
    boost::thread loop_thread;
    if (cfg.aggregation_) {
      loop_thread = boost::thread(
          std::bind(agg, opt.backend_ip_, opt.backend_port_, std::cref(cfg)));
    } else {
      loop_thread = boost::thread(
          std::bind(loop, opt.backend_ip_, opt.backend_port_, std::cref(cfg)));
    }

    std::ifstream is(opt.log_path_);
    std::string line;
    int i = 0;
    while (std::getline(is, line)) {
      ++i;
      if (i % 10000 == 0) {
        logger->info("input lines: {}", i);
      }
      while (!queue.push(std::move(line)))
        ;
    }
    done = true;
    loop_thread.join();
    logger->info("input: {}, handle: {}, agg: {}, {}%", i, total, aggre,
                 aggre * 100.0 / total);
  }

  return 0;
}

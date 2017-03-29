#include <signal.h>
#include <fstream>
#include <functional>
#include <boost/thread/thread.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include "spdlog/spdlog.h"
#include "snet/EventLoop.h"
#include "snet/Timer.h"

#include "fluorine/Option.hpp"
#include "fluorine/Timer.hpp"
#include "fluorine/Forwarder.hpp"
#include "fluorine/log/Parser.hpp"
#include "fluorine/log/Json.hpp"
#include "fluorine/config/Parser.hpp"
#include "fluorine/util/IPResolver.hpp"

using namespace fluorine;
using namespace fluorine::log;
using namespace fluorine::util;
using namespace fluorine::json;
using namespace fluorine::config;
using namespace fluorine::forwarder;

namespace lockfree = boost::lockfree;

static auto logger = spdlog::stdout_color_st("F");
static lockfree::spsc_queue<std::string, lockfree::capacity<8192>> queue;
static bool done = false;

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
        ToJson(log, json, config);
      }
      char *ch        = new char[json.size() + 1];
      ch[json.size()] = '\n';
      std::copy(json.begin(), json.end(), ch);
      std::unique_ptr<snet::Buffer> data(
          new snet::Buffer(ch, json.size() + 1, snet::OpDeleter));
      frontend.Send(std::move(data));
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

int main(int argc, char *argv[]) {
  signal(SIGPIPE, SIG_IGN);

  Option opt;
  ParseOption(argc, argv, opt);

  Config cfg;
  if (!ParseConfig(opt.config_path_, cfg)) {
    return 1;
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
    boost::thread loop_thread(
        std::bind(loop, opt.backend_ip_, opt.backend_port_, std::cref(cfg)));
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
    logger->info("input complete");
    done = true;
    loop_thread.join();
  }

  return 0;
}

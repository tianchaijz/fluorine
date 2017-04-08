#pragma once

#include <set>
#include <map>
#include <string>
#include <functional>
#include <ctime>
#include <time.h>

#include <boost/spirit/include/qi.hpp>
#include <boost/fusion/adapted/std_tuple.hpp>

#include "rapidjson/writer.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"

#include "fluorine/Macros.hpp"
#include "fluorine/log/Parser.hpp"
#include "fluorine/config/Parser.hpp"
#include "fluorine/util/Fast.hpp"
#include "fluorine/util/IPResolver.hpp"

namespace fluorine {
namespace json {
namespace qi = boost::spirit::qi;

using namespace rapidjson;
using namespace fluorine::log;
using namespace fluorine::util;
using namespace fluorine::config;
using std::string;

extern std::set<std::string> IPFields;
extern std::set<std::string> RequestFields;

typedef std::function<bool(Document &, string, string)> Handler;
typedef std::map<string, Handler> Handlers;
typedef std::tuple<string, string, string> Request;

template <typename Iterator = string::iterator>
struct RequestGrammar : qi::grammar<Iterator, Request()> {
  RequestGrammar() : RequestGrammar::base_type(request) {
    using namespace qi;
    scheme  = hold[+char_("a-z") >> lit("://")] | attr("http");
    request = +char_("A-Z") >> omit[+space] >> scheme >>
              (+~char_(" /") | attr("unknown"));
  }

private:
  qi::rule<Iterator, string()> scheme;
  qi::rule<Iterator, Request()> request;
};

inline bool string_handler(Document &doc, string k, string v) {
  Value key(k.c_str(), doc.GetAllocator()), val(v.c_str(), doc.GetAllocator());
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());
  return true;
}

// int
inline bool int32_handler(Document &doc, string k, string v) {
  Value key(k.c_str(), doc.GetAllocator()), val;
  int num;
  try {
    num = std::stoi(v);
  } catch (const std::exception &e) {
    std::cerr << "int32 error: " << v << std::endl;
    return false;
  }
  val.SetInt(num);
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());
  return true;
};

// long long
inline bool int64_handler(Document &doc, string k, string v) {
  Value key(k.c_str(), doc.GetAllocator()), val;
  int64_t num;
  try {
    num = std::stoll(v);
  } catch (const std::exception &e) {
    std::cerr << "int64 error: " << v << std::endl;
    return false;
  }
  val.SetInt64(num);
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());
  return true;
};

// double
inline bool double_handler(Document &doc, string k, string v) {
  Value key(k.c_str(), doc.GetAllocator()), val;
  double num;
  try {
    num = std::stod(v);
  } catch (const std::exception &e) {
    std::cerr << "double error: " << v << std::endl;
    return false;
  }
  val.SetDouble(num);
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());
  return true;
};

// ip address handler
inline bool ip_handler(Document &doc, string k, string v) {
  Value key(k.c_str(), doc.GetAllocator()), val(v.c_str(), doc.GetAllocator());
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());

  std::vector<string> fields(IPResolver::FieldNumber, "unknown");
  char *result;
  if (util::ResolveIP(v, &result)) {
    int i   = 0;
    char *s = result, *e = result;
    while (*e) {
      if (*e == '\t') {
        if (i < IPResolver::FieldNumber) {
          fields[i++] = string(s, e);
        }
        s = e + 1;
      }
      ++e;
    }

    if (i < IPResolver::FieldNumber) {
      fields[i] = string(s, e);
    }
  }

  string_handler(doc, k + "@country", fields[0]);
  string_handler(doc, k + "@province", fields[1]);
  string_handler(doc, k + "@city", fields[2]);
  string_handler(doc, k + "@isp", fields[4]);

  return true;
};

struct TimeLocal {
  int day_;
  std::string mon_;
  int year_;
  int hour_;
  int min_;
  int sec_;
  char sign_;
  int tz_hour_;
  int tz_min_;
};

template <typename Iterator = std::string::iterator>
struct TimeLocalGrammar : qi::grammar<Iterator, TimeLocal()> {
  TimeLocalGrammar() : TimeLocalGrammar::base_type(tl) {
    using namespace boost::spirit::qi;
    int_parser<unsigned, 10, 2, 2> int2_2_p;

    tl = int_ >> '/' >> +char_("a-zA-Z") >> '/' >> int_ >> ':' >> int_ >> ':' >>
         int_ >> ':' >> int_ >> omit[+space] >> char_("+-") >> int2_2_p >>
         int2_2_p;
  }

private:
  qi::rule<Iterator, TimeLocal()> tl;
};

inline bool time_local_handler(Document &doc, string k, string s) {
  static std::map<std::string, unsigned short> month = {
      {"Jan", 0}, {"Feb", 1}, {"Mar", 2}, {"Apr", 3}, {"May", 4},  {"Jun", 5},
      {"Jul", 6}, {"Aug", 7}, {"Sep", 8}, {"Oct", 9}, {"Nov", 10}, {"Dec", 11}};

  TimeLocalGrammar<> g;
  TimeLocal tl;

  bool ok = qi::parse(s.begin(), s.end(), g, tl);
  if (!ok) {
    return false;
  }

  auto it = month.find(tl.mon_);
  if (it == month.end()) {
    return false;
  }

  struct tm tm = {};

  tm.tm_mday = tl.day_;
  tm.tm_mon  = it->second;
  tm.tm_year = tl.year_ - 1900;
  tm.tm_hour = tl.hour_;
  tm.tm_min  = tl.min_;
  tm.tm_sec  = tl.sec_;

  time_t ts = cached_mktime(&tm);
  if (ts < 0) {
    return false;
  }

  int offset = tl.tz_hour_ * 3600 + tl.tz_min_ * 60;

  Value key(k.c_str(), doc.GetAllocator()), val;
  val.SetInt64(tl.sign_ == '+' ? ts + offset : ts - offset);
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());

  return true;
}

// request handler, like: "GET http:://foo.com/bar"
inline bool request_handler(Document &doc, string, string s) {
  RequestGrammar<> g;
  Request request;
  auto begin = s.begin();
  auto end   = s.end();

  bool ok = parse(begin, end, g, request);
  if (!ok) {
    std::cerr << "parse request failed: " << s << std::endl;
    return false;
  }

  Value method(std::get<0>(request).c_str(), doc.GetAllocator());
  Value scheme(std::get<1>(request).c_str(), doc.GetAllocator());
  Value domain(std::get<2>(request).c_str(), doc.GetAllocator());

  doc.AddMember("method", method.Move(), doc.GetAllocator());
  doc.AddMember("scheme", scheme.Move(), doc.GetAllocator());
  doc.AddMember("domain", domain.Move(), doc.GetAllocator());

  return true;
}

inline bool status_handler(Document &doc, string k, string v) {
  Value key(k.c_str(), doc.GetAllocator()), val;
  int num;
  try {
    num = std::stoi(v);
  } catch (const std::exception &e) {
    num = 0;
  }
  val.SetInt(num);
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());
  return true;
};

const Handlers handlers = {
    {"string", string_handler},   {"int", int32_handler},
    {"long long", int64_handler}, {"double", double_handler},
    {"ip", ip_handler},           {"time_local", time_local_handler},
    {"request", request_handler}, {"status", status_handler},
};

bool DocToString(Document *doc, std::string &json);
bool PopulateJsonDoc(Document *doc, const Log &log, const Config &cfg);
bool ToJsonString(Log &log, std::string &json, const Config &cfg);

} // namespace json
} // namespace fluorine

BOOST_FUSION_ADAPT_STRUCT(fluorine::json::TimeLocal,
    (int, day_)
    (std::string, mon_)
    (int, year_)
    (int, hour_)
    (int, min_)
    (int, sec_)
    (char, sign_)
    (int, tz_hour_)
    (int, tz_min_))

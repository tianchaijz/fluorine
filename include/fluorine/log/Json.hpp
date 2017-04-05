#pragma once

#include <set>
#include <string>
#include <unordered_map>
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

typedef std::function<void(Document &, string, string)> Handler;
typedef std::unordered_map<string, Handler> Handlers;
typedef std::tuple<string, string, string> Request;

template <typename Iterator = string::iterator>
struct RequestGrammar : qi::grammar<Iterator, Request()> {
  RequestGrammar() : RequestGrammar::base_type(request) {
    using namespace qi;
    scheme  = hold[+char_("a-z") >> lit("://")] | attr("http");
    request = +char_("A-Z") >> omit[+space] >> scheme >>
              (+~char_(" /") | attr("unknown")) >> omit[*char_];
  }

private:
  qi::rule<Iterator, string()> scheme;
  qi::rule<Iterator, Request()> request;
};

inline void string_handler(Document &doc, string k, string v) {
  Value key(k.c_str(), doc.GetAllocator()), val(v.c_str(), doc.GetAllocator());
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());
}

// int
inline void int32_handler(Document &doc, string k, string v) {
  Value key(k.c_str(), doc.GetAllocator()), val;
  int num;
  try {
    num = std::stoi(v);
  } catch (const std::exception &e) {
    std::cerr << "int32 error: " << v << std::endl;
    num = 0;
  }
  val.SetInt(num);
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());
};

// long long
inline void int64_handler(Document &doc, string k, string v) {
  Value key(k.c_str(), doc.GetAllocator()), val;
  int64_t num;
  try {
    num = std::stoll(v);
  } catch (const std::exception &e) {
    std::cerr << "int64 error: " << v << std::endl;
    num = 0;
  }
  val.SetInt64(num);
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());
};

// double
inline void double_handler(Document &doc, string k, string v) {
  Value key(k.c_str(), doc.GetAllocator()), val;
  double num;
  try {
    num = std::stod(v);
  } catch (const std::exception &e) {
    std::cerr << "double error: " << v << std::endl;
    num = 0.0;
  }
  val.SetDouble(num);
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());
};

// request handler, like: "GET http:://foo.com/bar"
inline void request_handler(Document &doc, string, string s) {
  RequestGrammar<> g;
  Request request;
  parse(s.begin(), s.end(), g, request);

  Value method(std::get<0>(request).c_str(), doc.GetAllocator());
  Value scheme(std::get<1>(request).c_str(), doc.GetAllocator());
  Value domain(std::get<2>(request).c_str(), doc.GetAllocator());
  doc.AddMember("method", method.Move(), doc.GetAllocator());
  doc.AddMember("scheme", scheme.Move(), doc.GetAllocator());
  doc.AddMember("domain", domain.Move(), doc.GetAllocator());
}

// ip address handler
inline void ip_handler(Document &doc, string k, string v) {
  Value key(k.c_str(), doc.GetAllocator()), val(v.c_str(), doc.GetAllocator());
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());

  char result[util::IPResolver::ResultLengthMax + 1];
  if (util::ResolveIP(v, result)) {
    std::vector<string> fields(IPResolver::FieldNumber, "unknown");

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

    string_handler(doc, k + "@country", fields[0]);
    string_handler(doc, k + "@province", fields[1]);
    string_handler(doc, k + "@city", fields[2]);
    string_handler(doc, k + "@isp", fields[4]);
  }
};

inline void time_local_handler(Document &doc, string k, string s) {
  struct tm tm;
  strptime(s.c_str(), "%d/%b/%Y:%H:%M:%S %z", &tm);
  Value key(k.c_str(), doc.GetAllocator()), val;
  val.SetInt64(mktime(&tm));
  doc.AddMember(key.Move(), val.Move(), doc.GetAllocator());
}

const Handlers handlers = {
    {"string", string_handler},   {"int", int32_handler},
    {"long long", int64_handler}, {"double", double_handler},
    {"ip", ip_handler},           {"time_local", time_local_handler},
    {"request", request_handler},
};

bool DocToString(Document *doc, std::string &json);
bool PopulateJsonDoc(Document *doc, const Log &log, const Config &cfg);
bool ToJsonString(Log &log, std::string &json, const Config &cfg);

} // namespace json
} // namespace fluorine

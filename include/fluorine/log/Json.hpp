#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <ctime>
#include <time.h>

#include <boost/spirit/include/qi.hpp>
#include <boost/fusion/adapted/std_tuple.hpp>

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "fluorine/Macros.hpp"
#include "fluorine/log/Parser.hpp"
#include "fluorine/config/Parser.hpp"
#include "fluorine/util/IPResolver.hpp"

namespace fluorine {
namespace json {
using namespace rapidjson;
using namespace fluorine::log;
using namespace fluorine::util;
using namespace fluorine::config;
using namespace boost::spirit;
using std::string;

typedef std::function<void(Writer<StringBuffer> &, string, string)> Handler;
typedef std::unordered_map<string, Handler> Handlers;
typedef std::tuple<string, string> Request;

template <typename Iterator = string::iterator>
struct RequestGrammar : qi::grammar<Iterator, Request()> {
  RequestGrammar() : RequestGrammar::base_type(request) {
    using namespace qi;
    request = +char_("A-Z") >> omit[+space] >>
              -(lit("http") >> -lit('s') >> lit("://")) >>
              (+~char_('/') | attr("unknown")) >> omit[*char_];
  }

private:
  qi::rule<Iterator, Request()> request;
};

inline void write_key(Writer<StringBuffer> &writer, string k) {
  writer.Key(k.c_str(), k.size());
};

inline void write_string(Writer<StringBuffer> &writer, string v) {
  writer.String(v.c_str(), v.size());
};

inline void string_handler(Writer<StringBuffer> &writer, string k, string v) {
  writer.Key(k.c_str(), k.size());
  writer.String(v.c_str(), v.size());
}

// int
inline void int32_handler(Writer<StringBuffer> &writer, string k, string v) {
  writer.Key(k.c_str(), k.size());
  writer.Int(std::stoi(v));
};

// long long
inline void int64_handler(Writer<StringBuffer> &writer, string k, string v) {
  writer.Key(k.c_str(), k.size());
  writer.Int64(std::stoll(v));
};

// double
inline void double_handler(Writer<StringBuffer> &writer, string k, string v) {
  writer.Key(k.c_str(), k.size());
  writer.Double(std::stod(v));
};

// request handler, like: "GET http:://foo.com/bar"
inline void request_handler(Writer<StringBuffer> &writer, string, string s) {
  RequestGrammar<> g;
  Request request;
  parse(s.begin(), s.end(), g, request);

  writer.Key("method");
  write_string(writer, std::get<0>(request));

  writer.Key("domain");
  write_string(writer, std::get<1>(request));
}

// ip address handler
inline void ip_handler(Writer<StringBuffer> &writer, string k, string v) {
  write_key(writer, k);
  writer.StartObject();

  writer.Key("ip");
  write_string(writer, v);

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

    writer.Key("country");
    write_string(writer, fields[0]);

    writer.Key("province");
    write_string(writer, fields[1]);

    writer.Key("city");
    write_string(writer, fields[2]);

    writer.Key("isp");
    write_string(writer, fields[4]);
  }

  writer.EndObject();
};

inline void time_local_handler(Writer<StringBuffer> &writer, string k,
                               string s) {
  write_key(writer, k);
  struct tm tm;
  strptime(s.c_str(), "%d/%b/%Y:%H:%M:%S %z", &tm);
  time_t t = mktime(&tm);
  writer.Int64(t);
}

const Handlers handlers = {
    {"string", string_handler},   {"int", int32_handler},
    {"long long", int64_handler}, {"double", double_handler},
    {"ip", ip_handler},           {"time_local", time_local_handler},
    {"request", request_handler},
};

void ToJson(Log &log, std::string &json, const Config &cfg) {
  if (static_cast<int>(log.size()) != cfg.field_number_) {
    fprintf(stderr, "invalid log, log fields: %lu, expected: %d\n", log.size(),
            cfg.field_number_);
    for (auto &field : log)
      std::cerr << "<" << field << ">";
    std::cerr << std::endl;
    return;
  }

  StringBuffer sb;
  Writer<StringBuffer> writer(sb);
  writer.StartObject();

  writer.Key("type");
  write_string(writer, cfg.name_);

  auto &attributes = cfg.attributes_;
  for (size_t i = 0, j = 0; i < attributes.size(); ++i) {
    auto attribute = attributes[i].attribute_;

    if (attribute[1] == Attribute::IGNORE) {
      ++j;
      continue;
    }

    auto it = handlers.find(attribute[0]);
    if (it == handlers.end()) {
      std::cerr << "invalid attribute: " << attribute[0] << std::endl;
      return;
    }

    int time_index = cfg.time_index_ - 1;
    int time_span  = cfg.time_span_;

    if (attribute[1] == Attribute::STORE && j < log.size()) {
      if (static_cast<int>(j) == time_index && time_span > 0) {
        it->second(writer, attributes[i].name_, log[j] + " " + log[j + 1]);
        j += 2;
      } else {
        it->second(writer, attributes[i].name_, log[j++]);
      }
    } else if (attribute[1] == Attribute::ADD) {
      it->second(writer, attributes[i].name_, attribute[2]);
    }
  }

  writer.EndObject();
  json = std::move(sb.GetString());
}

} // namespace json
} // namespace fluorine

#include <iostream>

#include "spdlog/spdlog.h"
#include "fluorine/log/Json.hpp"
#include "fluorine/log/Parser.hpp"

static auto logger = spdlog::stdout_color_st("Json");

namespace fluorine {
namespace json {

std::set<std::string> IPFields{"country", "province", "city", "isp"};
std::set<std::string> RequestFields{"method", "scheme", "domain"};

std::string JsonDocToString(Document *doc) {
  StringBuffer sb;
  Writer<StringBuffer> writer(sb);
  doc->Accept(writer);
  return sb.GetString();
}

bool PopulateJsonDoc(Document *doc, const Log &log, const Config &cfg) {
  if (cfg.field_number_ && static_cast<int>(log.size()) != cfg.field_number_) {
    logger->error("invalid log, log fields: {}, expected: {}", log.size(),
                  cfg.field_number_);
    return false;
  }

  doc->SetObject();
  string_handler(*doc, "type", cfg.name_);

  auto &attributes = cfg.attributes_;
  for (size_t i = 0, j = 0; i < attributes.size(); ++i) {
    auto attribute = attributes[i].attribute_;

    if (attribute[1] == Attribute::IGNORE) {
      ++j;
      continue;
    }

    auto it = handlers.find(attribute[0]);
    if (it == handlers.end()) {
      logger->error("invalid attribute: {}", attribute[0]);
      return false;
    }

    int time_index = cfg.time_index_ - 1;
    int time_span  = cfg.time_span_;

    if (attribute[1] == Attribute::STORE && j < log.size()) {
      if (static_cast<int>(j) == time_index && time_span > 0) {
        if (!it->second(*doc, attributes[i].name_, log[j] + " " + log[j + 1])) {
          return false;
        }
        j += 2;
      } else {
        if (!it->second(*doc, attributes[i].name_, log[j++])) {
          return false;
        }
      }
    } else if (attribute[1] == Attribute::ADD) {
      if (doc->HasMember(attributes[i].name_.c_str())) {
        doc->RemoveMember(attributes[i].name_.c_str());
      }

      if (!it->second(*doc, attributes[i].name_, attribute[2])) {
        return false;
      }
    }
  }

  return true;
}

bool LogToJsonString(Log &log, std::string &json, const Config &cfg) {
  Document doc;
  if (PopulateJsonDoc(&doc, log, cfg)) {
    json = JsonDocToString(&doc);
    return true;
  }
  return false;
}

} // namespace json
} // namespace fluorine

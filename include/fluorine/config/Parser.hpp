#pragma once

#include <fstream>
#include <iostream>

#include <boost/spirit/include/qi.hpp>
#include <boost/fusion/adapted/struct/adapt_struct.hpp>

namespace fluorine {
namespace config {

namespace qi    = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;

struct Attribute {
  std::string name_;
  std::vector<std::string> attribute_;

  const static std::string IGNORE;
  const static std::string STORE;
  const static std::string ADD;
};

const std::string Attribute::IGNORE = "0";
const std::string Attribute::STORE  = "1";
const std::string Attribute::ADD    = "2";

typedef std::vector<Attribute> Attributes;
typedef std::string::const_iterator iterator_type;

struct Config {
  std::string name_;
  int field_number_;
  int time_index_;
  int time_span_;
  Attributes attributes_;
};

template <typename Iterator>
struct Skipper : qi::grammar<Iterator> {
  Skipper() : Skipper::base_type(skip) {
    skip = ascii::space | "/*" >> *(qi::char_ - "*/") >> "*/";
  }

  qi::rule<Iterator> skip;
};

template <typename Iterator>
struct Grammar : qi::grammar<Iterator, Config(), Skipper<Iterator>> {
  Grammar() : Grammar::base_type(config) {
    using namespace qi;

    quoted = '"' >> *("\\" >> char_('"') | ~char_('"')) >> '"';
    name   = quoted | +char_("a-zA-Z0-9_");
    elem   = '[' >> (name % ',') >> ']';

    attribute  = name >> ':' >> elem >> ';';
    attributes = '{' >> *attribute >> '}';
    config =
        name >> '(' >> int_ >> ',' >> int_ >> ',' >> int_ >> ')' >> attributes;

    BOOST_SPIRIT_DEBUG_NODES((name)(elem)(attribute)(attributes));
  }

private:
  qi::rule<Iterator, std::string(), qi::no_skip_type> quoted, name;
  qi::rule<Iterator, std::vector<std::string>(), Skipper<Iterator>> elem;
  qi::rule<Iterator, Attribute(), Skipper<Iterator>> attribute;
  qi::rule<Iterator, Attributes(), Skipper<Iterator>> attributes;
  qi::rule<Iterator, Config(), Skipper<Iterator>> config;
};

bool Parse(iterator_type &begin, iterator_type &end, Config &cfg) {
  Grammar<iterator_type> g;
  Skipper<iterator_type> skip;

  return qi::phrase_parse(begin, end, g, skip, cfg);
}

bool ParseConfig(const std::string &path, Config &cfg) {
  std::ifstream is(path);
  std::stringstream buffer;

  if (is.is_open()) {
    buffer << is.rdbuf();
    is.close();
  } else {
    std::cerr << "unable to open: " << path << std::endl;
    return false;
  }

  std::string content = buffer.str();
  iterator_type begin = content.begin(), end = content.end();

  bool ok = Parse(begin, end, cfg);
  if (!ok || begin != end) {
    std::cerr << "parse failed" << std::endl;
    return false;
  }

  return true;
}

} // namespace config
} // namespace fluorine

BOOST_FUSION_ADAPT_STRUCT(fluorine::config::Config,
    (std::string, name_)
    (int, field_number_)
    (int, time_index_)
    (int, time_span_)
    (fluorine::config::Attributes, attributes_))

BOOST_FUSION_ADAPT_STRUCT(fluorine::config::Attribute,
    (std::string, name_)
    (std::vector<std::string>, attribute_))

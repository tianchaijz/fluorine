#pragma once

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/qi_optional.hpp>
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

typedef std::vector<Attribute> Attributes;
typedef std::string::const_iterator iterator_type;

struct Aggregation {
  std::string key_;
  std::string time_;
  int interval_;
  boost::optional<std::vector<std::string>> fields_;
};

struct Config {
  std::string name_;
  int field_number_;
  int time_index_;
  int time_span_;
  Attributes attributes_;
  boost::optional<Aggregation> aggregation_;
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

    attribute   = name >> ':' >> elem >> ';';
    attributes  = '{' >> *attribute >> '}';
    aggregation = '(' >> name >> ',' >> name >> ',' >> int_ >> ')' >>
                  -('[' >> (name % ',') >> ']');
    config = name >> '(' >> int_ >> ',' >> int_ >> ',' >> int_ >> ')' >>
             attributes >> -aggregation;

    BOOST_SPIRIT_DEBUG_NODES((name)(elem)(attribute)(attributes)(aggregation));
  }

private:
  qi::rule<Iterator, std::string(), qi::no_skip_type> quoted, name;
  qi::rule<Iterator, std::vector<std::string>(), Skipper<Iterator>> elem;
  qi::rule<Iterator, Attribute(), Skipper<Iterator>> attribute;
  qi::rule<Iterator, Attributes(), Skipper<Iterator>> attributes;
  qi::rule<Iterator, Aggregation(), Skipper<Iterator>> aggregation;
  qi::rule<Iterator, Config(), Skipper<Iterator>> config;
};

bool ParseConfig(const std::string &path, Config &cfg);

} // namespace config
} // namespace fluorine

BOOST_FUSION_ADAPT_STRUCT(fluorine::config::Config,
    (std::string, name_)
    (int, field_number_)
    (int, time_index_)
    (int, time_span_)
    (fluorine::config::Attributes, attributes_)
    (boost::optional<fluorine::config::Aggregation>, aggregation_))

BOOST_FUSION_ADAPT_STRUCT(fluorine::config::Attribute,
    (std::string, name_)
    (std::vector<std::string>, attribute_))

BOOST_FUSION_ADAPT_STRUCT(fluorine::config::Aggregation,
    (std::string, key_)
    (std::string, time_)
    (int, interval_)
    (boost::optional<std::vector<std::string>>, fields_))

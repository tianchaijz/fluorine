#pragma once

#include <string>
#include <boost/spirit/include/qi.hpp>

namespace fluorine {
namespace log {

namespace qi    = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;

typedef std::string Field;
typedef std::vector<Field> Log;
typedef Field::const_iterator iterator_type;

template <typename Iterator>
struct Grammar : qi::grammar<Iterator, Log(), qi::space_type> {
  Grammar(unsigned int fn = 0, unsigned int ti = 0)
      : Grammar::base_type(log), field_number(fn), time_index(ti) {
    using namespace qi;

    quoted    = '"' >> *("\\" >> char_('"') | ~char_('"')) >> '"';
    field     = quoted | +~char_(" \n");
    timestamp = +~char_("[]");
    time      = ('[' >> timestamp >> ']') | timestamp;

    if (time_index) {
      if (field_number < time_index) {
        log = repeat(time_index - 1)[field] >> time >> *field;
      } else {
        log = repeat(time_index - 1)[field] >> time >>
              repeat(field_number - time_index)[field];
      }
    } else if (field_number) {
      log = repeat(field_number)[field];
    } else {
      log = +field;
    }

    BOOST_SPIRIT_DEBUG_NODES((log)(field)(quoted));
  }

private:
  qi::rule<Iterator, Field(), qi::no_skip_type> field, quoted, time, timestamp;
  qi::rule<Iterator, Log(), qi::space_type> log;

  unsigned int field_number, time_index;
};

bool ParseLog(std::string &line, Log &log, unsigned int field_number = 0,
              unsigned int time_index = 0);

} // namespace log
} // namespace netease

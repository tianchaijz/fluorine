#include <map>
#include <set>
#include <stack>
#include <vector>
#include <string>

// #define BOOST_SPIRIT_DEBUG
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix.hpp>

#include <ctime>
#include <time.h>

class timer {
public:
  timer() : start_(0) {}
  timer(const timer &) = delete;
  void operator=(const timer &) = delete;

  void start() { start_ = clock(); }

  double elapsed_milliseconds() const {
    return static_cast<double>(clock() - start_) / CLOCKS_PER_SEC * 1000;
  }

private:
  clock_t start_;
};

template <typename Callable>
void bench(const Callable &cb, size_t calls) {
  timer t;
  t.start();
  for (size_t i = 0; i < calls; ++i) {
    cb();
  }
  double elapsed = t.elapsed_milliseconds();
  printf("%lu calls: %.2f ms\n", calls, elapsed);
}

namespace px = boost::phoenix;
namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;

using iterator_type = std::string::const_iterator;
typedef qi::rule<iterator_type, std::string(), qi::no_skip_type> LogRule;
typedef boost::uint32_t uchar;

struct LogElement {
  enum Type { Variable = 0, Const };

  struct Charset {
    char negative_;
    std::string value_;
  };

  int type_;
  std::string value_;

  boost::optional<Charset> charset_;
};

typedef std::vector<LogElement> LogFormat;

// clang-format off
BOOST_FUSION_ADAPT_STRUCT(LogElement::Charset,
    (char, negative_)
    (std::string, value_)
)

BOOST_FUSION_ADAPT_STRUCT(LogElement,
    (int, type_)
    (std::string, value_)
    (boost::optional<LogElement::Charset>, charset_)
)

struct push_escaped_string {
  template <typename Sig>
  struct result {
    typedef void type;
  };

  void operator()(std::string &s, uchar c) const {
    if (s.size() && s[s.size() - 1] == '\0') { std::cout << "(" << (unsigned char)c << std::endl; }
    switch (c) {
    case 'b': s += '\b'; break;
    case 't': s += '\t'; break;
    case 'n': s += '\n'; break;
    case 'f': s += '\f'; break;
    case 'r': s += '\r'; break;
    default: s += c; break;
    if (s[s.size() - 1] == '\0') { std::cout << "(" << (unsigned char)c << std::endl; }
    }
  }
};
// clang-format on

template <typename Iterator>
struct FormatParser_ : qi::grammar<Iterator, LogFormat(), qi::no_skip_type> {
  FormatParser_() : FormatParser_::base_type(grammar_) {
    using namespace qi;

    px::function<push_escaped_string> push_esc;

    // clang-format off
    escaped_ = '\\' > char_("btnfr\\^[]")[push_esc(_r1, _1)];
    charset_value_ = +(escaped_(_val) | (~char_(']'))[_val += _1]);
    charset_ = '[' > (char_('^') | attr('\0')) > charset_value_ > ']';
    variable_ = '$' > attr(LogElement::Type::Variable) > +char_("a-zA-Z0-9_") > -charset_;
    const_ = attr(LogElement::Type::Const) >> +(('\\' > char_("$\\")) | ~char_("$\\\n"));
    grammar_ = +(variable_ | const_);
    // clang-format on

    BOOST_SPIRIT_DEBUG_NODES((const_));
    BOOST_SPIRIT_DEBUG_NODES((charset_));
    BOOST_SPIRIT_DEBUG_NODES((variable_));
    BOOST_SPIRIT_DEBUG_NODES((grammar_));
  }

private:
  qi::rule<Iterator, LogFormat(), qi::no_skip_type> grammar_;
  qi::rule<Iterator, LogElement(), qi::no_skip_type> const_;
  qi::rule<Iterator, LogElement(), qi::no_skip_type> variable_;
  qi::rule<Iterator, LogElement::Charset, qi::no_skip_type> charset_;

  qi::rule<Iterator, void(std::string &), qi::no_skip_type> escaped_;
  LogRule charset_value_;
};

template <typename Iterator>
struct FormatParser__ : qi::grammar<Iterator, LogFormat(), qi::no_skip_type> {
  FormatParser__() : FormatParser__::base_type(grammar_) {
    using namespace qi;

    // clang-format off
    charset_escaped_symbols_.add
        ("b", '\b')
        ("t", '\t')
        ("f", '\f')
        ("r", '\r')
        ("[", '[')
        ("]", ']')
        ("^", '^')
        ("\\", '\\');
    const_ = attr(LogElement::Type::Const) >> +(('\\' > char_("$\\")) | ~char_("$\\"));
    charset_escaped_ = '\\' > charset_escaped_symbols_;
    charset_ = '[' > (char_('^') | attr('\0')) >> +(charset_escaped_ | ~char_(']')) > ']';
    variable_ = '$' > attr(LogElement::Type::Variable) >> +char_("a-zA-Z0-9_") >> -charset_;
    grammar_ = +(variable_ | const_);
    // clang-format on

    BOOST_SPIRIT_DEBUG_NODES((const_));
    BOOST_SPIRIT_DEBUG_NODES((charset_));
    BOOST_SPIRIT_DEBUG_NODES((variable_));
    BOOST_SPIRIT_DEBUG_NODES((grammar_));
  }

private:
  qi::rule<Iterator, LogFormat(), qi::no_skip_type> grammar_;
  qi::rule<Iterator, LogElement(), qi::no_skip_type> const_;
  qi::rule<Iterator, LogElement(), qi::no_skip_type> variable_;
  qi::rule<Iterator, LogElement::Charset, qi::no_skip_type> charset_;
  LogRule charset_escaped_;

  qi::symbols<char const, char const> charset_escaped_symbols_;
};

typedef FormatParser_<iterator_type> FormatParser;
typedef FormatParser__<iterator_type> FormatParser1;

template <typename Iterator>
struct LogParser_
    : qi::grammar<Iterator, std::vector<std::string>(), qi::no_skip_type> {
  LogParser_(LogFormat lf) : LogParser_::base_type(grammar_) {
    static std::map<char, char> pairs{
        {'\'', '\''}, {'"', '"'}, {'(', ')'},
        {'[', ']'},   {'{', '}'}, {'<', '>'},
    };

    auto build_encolsed_rule = [](char c, char dlm) {
      std::string s;
      std::set<char> cs;
      if (dlm) {
        cs = {c, pairs[c], dlm};
      } else {
        cs = {c, pairs[c]};
      }
      for (auto c : cs) {
        s += c;
      }
      LogRule rule = qi::lexeme[*('\\' > qi::char_(s) | ~(qi::char_(s)))];
      return rule.copy();
    };

    std::stack<char, std::deque<char>> stack;

    auto build_charset_rule = [](LogElement &le) {
      LogRule rule;
      auto &cs = le.charset_;
      if (cs->negative_) {
        rule = +~qi::char_(cs->value_);
      } else {
        rule = +qi::char_(cs->value_);
      }
      return rule.copy();
    };

    for (size_t i = 0; i < lf.size(); ++i) {
      auto &elm = lf[i];

      if (elm.type_ == LogElement::Type::Variable) {
        if (elm.charset_) {
          grammar_ = grammar_.copy() >> build_charset_rule(elm);
          continue;
        }

        auto it = default_rules_.find(elm.value_);
        if (it != default_rules_.end()) {
          grammar_ = grammar_.copy() >> it->second;
          continue;
        }

        char dlm = '\0';
        if (i < lf.size() - 1 && lf[i + 1].type_ == LogElement::Type::Const) {
          dlm = lf[i + 1].value_[0];
        }

        if (!stack.empty()) {
          grammar_ = grammar_.copy() >> build_encolsed_rule(stack.top(), dlm);
        } else if (dlm) {
          grammar_ = grammar_.copy() >> +~qi::char_(dlm);
        } else { /* the last */
          grammar_ = grammar_.copy() >> +qi::char_;
          break;
        }
      } else {
        const_rule_ = elm.value_;
        grammar_ = grammar_.copy() >> qi::omit[const_rule_.copy()];
        for (auto c : elm.value_) {
          if (!stack.empty() && pairs[stack.top()] == c) {
            stack.pop();
          } else if (pairs.find(c) != pairs.end()) {
            stack.push(c);
          }
        }
      }
    }

    BOOST_SPIRIT_DEBUG_NODES((grammar_));
    BOOST_SPIRIT_DEBUG_NODES((const_rule_));
  };

private:
  qi::rule<Iterator, std::vector<std::string>(), qi::no_skip_type> grammar_ =
      qi::eps;
  LogRule const_rule_;
  std::map<std::string, LogRule> default_rules_{
      {"domain", +qi::char_(".:-a-z0-9")},
  };
};

typedef std::unique_ptr<LogParser_<iterator_type>> LogParser;

void DumpLogFormat(LogFormat &lf) {
  auto chars = [](const std::string &s) -> std::string {
    std::string out;
    out.reserve(2 * s.size());
    for (auto c : s) {
      out.push_back(c);
      out.push_back(',');
    }
    return out;
  };

  for (auto &elm : lf) {
    switch (elm.type_) {
    case LogElement::Type::Const:
      std::cout << "(" << elm.value_ << ")(" << chars(elm.value_) << ")";
      break;
    case LogElement::Type::Variable:
      std::cout << "$" << elm.value_ << "(" << chars(elm.value_) << ")";
      if (elm.charset_) {
        auto &val = elm.charset_->value_;
        std::cout << "[" << elm.charset_->negative_ << val << "](" << chars(val)
                  << ")";
      }
      break;
    }
    std::cout << ' ';
  }

  std::cout << std::endl;
}

LogParser BuildGrammar(std::string format, bool &ok) {
  LogFormat lf;
  FormatParser1 fp;

  iterator_type iter = format.begin(), end = format.end();
  ok = parse(iter, end, fp, lf);
  if (!ok) {
    return nullptr;
  }

  DumpLogFormat(lf);

  {
    LogFormat lf;
    FormatParser fp;
    iterator_type iter = format.begin(), end = format.end();

    ok = parse(iter, end, fp, lf);
    if (!ok) {
      return nullptr;
    }

    DumpLogFormat(lf);
  }

  {
    LogFormat lf;
    FormatParser fp;

    auto cb = [&]() {
      iterator_type iter = format.begin(), end = format.end();
      parse(iter, end, fp, lf);
    };

    bench(cb, 10000);
  }

  {
    LogFormat lf;
    FormatParser1 fp;

    auto cb = [&]() {
      iterator_type iter = format.begin(), end = format.end();
      parse(iter, end, fp, lf);
    };

    bench(cb, 10000);
  }

  return LogParser(new LogParser_<iterator_type>(lf));
}

void parse_stdin(LogParser &lp) {
  std::string log;
  std::vector<std::string> fields;
  size_t total = 0, failed = 0;
  auto cb = [&]() {
    while (std::getline(std::cin, log)) {
      ++total;
      fields.clear();
      iterator_type iter = log.begin(), end = log.end();
      auto ok = parse(iter, end, *lp, fields);
      if (!ok) {
        std::cout << log << std::endl;
        ++failed;
      }
      if (total == 1) {
        for (auto x : fields) {
          std::cout << "(" << x << ")";
        }
        printf("\n");
      }
    }
  };
  bench(cb, 1);
  printf("total: %lu, failed: %lu\n", total, failed);
}

int main() {
  std::string format =
      R"($remote_addr $_ $_ [$date $zone] "$method $scheme://$domain[^/]$uri $HTTP/$version")";
  std::string log =
      R"(127.0.0.1 - - [23/Jul/2018:13:24:29 +0000] "GET http://q-q.com:80/hello HTTP/1.1" 'python-requests/2.18.4')";

  bool ok;
  auto lp = BuildGrammar(format, ok);

  assert(ok);

  std::vector<std::string> parsed;
  iterator_type iter = log.begin(), end = log.end();

  ok = parse(iter, end, *lp, parsed);
  assert(ok);

  for (auto x : parsed) {
    std::cout << "(" << x << ")" << std::endl;
  }

  format =
      R"($remote_addr $_ $_ [$time_local] "$method $url $protocol" $status $bytes_sent "$_" "$user_agent")";
  lp = BuildGrammar(format, ok);

  assert(ok);

  parse_stdin(lp);
}

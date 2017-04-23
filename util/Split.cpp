#include <assert.h>
#include <sys/time.h>
#include <ctime>
#include <cstdio>
#include <string>
#include <regex>
#include <utility>
#include <fstream>
#include <iostream>

#include "fmt/format.h"
#include "gzstream/gzstream.h"

#include <boost/program_options.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_stream.hpp>

#define ASSERT(expr)                                                     \
  if (!(expr)) {                                                         \
    fprintf(stderr, "%s:%d assertion failure: %s\n", __FILE__, __LINE__, \
            #expr);                                                      \
    abort();                                                             \
  }

namespace bio = boost::iostreams;

struct Option {
  std::string path_;
  int64_t size_;
  bool remove_ = false;
};

void parseOption(int argc, char *argv[], Option &opt) {
  using namespace boost::program_options;
  try {
    options_description desc("Usage");
    desc.add_options()("help,h", "print usage message");
    desc.add_options()("path,p", value(&opt.path_),
                       "specify the file to split");
    desc.add_options()("size",
                       value(&opt.size_)->default_value(512 * 1024 * 1024),
                       "split size");
    desc.add_options()("remove", bool_switch(&opt.remove_),
                       "remove the file to split");

    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);

    if (vm.count("help") || !vm.count("path")) {
      std::cout << desc << std::endl;
      exit(0);
    }

    notify(vm);
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    exit(1);
  }
}

const std::string currentDateTime() {
  char fmt[64], buf[64];
  struct timeval tv;
  struct tm *tm;

  gettimeofday(&tv, nullptr);
  tm = localtime(&tv.tv_sec);
  strftime(fmt, sizeof(fmt), "%Y-%m-%d %H:%M:%S.%%06u", tm);
  snprintf(buf, sizeof(buf), fmt, tv.tv_usec);
  return buf;
}

struct GzipLineSplitter {
  using OFStreamType = std::unique_ptr<std::ofstream>;
  GzipLineSplitter(std::string path, int64_t size, std::string prefix,
                   std::string suffix, bool remove)
      : path_(path), size_(size), prefix_(prefix + "_part_"), suffix_(suffix),
        remove_(remove),
        in_fd_(path_.c_str(), std::ios_base::in | std::ios_base::binary),
        out_index_(0), out_changed_(true) {}

  ~GzipLineSplitter() {}

  void Write(const std::string &data) {
    if (data.empty()) {
      return;
    }

    if (out_changed_) {
      out_path_ = prefix_ + std::to_string(out_index_) + "." + suffix_;
      std::cout << fmt::format("[{}] [WRITE] {}", currentDateTime(), out_path_)
                << std::endl;
      out_fd_.reset(new std::ofstream(out_path_, std::ios::binary));
      out_changed_ = false;
    }

    ASSERT(out_fd_->is_open());

    std::stringstream origin(data);
    std::stringstream compressed;
    bio::filtering_streambuf<bio::input> out;
    out.push(bio::gzip_compressor(bio::gzip_params(bio::gzip::best_speed)));
    out.push(origin);
    bio::copy(out, compressed);

    *out_fd_ << compressed.rdbuf();
  }

  inline void DoSplit() {
    std::string line, buf;
    int64_t nr = 0;
    while (std::getline(in_fd_, line)) {
      nr += line.size();
      if (nr > size_) {
        nr = 0;
        ++out_index_;
        out_changed_ = true;
      }

      buf += line + '\n';
      if (buf.size() > 4 * 1024 * 1024) {
        Write(buf);
        buf.clear();
      }
    }

    Write(buf);
  }

  void Split() {
    ASSERT(in_fd_.good());

    DoSplit();

    if (!in_fd_.eof()) {
      std::cout << fmt::format("[{}] [ERROR] partial split: {}",
                               currentDateTime(), path_)
                << std::endl;
      return;
    }

    if (remove_) {
      std::cout << fmt::format("[{}] [REMOVE] {}", currentDateTime(), path_)
                << std::endl;
      std::remove(path_.c_str());
    }
  }

private:
  std::string path_;
  int64_t size_;
  std::string prefix_;
  std::string suffix_;
  bool remove_;

  igzstream in_fd_;

  int64_t out_index_;
  std::string out_path_;
  OFStreamType out_fd_;
  bool out_changed_;
};

std::pair<std::string, std::string> parsePath(std::string path) {
  std::regex re("^(.+?)\\.([^.]+)$");
  std::smatch m;
  if (std::regex_search(path, m, re)) {
    return std::make_pair(m[1], m[2]);
  }

  return std::make_pair(path, "");
}

int main(int argc, char *argv[]) {
  Option opt;
  parseOption(argc, argv, opt);

  auto p = parsePath(opt.path_);
  GzipLineSplitter splitter(opt.path_, opt.size_, p.first, p.second,
                            opt.remove_);
  splitter.Split();

  return 0;
}

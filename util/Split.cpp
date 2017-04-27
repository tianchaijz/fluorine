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

template <typename... Args>
void Log(const std::string &format, Args... args) {
  std::cout << fmt::format(format, args...) << std::endl;
}

const std::string Time() {
  char fmt[64], buf[64];
  struct timeval tv;
  struct tm *tm;

  gettimeofday(&tv, nullptr);
  tm = localtime(&tv.tv_sec);
  strftime(fmt, sizeof(fmt), "%Y-%m-%d %H:%M:%S.%%06u", tm);
  snprintf(buf, sizeof(buf), fmt, tv.tv_usec);
  return buf;
}

inline void Rename(const char *src, const char *dest) {
  if (strlen(src) == 0 || strlen(dest) == 0) {
    return;
  }

  int rc = std::rename(src, dest);
  if (rc) {
    Log("[{}] [ERROR] rename {} to {}: {}", Time(), src, dest, strerror(errno));
  } else {
    Log("[{}] [WRITE] {}", Time(), dest);
  }
}

struct GzipLineSplitter {
  using OFStreamType = std::unique_ptr<ogzstream>;
  GzipLineSplitter(std::string path, int64_t size, std::string prefix,
                   std::string suffix, bool remove)
      : path_(path), size_(size), prefix_(prefix + "_part_"), suffix_(suffix),
        remove_(remove),
        in_fd_(path_.c_str(), std::ios_base::in | std::ios_base::binary),
        out_index_(0), out_changed_(true) {}

  ~GzipLineSplitter() {
    if (!in_fd_.eof()) {
      Log("[{}] [ERROR] partial split: {}", Time(), path_);
      return;
    }

    Rename(out_temp_path_.c_str(), out_path_.c_str());

    if (remove_) {
      Log("[{}] [REMOVE] {}", Time(), path_);
      std::remove(path_.c_str());
    }

    std::ofstream(path_ + "__splitted", std::ios::out);
  }

  void Split() {
    ASSERT(in_fd_.good());

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

private:
  void Write(const std::string &data) {
    if (data.empty()) {
      return;
    }

    if (out_changed_) {
      Rename(out_temp_path_.c_str(), out_path_.c_str());

      out_path_      = prefix_ + std::to_string(out_index_) + "." + suffix_;
      out_temp_path_ = out_path_ + "__temp";

      out_fd_.reset(new ogzstream(out_temp_path_.c_str(),
                                  std::ios::binary | std::ios::out,
                                  ogzstream::compression_level::best_speed));
      ASSERT(out_fd_);
      out_changed_ = false;
    }

    ASSERT(out_fd_->good());
    out_fd_->write(data.c_str(), data.size());
  }

  std::string path_;
  int64_t size_;
  std::string prefix_;
  std::string suffix_;
  bool remove_;

  igzstream in_fd_;

  int64_t out_index_;
  std::string out_path_;
  std::string out_temp_path_;
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

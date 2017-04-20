#include <fstream>
#include <iostream>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_stream.hpp>

int main(int, char *argv[]) {
  namespace bio = boost::iostreams;

  try {
    std::ifstream file(argv[1], std::ios_base::in | std::ios_base::binary);
    if (!file.is_open()) {
      std::cout << "open failed: " << argv[1] << std::endl;
      return 1;
    }
    boost::iostreams::filtering_stream<bio::input> in;
    in.push(bio::gzip_decompressor());
    in.push(file);

    std::string line;
    while (std::getline(in, line)) {
      std::cout << line << std::endl;
    }
  } catch (const boost::iostreams::gzip_error &e) {
    std::cout << e.what() << std::endl;
  }
}

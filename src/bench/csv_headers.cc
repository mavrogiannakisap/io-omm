#include "utils/bench.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cout << "Help: " << argv[0] << " op1[,op2...]" << std::endl;
    return 1;
  }
  file_oram::utils::PrintCsvHeaders(argv[1]);
  return 0;
}
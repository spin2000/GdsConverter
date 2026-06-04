#include <iomanip>
#include <fstream>
#include <iostream>
#include <string>

#include "gtGdsFormatter.h"
#include "gtGdsReader.h"

namespace {

void PrintUsage(std::ostream* output) {
  *output << "usage: gds2txt [-offset] [-cell CELL_NAME] input.gds output.txt\n";
}

bool ParseArgs(int argc, char** argv, gtFormatOptions* options,
               std::string* inputPath, std::string* outputPath,
               std::string* error) {
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "-offset") {
      options->showOffset = true;
      continue;
    }
    if (arg == "-cell") {
      if (index + 1 >= argc) {
        *error = "-cell requires CELL_NAME";
        return false;
      }
      options->filterCell = true;
      options->cellName = argv[++index];
      continue;
    }
    if (!arg.empty() && arg[0] == '-') {
      *error = "unknown option: " + arg;
      return false;
    }
    if (inputPath->empty()) {
      *inputPath = arg;
      continue;
    }
    if (outputPath->empty()) {
      *outputPath = arg;
      continue;
    }
    *error = "too many positional arguments";
    return false;
  }

  if (inputPath->empty()) {
    *error = "missing input file";
    return false;
  }
  if (outputPath->empty()) {
    *error = "missing output file";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  gtFormatOptions options;
  std::string inputPath;
  std::string outputPath;
  std::string error;
  if (!ParseArgs(argc, argv, &options, &inputPath, &outputPath, &error)) {
    std::cerr << "error: " << error << '\n';
    PrintUsage(&std::cerr);
    return 1;
  }

  std::ofstream output(outputPath);
  if (!output.is_open()) {
    std::cerr << "error: cannot open output file: " << outputPath << '\n';
    return 1;
  }

  gtGdsReader reader;
  if (!reader.Open(inputPath, &error)) {
    std::cerr << "error: " << error << '\n';
    return 1;
  }

  gtGdsFormatter formatter(options);
  while (true) {
    gtGdsRecord record;
    bool hasRecord = false;
    if (!reader.Next(&record, &hasRecord, &error)) {
      std::cerr << "error: " << error << '\n';
      return 1;
    }
    if (!hasRecord) {
      break;
    }
    if (!formatter.Consume(record, &output, &error)) {
      std::cerr << "error: offset=0x" << std::hex << std::setw(8)
                << std::setfill('0') << record.offset << std::dec << ": "
                << error << '\n';
      return 1;
    }
  }

  if (!formatter.Finish(&output, &std::cerr, &error)) {
    if (!error.empty()) {
      std::cerr << "error: " << error << '\n';
    }
    return 1;
  }
  if (!output.good()) {
    std::cerr << "error: failed while writing output file: " << outputPath
              << '\n';
    return 1;
  }
  return 0;
}

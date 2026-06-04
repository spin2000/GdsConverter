#ifndef SRC_GTGDSREADER_H_
#define SRC_GTGDSREADER_H_

#include <cstdint>
#include <string>

#include "gtGdsRecord.h"
#include "gtMmapWindow.h"

class gtGdsReader {
 public:
  explicit gtGdsReader(uint64_t windowSize = gtMmapWindow::kDefaultWindowSize);
  gtGdsReader(const gtGdsReader&) = delete;
  gtGdsReader& operator=(const gtGdsReader&) = delete;
  ~gtGdsReader();

  bool Open(const std::string& path, std::string* error);
  bool Next(gtGdsRecord* record, bool* hasRecord, std::string* error);

 private:
  static uint16_t ReadUInt16(const uint8_t* data);

  bool IsGzipFile(const std::string& path, bool* isGzip, std::string* error);
  bool IsTarArchive(const std::string& path, bool* isTar, std::string* error);
  bool DecompressGzipToTemp(const std::string& path, std::string* tempPath,
                            std::string* error);
  void RemoveTempFile();

  gtMmapWindow m_window;
  std::string m_tempPath;
  uint64_t m_offset = 0;
};

#endif  // SRC_GTGDSREADER_H_

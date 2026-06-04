#ifndef SRC_GTGDSRECORD_H_
#define SRC_GTGDSRECORD_H_

#include <cstdint>
#include <vector>

struct gtGdsRecord {
  uint64_t offset = 0;
  uint16_t length = 0;
  uint8_t recordType = 0;
  uint8_t dataType = 0;
  std::vector<uint8_t> payload;
};

#endif  // SRC_GTGDSRECORD_H_

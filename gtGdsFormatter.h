#ifndef SRC_GTGDSFORMATTER_H_
#define SRC_GTGDSFORMATTER_H_

#include <ostream>
#include <string>
#include <vector>

#include "gtGdsRecord.h"

struct gtFormatOptions {
  bool showOffset = false;
  bool filterCell = false;
  std::string cellName;
};

class gtGdsFormatter {
 public:
  explicit gtGdsFormatter(gtFormatOptions options);

  bool Consume(const gtGdsRecord& record, std::ostream* output,
               std::string* error);
  bool Finish(std::ostream* output, std::ostream* diagnostics,
              std::string* error);

 private:
  struct gtLine {
    uint64_t offset = 0;
    int indent = 0;
    std::vector<std::string> parts;
  };

  static int16_t ReadInt16(const std::vector<uint8_t>& data, size_t offset);
  static uint16_t ReadUInt16(const std::vector<uint8_t>& data, size_t offset);
  static int32_t ReadInt32(const std::vector<uint8_t>& data, size_t offset);
  static double ReadReal4(const std::vector<uint8_t>& data, size_t offset);
  static double ReadReal8(const std::vector<uint8_t>& data, size_t offset);
  static std::string DecodeString(const std::vector<uint8_t>& data,
                                  size_t fieldOffset, size_t fieldLength);
  static std::string EscapeString(const std::vector<uint8_t>& data,
                                  size_t fieldOffset, size_t fieldLength);
  static std::string HexByte(uint8_t value);
  static std::string HexWord(uint16_t value);
  static std::string HexPayload(const std::vector<uint8_t>& payload);
  static std::string FormatDouble(double value);
  static uint16_t FormatTimestampYear(uint16_t value);
  static std::string ToString(int64_t value);
  static bool IsElementStart(uint8_t recordType);
  static bool AllowsReal4(uint8_t recordType);

  bool BuildLine(const gtGdsRecord& record, gtLine* line, std::string* error);
  bool IsCompatibleDataType(const gtGdsRecord& record) const;
  bool AppendKnownPayload(const gtGdsRecord& record, const std::string& keyword,
                          gtLine* line, std::string* error);
  bool AppendGenericPayload(const gtGdsRecord& record, gtLine* line,
                            std::string* error);
  bool AppendInt16Values(const gtGdsRecord& record, gtLine* line,
                         std::string* error);
  bool AppendInt32Values(const gtGdsRecord& record, gtLine* line,
                         std::string* error);
  bool AppendReal8Values(const gtGdsRecord& record, gtLine* line,
                         std::string* error);
  bool AppendReal4Values(const gtGdsRecord& record, gtLine* line,
                         std::string* error);
  bool AppendBitArray(const gtGdsRecord& record, gtLine* line,
                      std::string* error);
  bool AppendString(const gtGdsRecord& record, gtLine* line, bool warnPadding);
  bool AppendFixedStrings(const gtGdsRecord& record, size_t fieldLength,
                          size_t fieldCount, gtLine* line,
                          std::string* error);
  bool AppendXy(const gtGdsRecord& record, gtLine* line, std::string* error);
  bool ValidateWidth(const gtGdsRecord& record, size_t width,
                     const std::string& name, std::string* error) const;
  void UseRawPayload(const gtGdsRecord& record, const std::string& warning,
                     gtLine* line);
  void UpdateStateBeforeLine(const gtGdsRecord& record, gtLine* line);
  void UpdateStateAfterLine(const gtGdsRecord& record, const gtLine& line);
  void EmitLine(const gtLine& line, std::ostream* output) const;
  void EmitLineParts(uint64_t offset, int indent,
                     const std::vector<std::string>& parts,
                     std::ostream* output) const;
  void AddWarning(const gtGdsRecord& record, const std::string& message);
  void FlushPendingStructure(std::ostream* output);

  gtFormatOptions m_options;
  bool m_inStructure = false;
  bool m_inElement = false;
  bool m_foundCell = false;
  bool m_seenEndlib = false;
  std::string m_currentCellName;
  std::vector<gtLine> m_pendingStructure;
  std::vector<std::string> m_warnings;
};

#endif  // SRC_GTGDSFORMATTER_H_

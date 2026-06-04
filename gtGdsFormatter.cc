#include "gtGdsFormatter.h"

#include <cstddef>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace {

constexpr uint8_t kDataNoData = 0x00;
constexpr uint8_t kDataBitArray = 0x01;
constexpr uint8_t kDataInt16 = 0x02;
constexpr uint8_t kDataInt32 = 0x03;
constexpr uint8_t kDataReal4 = 0x04;
constexpr uint8_t kDataReal8 = 0x05;
constexpr uint8_t kDataString = 0x06;
constexpr int kVariableDataType = -1;

struct gtRecordInfo {
  uint8_t recordType;
  int dataType;
  const char* keyword;
};

const gtRecordInfo kRecordInfo[] = {
    {0x00, kDataInt16, "header"},       {0x01, kDataInt16, "bgnlib"},
    {0x02, kDataString, "libname"},     {0x03, kDataReal8, "units"},
    {0x04, kDataNoData, "endlib"},      {0x05, kDataInt16, "bgnstr"},
    {0x06, kDataString, "strname"},     {0x07, kDataNoData, "endstr"},
    {0x08, kDataNoData, "boundary"},    {0x09, kDataNoData, "path"},
    {0x0A, kDataNoData, "sref"},        {0x0B, kDataNoData, "aref"},
    {0x0C, kDataNoData, "text"},        {0x0D, kDataInt16, "layer"},
    {0x0E, kDataInt16, "datatype"},     {0x0F, kDataInt32, "width"},
    {0x10, kDataInt32, "xy"},           {0x11, kDataNoData, "endel"},
    {0x12, kDataString, "sname"},       {0x13, kDataInt16, "colrow"},
    {0x14, kDataNoData, "textnode"},    {0x15, kDataNoData, "node"},
    {0x16, kDataInt16, "texttype"},     {0x17, kDataBitArray, "presentation"},
    {0x18, kVariableDataType, "spacing"},
    {0x19, kDataString, "string"},      {0x1A, kDataBitArray, "strans"},
    {0x1B, kDataReal8, "mag"},          {0x1C, kDataReal8, "angle"},
    {0x1D, kVariableDataType, "uinteger"},
    {0x1E, kVariableDataType, "ustring"},
    {0x1F, kDataString, "reflibs"},     {0x20, kDataString, "fonts"},
    {0x21, kDataInt16, "pathtype"},     {0x22, kDataInt16, "generations"},
    {0x23, kDataString, "attrtable"},   {0x24, kDataString, "styptable"},
    {0x25, kDataInt16, "strtype"},      {0x26, kDataBitArray, "elflags"},
    {0x27, kDataInt32, "elkey"},        {0x28, kVariableDataType, "linktype"},
    {0x29, kVariableDataType, "linkkeys"},
    {0x2A, kDataInt16, "nodetype"},     {0x2B, kDataInt16, "propattr"},
    {0x2C, kDataString, "propvalue"},   {0x2D, kDataNoData, "box"},
    {0x2E, kDataInt16, "boxtype"},      {0x2F, kDataInt32, "plex"},
    {0x30, kDataInt32, "bgnextn"},      {0x31, kDataInt32, "endextn"},
    {0x32, kDataInt16, "tapenum"},      {0x33, kDataInt16, "tapecode"},
    {0x34, kDataBitArray, "strclass"},  {0x35, kVariableDataType, "reserved"},
    {0x36, kDataInt16, "format"},       {0x37, kDataString, "mask"},
    {0x38, kDataNoData, "endmasks"},    {0x39, kDataInt16, "libdirsize"},
    {0x3A, kDataString, "srfname"},     {0x3B, kDataInt16, "libsecur"},
};

const gtRecordInfo* FindRecordInfo(uint8_t recordType) {
  for (const gtRecordInfo& info : kRecordInfo) {
    if (info.recordType == recordType) {
      return &info;
    }
  }
  return nullptr;
}

}  // namespace

gtGdsFormatter::gtGdsFormatter(gtFormatOptions options)
    : m_options(std::move(options)) {}

bool gtGdsFormatter::Consume(const gtGdsRecord& record, std::ostream* output,
                             std::string* error) {
  if (m_inElement &&
      (record.recordType == 0x05 || record.recordType == 0x07 ||
       record.recordType == 0x04 || IsElementStart(record.recordType))) {
    AddWarning(record, "missing endel before this record");
    m_inElement = false;
  }
  if (m_inStructure && (record.recordType == 0x05 || record.recordType == 0x04)) {
    AddWarning(record, "missing endstr before this record");
    if (m_options.filterCell) {
      FlushPendingStructure(output);
    }
    m_inStructure = false;
    m_currentCellName.clear();
  }

  gtLine line;
  UpdateStateBeforeLine(record, &line);
  if (!BuildLine(record, &line, error)) {
    return false;
  }

  const bool belongsToStructure = m_inStructure || record.recordType == 0x05;
  if (m_options.filterCell && belongsToStructure) {
    m_pendingStructure.push_back(line);
  } else {
    EmitLine(line, output);
  }

  UpdateStateAfterLine(record, line);
  if (m_options.filterCell && record.recordType == 0x07) {
    FlushPendingStructure(output);
  }

  return true;
}

bool gtGdsFormatter::Finish(std::ostream* output, std::ostream* diagnostics,
                            std::string* error) {
  (void)output;
  (void)error;
  for (const std::string& warning : m_warnings) {
    *diagnostics << warning << '\n';
  }
  if (m_inStructure) {
    *diagnostics << "warning: missing endstr before end of file\n";
    if (m_options.filterCell) {
      FlushPendingStructure(output);
    }
  }
  if (m_inElement) {
    *diagnostics << "warning: missing endel before end of file\n";
  }
  if (!m_seenEndlib) {
    *diagnostics << "warning: missing endlib before end of file\n";
  }
  if (m_options.filterCell && !m_foundCell) {
    *diagnostics << "error: cell not found: " << m_options.cellName << '\n';
    return false;
  }
  return true;
}

int16_t gtGdsFormatter::ReadInt16(const std::vector<uint8_t>& data,
                                  size_t offset) {
  const uint16_t value = ReadUInt16(data, offset);
  return static_cast<int16_t>(value);
}

uint16_t gtGdsFormatter::ReadUInt16(const std::vector<uint8_t>& data,
                                    size_t offset) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8) |
                               static_cast<uint16_t>(data[offset + 1]));
}

int32_t gtGdsFormatter::ReadInt32(const std::vector<uint8_t>& data,
                                  size_t offset) {
  const uint32_t value =
      (static_cast<uint32_t>(data[offset]) << 24) |
      (static_cast<uint32_t>(data[offset + 1]) << 16) |
      (static_cast<uint32_t>(data[offset + 2]) << 8) |
      static_cast<uint32_t>(data[offset + 3]);
  return static_cast<int32_t>(value);
}

double gtGdsFormatter::ReadReal4(const std::vector<uint8_t>& data,
                                 size_t offset) {
  bool allZero = true;
  for (size_t index = 0; index < 4; ++index) {
    if (data[offset + index] != 0) {
      allZero = false;
      break;
    }
  }
  if (allZero) {
    return 0.0;
  }

  const bool negative = (data[offset] & 0x80) != 0;
  const int exponent = static_cast<int>(data[offset] & 0x7f) - 64;
  uint32_t mantissa = 0;
  for (size_t index = 1; index < 4; ++index) {
    mantissa = (mantissa << 8) | static_cast<uint32_t>(data[offset + index]);
  }

  const double fraction =
      static_cast<double>(mantissa) / static_cast<double>(1ULL << 24);
  double value = fraction * std::pow(16.0, exponent);
  if (negative) {
    value = -value;
  }
  return value;
}

double gtGdsFormatter::ReadReal8(const std::vector<uint8_t>& data,
                                 size_t offset) {
  bool allZero = true;
  for (size_t index = 0; index < 8; ++index) {
    if (data[offset + index] != 0) {
      allZero = false;
      break;
    }
  }
  if (allZero) {
    return 0.0;
  }

  const bool negative = (data[offset] & 0x80) != 0;
  const int exponent = static_cast<int>(data[offset] & 0x7f) - 64;
  uint64_t mantissa = 0;
  for (size_t index = 1; index < 8; ++index) {
    mantissa = (mantissa << 8) | static_cast<uint64_t>(data[offset + index]);
  }

  const double fraction =
      static_cast<double>(mantissa) / static_cast<double>(1ULL << 56);
  double value = fraction * std::pow(16.0, exponent);
  if (negative) {
    value = -value;
  }
  return value;
}

std::string gtGdsFormatter::DecodeString(const std::vector<uint8_t>& data,
                                         size_t fieldOffset,
                                         size_t fieldLength) {
  size_t length = fieldLength;
  while (length > 0 && data[fieldOffset + length - 1] == 0) {
    --length;
  }
  return std::string(data.begin() + static_cast<std::ptrdiff_t>(fieldOffset),
                     data.begin() +
                         static_cast<std::ptrdiff_t>(fieldOffset + length));
}

std::string gtGdsFormatter::EscapeString(const std::vector<uint8_t>& data,
                                         size_t fieldOffset,
                                         size_t fieldLength) {
  const std::string value = DecodeString(data, fieldOffset, fieldLength);
  std::ostringstream stream;
  for (unsigned char ch : value) {
    if (ch == '"') {
      stream << "\\\"";
      continue;
    }
    if (ch == '\\') {
      stream << "\\\\";
      continue;
    }
    if (ch >= 0x20 && ch <= 0x7e) {
      stream << static_cast<char>(ch);
      continue;
    }
    stream << "\\x" << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(ch) << std::dec;
  }
  return stream.str();
}

std::string gtGdsFormatter::HexByte(uint8_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::setw(2) << std::setfill('0')
         << static_cast<int>(value);
  return stream.str();
}

std::string gtGdsFormatter::HexWord(uint16_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::setw(4) << std::setfill('0') << value;
  return stream.str();
}

std::string gtGdsFormatter::HexPayload(const std::vector<uint8_t>& payload) {
  std::ostringstream stream;
  stream << "0x";
  for (uint8_t byte : payload) {
    stream << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(byte);
  }
  return stream.str();
}

std::string gtGdsFormatter::FormatDouble(double value) {
  std::ostringstream stream;
  stream << std::setprecision(17) << value;
  return stream.str();
}

uint16_t gtGdsFormatter::FormatTimestampYear(uint16_t value) {
  if (value < 1900) {
    return static_cast<uint16_t>(value + 1900);
  }
  return value;
}

std::string gtGdsFormatter::ToString(int64_t value) {
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

bool gtGdsFormatter::IsElementStart(uint8_t recordType) {
  return recordType == 0x08 || recordType == 0x09 || recordType == 0x0A ||
         recordType == 0x0B || recordType == 0x0C || recordType == 0x15 ||
         recordType == 0x2D;
}

bool gtGdsFormatter::AllowsReal4(uint8_t recordType) {
  return recordType == 0x1B || recordType == 0x1C;
}

bool gtGdsFormatter::BuildLine(const gtGdsRecord& record, gtLine* line,
                               std::string* error) {
  const gtRecordInfo* info = FindRecordInfo(record.recordType);
  if (info == nullptr) {
    line->parts.push_back("record");
    line->parts.push_back(HexByte(record.recordType));
    line->parts.push_back(HexByte(record.dataType));
    line->parts.push_back("raw");
    line->parts.push_back(HexPayload(record.payload));
    return true;
  }

  line->parts.push_back(info->keyword);
  if (!IsCompatibleDataType(record)) {
    AddWarning(record, std::string(info->keyword) +
                           " uses unexpected datatype " +
                           HexByte(record.dataType));
    line->parts.push_back("raw");
    line->parts.push_back("datatype=" + HexByte(record.dataType));
    line->parts.push_back(HexPayload(record.payload));
    return true;
  }

  if (AppendKnownPayload(record, info->keyword, line, error)) {
    return true;
  }

  UseRawPayload(record, *error, line);
  error->clear();
  return true;
}

bool gtGdsFormatter::IsCompatibleDataType(const gtGdsRecord& record) const {
  const gtRecordInfo* info = FindRecordInfo(record.recordType);
  if (info == nullptr || info->dataType == kVariableDataType) {
    return true;
  }
  if (record.dataType == static_cast<uint8_t>(info->dataType)) {
    return true;
  }
  return AllowsReal4(record.recordType) && record.dataType == kDataReal4;
}

bool gtGdsFormatter::AppendKnownPayload(const gtGdsRecord& record,
                                        const std::string& keyword,
                                        gtLine* line, std::string* error) {
  switch (record.recordType) {
    case 0x00:
    case 0x0D:
    case 0x0E:
    case 0x16:
    case 0x21:
    case 0x22:
    case 0x25:
    case 0x2A:
    case 0x2B:
    case 0x2E:
    case 0x32:
    case 0x36:
    case 0x39:
      if (!ValidateWidth(record, 2, keyword, error)) {
        return false;
      }
      line->parts.push_back(ToString(ReadUInt16(record.payload, 0)));
      return true;
    case 0x01:
    case 0x05:
      if (!ValidateWidth(record, 24, keyword, error)) {
        return false;
      }
      for (size_t offset = 0; offset < record.payload.size(); offset += 2) {
        uint16_t value = ReadUInt16(record.payload, offset);
        if (offset == 0 || offset == 12) {
          value = FormatTimestampYear(value);
        }
        line->parts.push_back(ToString(value));
      }
      return true;
    case 0x02:
    case 0x06:
    case 0x12:
    case 0x19:
    case 0x23:
    case 0x24:
    case 0x2C:
    case 0x37:
    case 0x3A:
      return AppendString(record, line, true);
    case 0x03:
      if (!ValidateWidth(record, 16, keyword, error)) {
        return false;
      }
      line->parts.push_back(FormatDouble(ReadReal8(record.payload, 0)));
      line->parts.push_back(FormatDouble(ReadReal8(record.payload, 8)));
      return true;
    case 0x04:
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x11:
    case 0x14:
    case 0x15:
    case 0x2D:
    case 0x38:
      if (!ValidateWidth(record, 0, keyword, error)) {
        return false;
      }
      return true;
    case 0x0F:
    case 0x27:
    case 0x2F:
    case 0x30:
    case 0x31:
      if (!ValidateWidth(record, 4, keyword, error)) {
        return false;
      }
      line->parts.push_back(ToString(ReadInt32(record.payload, 0)));
      return true;
    case 0x10:
      return AppendXy(record, line, error);
    case 0x13:
      if (!ValidateWidth(record, 4, keyword, error)) {
        return false;
      }
      line->parts.push_back(ToString(ReadUInt16(record.payload, 0)));
      line->parts.push_back(ToString(ReadUInt16(record.payload, 2)));
      return true;
    case 0x17:
    case 0x1A:
    case 0x26:
    case 0x34:
      return AppendBitArray(record, line, error);
    case 0x1B:
    case 0x1C:
      if (record.dataType == kDataReal4) {
        if (!ValidateWidth(record, 4, keyword, error)) {
          return false;
        }
        line->parts.push_back(FormatDouble(ReadReal4(record.payload, 0)));
        AddWarning(record, keyword + " uses nonstandard real4 payload");
        return true;
      }
      if (!ValidateWidth(record, 8, keyword, error)) {
        return false;
      }
      line->parts.push_back(FormatDouble(ReadReal8(record.payload, 0)));
      return true;
    case 0x1F:
      return AppendFixedStrings(record, 44, 2, line, error);
    case 0x20:
      return AppendFixedStrings(record, 44, 4, line, error);
    case 0x33:
      if (!ValidateWidth(record, 12, keyword, error)) {
        return false;
      }
      for (size_t offset = 0; offset < record.payload.size(); offset += 2) {
        line->parts.push_back(ToString(ReadUInt16(record.payload, offset)));
      }
      return true;
    case 0x35:
      line->parts.push_back("raw");
      line->parts.push_back(HexPayload(record.payload));
      return true;
    case 0x3B:
      return AppendInt16Values(record, line, error);
    case 0x18:
    case 0x1D:
    case 0x1E:
    case 0x28:
    case 0x29:
      return AppendGenericPayload(record, line, error);
    default:
      return AppendGenericPayload(record, line, error);
  }
}

bool gtGdsFormatter::AppendGenericPayload(const gtGdsRecord& record,
                                          gtLine* line, std::string* error) {
  switch (record.dataType) {
    case kDataNoData:
      if (!ValidateWidth(record, 0, line->parts.front(), error)) {
        return false;
      }
      return true;
    case kDataBitArray:
      return AppendBitArray(record, line, error);
    case kDataInt16:
      return AppendInt16Values(record, line, error);
    case kDataInt32:
      return AppendInt32Values(record, line, error);
    case kDataReal4:
      return AppendReal4Values(record, line, error);
    case kDataReal8:
      return AppendReal8Values(record, line, error);
    case kDataString:
      return AppendString(record, line, true);
    default:
      line->parts.push_back("raw");
      line->parts.push_back("datatype=" + HexByte(record.dataType));
      line->parts.push_back(HexPayload(record.payload));
      return true;
  }
}

bool gtGdsFormatter::AppendInt16Values(const gtGdsRecord& record, gtLine* line,
                                       std::string* error) {
  if ((record.payload.size() % 2) != 0) {
    *error = "invalid int16 payload length";
    return false;
  }
  for (size_t offset = 0; offset < record.payload.size(); offset += 2) {
    line->parts.push_back(ToString(ReadInt16(record.payload, offset)));
  }
  return true;
}

bool gtGdsFormatter::AppendInt32Values(const gtGdsRecord& record, gtLine* line,
                                       std::string* error) {
  if ((record.payload.size() % 4) != 0) {
    *error = "invalid int32 payload length";
    return false;
  }
  for (size_t offset = 0; offset < record.payload.size(); offset += 4) {
    line->parts.push_back(ToString(ReadInt32(record.payload, offset)));
  }
  return true;
}

bool gtGdsFormatter::AppendReal8Values(const gtGdsRecord& record, gtLine* line,
                                       std::string* error) {
  if ((record.payload.size() % 8) != 0) {
    *error = "invalid real8 payload length";
    return false;
  }
  for (size_t offset = 0; offset < record.payload.size(); offset += 8) {
    line->parts.push_back(FormatDouble(ReadReal8(record.payload, offset)));
  }
  return true;
}

bool gtGdsFormatter::AppendReal4Values(const gtGdsRecord& record, gtLine* line,
                                       std::string* error) {
  if ((record.payload.size() % 4) != 0) {
    *error = "invalid real4 payload length";
    return false;
  }
  for (size_t offset = 0; offset < record.payload.size(); offset += 4) {
    line->parts.push_back(FormatDouble(ReadReal4(record.payload, offset)));
  }
  return true;
}

bool gtGdsFormatter::AppendBitArray(const gtGdsRecord& record, gtLine* line,
                                    std::string* error) {
  if (!ValidateWidth(record, 2, line->parts.front(), error)) {
    return false;
  }
  line->parts.push_back(HexWord(ReadUInt16(record.payload, 0)));
  return true;
}

bool gtGdsFormatter::AppendString(const gtGdsRecord& record, gtLine* line,
                                  bool warnPadding) {
  if (warnPadding && (record.payload.size() % 2) != 0) {
    AddWarning(record, "string payload length is odd");
  }
  if (warnPadding && record.payload.size() > 1) {
    const size_t lastIndex = record.payload.size() - 1;
    for (size_t index = 0; index < lastIndex; ++index) {
      if (record.payload[index] == 0) {
        AddWarning(record, "string payload contains embedded null padding");
        break;
      }
    }
  }
  if (warnPadding && record.payload.size() > 1 &&
      record.payload[record.payload.size() - 1] == 0 &&
      record.payload[record.payload.size() - 2] == 0) {
    AddWarning(record, "string payload contains multiple trailing null bytes");
  }
  line->parts.push_back("\"" +
                        EscapeString(record.payload, 0, record.payload.size()) +
                        "\"");
  return true;
}

bool gtGdsFormatter::AppendFixedStrings(const gtGdsRecord& record,
                                        size_t fieldLength, size_t fieldCount,
                                        gtLine* line, std::string* error) {
  const size_t expectedLength = fieldLength * fieldCount;
  if (!ValidateWidth(record, expectedLength, line->parts.front(), error)) {
    return false;
  }
  for (size_t index = 0; index < fieldCount; ++index) {
    const size_t fieldOffset = index * fieldLength;
    line->parts.push_back("\"" +
                          EscapeString(record.payload, fieldOffset,
                                       fieldLength) +
                          "\"");
  }
  return true;
}

bool gtGdsFormatter::AppendXy(const gtGdsRecord& record, gtLine* line,
                              std::string* error) {
  if ((record.payload.size() % 4) != 0) {
    *error = "invalid XY payload length";
    return false;
  }
  const size_t coordinateCount = record.payload.size() / 4;
  if ((coordinateCount % 2) != 0) {
    *error = "invalid XY coordinate count";
    return false;
  }

  const size_t pointCount = coordinateCount / 2;
  line->parts.push_back(ToString(static_cast<int64_t>(pointCount)));
  for (size_t offset = 0; offset < record.payload.size(); offset += 4) {
    line->parts.push_back(ToString(ReadInt32(record.payload, offset)));
  }
  return true;
}

bool gtGdsFormatter::ValidateWidth(const gtGdsRecord& record, size_t width,
                                   const std::string& name,
                                   std::string* error) const {
  if (record.payload.size() == width) {
    return true;
  }
  *error = "invalid " + name + " payload length";
  return false;
}

void gtGdsFormatter::UseRawPayload(const gtGdsRecord& record,
                                   const std::string& warning, gtLine* line) {
  AddWarning(record, warning + "; emitted raw payload");
  const std::string keyword = line->parts.empty() ? "record" : line->parts[0];
  line->parts.clear();
  line->parts.push_back(keyword);
  line->parts.push_back("raw");
  line->parts.push_back(HexPayload(record.payload));
}

void gtGdsFormatter::UpdateStateBeforeLine(const gtGdsRecord& record,
                                           gtLine* line) {
  line->offset = record.offset;
  if (record.recordType == 0x05 || record.recordType == 0x07) {
    line->indent = 4;
    return;
  }
  if (IsElementStart(record.recordType)) {
    line->indent = 8;
    return;
  }
  if (m_inElement) {
    line->indent = 12;
    return;
  }
  if (m_inStructure) {
    line->indent = 4;
    return;
  }
  line->indent = 0;
}

void gtGdsFormatter::UpdateStateAfterLine(const gtGdsRecord& record,
                                          const gtLine& line) {
  if (record.recordType == 0x05) {
    m_inStructure = true;
    m_currentCellName.clear();
    return;
  }
  if (record.recordType == 0x06 && !record.payload.empty()) {
    m_currentCellName =
        DecodeString(record.payload, 0, record.payload.size());
    return;
  }
  if (IsElementStart(record.recordType)) {
    m_inElement = true;
    return;
  }
  if (record.recordType == 0x11) {
    m_inElement = false;
    return;
  }
  if (record.recordType == 0x07) {
    m_inStructure = false;
    m_inElement = false;
    (void)line;
    return;
  }
  if (record.recordType == 0x04) {
    m_seenEndlib = true;
  }
}

void gtGdsFormatter::EmitLine(const gtLine& line, std::ostream* output) const {
  EmitLineParts(line.offset, line.indent, line.parts, output);
}

void gtGdsFormatter::EmitLineParts(
    uint64_t offset, int indent, const std::vector<std::string>& parts,
    std::ostream* output) const {
  if (parts.empty()) {
    return;
  }

  const auto emitPrefix = [this, offset, indent, output]() {
    if (m_options.showOffset) {
      *output << '[' << std::hex << std::setw(8) << std::setfill('0') << offset
              << std::dec << "]  ";
    }
    *output << std::string(indent, ' ');
  };

  if (parts.front() != "xy" || parts.size() <= 10) {
    emitPrefix();
    for (size_t index = 0; index < parts.size(); ++index) {
      if (index != 0) {
        *output << ' ';
      }
      *output << parts[index];
    }
    *output << '\n';
    return;
  }

  emitPrefix();
  *output << parts[0] << ' ' << parts[1];
  size_t coordinateIndex = 2;
  size_t pointsOnLine = 0;
  while (coordinateIndex + 1 < parts.size() && pointsOnLine < 4) {
    *output << ' ' << parts[coordinateIndex] << ' ' << parts[coordinateIndex + 1];
    coordinateIndex += 2;
    ++pointsOnLine;
  }
  *output << '\n';

  const int offsetPrefixLength = m_options.showOffset ? 12 : 0;
  const int continuationIndent =
      offsetPrefixLength + indent +
      static_cast<int>(parts[0].size() + 1 + parts[1].size() + 1);
  while (coordinateIndex + 1 < parts.size()) {
    *output << std::string(continuationIndent, ' ');
    pointsOnLine = 0;
    while (coordinateIndex + 1 < parts.size() && pointsOnLine < 4) {
      if (pointsOnLine != 0) {
        *output << ' ';
      }
      *output << parts[coordinateIndex] << ' ' << parts[coordinateIndex + 1];
      coordinateIndex += 2;
      ++pointsOnLine;
    }
    *output << '\n';
  }
}

void gtGdsFormatter::AddWarning(const gtGdsRecord& record,
                                const std::string& message) {
  std::ostringstream stream;
  stream << "warning: offset=0x" << std::hex << std::setw(8)
         << std::setfill('0') << record.offset << ": " << message;
  m_warnings.push_back(stream.str());
}

void gtGdsFormatter::FlushPendingStructure(std::ostream* output) {
  if (m_currentCellName == m_options.cellName) {
    for (const gtLine& line : m_pendingStructure) {
      EmitLine(line, output);
    }
    m_foundCell = true;
  }
  m_pendingStructure.clear();
  m_currentCellName.clear();
}

#include "gtGdsReader.h"

#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

gtGdsReader::gtGdsReader(uint64_t windowSize) : m_window(windowSize) {}

gtGdsReader::~gtGdsReader() { RemoveTempFile(); }

bool gtGdsReader::Open(const std::string& path, std::string* error) {
  RemoveTempFile();
  m_offset = 0;

  bool isGzip = false;
  if (!IsGzipFile(path, &isGzip, error)) {
    return false;
  }
  if (!isGzip) {
    return m_window.Open(path, error);
  }

  std::string tempPath;
  if (!DecompressGzipToTemp(path, &tempPath, error)) {
    return false;
  }
  bool isTar = false;
  if (!IsTarArchive(tempPath, &isTar, error)) {
    unlink(tempPath.c_str());
    return false;
  }
  if (isTar) {
    *error = "gzip payload is a tar archive; expected gzip-compressed GDSII stream";
    unlink(tempPath.c_str());
    return false;
  }
  if (!m_window.Open(tempPath, error)) {
    unlink(tempPath.c_str());
    return false;
  }

  m_tempPath = tempPath;
  return true;
}

bool gtGdsReader::Next(gtGdsRecord* record, bool* hasRecord,
                       std::string* error) {
  *hasRecord = false;
  const uint64_t fileSize = m_window.Size();
  if (m_offset == fileSize) {
    return true;
  }

  /* Some stream writers append a null padding word after ENDLIB. It is not a
     record header and must not be emitted as one. */
  if (fileSize - m_offset == 2) {
    uint8_t padding[2] = {};
    if (!m_window.Read(m_offset, sizeof(padding), padding, error)) {
      return false;
    }
    if (padding[0] == 0 && padding[1] == 0) {
      m_offset = fileSize;
      return true;
    }
  }

  if (fileSize - m_offset < 4) {
    std::ostringstream stream;
    stream << "offset=0x" << std::hex << std::setw(8) << std::setfill('0')
           << m_offset << ": truncated record header";
    *error = stream.str();
    return false;
  }

  uint8_t header[4] = {};
  if (!m_window.Read(m_offset, sizeof(header), header, error)) {
    return false;
  }

  const uint16_t length = ReadUInt16(header);
  if (length < 4) {
    std::ostringstream stream;
    stream << "offset=0x" << std::hex << std::setw(8) << std::setfill('0')
           << m_offset << ": record length is less than 4";
    *error = stream.str();
    return false;
  }
  if ((length % 2) != 0) {
    std::ostringstream stream;
    stream << "offset=0x" << std::hex << std::setw(8) << std::setfill('0')
           << m_offset << ": record length is odd";
    *error = stream.str();
    return false;
  }
  if (static_cast<uint64_t>(length) > fileSize - m_offset) {
    std::ostringstream stream;
    stream << "offset=0x" << std::hex << std::setw(8) << std::setfill('0')
           << m_offset << ": record exceeds file size";
    *error = stream.str();
    return false;
  }

  record->offset = m_offset;
  record->length = length;
  record->recordType = header[2];
  record->dataType = header[3];
  if (!m_window.Read(m_offset + 4, length - 4, &record->payload, error)) {
    return false;
  }

  m_offset += length;
  *hasRecord = true;
  return true;
}

uint16_t gtGdsReader::ReadUInt16(const uint8_t* data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                               static_cast<uint16_t>(data[1]));
}

bool gtGdsReader::IsGzipFile(const std::string& path, bool* isGzip,
                             std::string* error) {
  *isGzip = false;
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    *error = "open failed: " + path;
    return false;
  }

  uint8_t header[2] = {};
  input.read(reinterpret_cast<char*>(header), sizeof(header));
  const std::streamsize readCount = input.gcount();
  if (readCount == 0) {
    return true;
  }
  if (readCount < static_cast<std::streamsize>(sizeof(header))) {
    return true;
  }

  *isGzip = header[0] == 0x1f && header[1] == 0x8b;
  return true;
}

bool gtGdsReader::IsTarArchive(const std::string& path, bool* isTar,
                               std::string* error) {
  *isTar = false;
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    *error = "open failed: " + path;
    return false;
  }

  std::vector<uint8_t> header(512, 0);
  input.read(reinterpret_cast<char*>(header.data()),
             static_cast<std::streamsize>(header.size()));
  if (input.gcount() < 263) {
    return true;
  }

  /* POSIX tar headers carry the ustar marker at byte 257. GDSII stream data can
     be gzip-compressed directly, but a tar container changes the byte stream
     and cannot be decoded as records. */
  *isTar = header[257] == 'u' && header[258] == 's' && header[259] == 't' &&
           header[260] == 'a' && header[261] == 'r' &&
           (header[262] == '\0' || header[262] == ' ');
  return true;
}

bool gtGdsReader::DecompressGzipToTemp(const std::string& path,
                                       std::string* tempPath,
                                       std::string* error) {
  gzFile input = gzopen(path.c_str(), "rb");
  if (input == nullptr) {
    *error = "gzip open failed: " + path;
    return false;
  }

  std::vector<char> pathTemplate(
      {'/', 'p', 'r', 'i', 'v', 'a', 't', 'e', '/', 't', 'm', 'p', '/',
       'g', 'd', 's', '2', 't', 'x', 't', '.', 'X', 'X', 'X', 'X', 'X',
       'X', '\0'});
  const int outputFd = mkstemp(pathTemplate.data());
  if (outputFd < 0) {
    *error = "temporary file create failed: " + std::string(std::strerror(errno));
    gzclose(input);
    return false;
  }

  std::vector<uint8_t> buffer(64 * 1024, 0);
  while (true) {
    const int readCount =
        gzread(input, buffer.data(), static_cast<unsigned int>(buffer.size()));
    if (readCount < 0) {
      int zlibError = 0;
      const char* message = gzerror(input, &zlibError);
      *error = "gzip read failed: " + std::string(message == nullptr ? "" : message);
      close(outputFd);
      unlink(pathTemplate.data());
      gzclose(input);
      return false;
    }
    if (readCount == 0) {
      break;
    }

    int written = 0;
    while (written < readCount) {
      const ssize_t chunk =
          write(outputFd, buffer.data() + written,
                static_cast<size_t>(readCount - written));
      if (chunk < 0) {
        if (errno == EINTR) {
          continue;
        }
        *error = "temporary file write failed: " +
                 std::string(std::strerror(errno));
        close(outputFd);
        unlink(pathTemplate.data());
        gzclose(input);
        return false;
      }
      written += static_cast<int>(chunk);
    }
  }

  if (close(outputFd) != 0) {
    *error = "temporary file close failed: " + std::string(std::strerror(errno));
    unlink(pathTemplate.data());
    gzclose(input);
    return false;
  }
  const int closeResult = gzclose(input);
  if (closeResult != Z_OK) {
    *error = "gzip close failed";
    unlink(pathTemplate.data());
    return false;
  }

  *tempPath = pathTemplate.data();
  return true;
}

void gtGdsReader::RemoveTempFile() {
  m_window.Close();
  if (m_tempPath.empty()) {
    return;
  }
  unlink(m_tempPath.c_str());
  m_tempPath.clear();
}

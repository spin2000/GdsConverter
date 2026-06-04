#include "gtMmapWindow.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <limits>

gtMmapWindow::gtMmapWindow(uint64_t windowSize) : m_windowSize(windowSize) {}

gtMmapWindow::~gtMmapWindow() { Close(); }

bool gtMmapWindow::Open(const std::string& path, std::string* error) {
  Close();

  m_fd = open(path.c_str(), O_RDONLY);
  if (m_fd < 0) {
    *error = "open failed: " + std::string(std::strerror(errno));
    return false;
  }

  struct stat st {};
  if (fstat(m_fd, &st) != 0) {
    *error = "stat failed: " + std::string(std::strerror(errno));
    Close();
    return false;
  }
  if (st.st_size < 0) {
    *error = "file size is negative";
    Close();
    return false;
  }

  const long pageSize = sysconf(_SC_PAGESIZE);
  if (pageSize <= 0) {
    *error = "cannot determine system page size";
    Close();
    return false;
  }

  m_size = static_cast<uint64_t>(st.st_size);
  m_pageSize = static_cast<size_t>(pageSize);
  if (m_windowSize == 0) {
    m_windowSize = kDefaultWindowSize;
  }

  return true;
}

void gtMmapWindow::Close() {
  Unmap();
  if (m_fd >= 0) {
    close(m_fd);
    m_fd = -1;
  }
  m_size = 0;
}

uint64_t gtMmapWindow::Size() const { return m_size; }

bool gtMmapWindow::Read(uint64_t offset, size_t size, uint8_t* data,
                        std::string* error) {
  if (size == 0) {
    return true;
  }
  if (m_fd < 0) {
    *error = "file is not open";
    return false;
  }
  if (offset > m_size || size > m_size - offset) {
    *error = "read exceeds file size";
    return false;
  }

  size_t copied = 0;
  while (copied < size) {
    const uint64_t currentOffset = offset + copied;
    const uint64_t mapEnd = m_mapOffset + m_mapLength;
    if (m_data == nullptr || currentOffset < m_mapOffset ||
        currentOffset >= mapEnd) {
      if (!MapWindow(currentOffset, error)) {
        return false;
      }
    }

    const uint64_t refreshedMapEnd = m_mapOffset + m_mapLength;
    const uint64_t available64 = refreshedMapEnd - currentOffset;
    const size_t remaining = size - copied;
    const size_t available =
        available64 > std::numeric_limits<size_t>::max()
            ? std::numeric_limits<size_t>::max()
            : static_cast<size_t>(available64);
    const size_t chunk = remaining < available ? remaining : available;
    const size_t mapIndex = static_cast<size_t>(currentOffset - m_mapOffset);
    std::memcpy(data + copied, m_data + mapIndex, chunk);
    copied += chunk;
  }

  return true;
}

bool gtMmapWindow::Read(uint64_t offset, size_t size,
                        std::vector<uint8_t>* data, std::string* error) {
  data->assign(size, 0);
  if (size == 0) {
    return true;
  }
  return Read(offset, size, data->data(), error);
}

bool gtMmapWindow::MapWindow(uint64_t offset, std::string* error) {
  if (offset >= m_size) {
    *error = "map offset exceeds file size";
    return false;
  }

  Unmap();

  /* mmap offsets must be page aligned. The exposed window still begins at the
     aligned address, while callers index by absolute file offset. */
  const uint64_t pageSize = static_cast<uint64_t>(m_pageSize);
  const uint64_t alignedOffset = (offset / pageSize) * pageSize;
  const uint64_t pageDelta = offset - alignedOffset;
  const uint64_t desiredLength = m_windowSize + pageDelta;
  const uint64_t fileRemaining = m_size - alignedOffset;
  const uint64_t mapLength64 =
      desiredLength < fileRemaining ? desiredLength : fileRemaining;
  if (mapLength64 > std::numeric_limits<size_t>::max()) {
    *error = "mmap window is too large for this platform";
    return false;
  }

  void* mapped = mmap(nullptr, static_cast<size_t>(mapLength64), PROT_READ,
                      MAP_PRIVATE, m_fd, static_cast<off_t>(alignedOffset));
  if (mapped == MAP_FAILED) {
    *error = "mmap failed: " + std::string(std::strerror(errno));
    return false;
  }

  m_data = static_cast<const uint8_t*>(mapped);
  m_mapOffset = alignedOffset;
  m_mapLength = static_cast<size_t>(mapLength64);
  return true;
}

void gtMmapWindow::Unmap() {
  if (m_data == nullptr) {
    return;
  }
  munmap(const_cast<uint8_t*>(m_data), m_mapLength);
  m_data = nullptr;
  m_mapOffset = 0;
  m_mapLength = 0;
}

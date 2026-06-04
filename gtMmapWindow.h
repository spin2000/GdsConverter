#ifndef SRC_GTMMAPWINDOW_H_
#define SRC_GTMMAPWINDOW_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class gtMmapWindow {
 public:
  static constexpr uint64_t kDefaultWindowSize = 32ULL * 1024ULL * 1024ULL;

  explicit gtMmapWindow(uint64_t windowSize = kDefaultWindowSize);
  gtMmapWindow(const gtMmapWindow&) = delete;
  gtMmapWindow& operator=(const gtMmapWindow&) = delete;
  ~gtMmapWindow();

  bool Open(const std::string& path, std::string* error);
  void Close();

  uint64_t Size() const;
  bool Read(uint64_t offset, size_t size, uint8_t* data, std::string* error);
  bool Read(uint64_t offset, size_t size, std::vector<uint8_t>* data,
            std::string* error);

 private:
  bool MapWindow(uint64_t offset, std::string* error);
  void Unmap();

  int m_fd = -1;
  uint64_t m_size = 0;
  uint64_t m_windowSize = kDefaultWindowSize;
  uint64_t m_mapOffset = 0;
  size_t m_mapLength = 0;
  size_t m_pageSize = 0;
  const uint8_t* m_data = nullptr;
};

#endif  // SRC_GTMMAPWINDOW_H_

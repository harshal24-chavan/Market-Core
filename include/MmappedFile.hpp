#pragma once

#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

class MmappedFile {
private:
  int fd_ = -1;
  size_t size_ = 0;
  const char *data_ = nullptr;

public:
  explicit MmappedFile(const std::string &filepath) {

    fd_ = open(filepath.c_str(), O_RDONLY);
    if (fd_ == -1) {
      throw std::runtime_error("cannot open ITCH file");
    }

    struct stat s;
    fstat(fd_, &s);
    size_ = s.st_size;

    int FLAGS = MAP_PRIVATE;
    data_ =
        static_cast<const char *>(mmap(NULL, size_, PROT_READ, FLAGS, fd_, 0));
    if (data_ == MAP_FAILED) {
      close(fd_);
      throw std::runtime_error("mmap failure");
    }

    if (madvise((void *)(data_), size_, MADV_SEQUENTIAL) != 0) {
      // Non-fatal, but good to log if it fails
      std::cerr << "Warning: madvise(MADV_SEQUENTIAL) failed.\n";
    }
    std::cout << "file mapped.\n";
  }

  ~MmappedFile() {
    if (data_ && data_ != MAP_FAILED)
      munmap(const_cast<char *>(data_), size_);

    if (fd_ != -1)
      close(fd_);
  }

  MmappedFile(const MmappedFile &) = delete;
  MmappedFile &operator=(const MmappedFile &) = delete;

  // --- Getters ---
  const char *data() const { return data_; }
  size_t size() const { return size_; }
};

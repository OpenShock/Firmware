
#pragma once

#include <cstdint>

template<typename T, std::size_t N>
class ResizableBuffer {
public:
  ResizableBuffer(std::size_t size = N) : _bufferPtr(_buffer), _bufferSize(N) {
    static_assert(N > 0, "ResizableBuffer size must be greater than 0");
    static_assert(std::is_trivially_copyable<T>::value, "ResizableBuffer type must be trivially copyable");
    resize(size);
  }

  ~ResizableBuffer() { _deleteBuffer(); }

  T* ptr() { return _bufferPtr; }

  const T* ptr() const { return _bufferPtr; }

  std::size_t size() const { return _bufferSize; }

  std::size_t resize(std::size_t size) {
    if (size <= _bufferSize) {
      return _bufferSize;
    }

    _deleteBuffer();
    T* newBuffer = new T[size];
    _bufferPtr   = newBuffer;
    _bufferSize  = size;

    return _bufferSize;
  }

private:
  void _deleteBuffer() {
    if (_bufferPtr != _buffer) {
      delete[] _bufferPtr;
    }
  }

  T _buffer[N];
  T* _bufferPtr;
  std::size_t _bufferSize;
};

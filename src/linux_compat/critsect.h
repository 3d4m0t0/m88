#pragma once

#include <mutex>

class CriticalSection {
public:
  class Lock {
  public:
    explicit Lock(CriticalSection& c) : cs(c) { cs.lock(); }
    ~Lock() { cs.unlock(); }

  private:
    CriticalSection& cs;
  };

  void lock() { mtx.lock(); }
  void unlock() { mtx.unlock(); }

private:
  std::mutex mtx;
};

#pragma once
#include <Arduino.h>
#include <functional>
#include <limits>

constexpr uint8_t ASYNC_WRITE_FLAG_COPY = 1;

class AsyncClient {
 public:
  bool connectResult = true, isConnected = false, sendResult = true;
  size_t capacity = 512, writeLimit = std::numeric_limits<size_t>::max();
  int connectCalls = 0, closeCalls = 0;
  std::string host, written;
  uint16_t port = 0;
  std::function<void(void*, AsyncClient*)> connected, disconnected;
  std::function<void(void*, AsyncClient*, int8_t)> error;
  std::function<void(void*, AsyncClient*, void*, size_t)> data;
  void* connectArg = nullptr;
  void* disconnectArg = nullptr;
  void* errorArg = nullptr;
  void* dataArg = nullptr;

  void onConnect(std::function<void(void*, AsyncClient*)> cb, void* arg) {
    connected = cb;
    connectArg = arg;
  }
  void onDisconnect(std::function<void(void*, AsyncClient*)> cb, void* arg) {
    disconnected = cb;
    disconnectArg = arg;
  }
  void onError(std::function<void(void*, AsyncClient*, int8_t)> cb, void* arg) {
    error = cb;
    errorArg = arg;
  }
  void onData(std::function<void(void*, AsyncClient*, void*, size_t)> cb, void* arg) {
    data = cb;
    dataArg = arg;
  }
  bool connect(const char* name, uint16_t number) {
    ++connectCalls;
    host = name;
    port = number;
    written.clear();
    return connectResult;
  }
  void completeConnect() {
    isConnected = true;
    connected(connectArg, this);
  }
  void close(bool) {
    ++closeCalls;
    isConnected = false;
    if (disconnected) disconnected(disconnectArg, this);
  }
  void fail() {
    isConnected = false;
    error(errorArg, this, -1);
  }
  void receive(const std::string& chunk) {
    data(dataArg, this, const_cast<char*>(chunk.data()), chunk.size());
  }
  size_t space() { return capacity; }
  bool send() { return sendResult; }
  size_t add(const char* bytes, size_t length, uint8_t) {
    const size_t count = std::min(length, writeLimit);
    written.append(bytes, count);
    return count;
  }
};

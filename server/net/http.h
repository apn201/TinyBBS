#pragma once
#include <string>

class ServerHttp {
 public:
  struct Impl;
  bool start(const std::string &endpoint, const std::string &webRoot);
  void stop();
  void tickWebSocket();
 private:
  Impl *impl_ = nullptr;
};

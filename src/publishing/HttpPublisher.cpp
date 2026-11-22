#include "HttpPublisher.h"
#include <ESP8266WiFi.h>
#include <base64.h>
#include <string.h>

HttpPublisher::HttpPublisher(AsyncClient& client, const HttpPostConfiguration& cfg, Logger& log)
    : client_(client), cfg_(cfg), log_(log) {
  client_.onConnect(
      [](void* arg, AsyncClient*) {
        auto* self = static_cast<HttpPublisher*>(arg);
        self->connected_ = self->result_ == PublicationResult::Pending;
        if (!self->connected_) self->client_.close(true);
      },
      this);
  client_.onDisconnect(
      [](void* arg, AsyncClient*) {
        static_cast<HttpPublisher*>(arg)->finish(false,
                                                 "connection closed before response headers");
      },
      this);
  client_.onError(
      [](void* arg, AsyncClient*, int8_t) {
        static_cast<HttpPublisher*>(arg)->finish(false, "DNS/TCP connection failed");
      },
      this);
  client_.onData(
      [](void* arg, AsyncClient*, void* data, size_t length) {
        static_cast<HttpPublisher*>(arg)->receive(static_cast<const char*>(data), length);
      },
      this);
}

bool HttpPublisher::makeRequest(const String& payload) {
  const char* url = cfg_.url;
  if (strncmp(url, "http://", 7) != 0 || strpbrk(url, "\r\n")) return false;
  const char* authority = url + 7;
  const char* end = authority + strcspn(authority, "/?#");
  if (end == authority) return false;
  String authorization;
  const char* at = static_cast<const char*>(memchr(authority, '@', end - authority));
  if (at) {
    for (const char* p = authority; p < at; ++p) authorization += *p;
    authority = at + 1;
  }
  const char* colon = static_cast<const char*>(memchr(authority, ':', end - authority));
  port_ = 80;
  if (colon) {
    unsigned long port = 0;
    if (colon + 1 == end) return false;
    for (const char* p = colon + 1; p < end; ++p) {
      if (*p < '0' || *p > '9') return false;
      port = port * 10 + (*p - '0');
      if (port > 65535) return false;
    }
    if (!port) return false;
    port_ = static_cast<uint16_t>(port);
  }
  host_ = "";
  for (const char* p = authority; p < (colon ? colon : end); ++p) {
    if (*p <= ' ' || *p == '[' || *p == ']') return false;
    host_ += *p;
  }
  if (!host_.length()) return false;
  String path = *end == '/' ? "" : "/";
  for (const char* p = end; *p && *p != '#'; ++p) {
    if (*p == ' ') return false;
    path += *p;
  }
  // Includes space for headers/authentication while retaining WiFi/HTTP-server headroom.
  const size_t required = payload.length() + path.length() + strlen(cfg_.username) * 2 +
                          strlen(cfg_.password) * 2 + 1024;
  if (ESP.getFreeHeap() < required + 8192 || !request_.reserve(required)) return false;
  request_ = String("POST ") + path + " HTTP/1.1\r\nHost: " + host_ + ":" +
             String(static_cast<unsigned long>(port_)) +
             "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
             String(static_cast<unsigned long>(payload.length())) + "\r\n";
  if (cfg_.username[0]) authorization = String(cfg_.username) + ":" + cfg_.password;
  if (authorization.length()) {
    request_ += "Authorization: Basic " + base64::encode(authorization) + "\r\n";
  }
  request_ += String("\r\n") + payload;
  return true;
}

PublicationResult HttpPublisher::publish(const String& payload, uint32_t now) {
  update(now);
  if (result_ == PublicationResult::Pending) return PublicationResult::Failed;
  if (failureCooldown_ && uint32_t(now - failedAt_) < RETRY_MS) return PublicationResult::Failed;
  // Complete cleanup before reusing the transport, including a synchronous
  // connect() rejection for which the TCP library may not invoke a callback.
  result_ = PublicationResult::Pending;
  failure_ = nullptr;
  connected_ = false;
  sent_ = headerBytes_ = statusLength_ = 0;
  headerTail_ = 0;
  statusCode_ = 0;
  firstLine_ = true;
  startedAt_ = now;
  if (WiFi.status() != WL_CONNECTED) {
    finish(false, "WiFi unavailable");
  } else if (!makeRequest(payload)) {
    finish(false, "invalid HTTP URL or insufficient memory");
  } else if (!client_.connect(host_.c_str(), port_)) {
    finish(false, "DNS/TCP connection could not start");
  }
  update(now);
  return result_;
}

void HttpPublisher::finish(bool success, const char* reason) {
  if (result_ != PublicationResult::Pending) return;
  result_ = success ? PublicationResult::Succeeded : PublicationResult::Failed;
  failure_ = reason;
  cleanupPending_ = true;
}

void HttpPublisher::receive(const char* data, size_t length) {
  for (size_t i = 0; i < length && result_ == PublicationResult::Pending; ++i) {
    const char c = data[i];
    if (++headerBytes_ > 4096) {
      finish(false, "response headers too large");
      return;
    }
    if (firstLine_) {
      if (c == '\n') {
        statusLine_[statusLength_] = '\0';
        if (statusLength_ < 12 ||
            (strncmp(statusLine_, "HTTP/1.1 ", 9) && strncmp(statusLine_, "HTTP/1.0 ", 9)) ||
            statusLine_[9] < '1' || statusLine_[9] > '5' || statusLine_[10] < '0' ||
            statusLine_[10] > '9' || statusLine_[11] < '0' || statusLine_[11] > '9' ||
            (statusLine_[12] != ' ' && statusLine_[12] != '\r' && statusLine_[12] != '\0')) {
          finish(false, "malformed HTTP status line");
          return;
        }
        statusCode_ =
            (statusLine_[9] - '0') * 100 + (statusLine_[10] - '0') * 10 + statusLine_[11] - '0';
        firstLine_ = false;
      } else if (statusLength_ < sizeof(statusLine_) - 1) {
        statusLine_[statusLength_++] = c;
      } else {
        finish(false, "HTTP status line too large");
        return;
      }
    }
    headerTail_ = (headerTail_ << 8) | static_cast<uint8_t>(c);
    if (headerTail_ == 0x0d0a0d0a) {
      if (statusCode_ >= 100 && statusCode_ < 200 && statusCode_ != 101) {
        // An interim response is not a completed publication.
        firstLine_ = true;
        statusLength_ = 0;
        headerTail_ = 0;
      } else {
        finish(statusCode_ >= 200 && statusCode_ < 300 && sent_ == request_.length(),
               "non-2xx or premature HTTP response");
      }
    }
  }
}

void HttpPublisher::update(uint32_t now) {
  if (result_ == PublicationResult::Pending) {
    if (uint32_t(now - startedAt_) >= TIMEOUT_MS) {
      finish(false, "request timed out");
    } else if (connected_ && sent_ < request_.length()) {
      size_t amount = request_.length() - sent_;
      if (amount > 512) amount = 512;
      if (amount > client_.space()) amount = client_.space();
      if (amount) {
        const size_t queued = client_.add(request_.c_str() + sent_, amount, ASYNC_WRITE_FLAG_COPY);
        sent_ += queued;
        if (queued && !client_.send()) finish(false, "TCP send failed");
      }
    }
  }
  if (cleanupPending_) {
    cleanupPending_ = false;
    connected_ = false;
    client_.close(true);
    // Release request credentials/body; never include either in diagnostics.
    request_ = String();
    if (result_ == PublicationResult::Failed) {
      failureCooldown_ = true;
      failedAt_ = now;
      log_.warn("HTTP publish failed: %s (status=%d); retry after %lu ms\n", failure_, statusCode_,
                static_cast<unsigned long>(RETRY_MS));
    } else {
      failureCooldown_ = false;
      log_.debug("HTTP publish succeeded (status=%d)\n", statusCode_);
    }
  }
}

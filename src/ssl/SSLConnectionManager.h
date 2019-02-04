//
// Created by abdess on 1/18/19.
//

#pragma once

#include "../connection/connection.h"
#include "SSLContext.h"
#include "ssl_common.h"

namespace ssl {

class SSLConnectionManager {
  void setSslInfoCallback(Connection &ssl_connection, SslInfoCallback callback);

public:
  SSLContext *ssl_context;
  IO::IO_RESULT handleDataRead(Connection &ssl_connection);
  bool handleHandshake(Connection &ssl_connection);
  IO::IO_RESULT handleWrite(Connection &ssl_connection, const char *data,
                            size_t data_size, size_t &written);

  SSLConnectionManager();
  virtual ~SSLConnectionManager();
  bool init(SSLContext &context);
  bool init(const ListenerConfig &listener_config);

  bool initSslConnection(Connection &ssl_connection, bool client_mode = false);
};
} // namespace ssl

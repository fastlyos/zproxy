//
// Created by abdess on 4/5/18.
//

#include "connection.h"
#include "../debug/Debug.h"
#include "../util/Network.h"

Connection::Connection() : string_buffer(), socket_fd(-1), is_connected(false) {
  //address.ai_addr = new sockaddr();
}
Connection::~Connection() {
  is_connected = false;
  this->closeConnection();
  if (address != nullptr) {
    if (address->ai_addr != nullptr)
      delete address->ai_addr;
  }
  delete address;
}

int Connection::read() {
  char buffer[MAX_DATA_SIZE];
  bool should_close = false, done = false;
  ssize_t count = -1;
  size_t buffer_size = 0;

  while (!done) {
    count = ::recv(socket_fd, buffer + buffer_size, MAX_DATA_SIZE,
                   MSG_NOSIGNAL);
    if (count == -1) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        std::string error = "read() failed  ";
        error += std::strerror(errno);
        Debug::Log(error, LOG_NOTICE);
        should_close = true;
      }
      done = true;
    } else if (count == 0) {
      //  The  remote has closed the connection, wait for EPOLLRDHUP
      should_close = true;
      done = true;
    } else {
      buffer_size += static_cast<size_t>(count);
    }
  }
  string_buffer << buffer;
  return static_cast<int>(buffer_size);
}

int Connection::getFileDescriptor() const {
  return socket_fd;
}
void Connection::setFileDescriptor(int fd) {
  socket_fd = fd;
}
int Connection::writeTo(int fd) {
  bool done = false;
  size_t sent = 0;
  ssize_t count;

  while (!done) {
    count =
        ::send(fd, string_buffer.string().c_str() + sent,
               string_buffer.string().length() - sent,
               MSG_NOSIGNAL);
    if (count < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK /* && errno != EPIPE &&
          errno != ECONNRESET*/) {  // TODO:: What to do if connection closed
        std::string error = "write() failed  ";
        error += std::strerror(errno);
        Debug::Log(error, LOG_NOTICE);
      }
      done = true;
      break;
    } else if (count == 0) {
      done = true;
      break;
    } else {
      sent += static_cast<size_t>(count);
    }
  }
  string_buffer.erase(static_cast<unsigned int>(sent));
  return static_cast<int> (sent);
}
int Connection::write(const char *data, size_t buffer_size) {
  bool done = false;
  size_t sent = 0;
  ssize_t count;

  while (!done) {
    count =
        ::send(socket_fd, data + sent,
               buffer_size - sent,
               MSG_NOSIGNAL);
    if (count < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK /* && errno != EPIPE &&
          errno != ECONNRESET*/) {  // TODO:: What to do if connection closed
        std::string error = "write() failed  ";
        error += std::strerror(errno);
        Debug::Log(error, LOG_NOTICE);
      }
      done = true;
      break;
    } else if (count == 0) {
      done = true;
      break;
    } else {
      sent += static_cast<size_t>(count);
    }
  }
  return static_cast<int> (sent);
}

void Connection::closeConnection() {
  is_connected = false;
  ::shutdown(socket_fd, 2);
  ::close(socket_fd);
}
bool Connection::doConnect(addrinfo &address, int timeout) {
  long arg;
  fd_set sdset{};
  struct timeval tv{};
  socklen_t len;
  int result = -1, valopt;
  if ((socket_fd = socket(address.ai_family, SOCK_STREAM, 0)) < 0) {
    // TODO::LOG message
    Debug::logmsg(LOG_WARNING, "socket() failed ");
    return false;
  }
  if (timeout > 0) Network::setSocketNonBlocking(socket_fd);
  if ((result = ::connect(socket_fd, address.ai_addr, sizeof(address))) < 0) {
    if (errno == EINPROGRESS && timeout > 0) {
      tv.tv_sec = timeout;
      tv.tv_usec = 0;
      FD_ZERO(&sdset);
      FD_SET(socket_fd, &sdset);
      if (select(socket_fd + 1, NULL, &sdset, NULL, &tv) > 0) {
        len = sizeof(int);
        getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, (void *) (&valopt), &len);
        if (valopt != 0) {
          Debug::logmsg(LOG_NOTICE, "connect() error %d - %s\n", valopt,
                        strerror(valopt));
        }
          // connection established
        else {
          result = 0;
        }
      } else if (errno != EINPROGRESS) {
        Debug::logmsg(LOG_NOTICE, "connect() failed");
        return false;
      }

    } else {
      Debug::logmsg(LOG_NOTICE, "connect() error %d - %s\n", errno,
                    strerror(errno));
    }
  }
  if (timeout > 0) Network::setSocketNonBlocking(socket_fd, true);
  // Create stream object if connected
  return result != -1;
}

int Connection::doAccept() {
  int new_fd = -1;
  sockaddr_in clnt_addr{};
  socklen_t clnt_length = sizeof(clnt_addr);

  if ((new_fd =
           accept4(socket_fd, (sockaddr *) &clnt_addr, &clnt_length,
                   SOCK_NONBLOCK | SOCK_CLOEXEC)) < 0) {
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      return 0;  // We have processed all incoming connections.
    }
    std::string error = "accept() failed  ";
    error += std::strerror(errno);
    Debug::Log(error);
    // break;
    return -1;
  }
  if ((&clnt_addr)->sin_family == AF_INET ||
      (&clnt_addr)->sin_family == AF_INET6) {

    return new_fd;
  } else {
    ::close(new_fd);
    Debug::Log("HTTP connection prematurely closed by peer", LOG_WARNING);
  }

  return -1;
}
bool Connection::listen(std::string &address_str, int port) {
  this->address = Network::getAddress(address_str, port);
  if (this->address != nullptr) return listen(*this->address);
  return false;
}

bool Connection::listen(addrinfo &address) {
  this->address = &address;
  /* prepare the socket */
  if ((socket_fd = socket(
      address.ai_family == AF_INET ? PF_INET : PF_INET6,
      SOCK_STREAM, 0)) < 0) {
    Debug::logmsg(LOG_ERR, "socket () failed %s s - aborted",
                  strerror(errno));
    return false;
  }

  Network::setSoLingerOption(socket_fd);
  Network::setSoReuseAddrOption(socket_fd);
  Network::setTcpDeferAcceptOption(socket_fd);

  if (::bind(socket_fd, address.ai_addr,
             static_cast<socklen_t>(address.ai_addrlen)) < 0) {
    Debug::logmsg(LOG_ERR, "bind () failed %s s - aborted",
                  strerror(errno));
    ::close(socket_fd);
    socket_fd = -1;
    return false;
  }

  ::listen(socket_fd, 2048);
  return true;
}

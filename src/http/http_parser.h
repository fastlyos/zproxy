#pragma once

#include "http.h"
#include "picohttpparser.h"
#include "regex"
#include <map>
#include <string>
#include <sys/uio.h>

#define cmp_header_name(header, val)                                           \
  header->name_len == strlen(val) &&                                           \
      strncasecmp(header->name, val, header->name_len) == 0
#define cmp_header_value(header, val)                                          \
  header->value_len == strlen(val) &&                                          \
      strncasecmp(header->value, val, header->value_len) == 0

namespace http_parser {

enum class PARSE_RESULT { SUCCESS, FAILED, INCOMPLETE, TOOLONG };

using namespace http;

class HttpData {
public:
  HttpData();
  virtual ~HttpData() {
    extra_headers.clear();
    permanent_extra_headers.clear();
  }
  PARSE_RESULT parseRequest(const std::string &data, size_t *used_bytes,
                            bool reset = true);
  PARSE_RESULT parseRequest(const char *data, size_t data_size,
                            size_t *used_bytes, bool reset = true);

  PARSE_RESULT parseResponse(const std::string &data, size_t *used_bytes,
                             bool reset = true);
  PARSE_RESULT parseResponse(const char *data, size_t data_size,
                             size_t *used_bytes, bool reset = true);

  void printRequest();
  void printResponse();
  void reset_parser();

  bool getHeaderValue(http::HTTP_HEADER_NAME header_name, std::string &out_key);//FIXME
  bool getHeaderValue(const std::string &, std::string &out_key); //FIXME
  std::string getUrlParameter(const std::string &url);
  std::string getQueryParameter(const std::string &url, const std::string &sess_id);
  void setBuffer(char *ext_buffer, size_t ext_buffer_size);

public:
  std::vector< std::string> extra_headers;
  std::vector< std::string> permanent_extra_headers;
  std::vector<iovec> iov;
  void prepareToSend();
  inline void addHeader(http::HTTP_HEADER_NAME header_name,
                        const std::string &header_value,
                        bool permanent = false) {
    std::string newh;
    newh.reserve(http::http_info::headers_names_strings.at(header_name).size() + 2 + header_value.size() + 2);
    newh += http::http_info::headers_names_strings.at(header_name);
    newh += ": ";
    newh += header_value;
    newh += "\r\n";
    !permanent ? extra_headers.push_back(std::move(newh)) : permanent_extra_headers.push_back(std::move(newh));
  }
  inline void addHeader(const std::string &header_value, bool permanent = false) {
    std::string newh;
    newh.reserve(header_value.size() + 2);
    newh += header_value;
    newh += "\r\n";
    !permanent ? extra_headers.push_back(newh) : permanent_extra_headers.push_back(std::move(newh));
  }
#if CACHE_ENABLED
  bool pragma = false;
  bool cache_control = false;
#endif
  phr_header headers[MAX_HEADERS_SIZE];
  char *buffer;
  size_t buffer_size;
  size_t last_length;
  size_t num_headers;
  char *http_message; // indicate firl line in a http request / response
  size_t http_message_length;
  size_t headers_length;
  // request
  const char *method;
  size_t method_len;
  int minor_version;

  const char *path;
  size_t path_length;
  // response
  int http_status_code;

  const char *status_message;
  char *message;             // body data start
  size_t message_length;     // body data lenght in current received message
  size_t message_bytes_left; // content-lenght
  size_t content_length;
  size_t chunk_size_left;
  /** This enumerate indicates the chunked mechanism status. */
  http::CHUNKED_STATUS chunked_status{CHUNKED_STATUS::CHUNKED_DISABLED};
  http::HTTP_VERSION http_version;
  http::REQUEST_METHOD request_method;
  http::TRANSFER_ENCODING_TYPE transfer_encoding_type;

  bool hasPendingData();

  char *getBuffer() const;
  bool getHeaderSent() const;
  void setHeaderSent(bool value);
private:
  bool headers_sent{false};
};
} // namespace http_parser


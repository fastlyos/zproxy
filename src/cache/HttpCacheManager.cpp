﻿#if CACHE_ENABLED
#include "HttpCacheManager.h"
bool HttpCacheManager::isCached(HttpRequest &request) { return isCached(request.getUrl()); }

bool HttpCacheManager::isCached(const std::string &url) {
  size_t hashed_url = hashStr(url);
  if (cache.find(hashed_url) == cache.end()) {
    return false;
  } else {
    auto c_object = getCachedObject(url);
    //Check what storage to use
    switch (c_object->storage){
    case STORAGE_TYPE::RAMFS:
        return ram_storage->isStored(this->service_name,url);
    case STORAGE_TYPE::DISK:
        return disk_storage->isStored(this->service_name,url);
    default:
        return false;
    }
  }
}

// Returns the cache content with all the information stored
CacheObject *HttpCacheManager::getCachedObject(HttpRequest request) {
  return getCachedObject(request.getUrl());
}
// Returns the cache content with all the information stored
CacheObject *HttpCacheManager::getCachedObject(std::string url) {
  CacheObject *c_object = nullptr;
  auto iter = cache.find(hashStr(url));
  if (iter != cache.end())
    c_object = iter->second;
  return c_object;
}
size_t HttpCacheManager::hashStr(std::string str) {
  size_t str_hash = std::hash<std::string>{}(str);
  return str_hash;
}
// Store in cache the response if it doesn't exists
void HttpCacheManager::handleResponse(HttpResponse response,
                                      HttpRequest request) {
  /*
   *no-store, no-cache
   */
  // If the response/request is set as not cacheable, we can't cache it
  if (!response.c_opt.cacheable) {
    Debug::logmsg(LOG_DEBUG,
                  "The response or request disabled the caching system");
    return;
  } else if (response.cache_control == false && response.pragma == true) {
    // Check the pragma only if no cache-control header in request nor in
    // response, if the pragma was present, disable cache
    return;
  }
  //  Check status code
  if (response.http_status_code != 200 && response.http_status_code != 301 &&
      response.http_status_code != 308)
    return;
  if ( ((response.content_length + response.headers_length ) >= cache_max_size) && cache_max_size != 0 ){
    DEBUG_COUNTER_HIT(cache_stats__::cache_not_stored);
    Debug::logmsg(LOG_WARNING, "Not caching response with %d bytes size", response.content_length + response.headers_length);
    return;
  }

  // Check HTTP verb
  switch (http::http_info::http_verbs.at(
      std::string(request.method, request.method_len))) {
  case http::REQUEST_METHOD::GET:
    storeResponse(response, request);
    break;
  case http::REQUEST_METHOD::HEAD:
    if (isCached(request))
      updateResponse(response, request);
    break;
  default:
    return;
  }
  return;
}

void HttpCacheManager::updateResponse(HttpResponse response,
                                      HttpRequest request) {
  auto c_object = getCachedObject(request);
  if (response.content_length == 0)
    Debug::logmsg(LOG_WARNING, "Content-Length header with 0 value when trying "
                               "to update content in the cache");
  if (response.content_length != c_object->content_length) {
    Debug::logmsg(
        LOG_WARNING,
        "Content-Length in response and Content-Length cached missmatch for %s",
        request.getUrl().data());
    return;
  }
  if (response.etag.compare(c_object->etag) != 0) {
    Debug::logmsg(LOG_WARNING,
                  "ETag in response and ETag cached missmatch for %s",
                  request.getUrl().data());
    return;
  }
  c_object->staled = false;
  c_object->date = timeHelper::gmtTimeNow();

  return;
}
// Decide on whether to use RAMFS or disk
STORAGE_TYPE HttpCacheManager::getStorageType( HttpResponse response )
{
    size_t ram_size_left = ram_storage->max_size - ram_storage->current_size;
    //How are we deciding if
    size_t response_size = response.http_message_length + response.content_length;

    if ( response_size > ram_storage->max_size * 0.05 || response_size >= ram_size_left )
        return STORAGE_TYPE::DISK;
    else
        return STORAGE_TYPE::RAMFS;
}

HttpCacheManager::~HttpCacheManager() {
    // Free cache pattern
    if (cache_pattern != nullptr)
    {
        regfree(cache_pattern);
        cache_pattern = nullptr;
    }
    ram_storage->stopCacheStorage();
    disk_storage->stopCacheStorage();
}

void HttpCacheManager::cacheInit(regex_t *pattern, const int timeout, const string svc, long storage_size, int storage_threshold, string f_name) {
    if (pattern != nullptr) {
        if (pattern->re_pcre != nullptr) {
            this->cache_pattern = pattern;
            this->cache_timeout = timeout;
            this->cache_enabled = true;
            this->service_name = svc;
        }
        //Create directory, if fails, and it's not because the folder is already created, just return an error
        if (mkdir(ramfs_mount_point.data(),0777) == -1) {
            if (errno != EEXIST){
                Debug::logmsg(LOG_ERR, "Error creating the directory %s", ramfs_mount_point.data());
                exit( 1 );
            }
        }
        ramfs_mount_point += "/"+ f_name;
        disk_mount_point += "/" + f_name;
        //Cache initialization
#if MEMCACHED_ENABLED
        ram_storage = MemcachedCacheStorage::getInstance();
        ram_storage->initCacheStorage(static_cast<unsigned long>(storage_size), ramfs_mount_point);
        ram_storage->initServiceStorage(svc);
#else
        ram_storage = RamfsCacheStorage::getInstance();
        ram_storage->initCacheStorage(static_cast<unsigned long>(storage_size), ramfs_mount_point);
        ram_storage->initServiceStorage(svc);
        ram_storage->cache_thr = static_cast<double>(storage_threshold) / 100;
#endif
        disk_storage = DiskCacheStorage::getInstance();
        //Max size not useful yet
        disk_storage->initCacheStorage(0, disk_mount_point);
        disk_storage->initServiceStorage(svc);
    }
}

void HttpCacheManager::storeResponse(HttpResponse response,
                                     HttpRequest request) {
    CacheObject *c_object = new CacheObject;

    // Store the response date in the cache
    c_object->date = response.date;
    /*
   *max-age, s-maxage, etc.
   */
    // If the max_age is not set nor the timeout exist, we have to calculate
    // heuristically
    if (response.c_opt.max_age >= 0 && this->cache_timeout != 0)
        // Set the most restrictive value
        response.c_opt.max_age > this->cache_timeout
                ? c_object->max_age = this->cache_timeout
                : c_object->max_age = response.c_opt.max_age;
    else if (this->cache_timeout >= 0)
        // Store the config file timeout
        c_object->max_age = this->cache_timeout;
    else if (response.c_opt.max_age >= 0)
        // Store the response cache max-age
        c_object->max_age = response.c_opt.max_age;
    else if (response.last_mod >= 0) {
        // heuristic algorithm -> 10% of last-modified
        time_t now = timeHelper::gmtTimeNow();
        c_object->max_age = (now - response.last_mod) * 0.1;
    } else {
        // If not available value, use the defined default timeout
        c_object->max_age = DEFAULT_TIMEOUT;
    }
    /*
*must-revalidate, proxy-revalidate
*/
    if (response.expires >= 0)
        c_object->expires = response.expires;
    // If there is etag, then store it
    if (!response.etag.empty())
        c_object->etag = response.etag;
    c_object->revalidate = response.c_opt.revalidate;

    // Reset the stale flag, the cache has been created or updated
    c_object->staled = false;
  c_object->content_length = response.content_length;
  c_object->no_cache_response = response.c_opt.no_cache;

  //Check what storage to use
  STORAGE_STATUS err;
  std::string rel_path = service_name + "/" + to_string(std::hash<std::string>()(request.getUrl()));
//  Debug::logmsg(LOG_NOTICE, "We are comparing values: message_length %d + headers_length %d = %d , against buffer_size: %d total: %d", response.message_length, response.headers_length, (response.message_length + response.headers_length), response.buffer_size, (response.content_length + response.headers_length - response.buffer_size) );
//  Debug::logmsg(LOG_NOTICE, "We are CREATING a file entry with %d data and waiting for %d", response.buffer_size, (response.content_length + response.headers_length - response.buffer_size));
  switch (getStorageType(response)){
  case STORAGE_TYPE::RAMFS:
      if( isCached(request) )
      {
        auto old_object = getCachedObject(request);
        ram_storage->current_size -= (old_object->content_length + old_object->headers_size);
      }
      c_object->storage = STORAGE_TYPE::RAMFS;
      err = ram_storage->putInStorage(rel_path, std::string(response.buffer,response.buffer_size), (response.content_length + response.headers_length));
      if (err == STORAGE_STATUS::SUCCESS){
          DEBUG_COUNTER_HIT(cache_stats__::cache_RAM_entries);
      }
      break;
  case STORAGE_TYPE::DISK:
      if( isCached(request) )
      {
        auto old_object = getCachedObject(request);
        disk_storage->current_size -= (old_object->content_length + old_object->headers_size);
      }
      c_object->storage = STORAGE_TYPE::DISK;
      err = disk_storage->putInStorage(rel_path, std::string(response.buffer,response.buffer_size), (response.content_length + response.headers_length));
      if (err == STORAGE_STATUS::SUCCESS)
          DEBUG_COUNTER_HIT(cache_stats__::cache_DISK_entries);
      break;
  default:
      return;
  }
  // If success, store in the unordered map
  if ( err != STORAGE_STATUS::SUCCESS)
    Debug::logmsg(LOG_ERR, "Error trying to store the response in storage");
  else
      c_object->headers_size = response.headers_length;
      cache[hashStr(request.getUrl())] = c_object;
  return;
}

// Append pending data to its cached content
void HttpCacheManager::appendData(char *msg, size_t msg_size, std::string url) {
    Debug::logmsg(LOG_NOTICE, "Appending %d data to %s stored response", msg_size, url.data());
    auto c_object = getCachedObject(url);
    std::string rel_path = service_name + "/" + to_string(std::hash <std::string> () (url));
    //Check what storage to use
    switch (c_object->storage){
    case STORAGE_TYPE::RAMFS:
        ram_storage->appendData(rel_path, std::string(msg, msg_size));
        break;
    case STORAGE_TYPE::DISK:
        disk_storage->appendData(rel_path, std::string(msg, msg_size));
        break;
    default:
        return;
    }
    return;
}

// Check the freshness of the cached content
bool HttpCacheManager::isFresh(HttpRequest &request) {
  auto c_object = getCachedObject(request);
  if (c_object == nullptr)
    return false;
  updateContentStale(c_object);

  return (c_object->staled ? false : true);
}

// Check if the cached content can be served, depending on request
// cache-control values
bool HttpCacheManager::canBeServed(HttpRequest &request) {
  if (request.c_opt.no_cache || (!request.cache_control && request.pragma) || !isCached(request))
    return false;

  bool serveable = isFresh(request);
  std::time_t now = timeHelper::gmtTimeNow();
  CacheObject *c_object = getCachedObject(request);

  // if staled and must revalidate is included, we MUST revalidate the
  // response
  if (!serveable && c_object->revalidate)
    return false;
  // If max-age request directive is set, we must check if the response
  // complies
  if (request.c_opt.max_age >= 0) {
    if (!c_object->staled)
      if ((now - c_object->date) > request.c_opt.max_age)
        serveable = false;
  }
  // Check if complies with the request directive min-fresh
  if (request.c_opt.min_fresh >= 0) {
    if (!c_object->staled)
      if ((now - c_object->date) > request.c_opt.min_fresh)
        return false;
  }
  // Check if complies with the request directive max-stale
  if (request.c_opt.max_stale >= 0) {
    if (c_object->staled && !c_object->revalidate)
      if ((now - c_object->date - c_object->max_age) < request.c_opt.max_stale)
        serveable = true;
  }

  return serveable;
}

void HttpCacheManager::updateContentStale(CacheObject *c_object) {
  if (c_object->staled != true) {
    time_t now = timeHelper::gmtTimeNow();
    long int age_limit = 0;
    if (c_object->max_age >= 0 && !c_object->heuristic)
      age_limit = c_object->max_age;
    else if (c_object->expires >= 0)
      age_limit = c_object->expires;
    else if (c_object->max_age >= 0 && c_object->heuristic)
      age_limit = c_object->max_age;
    if ((now - c_object->date) > age_limit) {
      c_object->staled = true;
      DEBUG_COUNTER_HIT(cache_stats__::cache_staled_entries);
    }
  }
}
int HttpCacheManager::createCacheResponse(HttpRequest request,
                                          HttpResponse &cached_response) {
  auto c_object = getCachedObject(request);
  updateContentStale(c_object);

  size_t parsed = 0;
  std::string buff;
  std::string rel_path = service_name + "/" + to_string(std::hash<std::string>()(request.getUrl()));
  //Get the response from the right storage
  switch(c_object->storage){
  case STORAGE_TYPE::RAMFS:
      ram_storage->getFromStorage(rel_path, buff );
      break;
  case STORAGE_TYPE::DISK:
      disk_storage->getFromStorage(rel_path, buff );
      break;
  default:
      return -1;
  }

  auto ret = cached_response.parseResponse(buff, &parsed);
  cached_response.cached = true;

  for (size_t j = 0; j < cached_response.num_headers; j++) {
    if ( std::string(cached_response.headers[j].name).compare("date") == 0 )
    {
        cached_response.headers[j].header_off = true;
        cached_response.addHeader(http::HTTP_HEADER_NAME::DATE, timeHelper::strTime(c_object->date)->data() );
    }
    cached_response.headers[j].header_off = false;
  }

  if (ret == http_parser::PARSE_RESULT::FAILED) {
    Debug::logmsg(LOG_ERR, "The cached response failed to be parsed");
    return -1;
  } else if (ret == http_parser::PARSE_RESULT::SUCCESS) {
    // Add warning header
    std::vector<std::string> w_codes;
    std::vector<std::string> w_text;
    // Take the date for the warning
    std::string *w_date = timeHelper::strTimeNow();
    // Create warnings if needed
    if (c_object->staled) {
      w_codes.push_back(std::to_string(http::WARNING_CODE::RESPONSE_STALE));
      w_text.push_back(http::http_info::warning_code_values_strings.at(
          http::WARNING_CODE::RESPONSE_STALE));
    }
    // Defined by RFC7234
    if (c_object->heuristic && c_object->max_age >= 86400 && c_object->staled) {
      w_codes.push_back(
          std::to_string(http::WARNING_CODE::HEURISTIC_EXPIRATION));
      w_text.push_back(http::http_info::warning_code_values_strings.at(
          http::WARNING_CODE::HEURISTIC_EXPIRATION));
    }
    // Add warning headers if needed
    for (unsigned long i = 0; i < w_codes.size() && i < w_text.size(); i++) {
      cached_response.addHeader(http::HTTP_HEADER_NAME::WARNING,
                                w_codes.at(i) + " - " + "\"" + w_text.at(i) +
                                    "\" \"" + w_date->data() + "\"");
    }
    // Add Age header
    time_t now = timeHelper::getAge(c_object->date);
    cached_response.addHeader(
        http::HTTP_HEADER_NAME::AGE,
        std::to_string(
            now >= 0 ? now : 0)); // ensure that it is greater or equal than 0
  }

  return 0;
}

std::string HttpCacheManager::handleCacheTask(ctl::CtlTask &task)
{
    if (task.subject != ctl::CTL_SUBJECT::CACHE)
        return JSON_OP_RESULT::ERROR;
    switch (task.command)
    {
    case ctl::CTL_COMMAND::DELETE:{
        auto json_data = JsonParser::parse(task.data);
        if ( json_data == nullptr )
            return JSON_OP_RESULT::ERROR;
        //Error handling when trying to use the key
        try {
          json_data->at(JSON_KEYS::CACHE_CONTENT);
        }
        catch (const std::out_of_range& oor) {
          std::cerr << "Wrong key found, must be \"" << JSON_KEYS::CACHE_CONTENT << "\", caused by " << oor.what() << '\n';
          return JSON_OP_RESULT::ERROR;
        }
        auto url = dynamic_cast<JsonDataValue *>(json_data->at(JSON_KEYS::CACHE_CONTENT).get())->string_value;

        CacheObject * c_object = nullptr;
        c_object = getCachedObject( url );
        if ( c_object == nullptr )
            Debug::logmsg(LOG_WARNING, "Request %s not cached", url.data());
        else{
            size_t hash_url = std::hash<std::string>()(url);
            std::string path(service_name + "/" + to_string(hash_url));
            STORAGE_STATUS err;
            switch(c_object->storage)
            {
                case STORAGE_TYPE::RAMFS:
                    err = ram_storage->deleteInStorage(path);
                    break;
                case STORAGE_TYPE::DISK:
                    err = disk_storage->deleteInStorage(path);
                    break;
                case STORAGE_TYPE::MEMCACHED:
                case STORAGE_TYPE::TMPFS:
                case STORAGE_TYPE::STDMAP:
                default:
                    break;
            }
            if ( err == STORAGE_STATUS::SUCCESS )
            {
                this->cache.erase(hash_url);
                return JSON_OP_RESULT::OK;
            }
        }
        break;
    }
    default:
            Debug::logmsg(LOG_ERR, "Not a valid cache command");
            return JSON_OP_RESULT::ERROR;
    }
    return JSON_OP_RESULT::ERROR;
}
#endif

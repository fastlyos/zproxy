//
// Created by abdess on 9/28/18.
//

#include "ControlManager.h"
#include "../http/HttpRequest.h"
#include "../http/HttpStatus.h"
#include "../util/environment.h"

#define CTL_DEFAULT_IP "127.0.0.1"
#define CTL_DEFAULT_PORT 6001

using namespace ctl;

std::unique_ptr<ControlManager> ControlManager::instance =
    std::unique_ptr<ControlManager>(new ControlManager);

ctl::ControlManager::ControlManager(ctl::CTL_INTERFACE_MODE listener_mode)
    : is_running(false), ctl_listener_mode(listener_mode) {}

ctl::ControlManager::~ControlManager() { stop(); }

bool ctl::ControlManager::init(Config &configuration,
                               ctl::CTL_INTERFACE_MODE listener_mode) {
  ctl_listener_mode = ctl::CTL_INTERFACE_MODE::CTL_UNIX != listener_mode
                          ? listener_mode
                          : ctl_listener_mode;
  if (listener_mode == CTL_UNIX) {
    std::string control_path_name(configuration.ctrl_name);
    control_listener.listen(control_path_name);
    if (configuration.ctrl_user)
      Environment::setFileUserName(std::string(configuration.ctrl_user),
                                   control_path_name);
    if (configuration.ctrl_group)
      Environment::setFileGroupName(std::string(configuration.ctrl_group),
                                    control_path_name);
    if (configuration.ctrl_mode > 0)
      Environment::setFileUserMode(configuration.ctrl_mode, control_path_name);
  } else {
    control_listener.listen(CTL_DEFAULT_IP, CTL_DEFAULT_PORT);
  }
  handleAccept(control_listener.getFileDescriptor());
  return true;
}

void ctl::ControlManager::start() {
  is_running = true;
  control_thread = std::thread([this] { doWork(); });
  helper::ThreadHelper::setThreadName("CTL_WORKER",
                                      control_thread.native_handle());
}

void ctl::ControlManager::stop() {
  is_running = false;
  control_thread.join();
}

void ctl::ControlManager::HandleEvent(int fd, EVENT_TYPE event_type,
                                      EVENT_GROUP event_group) {
  if (event_group != CTL_INTERFACE && event_group != ACCEPTOR) {
    ::close(fd);
    return;
  }

  switch (event_type) {
    case CONNECT: {
      int new_fd;
      new_fd = control_listener.doAccept();
      if (new_fd > 0) {
        addFd(new_fd, EVENT_TYPE::READ, EVENT_GROUP::CTL_INTERFACE);
      }
      break;
    }
    case READ: {
      Connection connection;
      HttpRequest request;
      connection.setFileDescriptor(fd);
      auto res = connection.read();
      if (res != IO::SUCCESS && res != IO::DONE_TRY_AGAIN) {
        deleteFd(fd);
        ::close(fd);
        return;
      }
      size_t parsed = 0;
      auto parse_result = request.parseRequest(connection.buffer,
                                               connection.buffer_size, &parsed);

      if (parse_result != http_parser::PARSE_RESULT::SUCCESS) {
        deleteFd(fd);
        connection.closeConnection();
        return;
      }
      Debug::logmsg(LOG_INFO, "CTL API Request: %s", connection.buffer);

      std::string response = handleCommand(request);

      if (!response.empty()) {
        connection.write(response.c_str(), response.length());
      }

      deleteFd(fd);
      connection.closeConnection();
      return;
    }
    default:
      // why would we be here???
      deleteFd(fd);
      ::close(fd);
      break;
  }
}

void ctl::ControlManager::doWork() {
  while (is_running) {
    if (loopOnce() < 1) {
      // this should not happends
    }
  }
}

ctl::ControlManager *ctl::ControlManager::getInstance() {
  if (instance == nullptr)
    instance = std::unique_ptr<ControlManager>(new ControlManager());
  return instance.get();
}
std::string ctl::ControlManager::handleCommand(HttpRequest &request) {
  /* https://www.restapitutorial.com/lessons/httpmethods.html */

  CtlTask task;
  // get task action
  switch (request.getRequestMethod()) {
    case http::RM_DELETE:
      task.command = CTL_CMD_DELETE;
      break;
    case http::RM_POST:
      task.command = CTL_CMD_ADD;
      break;
    case http::RM_PUT:
    case http::RM_PATCH:
    case http::RM_UPDATE:
      task.command = CTL_CMD_UPDATE;
      break;
    case http::RM_GET:
      task.command = CTL_CMD_GET;
      break;
    case http::RM_SUBSCRIBE:
      task.command = CTL_CMD_SUSCRIBE;
      break;
    case http::RM_UNSUBSCRIBE:
      task.command = CTL_CMD_UNSUSCRIBE;
      break;
    default:
      return HttpStatus::getHttpResponse(HttpStatus::Code::MethodNotAllowed, "",
                                         "");
  }

  // remove tailing "/"

  if (!setTaskTarget(request, task) && task.target == CTL_NONE) {
    Debug::logmsg(LOG_WARNING, "Bad API request : %s",
                  request.getUrl().c_str());
    return HttpStatus::getHttpResponse(HttpStatus::Code::BadRequest, "", "");
  }
  if (task.command == CTL_CMD_ADD || task.command == CTL_CMD_UPDATE) {
    task.data = std::string(request.buffer + request.headers_length,
                            request.buffer_size - request.headers_length);
  }

  auto result = notify(task, false);
  std::string res = "{";
  for (auto &future_result : result) {
    res += future_result.get();
  }
  res += "}";

  auto response = HttpStatus::getHttpResponse(HttpStatus::Code::OK, "", res);
  return response;
}

bool ControlManager::setTaskTarget(HttpRequest &request, CtlTask &task) {
  std::istringstream f(request.getUrl());
  std::string str;
  bool done = false;
  while (getline(f, str, '/') && !done) {
    switch (str[0]) {
      case 'l': {
        if (str == "listener") {
          if (setListenerTarget(task, f)) {
            return true;
          }
        }
        break;
      }
      case 's': {
        if (str == "service") {
          if (setServiceTarget(task, f)) {
            return true;
          }
        }
        break;
      }
      case 'b': {
        if (str == "backend") {
          if (setBackendTarget(task, f)) {
            return true;
          }
        }
        break;
      }
    }
  }
  return false;
}

bool ControlManager::setListenerTarget(CtlTask &task, std::istringstream &ss) {
  std::string str;
  task.target = CTL_LISTENER;
  if (getline(ss, str, '/')) {
    if (!helper::try_lexical_cast<int>(str, task.listener_id)) {
      return false;
    }
    if (getline(ss, str, '/')) {
      if (str == "service") {
        return setServiceTarget(task, ss);
      } else if (str == "config") {
        task.subject = CTL_SB_CONFIG;
      } else if (str == "status") {
        task.subject = CTL_SB_STATUS;
      } else {
        return false;
      }
    }
  }
  return true;
}

bool ControlManager::setServiceTarget(CtlTask &task, std::istringstream &ss) {
  std::string str;
  task.target = CTL_SERVICE_MANAGER;
  if (getline(ss, str, '/')) {
    if (!helper::try_lexical_cast<int>(str, task.service_id)) {
      task.service_id = -1;
      task.service_name = str;
    }
    task.target = CTL_SERVICE;
    if (getline(ss, str, '/')) {
      if (str == "backend") {
        return setBackendTarget(task, ss);
      } else if (str == "config") {
        task.subject = CTL_SB_CONFIG;
      } else if (str == "status") {
        task.subject = CTL_SB_STATUS;
      } else if (str == "session") {
        task.subject = CTL_SB_STATUS;
      } else {
        return false;
      }
    }
  }
  return true;
}

bool ControlManager::setBackendTarget(CtlTask &task, std::istringstream &ss) {
  std::string str;
  task.target = CTL_BACKEND;
  if (getline(ss, str, '/')) {
    if (!helper::try_lexical_cast<int>(str, task.backend_id)) {
      task.backend_id = -1;
      task.backend_name = str;
    }
    if (getline(ss, str, '/')) {
      if (str == "config") {
        task.subject = CTL_SB_CONFIG;
      } else if (str == "status") {
        task.subject = CTL_SB_STATUS;
      } else {
        return false;
      }
    }
  }
  return true;
}

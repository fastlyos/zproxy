
#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <malloc.h>
#include <openssl/engine.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <cstdio>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <string>
#include "pound_struct.h"
#include "svc.h"
#include <openssl/lhash.h>

#ifndef F_CONF
constexpr auto F_CONF = "/usr/local/etc/zhttp.cfg";
#endif
#ifndef F_PID
constexpr auto F_PID = "/var/run/zhttp.pid";
#endif
constexpr int MAX_FIN = 100;
constexpr int UNIX_PATH_MAX = 108;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
#define general_name_string(n)                                             \
  reinterpret_cast<unsigned char *>(strndup(                               \
      reinterpret_cast<const char *>(ASN1_STRING_get0_data(n->d.dNSName)), \
      ASN1_STRING_length(n->d.dNSName) + 1))
#else
# define general_name_string(n) \
	(unsigned char*) \
	strndup((char*)ASN1_STRING_data(n->d.dNSName),	\
	       ASN1_STRING_length(n->d.dNSName) + 1)
#endif



class Config {
  const char *xhttp[5] = {
      "^(GET|POST|HEAD) ([^ ]+) HTTP/1.[01].*$",
      "^(GET|POST|HEAD|PUT|PATCH|DELETE) ([^ ]+) HTTP/1.[01].*$",
      "^(GET|POST|HEAD|PUT|PATCH|DELETE|LOCK|UNLOCK|PROPFIND|PROPPATCH|SEARCH|"
      "MKCOL|MKCALENDAR|MOVE|COPY|OPTIONS|TRACE|MKACTIVITY|CHECKOUT|MERGE|"
      "REPORT) ([^ ]+) HTTP/1.[01].*$",
      "^(GET|POST|HEAD|PUT|PATCH|DELETE|LOCK|UNLOCK|PROPFIND|PROPPATCH|SEARCH|"
      "MKCOL|MKCALENDAR|MOVE|COPY|OPTIONS|TRACE|MKACTIVITY|CHECKOUT|MERGE|"
      "REPORT|SUBSCRIBE|UNSUBSCRIBE|BPROPPATCH|POLL|BMOVE|BCOPY|BDELETE|"
      "BPROPFIND|NOTIFY|CONNECT) ([^ ]+) HTTP/1.[01].*$",
      "^(GET|POST|HEAD|PUT|PATCH|DELETE|LOCK|UNLOCK|PROPFIND|PROPPATCH|SEARCH|"
      "MKCOL|MKCALENDAR|MOVE|COPY|OPTIONS|TRACE|MKACTIVITY|CHECKOUT|MERGE|"
      "REPORT|SUBSCRIBE|UNSUBSCRIBE|BPROPPATCH|POLL|BMOVE|BCOPY|BDELETE|"
      "BPROPFIND|NOTIFY|CONNECT|RPC_IN_DATA|RPC_OUT_DATA) ([^ ]+) HTTP/1.[01].*$",
  };

  int log_level;
  int def_facility;
  int clnt_to;
  int be_to;
  int be_connto;
  bool dynscale;
  int ignore_case;
  std::array<std::string, MAX_FIN> f_name;
  FILE *f_in[MAX_FIN];
  int n_lin[MAX_FIN];
  size_t cur_fin;
  DH *DHCustom_params;
  int EC_nid;

 public:
  /*
   * Global variables needed by everybody
   */

  std::string user,        /* user to run as */
      group,               /* group to run as */
      name,                /* farm name to run as */
      root_jail,           /* directory to chroot to */
      pid_name,            /* file to record pid in */
      ctrl_name,           /* control socket name */
      ctrl_ip,   /* control socket ip */
      ctrl_user,           /* control socket username */
      ctrl_group,          /* control socket group name */
      engine_id; /* openssl engine id*/

  long ctrl_mode; /* octal mode of the control socket */

  static int numthreads;      /* number of worker threads */
  int anonymise,              /* anonymise client address */
      alive_to,               /* check interval for resurrection */
      daemonize,              /* run as daemon */
      log_facility,           /* log facility to use */
      print_log,              /* print log messages to stdout/stderr */
      grace,                  /* grace period before shutdown */
      ignore_100,             /* ignore header "Expect: 100-continue"*/
  /* 1 Ignore header (Default)*/
  /* 0 Manages header */
      ctrl_port = 0, sync_is_enabled; /*session sync enabled*/
#ifdef CACHE_ENABLED
  long cache_s;
  int cache_thr;
  std::string cache_ram_path;
  std::string cache_disk_path;
#endif
  int conf_init(const std::string &name);

 private:
  regex_t Empty, Comment, User, Group, Name, RootJail, Daemon, LogFacility,
      LogLevel, Alive, SSLEngine, Control, ControlIP, ControlPort;
  regex_t ListenHTTP, ListenHTTPS, End, Address, Port, Cert, CertDir, xHTTP,
      Client, CheckURL;
  regex_t Err414, Err500, Err501, Err503, SSLConfigFile, SSLConfigSection, ErrNoSsl, NoSslRedirect, MaxRequest,
      HeadRemove, RewriteLocation, RewriteDestination, RewriteHost;
  regex_t Service, ServiceName, URL, OrURLs, HeadRequire, HeadDeny, BackEnd,
      Emergency, Priority, HAport, HAportAddr, StrictTransportSecurity;
  regex_t Redirect, TimeOut, Session, Type, TTL, ID, DynScale,  PinnedConnection, RoutingPolicy, NfMark, CompressionAlgorithm;
  regex_t ClientCert, AddHeader, DisableProto, SSLAllowClientRenegotiation,
      SSLHonorCipherOrder, Ciphers;
  regex_t CAlist, VerifyList, CRLlist, NoHTTPS11, Grace, Include, ConnTO,
      IgnoreCase, Ignore100continue, HTTPS;
  regex_t Disabled, Threads, CNName, Anonymise, DHParams, ECDHCurve;
  regex_t ControlGroup, ControlUser, ControlMode;
  regex_t ThreadModel;
  regex_t IncludeDir;
  regex_t ForceHTTP10, SSLUncleanShutdown;
  regex_t BackendKey, BackendCookie;
#ifdef CACHE_ENABLED
  regex_t Cache, CacheContent, CacheTO, CacheThreshold, CacheRamSize, MaxSize, CacheDiskPath, CacheRamPath; /* Cache configuration regex */
#endif
 public:

  static regex_t HEADER,    /* Allowed header */
      CHUNK_HEAD,    /* chunk header line */
      RESP_SKIP,     /* responses for which we skip response */
      RESP_IGN,      /* responses for which we ignore content */
      LOCATION,      /* the host we are redirected to */
      AUTHORIZATION; /* the Authorisation header */

  bool compile_regex();
  void clean_regex();

  void conf_err(const char *msg);
  char *conf_fgets(char *buf, const int max);
  void include_dir(const char *conf_path);

  /*
   * return the file contents as a string
   */
  std::string file2str(const char *fname);

  /*
   * parse an HTTP listener
   */
  ListenerConfig *parse_HTTP(void);

  /*
   * parse an HTTPS listener
   */
  ListenerConfig *parse_HTTPS(void);

  unsigned char **get_subjectaltnames(X509 *x509, unsigned int *count);

  void load_cert(int has_other, ListenerConfig *res, char *filename);

  void load_certdir(int has_other, ListenerConfig *res,
                    const std::string &dir_path);

  /*
   * parse a service
   */
  ServiceConfig *parseService(const char *svc_name);
  /*
   * Dummy certificate verification - always OK
   */
  static int verify_OK(int pre_ok, X509_STORE_CTX *ctx);

  /*
   * parse an OrURLs block
   *
   * Forms a composite pattern of all URLs within
   * of the form ((url1)|(url2)|(url3)) (and so on)
   */
  char *parse_orurls();

  /*
   * parse a back-end
   */
  BackendConfig *parseBackend(const char * svc_name, const int is_emergency);
  /*
   * Parse the cache configuration
   */
#ifdef CACHE_ENABLED
  void parseCache(ServiceConfig *const svc);
#endif
  /*
   * parse a session
   */
  void parseSession(ServiceConfig *const svc);
  /*
   * parse the config file
   */
  void parse_file(void);


  public:
   ServiceConfig *services;   /* global services (if any) */
   ListenerConfig *listeners; /* all available listeners */

  public:
   Config();
   ~Config();

   /*
    * prepare to parse the arguments/config file
    */
   void parseConfig(const int argc, char **const argv);
   bool exportConfigToJsonFile(std::string save_path);
};


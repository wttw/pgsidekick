#include <getopt.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <time.h>

#include <libpq-fe.h>

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

#define EXIT_BADCONN 2

#define EXIT_USER 3

// Yay, C++ finger memory
#ifndef bool
typedef char bool;
#endif

#ifndef true
#define true    ((bool) 1)
#endif

#ifndef false
#define false   ((bool) 0)
#endif

#define PING_INTERVAL 300

// i18n
#define _(X) X

struct opts {
  char *dbname;
  char *host;
  char *port;
  char *username;
  bool no_password;
  bool verbose;
  char *listen;
};

const char *progname = "pglater";

static void parse_options(int argc, char *argv[], struct opts *options);
static void usage();
static void showVersion();

int main(int argc, char *argv[])
{
  struct opts options;
  char *password = NULL;
  char *password_prompt = NULL;
  PGconn *db;
  bool new_pass;
  PGresult *res;
  PGnotify *notify;

  parse_options(argc, argv, &options);

  if (options.username == NULL) {
    password_prompt = strdup("Password: ");
  } else {
    char *prompt_template = "Password for user %s: ";
    password_prompt = malloc(strlen(prompt_template) - 2 
      + strlen(options.username) - 1);
    sprintf(password_prompt, prompt_template, options.username);
  }

  do {
#define PARAMS_ARRAY_SIZE 7
    const char **keywords = malloc(PARAMS_ARRAY_SIZE * sizeof(*keywords));
    const char **values = malloc(PARAMS_ARRAY_SIZE * sizeof(*values));

    keywords[0] = "host";
    values[0] = options.host;
    keywords[1] = "port";
    values[1] = options.port;
    keywords[2] = "user";
    values[2] = options.username;
    keywords[3] = "password";
    values[3] = password;
    keywords[4] = "dbname";
    values[4] = options.dbname;
    keywords[5] = "fallback_application_name";
    values[5] = progname;
    keywords[6] = NULL;
    values[6] = NULL;

    new_pass = false;
    db = PQconnectdbParams(keywords, values, 1);
    free(keywords);
    free(values);

    if (PQstatus(db) == CONNECTION_BAD &&
      PQconnectionNeedsPassword(db) &&
      !options.no_password) {
      PQfinish(db);
      if (password != NULL) {
        free(password);
      }
      password = getpass(password_prompt);
    }
  } while(new_pass);

  free(password);
  free(password_prompt);

  if (PQstatus(db) == CONNECTION_BAD) {
    fprintf(stderr, "%s: %s", progname, PQerrorMessage(db));
    PQfinish(db);
    exit(EXIT_BADCONN);
  }

  if (options.listen == NULL) {
    options.listen = "pglater";
  }

  if (options.verbose) {
    fprintf(stderr, "Listening on channel %s\n", options.listen);
  }
  char *id = PQescapeIdentifier(db, options.listen, strlen(options.listen));
  if (id == NULL) {
    fprintf(stderr, "Failed to escape '%s': %s\n", 
      options.listen, PQerrorMessage(db));
    PQfinish(db);
    exit(EXIT_FAILURE);
  }

  char *listen = malloc(strlen(id) + 7);
  sprintf(listen, "listen %s", id);
  PQfreemem(id);
  res = PQexec(db, listen);
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    fprintf(stderr, "Listen command failed: %s\n", PQerrorMessage(db));
    PQclear(res);
    PQfinish(db);
    exit(EXIT_FAILURE);
  }
  PQclear(res);
  
  time_t callback_time = 0;
  char *callback_command = 0;
  for (;;) {
    int sock;
    fd_set input_mask;
    struct timeval timeout;

    sock = PQsocket(db);
    if (sock < 0) {
      fprintf(stderr, "Failed to retrieve socket\n");
      PQfinish(db);
      exit(EXIT_FAILURE);
    }

    FD_ZERO(&input_mask);
    FD_SET(sock, &input_mask);
    time_t now = time(0);
    if (callback_time == 0 || callback_time > now + PING_INTERVAL) {
      timeout.tv_sec = PING_INTERVAL;
    } else if(callback_time <= now) {
      timeout.tv_sec = 0;
    } else {
      timeout.tv_sec = callback_time - now;
    }
    timeout.tv_usec = 0;
    if (options.verbose) {
      fprintf(stderr, "Waiting for up to %ld seconds\n", timeout.tv_sec);
    }
    switch (select(sock + 1, &input_mask, NULL, NULL, &timeout)) {
      case -1:
        /* error */
        fprintf(stderr, "select() failed: %s\n", strerror(errno));
        PQfinish(db);
        exit(EXIT_FAILURE);
        break;
      case 0:
        /* timeout */
        now = time(0);
        if (callback_time && callback_time <= now) {
          if (options.verbose) {
            fprintf(stderr, "Calling %s\n", callback_command);
          }
          res = PQexec(db, callback_command);
          if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "Callback command failed: %s\nCommand: %s\n", PQerrorMessage(db), callback_command);
            PQfinish(db);
            exit(EXIT_FAILURE);
          }
          PQclear(res);
          callback_time = 0;
          free(callback_command);
          callback_command = 0;
        } else {
          if (options.verbose) {
            fprintf(stderr, "Pinging database\n");
          }
          res = PQexec(db, "select 1");
          if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "Ping command failed: %s\n", PQerrorMessage(db));
            PQfinish(db);
            exit(EXIT_BADCONN);
          }
          PQclear(res);
        }
        break;
      default:
        
      break;
    }
    PQconsumeInput(db);
    while ((notify = PQnotifies(db)) != NULL) {
      int callback_delay;
      int callback_starts;
      if (options.verbose) {
        fprintf(stderr, "Received notification: %s\n", notify->extra);
      }
      if (1 == sscanf(notify->extra, "%d %n", &callback_delay, &callback_starts)) {
        if (callback_command) {
          free(callback_command);
        }
        callback_command = strdup(notify->extra + callback_starts);
        callback_time = time(0) + callback_delay;
      } else {
        fprintf(stderr, "Unable to parse notification '%s'\n", notify->extra);
        PQfinish(db);
        exit(EXIT_FAILURE);
      }
      PQfreemem(notify);
    }
  }
}

static void parse_options(int argc, char *argv[], struct opts * options)
{
  static struct option long_options[] =
  {
      {"dbname", required_argument, NULL, 'd'},
      {"host", required_argument, NULL, 'h'},
      {"listen", required_argument, NULL, 'l'},
      {"port", required_argument, NULL, 'p'},
      {"quiet", no_argument, NULL, 'q'},
      {"verbose", no_argument, NULL, 'v'},
      {"username", required_argument, NULL, 'U'},
      {"version", no_argument, NULL, 'V'},
      {"no-password", no_argument, NULL, 'w'},
      {"help", no_argument, NULL, '?'}
  };
  int optindex;
  extern char *optarg;
  extern int optind;
  int c;

  memset(options, 0, sizeof *options);

  while ((c = getopt_long(argc, argv, "d:h:l:p:qU:Vvw?", long_options, &optindex)) != -1) {
    switch (c)
    {
      case 'd':
        options->dbname = optarg;
        break;
      case 'h':
        options->host = optarg;
        break;
      case 'l':
        options->listen = optarg;
        break;
      case 'p':
        options->port = optarg;
        break;
      case 'q':
        break;
      case 'U':
        options->username = optarg;
        break;
      case 'V':
        showVersion();
        exit(EXIT_SUCCESS);
        break;
      case 'v':
        options->verbose = true;
        break;
      case 'w':
        options->no_password = true;
        break;
      case '?':
        if (strcmp(argv[optind-1], "-?") == 0 
          || strcmp(argv[optind-1], "--help") == 0) {
          usage();
          exit(EXIT_SUCCESS);
        }
        /* FALLTHROUGH */
      default:
        fprintf(stderr, "Try \"%s --help\" for more information.\n",
                                                        progname);
        exit(EXIT_FAILURE);
    }
  }
}

static void usage()
{
  printf(_("Usage:\n"));
  printf(_("  %s [OPTION]... [DBNAME [USERNAME]]\n\n"), progname);
  printf(_("  -l, --listen=CHANNEL     listen for notifications on this channel (default pglater)\n"));
  printf(_("      --help               show this help, then exit\n"));
  printf(_("  -v, --verbose            log activity to stderr\n"));
  printf(_("      --version            output version information, then exit\n"));

  printf(_("\nConnection options:\n"));
  printf(_("  -h, --host=HOSTNAME      database server host or socket directory\n"));
  printf(_("  -p, --port=PORT          database server port\n"));
  printf(_("  -U, --username=USERNAME  database user name\n"));
  printf(_("  -d, --dbname=DBNAME      database name to connect to\n"));
  printf(_("  -w, --no-password        never prompt for password\n"));
}

static void showVersion()
{
  printf(_("Version goes here\n"));
}

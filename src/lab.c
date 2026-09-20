#define _POSIX_C_SOURCE 200809L

#include "lab.h"
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_PORT "25"
#define DEFAULT_HELO "localhost"

static void print_usage(FILE *stream)
{
  fprintf(stream,
          "Usage: myapp -f <from> -t <to> [-s subject] [-b body] [-p port]\n"
          "          [-H helo-host] <server>\n\n"
          "  -f <from>       envelope sender, for example you@example.com\n"
          "  -t <to>         envelope recipient\n"
          "  -s <subject>    subject line (default: empty)\n"
          "  -b <body>       message body (default: read from stdin)\n"
          "  -p <port>       port or service name (default: 25)\n"
          "  -H <helo-host>  host name sent with HELO (default: localhost)\n"
          "  <server>        host name or address of the mail server\n");
}

static int has_line_break(const char *value)
{
  return value != NULL &&
         (strchr(value, '\r') != NULL || strchr(value, '\n') != NULL);
}

static int append_data(char **buffer, size_t *length, size_t *capacity,
                       const char *data, size_t data_length)
{
  size_t required;
  size_t new_capacity;
  char *new_buffer;

  if (data_length > SIZE_MAX - *length - 1U)
  {
    return -1;
  }
  required = *length + data_length + 1U;
  if (required > *capacity)
  {
    new_capacity = *capacity == 0U ? 256U : *capacity;
    while (new_capacity < required)
    {
      if (new_capacity > SIZE_MAX / 2U)
      {
        new_capacity = required;
        break;
      }
      new_capacity *= 2U;
    }
    new_buffer = realloc(*buffer, new_capacity);
    if (new_buffer == NULL)
    {
      return -1;
    }
    *buffer = new_buffer;
    *capacity = new_capacity;
  }
  memcpy(*buffer + *length, data, data_length);
  *length += data_length;
  (*buffer)[*length] = '\0';
  return 0;
}

static char *read_stdin_body(void)
{
  char *body = NULL;
  size_t length = 0U;
  size_t capacity = 0U;
  char chunk[512];
  size_t count;

  while ((count = fread(chunk, 1U, sizeof(chunk), stdin)) > 0U)
  {
    if (append_data(&body, &length, &capacity, chunk, count) != 0)
    {
      free(body);
      return NULL;
    }
  }
  if (ferror(stdin) != 0)
  {
    free(body);
    return NULL;
  }
  if (body == NULL)
  {
    // If no data was read, return an empty string instead of NULL
    body = calloc(1U, 1U);
  }
  return body;
}

static char *make_command(const char *prefix, const char *value)
{
  int length = snprintf(NULL, 0, "%s%s", prefix, value);
  char *command;

  if (length < 0)
  {
    return NULL;
  }
  command = malloc((size_t)length + 1U);
  if (command == NULL)
  {
    return NULL;
  }
  if (snprintf(command, (size_t)length + 1U, "%s%s", prefix, value) != length)
  {
    free(command);
    return NULL;
  }
  return command;
}

static char *build_data(const char *from, const char *to, const char *subject,
                        const char *body)
{
  char *payload = NULL;
  size_t length = 0U;
  size_t capacity = 0U;
  const char *line_start = body;
  const char *cursor = body;
  size_t line_length;

  if (append_data(&payload, &length, &capacity, "From: <", 7U) != 0 ||
      append_data(&payload, &length, &capacity, from, strlen(from)) != 0 ||
      append_data(&payload, &length, &capacity, ">\r\nTo: <", 8U) != 0 ||
      append_data(&payload, &length, &capacity, to, strlen(to)) != 0 ||
      append_data(&payload, &length, &capacity, ">\r\nSubject: ", 12U) != 0 ||
      append_data(&payload, &length, &capacity, subject, strlen(subject)) != 0 ||
      append_data(&payload, &length, &capacity, "\r\n\r\n", 4U) != 0)
  {
    free(payload);
    return NULL;
  }

  while (*cursor != '\0')
  {
    if (line_start == cursor && *cursor == '.' &&
        append_data(&payload, &length, &capacity, ".", 1U) != 0)
    {
      free(payload);
      return NULL;
    }
    if (*cursor == '\n')
    {
      line_length = (size_t)(cursor - line_start);
      if (line_length > 0U && line_start[line_length - 1U] == '\r')
      {
        line_length--;
      }
      if (append_data(&payload, &length, &capacity, line_start, line_length) != 0 ||
          append_data(&payload, &length, &capacity, "\r\n", 2U) != 0)
      {
        free(payload);
        return NULL;
      }
      cursor++;
      line_start = cursor;
    }
    else
    {
      cursor++;
    }
  }
  if (cursor != line_start)
  {
    line_length = (size_t)(cursor - line_start);
    if (line_length > 0U && line_start[line_length - 1U] == '\r')
    {
      line_length--;
    }
    if (append_data(&payload, &length, &capacity, line_start, line_length) != 0 ||
        append_data(&payload, &length, &capacity, "\r\n", 2U) != 0)
    {
      free(payload);
      return NULL;
    }
  }
  if (append_data(&payload, &length, &capacity, ".\r\n", 3U) != 0)
  {
    free(payload);
    return NULL;
  }
  return payload;
}

static int send_all(int socket_fd, const char *data, size_t length)
{
  size_t sent = 0U;
  ssize_t result;

  while (sent < length)
  {
    result = send(socket_fd, data + sent, length - sent, 0);
    if (result <= 0)
    {
      return -1;
    }
    sent += (size_t)result;
  }
  return 0;
}

static int read_line(int socket_fd, char **line)
{
  char *result = NULL;
  size_t length = 0U;
  size_t capacity = 0U;
  char character;
  ssize_t received;

  for (;;)
  {
    received = recv(socket_fd, &character, 1U, 0);
    if (received <= 0)
    {
      free(result);
      return -1;
    }
    if (append_data(&result, &length, &capacity, &character, 1U) != 0)
    {
      free(result);
      return -1;
    }
    if (character == '\n')
    {
      if (length >= 2U && result[length - 2U] == '\r')
      {
        result[length - 2U] = '\0';
      }
      else
      {
        result[length - 1U] = '\0';
      }
      *line = result;
      return 0;
    }
  }
}

static int read_reply(int socket_fd, char **last_line)
{
  char *line = NULL;
  char *previous = NULL;
  char *end;
  long code;

  for (;;)
  {
    if (read_line(socket_fd, &line) != 0)
    {
      free(previous);
      return -1;
    }
    if (strlen(line) < 4U || (line[3] != '-' && line[3] != ' '))
    {
      free(previous);
      *last_line = line;
      return -2;
    }
    errno = 0;
    code = strtol(line, &end, 10);
    if (errno != 0 || end != line + 3 || code < 100L || code > 599L)
    {
      free(previous);
      *last_line = line;
      return -2;
    }
    free(previous);
    previous = line;
    line = NULL;
    if (previous[3] == ' ')
    {
      *last_line = previous;
      return (int)code;
    }
  }
}

static int expect_reply(int socket_fd, int expected, const char *step)
{
  char *reply = NULL;
  int code = read_reply(socket_fd, &reply);

  if (code < 0)
  {
    if (reply != NULL)
    {
      fprintf(stderr, "SMTP error after %s: malformed reply: %s\n", step, reply);
    }
    else
    {
      fprintf(stderr, "SMTP error after %s: connection closed while reading reply\n", step);
    }
    free(reply);
    return -1;
  }
  if (code != expected)
  {
    fprintf(stderr, "SMTP error after %s: expected %d, got %d: %s\n",
            step, expected, code, reply);
    free(reply);
    return -1;
  }
  free(reply);
  return 0;
}

static int send_command(int socket_fd, const char *command, int expected)
{
  size_t length = strlen(command);
  char *wire_command = malloc(length + 3U);
  int result;

  if (wire_command == NULL)
  {
    return -1;
  }
  memcpy(wire_command, command, length);
  memcpy(wire_command + length, "\r\n", 3U);
  result = send_all(socket_fd, wire_command, length + 2U);
  free(wire_command);
  if (result != 0)
  {
    return -1;
  }
  return expect_reply(socket_fd, expected, command);
}

static int connect_to_server(const char *server, const char *port)
{
  struct addrinfo hints;
  struct addrinfo *addresses = NULL;
  struct addrinfo *address;
  int socket_fd = -1;
  int result;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  result = getaddrinfo(server, port, &hints, &addresses);
  if (result != 0)
  {
    fprintf(stderr, "Could not resolve %s:%s: %s\n", server, port, gai_strerror(result));
    return -1;
  }
  for (address = addresses; address != NULL; address = address->ai_next)
  {
    socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (socket_fd < 0)
    {
      continue;
    }
    if (connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0)
    {
      break;
    }
    close(socket_fd);
    socket_fd = -1;
  }
  if (socket_fd < 0)
  {
    fprintf(stderr, "Could not connect to %s:%s: %s\n", server, port, strerror(errno));
  }
  freeaddrinfo(addresses);
  return socket_fd;
}

char *get_greeting(const char *restrict name)
{
  if (name == NULL)
  {
    return NULL;
  }

  // Allocate memory for the greeting message
  int length = snprintf(NULL, 0, "Hello, %s!", name);
  if (length < 0) // GCOVR_EXCL_START
  {
    return NULL; // snprintf failed
  } // GCOVR_EXCL_STOP

  //Casting is safe here because we know length is non-negative
  size_t alloc_size = (size_t) length + 1; // +1 for the null terminator
  char *greeting = malloc( alloc_size);


  if (greeting == NULL) // GCOVR_EXCL_START
  {
    return NULL; // Memory allocation failed
  }  // GCOVR_EXCL_STOP


  // Create the greeting message
  snprintf(greeting, alloc_size, "Hello, %s!", name);

  return greeting;
}

struct smtp_options
{
  const char *from;
  const char *to;
  const char *subject;
  const char *body_argument;
  const char *port;
  const char *helo_host;
  const char *server;
};

static int parse_options(int argc, char **argv, struct smtp_options *options)
{
  int option;

  options->from = NULL;
  options->to = NULL;
  options->subject = "";
  options->body_argument = NULL;
  options->port = DEFAULT_PORT;
  options->helo_host = DEFAULT_HELO;

  opterr = 0;
  optind = 1;
  while ((option = getopt(argc, argv, "f:t:s:b:p:H:")) != -1)
  {
    switch (option)
    {
      case 'f':
        options->from = optarg;
        break;
      case 't':
        options->to = optarg;
        break;
      case 's':
        options->subject = optarg;
        break;
      case 'b':
        options->body_argument = optarg;
        break;
      case 'p':
        options->port = optarg;
        break;
      case 'H':
        options->helo_host = optarg;
        break;
      default:
        return -1;
    }
  }
  if (options->from == NULL || options->to == NULL || optind != argc - 1 ||
      has_line_break(options->from) != 0 || has_line_break(options->to) != 0 ||
      has_line_break(options->subject) != 0 ||
      has_line_break(options->helo_host) != 0)
  {
    return -1;
  }
  options->server = argv[optind];
  return 0;
}

static int prepare_message(const struct smtp_options *options,
                           char **body, char **payload)
{
  if (options->body_argument != NULL)
  {
    *body = strdup(options->body_argument);
  }
  else
  {
    *body = read_stdin_body();
  }
  if (*body == NULL)
  {
    fprintf(stderr, "Could not read the message body.\n");
    return -1;
  }
  *payload = build_data(options->from, options->to, options->subject, *body); // MLS Refactor
  if (*payload == NULL)
  {
    fprintf(stderr, "Could not build the message.\n");
    free(*body);
    *body = NULL;
    return -1;
  }
  return 0;
}

static char *make_address_command(const char *prefix, const char *address)
{
  char *command = make_command(prefix, address);
  size_t command_length;
  char *resized_command;

  if (command == NULL)
  {
    return NULL;
  }
  command_length = strlen(command);
  resized_command = realloc(command, command_length + 2U);
  if (resized_command == NULL)
  {
    free(command);
    return NULL;
  }
  memcpy(resized_command + command_length, ">", 2U);
  return resized_command;
}

static int run_smtp_session(int socket_fd, const struct smtp_options *options,
                            const char *payload)
{
  char *command = NULL;
  int result = -1;

  if (expect_reply(socket_fd, 220, "connection greeting") != 0)
  {
    goto cleanup;
  }
  command = make_command("HELO ", options->helo_host);
  if (command == NULL || send_command(socket_fd, command, 250) != 0)
  {
    fprintf(stderr, "SMTP error while sending HELO.\n");
    goto cleanup;
  }
  free(command);
  command = make_address_command("MAIL FROM:<", options->from);
  if (command == NULL || send_command(socket_fd, command, 250) != 0)
  {
    fprintf(stderr, "SMTP error while sending MAIL FROM.\n");
    goto cleanup;
  }
  free(command);
  command = make_address_command("RCPT TO:<", options->to);
  if (command == NULL || send_command(socket_fd, command, 250) != 0)
  {
    fprintf(stderr, "SMTP error while sending RCPT TO.\n");
    goto cleanup;
  }
  free(command);
  command = NULL;
  if (send_command(socket_fd, "DATA", 354) != 0 ||
      send_all(socket_fd, payload, strlen(payload)) != 0 ||
      expect_reply(socket_fd, 250, "message data") != 0)
  {
    fprintf(stderr, "SMTP error while sending message data.\n");
    goto cleanup;
  }
  if (send_command(socket_fd, "QUIT", 221) != 0)
  {
    fprintf(stderr, "SMTP error while sending QUIT.\n");
    goto cleanup;
  }
  result = 0;

cleanup:
  free(command);
  close(socket_fd);
  return result;
}

int smtp_client_run(int argc, char **argv)
{
  struct smtp_options options;
  char *body = NULL;
  char *payload = NULL;
  int socket_fd = -1;
  int result;

  if (argc == 1)
  {
    print_usage(stdout);
    return 0;
  }

  if (parse_options(argc, argv, &options) != 0)
  {
    fprintf(stderr, "Invalid command line or a value contains CR/LF.\n");
    print_usage(stderr);
    return 1;
  }

  if (prepare_message(&options, &body, &payload) != 0)
  {
    return 2;
  }
  socket_fd = connect_to_server(options.server, options.port);
  if (socket_fd < 0)
  {
    result = 2;
  }
  else
  {
    result = run_smtp_session(socket_fd, &options, payload) == 0 ? 0 : 2;
  }
  free(payload);
  free(body);
  return result;
}

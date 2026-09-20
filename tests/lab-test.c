#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "harness/unity.h"
#include "../src/lab.h"

struct scripted_transport
{
  const char *input;
  size_t input_length;
  size_t input_offset;
  size_t maximum_read;
  char output[4096];
  size_t output_length;
};

static ssize_t scripted_read(void *context, void *buffer, size_t length)
{
  struct scripted_transport *script = context;
  size_t remaining = script->input_length - script->input_offset;
  size_t amount = length < remaining ? length : remaining;

  if (amount > script->maximum_read)
  {
    amount = script->maximum_read;
  }
  if (amount == 0U)
  {
    return 0;
  }
  memcpy(buffer, script->input + script->input_offset, amount);
  script->input_offset += amount;
  return (ssize_t)amount;
}

static ssize_t scripted_write(void *context, const void *buffer, size_t length)
{
  struct scripted_transport *script = context;

  if (length > sizeof(script->output) - script->output_length)
  {
    return -1;
  }
  memcpy(script->output + script->output_length, buffer, length);
  script->output_length += length;
  return (ssize_t)length;
}

static ssize_t failing_write(void *context, const void *buffer, size_t length)
{
  (void)context;
  (void)buffer;
  (void)length;
  return -1;
}

static struct smtp_transport make_scripted_transport(struct scripted_transport *script)
{
  struct smtp_transport transport = {
    scripted_read,
    scripted_write,
    script
  };
  return transport;
}

void setUp(void)
{
  printf("Setting up tests...\n");
}

void tearDown(void)
{
  printf("Tearing down tests...\n");
}

void test_get_greeting(void)
{
  char *greeting = get_greeting("Alice");
  TEST_ASSERT_NOT_NULL(greeting);
  TEST_ASSERT_EQUAL_STRING("Hello, Alice!", greeting);
  free(greeting);
  greeting = get_greeting(NULL);
  TEST_ASSERT_NULL(greeting);
  greeting = get_greeting("");
  TEST_ASSERT_NOT_NULL(greeting);
  TEST_ASSERT_EQUAL_STRING("Hello, !", greeting);
  free(greeting);
}

void test_smtp_read_line_fragmented(void)
{
  struct scripted_transport script = {"250 hello\r\n", 11U, 0U, 1U, {0}, 0U};
  struct smtp_transport transport = make_scripted_transport(&script);
  char *line = NULL;

  TEST_ASSERT_EQUAL_INT(0, smtp_read_line(&transport, &line));
  TEST_ASSERT_EQUAL_STRING("250 hello", line);
  free(line);
}

void test_smtp_read_line_lf_only(void)
{
  const char input[] = "250 hello\n";
  struct scripted_transport script = {input, sizeof(input) - 1U, 0U, 1U, {0}, 0U};
  struct smtp_transport transport = make_scripted_transport(&script);
  char *line = NULL;

  TEST_ASSERT_EQUAL_INT(0, smtp_read_line(&transport, &line));
  TEST_ASSERT_EQUAL_STRING("250 hello", line);
  free(line);
}

void test_smtp_read_line_oversized(void)
{
  char input[2048];
  struct scripted_transport script;
  struct smtp_transport transport;
  char *line = NULL;

  memset(input, 'x', sizeof(input));
  memcpy(input, "250 ", 4U);
  input[sizeof(input) - 2U] = '\r';
  input[sizeof(input) - 1U] = '\n';
  script.input = input;
  script.input_length = sizeof(input);
  script.input_offset = 0U;
  script.maximum_read = 1U;
  script.output_length = 0U;
  transport = make_scripted_transport(&script);

  TEST_ASSERT_EQUAL_INT(0, smtp_read_line(&transport, &line));
  TEST_ASSERT_EQUAL_UINT(sizeof(input) - 2U, strlen(line));
  free(line);
}

void test_smtp_read_reply_multiline(void)
{
  const char input[] = "250-first\r\n250-second\r\n250 final\r\n";
  struct scripted_transport script = {input, sizeof(input) - 1U, 0U, 2U, {0}, 0U};
  struct smtp_transport transport = make_scripted_transport(&script);
  char *line = NULL;

  TEST_ASSERT_EQUAL_INT(250, smtp_read_reply(&transport, &line));
  TEST_ASSERT_EQUAL_STRING("250 final", line);
  free(line);
}

void test_smtp_read_reply_disconnect(void)
{
  const char input[] = "250-incomplete\r\n";
  struct scripted_transport script = {input, sizeof(input) - 1U, 0U, 1U, {0}, 0U};
  struct smtp_transport transport = make_scripted_transport(&script);
  char *line = NULL;

  TEST_ASSERT_EQUAL_INT(-1, smtp_read_reply(&transport, &line));
  TEST_ASSERT_NULL(line);
}

void test_smtp_read_reply_malformed(void)
{
  const char input[] = "not a reply\r\n";
  struct scripted_transport script = {input, sizeof(input) - 1U, 0U, 16U, {0}, 0U};
  struct smtp_transport transport = make_scripted_transport(&script);
  char *line = NULL;

  TEST_ASSERT_EQUAL_INT(-2, smtp_read_reply(&transport, &line));
  TEST_ASSERT_EQUAL_STRING("not a reply", line);
  free(line);
}

void test_smtp_read_reply_invalid_code(void)
{
  const char input[] = "999 invalid\r\n";
  struct scripted_transport script = {input, sizeof(input) - 1U, 0U, 16U, {0}, 0U};
  struct smtp_transport transport = make_scripted_transport(&script);
  char *line = NULL;

  TEST_ASSERT_EQUAL_INT(-2, smtp_read_reply(&transport, &line));
  TEST_ASSERT_EQUAL_STRING("999 invalid", line);
  free(line);
}

void test_smtp_send_command(void)
{
  const char input[] = "250 accepted\r\n";
  struct scripted_transport script = {input, sizeof(input) - 1U, 0U, 1U, {0}, 0U};
  struct smtp_transport transport = make_scripted_transport(&script);

  TEST_ASSERT_EQUAL_INT(0, smtp_send_command(&transport, "HELO host", 250));
  script.output[script.output_length] = '\0';
  TEST_ASSERT_EQUAL_STRING("HELO host\r\n", script.output);
}

void test_smtp_send_command_write_failure(void)
{
  const char input[] = "250 accepted\r\n";
  struct scripted_transport script = {input, sizeof(input) - 1U, 0U, 16U, {0}, 0U};
  struct smtp_transport transport = {scripted_read, failing_write, &script};

  TEST_ASSERT_EQUAL_INT(-1, smtp_send_command(&transport, "HELO host", 250));
}

void test_smtp_send_command_malformed_reply(void)
{
  const char input[] = "250x malformed\r\n";
  struct scripted_transport script = {input, sizeof(input) - 1U, 0U, 16U, {0}, 0U};
  struct smtp_transport transport = make_scripted_transport(&script);

  TEST_ASSERT_EQUAL_INT(-1, smtp_send_command(&transport, "HELO host", 250));
}

void test_smtp_send_command_disconnected_reply(void)
{
  struct scripted_transport script = {NULL, 0U, 0U, 16U, {0}, 0U};
  struct smtp_transport transport = make_scripted_transport(&script);

  TEST_ASSERT_EQUAL_INT(-1, smtp_send_command(&transport, "HELO host", 250));
}

static struct smtp_options test_options(void)
{
  struct smtp_options options = {
    "sender@example.com", "recipient@example.com", "subject", NULL,
    "25", "test-host", "server"
  };
  return options;
}

static void append_response(char *responses, size_t *length, int code)
{
  int written = snprintf(responses + *length, 256U, "%d response\r\n", code);
  *length += (size_t)written;
}

void test_smtp_session_happy_path(void)
{
  const char payload[] = "From: sender\r\n\r\nbody\r\n.\r\n";
  char responses[2048] = {0};
  size_t response_length = 0U;
  struct scripted_transport script;
  struct smtp_transport transport;
  struct smtp_options options = test_options();

  append_response(responses, &response_length, 220);
  append_response(responses, &response_length, 250);
  append_response(responses, &response_length, 250);
  append_response(responses, &response_length, 250);
  append_response(responses, &response_length, 354);
  append_response(responses, &response_length, 250);
  append_response(responses, &response_length, 221);
  script.input = responses;
  script.input_length = response_length;
  script.input_offset = 0U;
  script.maximum_read = 3U;
  script.output_length = 0U;
  transport = make_scripted_transport(&script);

  TEST_ASSERT_EQUAL_INT(0, smtp_session_run(&transport, &options, payload));
  script.output[script.output_length] = '\0';
  TEST_ASSERT_NOT_NULL(strstr(script.output, "HELO test-host\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(script.output, "MAIL FROM:<sender@example.com>\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(script.output, "RCPT TO:<recipient@example.com>\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(script.output, "DATA\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(script.output, "QUIT\r\n"));
}

void test_smtp_session_wrong_status_codes(void)
{
  const char payload[] = "payload\r\n.\r\n";
  struct smtp_options options = test_options();
  const int expected_codes[] = {220, 250, 250, 250, 354, 250, 221};
  int wrong_step;

  for (wrong_step = 0; wrong_step < 7; wrong_step++)
  {
    char responses[2048] = {0};
    size_t response_length = 0U;
    struct scripted_transport script;
    struct smtp_transport transport;
    int response_index;

    for (response_index = 0; response_index < 7; response_index++)
    {
      int response_code = response_index == wrong_step ? 550 : expected_codes[response_index];
      append_response(responses, &response_length, response_code);
    }
    script.input = responses;
    script.input_length = response_length;
    script.input_offset = 0U;
    script.maximum_read = 16U;
    script.output_length = 0U;
    transport = make_scripted_transport(&script);

    TEST_ASSERT_EQUAL_INT(-1, smtp_session_run(&transport, &options, payload));
  }
}

void test_smtp_socket_transport(void)
{
  int sockets[2];
  char buffer[8] = {0};
  const char message[] = "hello";
  int socket_fd;

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  socket_fd = sockets[0];
  TEST_ASSERT_EQUAL_INT(5, (int)smtp_socket_write(&socket_fd, message, 5U));
  TEST_ASSERT_EQUAL_INT(5, (int)read(sockets[1], buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("hello", buffer);
  TEST_ASSERT_EQUAL_INT(5, (int)write(sockets[1], message, 5U));
  memset(buffer, 0, sizeof(buffer));
  TEST_ASSERT_EQUAL_INT(5, (int)smtp_socket_read(&socket_fd, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("hello", buffer);
  smtp_socket_close(sockets[0]);
  smtp_socket_close(sockets[1]);
}

void test_smtp_socket_connect_failure(void)
{
  int socket_fd = -1;
  struct smtp_transport transport = {0};

  TEST_ASSERT_EQUAL_INT(-1, smtp_socket_connect("invalid.invalid", "2525",
                                                &socket_fd, &transport));
}

void test_smtp_client_argument_paths(void)
{
  char *no_arguments[] = {"myapp", NULL};
  char *invalid_arguments[] = {"myapp", "-f", "sender@example.com", NULL};
  char *unknown_option[] = {"myapp", "-z", NULL};
  char *stdin_arguments[] = {
    "myapp", "-f", "sender@example.com", "-t", "recipient@example.com",
    "invalid.invalid", NULL
  };
  int input_pipe[2];
  int saved_stdin;

  TEST_ASSERT_EQUAL_INT(0, smtp_client_run(1, no_arguments));
  TEST_ASSERT_EQUAL_INT(1, smtp_client_run(3, invalid_arguments));
  TEST_ASSERT_EQUAL_INT(1, smtp_client_run(2, unknown_option));
  TEST_ASSERT_EQUAL_INT(0, pipe(input_pipe));
  TEST_ASSERT_EQUAL_INT(5, (int)write(input_pipe[1], "body\n", 5U));
  close(input_pipe[1]);
  saved_stdin = dup(STDIN_FILENO);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(0, saved_stdin);
  TEST_ASSERT_EQUAL_INT(0, dup2(input_pipe[0], STDIN_FILENO));
  close(input_pipe[0]);
  TEST_ASSERT_EQUAL_INT(2, smtp_client_run(6, stdin_arguments));
  TEST_ASSERT_EQUAL_INT(0, dup2(saved_stdin, STDIN_FILENO));
  close(saved_stdin);
}

static int read_socket_line(int socket_fd, char *line, size_t capacity)
{
  size_t length = 0U;
  char character;

  while (length + 1U < capacity)
  {
    if (read(socket_fd, &character, 1U) != 1)
    {
      return -1;
    }
    line[length++] = character;
    if (character == '\n')
    {
      line[length] = '\0';
      return 0;
    }
  }
  return -1;
}

static int run_loopback_server(int listen_fd)
{
  int client_fd;
  char line[256];
  char data[2048] = {0};
  size_t data_length = 0U;
  const char *responses[] = {
    "220 local\r\n",
    "250 hello\r\n",
    "250 sender\r\n",
    "250 recipient\r\n",
    "354 data\r\n",
    "250 queued\r\n",
    "221 bye\r\n"
  };
  size_t response_index;

  client_fd = accept(listen_fd, NULL, NULL);
  if (client_fd < 0)
  {
    return 1;
  }
  if (write(client_fd, responses[0], strlen(responses[0])) < 0)
  {
    close(client_fd);
    return 1;
  }
  for (response_index = 1U; response_index < 5U; response_index++)
  {
    if (read_socket_line(client_fd, line, sizeof(line)) != 0 ||
        write(client_fd, responses[response_index], strlen(responses[response_index])) < 0)
    {
      close(client_fd);
      return 1;
    }
  }
  while (data_length + 1U < sizeof(data))
  {
    if (read(client_fd, data + data_length, 1U) != 1)
    {
      close(client_fd);
      return 1;
    }
    data_length++;
    data[data_length] = '\0';
    if (data_length >= 5U && strcmp(data + data_length - 5U, "\r\n.\r\n") == 0)
    {
      break;
    }
  }
  if (write(client_fd, responses[5], strlen(responses[5])) < 0 ||
      read_socket_line(client_fd, line, sizeof(line)) != 0 ||
      write(client_fd, responses[6], strlen(responses[6])) < 0)
  {
    close(client_fd);
    return 1;
  }
  close(client_fd);
  return 0;
}

void test_smtp_client_loopback(void)
{
  int listen_fd;
  int child_status;
  int port;
  pid_t child_pid;
  struct sockaddr_in address;
  char port_text[16];
  char *arguments[15];

  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(0, listen_fd);
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(0U);
  TEST_ASSERT_EQUAL_INT(0, bind(listen_fd, (struct sockaddr *)&address, sizeof(address)));
  TEST_ASSERT_EQUAL_INT(0, listen(listen_fd, 1));
  {
    socklen_t address_length = sizeof(address);
    TEST_ASSERT_EQUAL_INT(0, getsockname(listen_fd, (struct sockaddr *)&address, &address_length));
  }
  port = (int)ntohs(address.sin_port);
  TEST_ASSERT_GREATER_THAN_INT(0, port);
  (void)snprintf(port_text, sizeof(port_text), "%d", port);
  child_pid = fork();
  TEST_ASSERT_GREATER_OR_EQUAL_INT(0, child_pid);
  if (child_pid == 0)
  {
    int server_result = run_loopback_server(listen_fd);
    close(listen_fd);
    _exit(server_result);
  }
  close(listen_fd);
  arguments[0] = "myapp";
  arguments[1] = "-f";
  arguments[2] = "sender@example.com";
  arguments[3] = "-t";
  arguments[4] = "recipient@example.com";
  arguments[5] = "-s";
  arguments[6] = "subject";
  arguments[7] = "-b";
  arguments[8] = ".first\r\nsecond\r";
  arguments[9] = "-p";
  arguments[10] = port_text;
  arguments[11] = "-H";
  arguments[12] = "test-host";
  arguments[13] = "localhost";
  arguments[14] = NULL;
  TEST_ASSERT_EQUAL_INT(0, smtp_client_run(14, arguments));
  TEST_ASSERT_EQUAL_INT(child_pid, waitpid(child_pid, &child_status, 0));
  TEST_ASSERT_TRUE(WIFEXITED(child_status));
  TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(child_status));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_get_greeting);
  RUN_TEST(test_smtp_read_line_fragmented);
  RUN_TEST(test_smtp_read_line_lf_only);
  RUN_TEST(test_smtp_read_line_oversized);
  RUN_TEST(test_smtp_read_reply_multiline);
  RUN_TEST(test_smtp_read_reply_disconnect);
  RUN_TEST(test_smtp_read_reply_malformed);
  RUN_TEST(test_smtp_read_reply_invalid_code);
  RUN_TEST(test_smtp_send_command);
  RUN_TEST(test_smtp_send_command_write_failure);
  RUN_TEST(test_smtp_send_command_malformed_reply);
  RUN_TEST(test_smtp_send_command_disconnected_reply);
  RUN_TEST(test_smtp_session_happy_path);
  RUN_TEST(test_smtp_session_wrong_status_codes);
  RUN_TEST(test_smtp_socket_transport);
  RUN_TEST(test_smtp_socket_connect_failure);
  RUN_TEST(test_smtp_client_argument_paths);
  RUN_TEST(test_smtp_client_loopback);
  return UNITY_END();
}
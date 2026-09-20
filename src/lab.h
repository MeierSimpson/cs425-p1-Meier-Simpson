#ifndef LAB_H
#define LAB_H

#include <stddef.h>
#include <sys/types.h>

typedef ssize_t (*smtp_read_callback)(void *context, void *buffer, size_t length);
typedef ssize_t (*smtp_write_callback)(void *context, const void *buffer, size_t length);

struct smtp_transport
{
	smtp_read_callback read;
	smtp_write_callback write;
	void *context;
};

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

int smtp_read_line(const struct smtp_transport *transport, char **line);
int smtp_read_reply(const struct smtp_transport *transport, char **last_line);
int smtp_send_command(const struct smtp_transport *transport,
											const char *command, int expected);
int smtp_session_run(const struct smtp_transport *transport,
										 const struct smtp_options *options, const char *payload);

ssize_t smtp_socket_read(void *context, void *buffer, size_t length);
ssize_t smtp_socket_write(void *context, const void *buffer, size_t length);
int smtp_socket_connect(const char *server, const char *port,
												int *socket_fd, struct smtp_transport *transport);
void smtp_socket_close(int socket_fd);

/** * @brief Returns a greeting message.
 *
 * This function returns a string that contains a greeting message.
 * The string is allocated with malloc and should be freed by the caller.
 * @param name The name to include in the greeting.
 * @return A greeting string.
 */
char* get_greeting(const char* restrict name);

int smtp_client_run(int argc, char **argv);


#endif // LAB_H

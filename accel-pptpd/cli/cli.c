#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "triton.h"

#include "telnet.h"
#include "cli.h"
#include "log.h"

#define MAX_CMD_ITEMS 100
#define MSG_SYNTAX_ERROR "syntax error\r\n"
#define MSG_UNKNOWN_CMD "command unknown\r\n"

static LIST_HEAD(simple_cmd_list);
static LIST_HEAD(regexp_cmd_list);

void __export cli_register_simple_cmd(struct cli_simple_cmd_t *cmd)
{
	list_add_tail(&cmd->entry, &simple_cmd_list);
}

void __export cli_register_regexp_cmd(struct cli_regexp_cmd_t *cmd)
{
	int err;
	cmd->re = pcre_compile2(cmd->pattern, cmd->options, &err, NULL, NULL, NULL);
	if (!cmd->re) {
		log_emerg("cli: failed to compile regexp %s: %i\n", cmd->pattern, err);
		_exit(EXIT_FAILURE);
	}
	list_add_tail(&cmd->entry, &simple_cmd_list);
}

int __export cli_send(void *client, const char *data)
{
	struct client_t *cln = (struct client_t *)client;

	return telnet_send(cln, data, strlen(data));
}


static char *skip_word(char *ptr)
{
	for(; *ptr; ptr++)
		if (*ptr == ' ' || *ptr == '\t' || *ptr == '\n') 
			break;
	return ptr;
}
static char *skip_space(char *ptr)
{
	for(; *ptr; ptr++)
		if (*ptr != ' ' && *ptr != '\t')
			break;
	return ptr;
}
static int split(char *buf, char **ptr)
{
	int i;

	ptr[0] = buf;

	for (i = 1; i <= MAX_CMD_ITEMS; i++) {
		buf = skip_word(buf);
		if (!*buf)
			return i;
		
		*buf = 0;
		
		buf = skip_space(buf + 1);
		if (!*buf)
			return i;

		ptr[i] = buf;
	}

	buf = skip_word(buf);
	*buf = 0;

	return i;
}

int process_cmd(struct client_t *cln)
{
	struct cli_simple_cmd_t *cmd1;
	struct cli_regexp_cmd_t *cmd2;
	char *f[MAX_CMD_ITEMS];
	int r, i, n, found = 0;

	n = split((char *)cln->cmdline, f);

	if (n >= 1 && !strcmp(f[0], "help")) {
		list_for_each_entry(cmd1, &simple_cmd_list, entry) {
			if (cmd1->help && cmd1->help(f, n, cln))
				return -1;
		}
		list_for_each_entry(cmd2, &regexp_cmd_list, entry) {
			if (cmd2->help && cmd1->help(f, n, cln))
				return -1;
		}

		return 0;
	}

	list_for_each_entry(cmd1, &simple_cmd_list, entry) {
		if (cmd1->hdr_len && n >= cmd1->hdr_len) {
			for (i = 0; i < cmd1->hdr_len; i++) {
				if (strcmp(cmd1->hdr[i], f[i]))
					break;
			}
			if (i < cmd1->hdr_len)
				continue;
			r = cmd1->exec((char *)cln->cmdline, f, n, cln);
			switch (r) {
				case CLI_CMD_EXIT:
					telnet_disconnect(cln);
				case CLI_CMD_FAILED:
					return -1;
				case CLI_CMD_SYNTAX:
					if (telnet_send(cln, MSG_SYNTAX_ERROR, sizeof(MSG_SYNTAX_ERROR)))
						return -1;
					return 0;
				case CLI_CMD_OK:
					found = 1;
			}
		}
	}

	list_for_each_entry(cmd2, &regexp_cmd_list, entry) {
		r = cmd2->exec((char *)cln->cmdline, cln);
		switch (r) {
			case CLI_CMD_EXIT:
				telnet_disconnect(cln);
			case CLI_CMD_FAILED:
				return -1;
			case CLI_CMD_SYNTAX:
				if (telnet_send(cln, MSG_SYNTAX_ERROR, sizeof(MSG_SYNTAX_ERROR)))
					return -1;
				return 0;
			case CLI_CMD_OK:
				found = 1;
		}
	}

	if (!found) {
		if (telnet_send(cln, MSG_UNKNOWN_CMD, sizeof(MSG_UNKNOWN_CMD)))
			return -1;
	}

	return 0;
}


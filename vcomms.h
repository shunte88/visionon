#ifndef VISSY_COMMS_H
#define VISSY_COMMS_H

struct CliConn *client;
struct vissy_settings;
struct vissy_stats;

void vcomms_init(struct vissy_settings *se, struct vissy_stats *st);

int vcomms_received(struct CliConn *client, char *message,
                    const char *url_to_handle);
int vcomms_parseparam(struct CliConn *client, char *parameters);
int sendmessages(char *buf);
int vcomms_payload_encode(char *outbuffer, char *message);
int vcomms_chunk_encoding(char *outbuffer, char *message);
void create_time_line(char *buffer);

int vcomms_send_handshake(struct CliConn *client);
int vcomms_send_badreq(struct CliConn *client);
int vcomms_send_notfound(struct CliConn *client);
int vcomms_send_notsupported(struct CliConn *client);
int vcomms_send_head(struct CliConn *client);
int vcomms_send_options(struct CliConn *client);

char *chop(char *str);
int get_pos(char *str, char c);
int emptyStr(char *str);
char *nextNotWhitespace(char *str);
int startWithStr(char *str, const char *pattern);

#endif

// end.

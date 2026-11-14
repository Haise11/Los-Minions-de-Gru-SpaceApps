#ifndef MONGOOSE_H
#define MONGOOSE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MG_MAX_HTTP_HEADERS 32

enum {
    MG_EV_ERROR,
    MG_EV_OPEN,
    MG_EV_POLL,
    MG_EV_RESOLVE,
    MG_EV_CONNECT,
    MG_EV_ACCEPT,
    MG_EV_TLS_HS,
    MG_EV_READ,
    MG_EV_WRITE,
    MG_EV_CLOSE,
    MG_EV_HTTP_HDRS,
    MG_EV_HTTP_MSG
};

struct mg_str {
    const char *buf;
    size_t len;
};

struct mg_http_header {
    struct mg_str name;
    struct mg_str value;
};

struct mg_http_message {
    struct mg_str method;
    struct mg_str uri;
    struct mg_str query;
    struct mg_str proto;
    struct mg_http_header headers[MG_MAX_HTTP_HEADERS];
    struct mg_str body;
    struct mg_str head;
    struct mg_str message;
};

struct mg_http_serve_opts {
    const char *root_dir;
    const char *ssi_pattern;
    const char *extra_headers;
    const char *mime_types;
    const char *page404;
    void *fs;
};

struct mg_connection;
struct mg_mgr;

typedef void (*mg_event_handler_t)(struct mg_connection *c, int ev, void *ev_data);

struct mg_connection {
    struct mg_connection *next;
    struct mg_mgr *mgr;
    int fd;
    bool is_listening;
    bool is_closing;
    mg_event_handler_t fn;
    void *fn_data;
    char *recv_buf;
    size_t recv_len;
    size_t recv_cap;
};

struct mg_mgr {
    struct mg_connection *conns;
};

struct mg_str mg_str(const char *s);
struct mg_str mg_str_n(const char *s, size_t n);
int mg_strcmp(const struct mg_str str1, const struct mg_str str2);
bool mg_match(struct mg_str str, struct mg_str pattern, struct mg_str *caps);
bool mg_http_match_uri(const struct mg_http_message *hm, const char *glob);
struct mg_str *mg_http_get_header(struct mg_http_message *hm, const char *name);

void mg_mgr_init(struct mg_mgr *mgr);
void mg_mgr_free(struct mg_mgr *mgr);
struct mg_connection *mg_http_listen(struct mg_mgr *mgr, const char *url,
                                     mg_event_handler_t fn, void *fn_data);
void mg_mgr_poll(struct mg_mgr *mgr, int ms);

void mg_http_reply(struct mg_connection *c, int status_code, const char *headers,
                   const char *body_fmt, ...);
void mg_http_serve_dir(struct mg_connection *c, struct mg_http_message *hm,
                       const struct mg_http_serve_opts *opts);
void mg_http_serve_file(struct mg_connection *c, struct mg_http_message *hm,
                        const char *path, const struct mg_http_serve_opts *opts);

#ifdef __cplusplus
}
#endif

#endif /* MONGOOSE_H */

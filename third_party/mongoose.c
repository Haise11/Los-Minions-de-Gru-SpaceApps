#define _POSIX_C_SOURCE 200809L
#include "mongoose.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

struct mg_str mg_str(const char *s) {
    struct mg_str ms;
    ms.buf = s;
    ms.len = (s == NULL) ? 0 : strlen(s);
    return ms;
}

struct mg_str mg_str_n(const char *s, size_t n) {
    struct mg_str ms;
    ms.buf = s;
    ms.len = n;
    return ms;
}

int mg_strcmp(const struct mg_str str1, const struct mg_str str2) {
    size_t i = 0;
    while (i < str1.len && i < str2.len) {
        int c1 = (unsigned char)str1.buf[i];
        int c2 = (unsigned char)str2.buf[i];
        if (c1 != c2) return c1 - c2;
        i++;
    }
    if (str1.len < str2.len) return -1;
    if (str1.len > str2.len) return 1;
    return 0;
}

static int mg_strncasecmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int c1 = tolower((unsigned char)s1[i]);
        int c2 = tolower((unsigned char)s2[i]);
        if (c1 != c2) return c1 - c2;
        if (c1 == '\0') return 0;
    }
    return 0;
}

bool mg_match(struct mg_str str, struct mg_str pattern, struct mg_str *caps) {
    size_t si = 0, pi = 0, cap_idx = 0;
    if (caps) {
        for (int k = 0; k < 4; k++) {
            caps[k].buf = NULL;
            caps[k].len = 0;
        }
    }
    while (pi < pattern.len || si < str.len) {
        if (pi < pattern.len && (pattern.buf[pi] == '*' || pattern.buf[pi] == '#')) {
            char wild = pattern.buf[pi++];
            size_t cap_start = si;
            if (wild == '*') {
                /* '*' matches a single path segment (anything except '/') */
                if (pi == pattern.len) {
                    while (si < str.len && str.buf[si] != '/') {
                        si++;
                    }
                } else {
                    while (si < str.len && str.buf[si] != '/' && str.buf[si] != pattern.buf[pi]) {
                        si++;
                    }
                }
            } else {
                /* '#' matches across '/' */
                if (pi == pattern.len) {
                    si = str.len;
                } else {
                    while (si < str.len && str.buf[si] != pattern.buf[pi]) {
                        si++;
                    }
                }
            }
            if (caps && cap_idx < 4) {
                caps[cap_idx].buf = str.buf + cap_start;
                caps[cap_idx].len = si - cap_start;
                cap_idx++;
            }
        } else if (pi < pattern.len && si < str.len && pattern.buf[pi] == str.buf[si]) {
            pi++;
            si++;
        } else {
            return false;
        }
    }
    return (pi == pattern.len && si == str.len);
}

bool mg_http_match_uri(const struct mg_http_message *hm, const char *glob) {
    if (!hm || !glob) return false;
    return mg_match(hm->uri, mg_str(glob), NULL);
}

struct mg_str *mg_http_get_header(struct mg_http_message *hm, const char *name) {
    if (!hm || !name) return NULL;
    size_t nlen = strlen(name);
    for (int i = 0; i < MG_MAX_HTTP_HEADERS; i++) {
        if (hm->headers[i].name.len == 0) break;
        if (hm->headers[i].name.len == nlen &&
            mg_strncasecmp(hm->headers[i].name.buf, name, nlen) == 0) {
            return &hm->headers[i].value;
        }
    }
    return NULL;
}

void mg_mgr_init(struct mg_mgr *mgr) {
    if (!mgr) return;
    memset(mgr, 0, sizeof(*mgr));
    signal(SIGPIPE, SIG_IGN);
}

static void close_conn(struct mg_connection *c) {
    if (!c) return;
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    free(c->recv_buf);
    c->recv_buf = NULL;
    c->recv_len = 0;
    c->recv_cap = 0;
}

void mg_mgr_free(struct mg_mgr *mgr) {
    if (!mgr) return;
    struct mg_connection *c = mgr->conns;
    while (c) {
        struct mg_connection *next = c->next;
        close_conn(c);
        free(c);
        c = next;
    }
    mgr->conns = NULL;
}

static int parse_port(const char *url) {
    if (!url) return 8080;
    const char *last_colon = strrchr(url, ':');
    if (last_colon && isdigit((unsigned char)last_colon[1])) {
        int p = atoi(last_colon + 1);
        if (p > 0 && p <= 65535) return p;
    }
    return 8080;
}

struct mg_connection *mg_http_listen(struct mg_mgr *mgr, const char *url,
                                     mg_event_handler_t fn, void *fn_data) {
    if (!mgr || !url) return NULL;
    int port = parse_port(url);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }
    if (listen(fd, 64) < 0) {
        close(fd);
        return NULL;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct mg_connection *c = (struct mg_connection *)calloc(1, sizeof(*c));
    if (!c) {
        close(fd);
        return NULL;
    }
    c->mgr = mgr;
    c->fd = fd;
    c->is_listening = true;
    c->fn = fn;
    c->fn_data = fn_data;
    c->next = mgr->conns;
    mgr->conns = c;
    return c;
}

/* Returns:
 * > 0: total HTTP message length when full request + body is available
 * == 0: incomplete request, need more bytes
 * < 0: malformed HTTP request
 */
static int parse_http_request(const char *buf, size_t len, struct mg_http_message *hm) {
    memset(hm, 0, sizeof(*hm));
    const char *end_of_headers = NULL;
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            end_of_headers = buf + i + 4;
            break;
        }
    }
    if (!end_of_headers) {
        return (len > 65536) ? -1 : 0;
    }

    size_t head_len = (size_t)(end_of_headers - buf);
    hm->head.buf = buf;
    hm->head.len = head_len;

    /* Request line: METHOD URI PROTO\r\n */
    const char *p = buf;
    const char *line_end = strstr(p, "\r\n");
    if (!line_end || line_end >= end_of_headers) return -1;

    const char *sp1 = memchr(p, ' ', (size_t)(line_end - p));
    if (!sp1) return -1;
    hm->method.buf = p;
    hm->method.len = (size_t)(sp1 - p);

    const char *uri_start = sp1 + 1;
    while (uri_start < line_end && *uri_start == ' ') uri_start++;
    const char *sp2 = memchr(uri_start, ' ', (size_t)(line_end - uri_start));
    if (!sp2) return -1;

    const char *qmark = memchr(uri_start, '?', (size_t)(sp2 - uri_start));
    if (qmark) {
        hm->uri.buf = uri_start;
        hm->uri.len = (size_t)(qmark - uri_start);
        hm->query.buf = qmark + 1;
        hm->query.len = (size_t)(sp2 - (qmark + 1));
    } else {
        hm->uri.buf = uri_start;
        hm->uri.len = (size_t)(sp2 - uri_start);
    }

    hm->proto.buf = sp2 + 1;
    hm->proto.len = (size_t)(line_end - (sp2 + 1));

    /* Headers */
    p = line_end + 2;
    int h_idx = 0;
    size_t content_length = 0;
    while (p < end_of_headers - 2 && h_idx < MG_MAX_HTTP_HEADERS) {
        const char *hend = strstr(p, "\r\n");
        if (!hend || hend == p) break;
        const char *colon = memchr(p, ':', (size_t)(hend - p));
        if (colon) {
            hm->headers[h_idx].name.buf = p;
            hm->headers[h_idx].name.len = (size_t)(colon - p);
            const char *vstart = colon + 1;
            while (vstart < hend && (*vstart == ' ' || *vstart == '\t')) vstart++;
            hm->headers[h_idx].value.buf = vstart;
            hm->headers[h_idx].value.len = (size_t)(hend - vstart);

            if (hm->headers[h_idx].name.len == 14 &&
                mg_strncasecmp(hm->headers[h_idx].name.buf, "Content-Length", 14) == 0) {
                char cl_buf[32];
                size_t cl_len = hm->headers[h_idx].value.len;
                if (cl_len >= sizeof(cl_buf)) cl_len = sizeof(cl_buf) - 1;
                memcpy(cl_buf, hm->headers[h_idx].value.buf, cl_len);
                cl_buf[cl_len] = '\0';
                char *endptr = NULL;
                long cl = strtol(cl_buf, &endptr, 10);
                if (endptr == cl_buf || cl < 0 || cl > 1024 * 1024) return -1;
                content_length = (size_t)cl;
            }
            h_idx++;
        }
        p = hend + 2;
    }

    if (len < head_len + content_length) {
        return 0; /* Wait for more body bytes */
    }

    hm->body.buf = end_of_headers;
    hm->body.len = content_length;
    hm->message.buf = buf;
    hm->message.len = head_len + content_length;
    return (int)(head_len + content_length);
}

void mg_mgr_poll(struct mg_mgr *mgr, int ms) {
    if (!mgr) return;

    fd_set rfds;
    FD_ZERO(&rfds);
    int max_fd = -1;

    for (struct mg_connection *c = mgr->conns; c != NULL; c = c->next) {
        if (c->fd >= 0 && !c->is_closing) {
            FD_SET(c->fd, &rfds);
            if (c->fd > max_fd) max_fd = c->fd;
        }
    }

    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    int ready = select(max_fd + 1, &rfds, NULL, NULL, &tv);
    if (ready > 0) {
        for (struct mg_connection *c = mgr->conns; c != NULL; c = c->next) {
            if (c->fd < 0 || !FD_ISSET(c->fd, &rfds)) continue;

            if (c->is_listening) {
                struct sockaddr_in cli_addr;
                socklen_t cli_len = sizeof(cli_addr);
                int cfd = accept(c->fd, (struct sockaddr *)&cli_addr, &cli_len);
                if (cfd >= 0) {
                    struct mg_connection *nc = (struct mg_connection *)calloc(1, sizeof(*nc));
                    if (!nc) {
                        close(cfd);
                    } else {
                        nc->mgr = mgr;
                        nc->fd = cfd;
                        nc->is_listening = false;
                        nc->fn = c->fn;
                        nc->fn_data = c->fn_data;
                        nc->recv_cap = 8192;
                        nc->recv_buf = (char *)malloc(nc->recv_cap);
                        if (!nc->recv_buf) {
                            close(cfd);
                            free(nc);
                        } else {
                            nc->next = mgr->conns;
                            mgr->conns = nc;
                        }
                    }
                }
            } else {
                if (c->recv_len + 4096 + 1 > c->recv_cap) {
                    size_t new_cap = c->recv_cap * 2;
                    if (new_cap > 1024 * 1024) {
                        c->is_closing = true;
                        continue;
                    }
                    char *nb = (char *)realloc(c->recv_buf, new_cap);
                    if (!nb) {
                        c->is_closing = true;
                        continue;
                    }
                    c->recv_buf = nb;
                    c->recv_cap = new_cap;
                }
                ssize_t n = recv(c->fd, c->recv_buf + c->recv_len, c->recv_cap - c->recv_len - 1, 0);
                if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                    continue;
                }
                if (n <= 0) {
                    c->is_closing = true;
                } else {
                    c->recv_len += (size_t)n;
                    c->recv_buf[c->recv_len] = '\0';

                    struct mg_http_message hm;
                    int req_len = parse_http_request(c->recv_buf, c->recv_len, &hm);
                    if (req_len < 0) {
                        c->is_closing = true;
                    } else if (req_len > 0) {
                        if (c->fn) {
                            c->fn(c, MG_EV_HTTP_MSG, &hm);
                        }
                        c->is_closing = true;
                    }
                }
            }
        }
    }

    /* Clean up closed connections */
    struct mg_connection **pp = &mgr->conns;
    while (*pp) {
        struct mg_connection *curr = *pp;
        if (curr->is_closing) {
            *pp = curr->next;
            close_conn(curr);
            free(curr);
        } else {
            pp = &curr->next;
        }
    }
}

static const char *status_reason(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 422: return "Unprocessable Entity";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

static void send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            break;
        }
        sent += (size_t)n;
    }
}

void mg_http_reply(struct mg_connection *c, int status_code, const char *headers,
                   const char *body_fmt, ...) {
    if (!c || c->fd < 0) return;

    va_list ap;
    va_start(ap, body_fmt);
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int body_len = vsnprintf(NULL, 0, body_fmt ? body_fmt : "", ap);
    va_end(ap);

    if (body_len < 0) {
        va_end(ap_copy);
        return;
    }

    char *body = (char *)malloc((size_t)body_len + 1);
    if (!body) {
        va_end(ap_copy);
        return;
    }
    vsnprintf(body, (size_t)body_len + 1, body_fmt ? body_fmt : "", ap_copy);
    va_end(ap_copy);

    char head[1024];
    int hlen = snprintf(head, sizeof(head),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Length: %d\r\n"
                        "Connection: close\r\n"
                        "%s"
                        "\r\n",
                        status_code, status_reason(status_code), body_len,
                        headers ? headers : "");

    if (hlen > 0) {
        send_all(c->fd, head, (size_t)hlen < sizeof(head) ? (size_t)hlen : sizeof(head) - 1);
    }
    if (body_len > 0) {
        send_all(c->fd, body, (size_t)body_len);
    }
    free(body);
    c->is_closing = true;
}

static const char *guess_mime_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(dot, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(dot, ".svg") == 0) return "image/svg+xml";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".ico") == 0) return "image/x-icon";
    if (strcmp(dot, ".txt") == 0 || strcmp(dot, ".md") == 0) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static void url_decode(const char *src, size_t src_len, char *dst, size_t dst_cap) {
    size_t di = 0;
    for (size_t si = 0; si < src_len && di + 1 < dst_cap; si++) {
        if (src[si] == '%' && si + 2 < src_len &&
            isxdigit((unsigned char)src[si + 1]) && isxdigit((unsigned char)src[si + 2])) {
            char hex[3] = {src[si + 1], src[si + 2], '\0'};
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 2;
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

void mg_http_serve_file(struct mg_connection *c, struct mg_http_message *hm,
                        const char *path, const struct mg_http_serve_opts *opts) {
    (void)hm;
    if (!c || c->fd < 0 || !path) return;

    struct stat st;
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
        mg_http_reply(c, 404, "Content-Type: text/plain; charset=utf-8\r\n", "404 Not Found\n");
        return;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        mg_http_reply(c, 404, "Content-Type: text/plain; charset=utf-8\r\n", "404 Not Found\n");
        return;
    }

    size_t fsize = (size_t)st.st_size;
    char *buf = (char *)malloc(fsize > 0 ? fsize : 1);
    if (!buf) {
        fclose(fp);
        mg_http_reply(c, 500, "Content-Type: text/plain; charset=utf-8\r\n", "500 Out of Memory\n");
        return;
    }
    size_t nread = fread(buf, 1, fsize, fp);
    fclose(fp);

    const char *mime = guess_mime_type(path);
    char head[1024];
    int hlen = snprintf(head, sizeof(head),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: close\r\n"
                        "%s"
                        "\r\n",
                        mime, nread, (opts && opts->extra_headers) ? opts->extra_headers : "");

    if (hlen > 0) {
        send_all(c->fd, head, (size_t)hlen < sizeof(head) ? (size_t)hlen : sizeof(head) - 1);
    }
    if (nread > 0) {
        send_all(c->fd, buf, nread);
    }
    free(buf);
    c->is_closing = true;
}

void mg_http_serve_dir(struct mg_connection *c, struct mg_http_message *hm,
                       const struct mg_http_serve_opts *opts) {
    if (!c || !hm || !opts || !opts->root_dir) return;

    char decoded_uri[512];
    url_decode(hm->uri.buf, hm->uri.len, decoded_uri, sizeof(decoded_uri));

    if (strstr(decoded_uri, "..") != NULL) {
        mg_http_reply(c, 400, "Content-Type: text/plain; charset=utf-8\r\n", "Invalid URI\n");
        return;
    }

    char full_path[1024];
    if (strcmp(decoded_uri, "/") == 0 || decoded_uri[0] == '\0') {
        snprintf(full_path, sizeof(full_path), "%s/index.html", opts->root_dir);
    } else {
        snprintf(full_path, sizeof(full_path), "%s%s", opts->root_dir, decoded_uri);
    }

    struct stat st;
    if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        size_t len = strlen(full_path);
        if (len > 0 && full_path[len - 1] == '/') {
            snprintf(full_path + len, sizeof(full_path) - len, "index.html");
        } else {
            snprintf(full_path + len, sizeof(full_path) - len, "/index.html");
        }
    }

    mg_http_serve_file(c, hm, full_path, opts);
}

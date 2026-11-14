#ifndef SERVER_H
#define SERVER_H

#include "health_types.h"
#include "../third_party/mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Manejador principal de eventos HTTP para Mongoose */
void server_http_handler(struct mg_connection *c, int ev, void *ev_data);

#ifdef __cplusplus
}
#endif

#endif /* SERVER_H */

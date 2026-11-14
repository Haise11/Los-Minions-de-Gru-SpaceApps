#define _POSIX_C_SOURCE 200809L
#include "astronaut.h"
#include "health_engine.h"
#include "health_types.h"
#include "mission.h"
#include "server.h"
#include "storage.h"
#include "../third_party/mongoose.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t s_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    s_running = 0;
}

int main(int argc, char *argv[]) {
    const char *data_dir = "data";
    int port = 8080;

    const char *env_port = getenv("PORT");
    if (env_port && atoi(env_port) > 0) {
        port = atoi(env_port);
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            data_dir = argv[++i];
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    static AppState state;
    if (storage_load_all(&state, data_dir) != OK) {
        fprintf(stderr, "[ERROR] No se pudo inicializar el almacenamiento desde '%s'\n", data_dir);
        return 1;
    }

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    char listen_url[64];
    snprintf(listen_url, sizeof(listen_url), "http://0.0.0.0:%d", port);

    if (!mg_http_listen(&mgr, listen_url, server_http_handler, &state)) {
        fprintf(stderr, "[ERROR] No se pudo abrir el puerto HTTP en %s\n", listen_url);
        mg_mgr_free(&mgr);
        return 1;
    }

    printf("===================================================================\n");
    printf("  SPACE HEALTH MONITOR — NASA HRP Autonomous Medical System (C11)\n");
    printf("===================================================================\n");
    printf("  Directorio de datos : %s/\n", state.data_dir);
    printf("  Misiones cargadas   : %d\n", state.mission_count);
    printf("  Tripulantes activos : %d\n", state.astronaut_count);
    for (int i = 0; i < state.astronaut_count; i++) {
        const Astronaut *a = &state.astronauts[i];
        printf("    - [%s] %-22s | %-6s | Score: %3d%% | EVA: %-10s | %s\n",
               a->id,
               a->nombre,
               alert_severity_to_str(a->report.global_status),
               a->report.health_score,
               aptitude_to_str(a->report.eva_aptitude),
               a->report.resumen_estado);
    }
    printf("-------------------------------------------------------------------\n");
    printf("  Servidor web 100%% offline escuchando en: http://localhost:%d\n", port);
    printf("  Presiona Ctrl+C para detener el servidor.\n");
    printf("===================================================================\n");
    fflush(stdout);

    while (s_running) {
        mg_mgr_poll(&mgr, 50);
    }

    printf("\n[INFO] Cerrando servidor y liberando recursos...\n");
    mg_mgr_free(&mgr);
    return 0;
}

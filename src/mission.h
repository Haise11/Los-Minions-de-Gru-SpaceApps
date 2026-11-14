#ifndef MISSION_H
#define MISSION_H

#include "health_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Inicializa entorno de cabina con parametros nominales ISS/Artemis */
void cabin_init_nominal(CabinEnv *env);

/* Inicializa estructura de mision */
void mission_init(Mission *m, const char *id, const char *nombre, const char *destino);

/* Valida rangos fisicos posibles para CabinEnv */
StatusCode validate_cabin(const CabinEnv *env);

/* Gestion de tripulantes dentro de una mision */
int mission_has_crew(const Mission *m, const char *astronaut_id);
StatusCode mission_add_crew(Mission *m, const char *astronaut_id);
StatusCode mission_remove_crew(Mission *m, const char *astronaut_id);

/* Busqueda en AppState */
Mission *state_find_mission(AppState *state, const char *mission_id);
Astronaut *state_find_astronaut(AppState *state, const char *astronaut_id);

#ifdef __cplusplus
}
#endif

#endif /* MISSION_H */

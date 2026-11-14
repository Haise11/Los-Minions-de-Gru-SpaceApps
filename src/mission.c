#include "mission.h"
#include "astronaut.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void safe_strcpy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

void cabin_init_nominal(CabinEnv *env) {
    if (!env) return;
    memset(env, 0, sizeof(*env));
    env->presion_cabina_kpa = 101.3f;
    env->po2_kpa            = 21.3f;
    env->pco2_kpa           = 0.35f;
    env->tasa_rad_msv_h     = 0.02f;
    env->evento_spe_activo  = 0;
    env->gravedad_actual    = GRAV_MICRO;
    env->latencia_com_seg   = 1.3f;
}

void mission_init(Mission *m, const char *id, const char *nombre, const char *destino) {
    if (!m) return;
    memset(m, 0, sizeof(*m));
    safe_strcpy(m->id, sizeof(m->id), (id && id[0]) ? id : "artemis_sim");
    safe_strcpy(m->nombre, sizeof(m->nombre), (nombre && nombre[0]) ? nombre : "Artemis / Transito a Marte");
    safe_strcpy(m->destino, sizeof(m->destino), (destino && destino[0]) ? destino : "Transito a Marte");
    m->dia_actual = 14;
    cabin_init_nominal(&m->env);
    m->crew_count = 0;
}

static int is_valid_float(float v, float min_v, float max_v) {
    if (isnan(v) || isinf(v)) return 0;
    return (v >= min_v && v <= max_v);
}

StatusCode validate_cabin(const CabinEnv *env) {
    if (!env) return ERR_INVALID_RANGE;
    if (!is_valid_float(env->presion_cabina_kpa, 20.0f, 120.0f)) return ERR_INVALID_RANGE;
    if (!is_valid_float(env->po2_kpa, 5.0f, 105.0f))             return ERR_INVALID_RANGE;
    if (!is_valid_float(env->pco2_kpa, 0.0f, 10.0f))             return ERR_INVALID_RANGE;
    if (!is_valid_float(env->tasa_rad_msv_h, 0.0f, 500.0f))      return ERR_INVALID_RANGE;
    if (!is_valid_float(env->latencia_com_seg, 0.0f, 10000.0f))  return ERR_INVALID_RANGE;
    if (env->evento_spe_activo < 0 || env->evento_spe_activo > 1) return ERR_INVALID_RANGE;
    if ((int)env->gravedad_actual < (int)GRAV_MICRO || (int)env->gravedad_actual > (int)GRAV_DYNAMIC) {
        return ERR_INVALID_RANGE;
    }
    return OK;
}

int mission_has_crew(const Mission *m, const char *astronaut_id) {
    if (!m || !astronaut_id) return 0;
    for (int i = 0; i < m->crew_count; i++) {
        if (strcmp(m->crew_ids[i], astronaut_id) == 0) return 1;
    }
    return 0;
}

StatusCode mission_add_crew(Mission *m, const char *astronaut_id) {
    if (!m || !is_valid_id(astronaut_id)) return ERR_INVALID_ID;
    if (mission_has_crew(m, astronaut_id)) return OK;
    if (m->crew_count >= MAX_CREW_PER_MISSION) return ERR_LIMIT_REACHED;
    safe_strcpy(m->crew_ids[m->crew_count], sizeof(m->crew_ids[0]), astronaut_id);
    m->crew_count++;
    return OK;
}

StatusCode mission_remove_crew(Mission *m, const char *astronaut_id) {
    if (!m || !astronaut_id) return ERR_INVALID_ID;
    for (int i = 0; i < m->crew_count; i++) {
        if (strcmp(m->crew_ids[i], astronaut_id) == 0) {
            for (int j = i; j < m->crew_count - 1; j++) {
                safe_strcpy(m->crew_ids[j], sizeof(m->crew_ids[0]), m->crew_ids[j + 1]);
            }
            m->crew_ids[m->crew_count - 1][0] = '\0';
            m->crew_count--;
            return OK;
        }
    }
    return ERR_NOT_FOUND;
}

Mission *state_find_mission(AppState *state, const char *mission_id) {
    if (!state || !mission_id) return NULL;
    for (int i = 0; i < state->mission_count; i++) {
        if (strcmp(state->missions[i].id, mission_id) == 0) {
            return &state->missions[i];
        }
    }
    return NULL;
}

Astronaut *state_find_astronaut(AppState *state, const char *astronaut_id) {
    if (!state || !astronaut_id) return NULL;
    for (int i = 0; i < state->astronaut_count; i++) {
        if (strcmp(state->astronauts[i].id, astronaut_id) == 0) {
            return &state->astronauts[i];
        }
    }
    return NULL;
}

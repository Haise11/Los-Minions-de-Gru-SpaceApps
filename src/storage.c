#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include "astronaut.h"
#include "health_engine.h"
#include "mission.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void safe_strcpy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static void json_get_float(const cJSON *obj, const char *key, float *out) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!it) return;
    if (cJSON_IsNumber(it) && !isnan(it->valuedouble) && !isinf(it->valuedouble)) {
        *out = (float)it->valuedouble;
    } else {
        *out = NAN;
    }
}

static void json_get_int(const cJSON *obj, const char *key, int *out) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!it) return;
    if (cJSON_IsNumber(it) && !isnan(it->valuedouble) && !isinf(it->valuedouble) &&
        it->valuedouble >= -100000.0 && it->valuedouble <= 10000000.0 &&
        floor(it->valuedouble) == it->valuedouble) {
        *out = (int)it->valuedouble;
    } else {
        *out = -999999;
    }
}

static void json_get_bool_int(const cJSON *obj, const char *key, int *out) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!it) return;
    if (cJSON_IsBool(it)) {
        *out = cJSON_IsTrue(it) ? 1 : 0;
    } else {
        json_get_int(obj, key, out);
    }
}

static void json_get_uint(const cJSON *obj, const char *key, unsigned int *out) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!it) return;
    if (cJSON_IsNumber(it) && !isnan(it->valuedouble) && !isinf(it->valuedouble) &&
        it->valuedouble >= 0.0 && it->valuedouble <= 511.0 &&
        floor(it->valuedouble) == it->valuedouble) {
        *out = (unsigned int)it->valuedouble;
    } else {
        *out = 0xFFFFFFFFu;
    }
}

static void json_get_long(const cJSON *obj, const char *key, long *out) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!it) return;
    if (cJSON_IsNumber(it) && !isnan(it->valuedouble) && !isinf(it->valuedouble) &&
        it->valuedouble >= 0.0) {
        *out = (long)it->valuedouble;
    }
}

static void json_get_str(const cJSON *obj, const char *key, char *dst, size_t dst_size) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(it) && it->valuestring && it->valuestring[0] != '\0') {
        safe_strcpy(dst, dst_size, it->valuestring);
    }
}

/* =========================================================================
 * Baseline <-> JSON
 * ========================================================================= */
cJSON *baseline_to_json(const AstronautBaseline *b, int include_rad_limit) {
    if (!b) return NULL;
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;

    cJSON_AddNumberToObject(o, "fc_base", b->fc_base);
    cJSON_AddNumberToObject(o, "pa_sis_base", b->pa_sis_base);
    cJSON_AddNumberToObject(o, "pa_dia_base", b->pa_dia_base);
    cJSON_AddNumberToObject(o, "spo2_base", b->spo2_base);
    cJSON_AddNumberToObject(o, "temp_base", b->temp_base);
    cJSON_AddNumberToObject(o, "glucosa_base", b->glucosa_base);
    cJSON_AddNumberToObject(o, "peso_base", b->peso_base);
    cJSON_AddNumberToObject(o, "sueno_base_h", b->sueno_base_h);
    cJSON_AddNumberToObject(o, "animo_base", b->animo_base);
    cJSON_AddNumberToObject(o, "estres_base", b->estres_base);
    cJSON_AddNumberToObject(o, "cognitivo_base_ms", b->cognitivo_base_ms);
    cJSON_AddNumberToObject(o, "calorias_objetivo_kcal", b->calorias_objetivo_kcal);
    cJSON_AddNumberToObject(o, "hidratacion_objetivo_ml", b->hidratacion_objetivo_ml);
    cJSON_AddNumberToObject(o, "apetito_base", b->apetito_base);
    cJSON_AddNumberToObject(o, "vision_base", b->vision_base);
    cJSON_AddNumberToObject(o, "densidad_osea_base_pct", b->densidad_osea_base_pct);
    cJSON_AddNumberToObject(o, "ejercicio_objetivo_min", b->ejercicio_objetivo_min);
    cJSON_AddNumberToObject(o, "pre_resp_o2_objetivo_min", b->pre_resp_o2_objetivo_min);
    cJSON_AddNumberToObject(o, "dosis_diaria_nominal_msv", b->dosis_diaria_nominal_msv);

    if (include_rad_limit) {
        cJSON_AddNumberToObject(o, "NASA_CAREER_RAD_LIMIT_MSV", NASA_CAREER_RAD_LIMIT_MSV);
    }
    return o;
}

void baseline_from_json(AstronautBaseline *b, const cJSON *obj) {
    if (!b || !cJSON_IsObject(obj)) return;
    json_get_float(obj, "fc_base", &b->fc_base);
    json_get_float(obj, "pa_sis_base", &b->pa_sis_base);
    json_get_float(obj, "pa_dia_base", &b->pa_dia_base);
    json_get_float(obj, "spo2_base", &b->spo2_base);
    json_get_float(obj, "temp_base", &b->temp_base);
    json_get_float(obj, "glucosa_base", &b->glucosa_base);
    json_get_float(obj, "peso_base", &b->peso_base);
    json_get_float(obj, "sueno_base_h", &b->sueno_base_h);
    json_get_int(obj, "animo_base", &b->animo_base);
    json_get_int(obj, "estres_base", &b->estres_base);
    json_get_float(obj, "cognitivo_base_ms", &b->cognitivo_base_ms);
    json_get_int(obj, "calorias_objetivo_kcal", &b->calorias_objetivo_kcal);
    json_get_int(obj, "hidratacion_objetivo_ml", &b->hidratacion_objetivo_ml);
    json_get_int(obj, "apetito_base", &b->apetito_base);
    json_get_float(obj, "vision_base", &b->vision_base);
    json_get_float(obj, "densidad_osea_base_pct", &b->densidad_osea_base_pct);
    json_get_int(obj, "ejercicio_objetivo_min", &b->ejercicio_objetivo_min);
    json_get_int(obj, "pre_resp_o2_objetivo_min", &b->pre_resp_o2_objetivo_min);
    json_get_float(obj, "dosis_diaria_nominal_msv", &b->dosis_diaria_nominal_msv);
}

/* =========================================================================
 * DailyHealthSample <-> JSON
 * ========================================================================= */
cJSON *sample_to_json(const DailyHealthSample *s) {
    if (!s) return NULL;
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;

    cJSON_AddNumberToObject(o, "dia_mision", s->dia_mision);
    cJSON_AddNumberToObject(o, "timestamp_unix", (double)s->timestamp_unix);
    cJSON_AddNumberToObject(o, "fc", s->fc);
    cJSON_AddNumberToObject(o, "pa_sis", s->pa_sis);
    cJSON_AddNumberToObject(o, "pa_dia", s->pa_dia);
    cJSON_AddNumberToObject(o, "spo2", s->spo2);
    cJSON_AddNumberToObject(o, "temp_c", s->temp_c);
    cJSON_AddNumberToObject(o, "glucosa_mg_dl", s->glucosa_mg_dl);
    cJSON_AddNumberToObject(o, "peso_kg", s->peso_kg);
    cJSON_AddNumberToObject(o, "dosis_dia_msv", s->dosis_dia_msv);
    cJSON_AddNumberToObject(o, "vision_actual", s->vision_actual);
    cJSON_AddNumberToObject(o, "dolor_cabeza", s->dolor_cabeza);
    cJSON_AddNumberToObject(o, "mareo_vestibular", s->mareo_vestibular);
    cJSON_AddNumberToObject(o, "ejercicio_min", s->ejercicio_min);
    cJSON_AddNumberToObject(o, "dia_descanso", s->dia_descanso);
    cJSON_AddNumberToObject(o, "tuvo_eva", s->tuvo_eva);
    cJSON_AddNumberToObject(o, "pre_resp_o2_min", s->pre_resp_o2_min);
    cJSON_AddNumberToObject(o, "sintomas_flags", (double)s->sintomas_flags);
    cJSON_AddNumberToObject(o, "sueno_horas", s->sueno_horas);
    cJSON_AddNumberToObject(o, "sueno_calidad", s->sueno_calidad);
    cJSON_AddNumberToObject(o, "animo", s->animo);
    cJSON_AddNumberToObject(o, "estres", s->estres);
    cJSON_AddNumberToObject(o, "cognitivo_ms", s->cognitivo_ms);
    cJSON_AddNumberToObject(o, "cohesion_equipo", s->cohesion_equipo);
    cJSON_AddNumberToObject(o, "apetito_comida", s->apetito_comida);
    cJSON_AddNumberToObject(o, "calorias_kcal", s->calorias_kcal);
    cJSON_AddNumberToObject(o, "hidratacion_ml", s->hidratacion_ml);
    cJSON_AddNumberToObject(o, "fase_ciclo", s->fase_ciclo);
    cJSON_AddStringToObject(o, "fase_ciclo_str", hormonal_phase_to_str((HormonalCyclePhase)s->fase_ciclo));
    cJSON_AddNumberToObject(o, "dia_ciclo", s->dia_ciclo);
    cJSON_AddNumberToObject(o, "colico_menstrual", s->colico_menstrual);
    cJSON_AddNumberToObject(o, "flujo_menstrual", s->flujo_menstrual);
    cJSON_AddNumberToObject(o, "suplemento_hierro", s->suplemento_hierro);
    return o;
}

void sample_from_json_partial(DailyHealthSample *s, const cJSON *obj) {
    if (!s || !cJSON_IsObject(obj)) return;
    json_get_int(obj, "dia_mision", &s->dia_mision);
    json_get_long(obj, "timestamp_unix", &s->timestamp_unix);
    json_get_float(obj, "fc", &s->fc);
    json_get_float(obj, "pa_sis", &s->pa_sis);
    json_get_float(obj, "pa_dia", &s->pa_dia);
    json_get_float(obj, "spo2", &s->spo2);
    json_get_float(obj, "temp_c", &s->temp_c);
    json_get_float(obj, "glucosa_mg_dl", &s->glucosa_mg_dl);
    json_get_float(obj, "peso_kg", &s->peso_kg);
    json_get_float(obj, "dosis_dia_msv", &s->dosis_dia_msv);
    json_get_float(obj, "vision_actual", &s->vision_actual);
    json_get_int(obj, "dolor_cabeza", &s->dolor_cabeza);
    json_get_int(obj, "mareo_vestibular", &s->mareo_vestibular);
    json_get_int(obj, "ejercicio_min", &s->ejercicio_min);
    json_get_bool_int(obj, "dia_descanso", &s->dia_descanso);
    json_get_bool_int(obj, "tuvo_eva", &s->tuvo_eva);
    json_get_int(obj, "pre_resp_o2_min", &s->pre_resp_o2_min);
    json_get_uint(obj, "sintomas_flags", &s->sintomas_flags);
    json_get_float(obj, "sueno_horas", &s->sueno_horas);
    json_get_int(obj, "sueno_calidad", &s->sueno_calidad);
    json_get_int(obj, "animo", &s->animo);
    json_get_int(obj, "estres", &s->estres);
    json_get_float(obj, "cognitivo_ms", &s->cognitivo_ms);
    json_get_int(obj, "cohesion_equipo", &s->cohesion_equipo);
    json_get_int(obj, "apetito_comida", &s->apetito_comida);
    json_get_int(obj, "calorias_kcal", &s->calorias_kcal);
    json_get_int(obj, "hidratacion_ml", &s->hidratacion_ml);

    const cJSON *fc_it = cJSON_GetObjectItemCaseSensitive(obj, "fase_ciclo");
    if (fc_it) {
        if (cJSON_IsString(fc_it) && fc_it->valuestring) {
            if (strcmp(fc_it->valuestring, "CYCLE_NA") == 0) s->fase_ciclo = CYCLE_NA;
            else if (strcmp(fc_it->valuestring, "CYCLE_MENSTRUAL") == 0) s->fase_ciclo = CYCLE_MENSTRUAL;
            else if (strcmp(fc_it->valuestring, "CYCLE_FOLLICULAR") == 0) s->fase_ciclo = CYCLE_FOLLICULAR;
            else if (strcmp(fc_it->valuestring, "CYCLE_OVULATORY") == 0) s->fase_ciclo = CYCLE_OVULATORY;
            else if (strcmp(fc_it->valuestring, "CYCLE_LUTEAL") == 0) s->fase_ciclo = CYCLE_LUTEAL;
            else if (strcmp(fc_it->valuestring, "CYCLE_SUPPRESSED") == 0) s->fase_ciclo = CYCLE_SUPPRESSED;
            else s->fase_ciclo = -1;
        } else {
            json_get_int(obj, "fase_ciclo", &s->fase_ciclo);
        }
    }
    json_get_int(obj, "dia_ciclo", &s->dia_ciclo);
    json_get_int(obj, "colico_menstrual", &s->colico_menstrual);
    json_get_int(obj, "flujo_menstrual", &s->flujo_menstrual);
    json_get_bool_int(obj, "suplemento_hierro", &s->suplemento_hierro);
}

/* =========================================================================
 * CabinEnv <-> JSON
 * ========================================================================= */
cJSON *cabin_to_json(const CabinEnv *env) {
    if (!env) return NULL;
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddNumberToObject(o, "presion_cabina_kpa", env->presion_cabina_kpa);
    cJSON_AddNumberToObject(o, "po2_kpa", env->po2_kpa);
    cJSON_AddNumberToObject(o, "pco2_kpa", env->pco2_kpa);
    cJSON_AddNumberToObject(o, "tasa_rad_msv_h", env->tasa_rad_msv_h);
    cJSON_AddNumberToObject(o, "evento_spe_activo", env->evento_spe_activo);
    cJSON_AddNumberToObject(o, "gravedad_actual", (int)env->gravedad_actual);
    cJSON_AddStringToObject(o, "gravedad_nombre", gravity_mode_to_str(env->gravedad_actual));
    cJSON_AddNumberToObject(o, "latencia_com_seg", env->latencia_com_seg);
    return o;
}

void cabin_from_json_partial(CabinEnv *env, const cJSON *obj) {
    if (!env || !cJSON_IsObject(obj)) return;
    json_get_float(obj, "presion_cabina_kpa", &env->presion_cabina_kpa);
    json_get_float(obj, "po2_kpa", &env->po2_kpa);
    json_get_float(obj, "pco2_kpa", &env->pco2_kpa);
    json_get_float(obj, "tasa_rad_msv_h", &env->tasa_rad_msv_h);
    json_get_bool_int(obj, "evento_spe_activo", &env->evento_spe_activo);
    json_get_float(obj, "latencia_com_seg", &env->latencia_com_seg);

    const cJSON *g = cJSON_GetObjectItemCaseSensitive(obj, "gravedad_actual");
    if (g) {
        if (cJSON_IsNumber(g) && !isnan(g->valuedouble) && !isinf(g->valuedouble) &&
            floor(g->valuedouble) == g->valuedouble) {
            env->gravedad_actual = (GravityMode)((int)g->valuedouble);
        } else if (cJSON_IsString(g) && g->valuestring) {
            if (strcmp(g->valuestring, "GRAV_MICRO") == 0) env->gravedad_actual = GRAV_MICRO;
            else if (strcmp(g->valuestring, "GRAV_MOON") == 0) env->gravedad_actual = GRAV_MOON;
            else if (strcmp(g->valuestring, "GRAV_MARS") == 0) env->gravedad_actual = GRAV_MARS;
            else if (strcmp(g->valuestring, "GRAV_EARTH") == 0) env->gravedad_actual = GRAV_EARTH;
            else if (strcmp(g->valuestring, "GRAV_DYNAMIC") == 0) env->gravedad_actual = GRAV_DYNAMIC;
            else env->gravedad_actual = (GravityMode)-1;
        } else {
            env->gravedad_actual = (GravityMode)-1;
        }
    }
}

/* =========================================================================
 * EvaluationReport <-> JSON
 * ========================================================================= */
cJSON *report_to_json(const EvaluationReport *rep) {
    if (!rep) return NULL;
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;

    cJSON_AddNumberToObject(o, "global_status", (int)rep->global_status);
    cJSON_AddStringToObject(o, "global_status_str", alert_severity_to_str(rep->global_status));
    cJSON_AddNumberToObject(o, "health_score", rep->health_score);
    cJSON_AddNumberToObject(o, "eva_aptitude", (int)rep->eva_aptitude);
    cJSON_AddStringToObject(o, "eva_aptitude_str", aptitude_to_str(rep->eva_aptitude));
    cJSON_AddStringToObject(o, "resumen_estado", rep->resumen_estado);
    cJSON_AddNumberToObject(o, "autonomous_mode", rep->autonomous_mode);
    cJSON_AddNumberToObject(o, "alert_count", rep->alert_count);
    cJSON_AddNumberToObject(o, "critical_alert_count", rep->critical_alert_count);
    cJSON_AddNumberToObject(o, "is_very_critical", rep->is_very_critical);

    cJSON *sys_arr = cJSON_AddArrayToObject(o, "system_status");
    for (int i = 0; i < BODY_SYSTEM_COUNT; i++) {
        cJSON_AddItemToArray(sys_arr, cJSON_CreateNumber((int)rep->system_status[i]));
    }

    cJSON *alerts_arr = cJSON_AddArrayToObject(o, "alerts");
    for (int i = 0; i < rep->alert_count; i++) {
        const HealthAlert *al = &rep->alerts[i];
        cJSON *ao = cJSON_CreateObject();
        cJSON_AddStringToObject(ao, "id_regla", al->id_regla);
        cJSON_AddNumberToObject(ao, "hazard", (int)al->hazard);
        cJSON_AddStringToObject(ao, "hazard_str", hazard_to_str(al->hazard));
        cJSON_AddNumberToObject(ao, "system", (int)al->system);
        cJSON_AddStringToObject(ao, "system_str", body_system_to_str(al->system));
        cJSON_AddNumberToObject(ao, "severity", (int)al->severity);
        cJSON_AddStringToObject(ao, "severity_str", alert_severity_to_str(al->severity));
        cJSON_AddStringToObject(ao, "referencia_nasa", al->referencia_nasa);
        cJSON_AddStringToObject(ao, "titulo", al->titulo);
        cJSON_AddStringToObject(ao, "explicacion", al->explicacion);
        cJSON_AddStringToObject(ao, "contramedida", al->contramedida);
        cJSON_AddItemToArray(alerts_arr, ao);
    }
    return o;
}

/* =========================================================================
 * Astronaut <-> JSON
 * ========================================================================= */
cJSON *astronaut_summary_to_json(const Astronaut *a) {
    if (!a) return NULL;
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;

    cJSON_AddStringToObject(o, "id", a->id);
    cJSON_AddStringToObject(o, "nombre", a->nombre);
    cJSON_AddStringToObject(o, "rol", a->rol);
    cJSON_AddStringToObject(o, "sexo", a->sexo[0] ? a->sexo : "M");
    cJSON_AddStringToObject(o, "mission_id", a->mission_id);
    cJSON_AddStringToObject(o, "avatar", a->avatar);
    cJSON_AddNumberToObject(o, "global_status", (int)a->report.global_status);
    cJSON_AddStringToObject(o, "global_status_str", alert_severity_to_str(a->report.global_status));
    cJSON_AddNumberToObject(o, "health_score", a->report.health_score);
    cJSON_AddNumberToObject(o, "eva_aptitude", (int)a->report.eva_aptitude);
    cJSON_AddStringToObject(o, "eva_aptitude_str", aptitude_to_str(a->report.eva_aptitude));
    cJSON_AddStringToObject(o, "resumen_estado", a->report.resumen_estado);
    cJSON_AddNumberToObject(o, "alert_count", a->report.alert_count);
    cJSON_AddNumberToObject(o, "critical_alert_count", a->report.critical_alert_count);
    cJSON_AddNumberToObject(o, "is_very_critical", a->report.is_very_critical);
    const DailyHealthSample *latest = astronaut_get_latest_sample(a);
    cJSON_AddNumberToObject(o, "fase_ciclo", latest ? (int)latest->fase_ciclo : 0);
    return o;
}

cJSON *astronaut_to_json(const Astronaut *a, int include_report) {
    if (!a) return NULL;
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;

    cJSON_AddStringToObject(o, "id", a->id);
    cJSON_AddStringToObject(o, "nombre", a->nombre);
    cJSON_AddStringToObject(o, "rol", a->rol);
    cJSON_AddStringToObject(o, "sexo", a->sexo[0] ? a->sexo : "M");
    cJSON_AddStringToObject(o, "mission_id", a->mission_id);
    cJSON_AddStringToObject(o, "avatar", a->avatar);
    cJSON_AddNumberToObject(o, "radiacion_previa_msv", a->radiacion_previa_msv);
    cJSON_AddNumberToObject(o, "radiacion_mision_msv", a->radiacion_mision_msv);
    cJSON_AddNumberToObject(o, "radiacion_total_msv", a->radiacion_previa_msv + a->radiacion_mision_msv);

    cJSON_AddItemToObject(o, "baseline", baseline_to_json(&a->baseline, 0));

    cJSON *hist = cJSON_AddArrayToObject(o, "history");
    for (int i = 0; i < a->history.count; i++) {
        const DailyHealthSample *s = ring_buffer_get_chronological(&a->history, i);
        if (s) {
            cJSON_AddItemToArray(hist, sample_to_json(s));
        }
    }

    if (include_report) {
        cJSON_AddItemToObject(o, "report", report_to_json(&a->report));
    }
    return o;
}

StatusCode astronaut_from_json(Astronaut *a, const cJSON *obj, const AstronautBaseline *defaults) {
    if (!a || !cJSON_IsObject(obj)) return ERR_PARSE;

    char id_buf[32] = {0};
    json_get_str(obj, "id", id_buf, sizeof(id_buf));
    if (!is_valid_id(id_buf)) return ERR_INVALID_ID;

    const cJSON *nom_it = cJSON_GetObjectItemCaseSensitive(obj, "nombre");
    if (nom_it && (!cJSON_IsString(nom_it) || !nom_it->valuestring || nom_it->valuestring[0] == '\0')) {
        return ERR_PARSE;
    }
    const cJSON *rol_it = cJSON_GetObjectItemCaseSensitive(obj, "rol");
    if (rol_it && (!cJSON_IsString(rol_it) || !rol_it->valuestring || rol_it->valuestring[0] == '\0')) {
        return ERR_PARSE;
    }
    const cJSON *sex_it = cJSON_GetObjectItemCaseSensitive(obj, "sexo");
    if (sex_it) {
        if (!cJSON_IsString(sex_it) || !is_valid_sex(sex_it->valuestring)) {
            return ERR_INVALID_RANGE;
        }
    }
    const cJSON *mid_it = cJSON_GetObjectItemCaseSensitive(obj, "mission_id");
    if (mid_it && (!cJSON_IsString(mid_it) || !is_valid_id(mid_it->valuestring))) {
        return ERR_INVALID_ID;
    }
    const cJSON *ava_it = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    if (ava_it && (!cJSON_IsString(ava_it) || !ava_it->valuestring || ava_it->valuestring[0] == '\0')) {
        return ERR_PARSE;
    }

    astronaut_init_from_defaults(a, id_buf, NULL, NULL, NULL, NULL, defaults);
    json_get_str(obj, "nombre", a->nombre, sizeof(a->nombre));
    json_get_str(obj, "rol", a->rol, sizeof(a->rol));
    if (sex_it && sex_it->valuestring) {
        normalize_sex_str(a->sexo, sizeof(a->sexo), sex_it->valuestring);
    }
    json_get_str(obj, "mission_id", a->mission_id, sizeof(a->mission_id));
    if (!is_valid_id(a->mission_id)) return ERR_INVALID_ID;
    if (ava_it && ava_it->valuestring) {
        if (strncmp(ava_it->valuestring, "data:image/", 11) == 0) {
            StatusCode arc = storage_save_custom_avatar(a->id, ava_it->valuestring, a->avatar, sizeof(a->avatar));
            if (arc != OK) return arc;
        } else {
            safe_strcpy(a->avatar, sizeof(a->avatar), ava_it->valuestring);
        }
    }
    json_get_float(obj, "radiacion_previa_msv", &a->radiacion_previa_msv);
    if (isnan(a->radiacion_previa_msv) || isinf(a->radiacion_previa_msv) ||
        a->radiacion_previa_msv < 0.0f || a->radiacion_previa_msv > 10000.0f) {
        return ERR_INVALID_RANGE;
    }

    float explicit_rad_mision = 0.0f;
    int has_rad_mision = cJSON_HasObjectItem(obj, "radiacion_mision_msv");
    if (has_rad_mision) {
        json_get_float(obj, "radiacion_mision_msv", &explicit_rad_mision);
        if (isnan(explicit_rad_mision) || isinf(explicit_rad_mision) ||
            explicit_rad_mision < 0.0f || explicit_rad_mision > 10000.0f) {
            return ERR_INVALID_RANGE;
        }
    }

    const cJSON *base_obj = cJSON_GetObjectItemCaseSensitive(obj, "baseline");
    if (base_obj) {
        if (!cJSON_IsObject(base_obj)) return ERR_PARSE;
        baseline_from_json(&a->baseline, base_obj);
        StatusCode brc = validate_baseline(&a->baseline);
        if (brc != OK) return brc;
    }

    const cJSON *hist_arr = cJSON_GetObjectItemCaseSensitive(obj, "history");
    if (hist_arr) {
        if (!cJSON_IsArray(hist_arr)) return ERR_PARSE;
        const cJSON *elem = NULL;
        int day_counter = 1;
        cJSON_ArrayForEach(elem, hist_arr) {
            if (!cJSON_IsObject(elem)) return ERR_PARSE;
            DailyHealthSample s;
            sample_from_baseline(&s, &a->baseline, day_counter++);
            sample_from_json_partial(&s, elem);
            StatusCode src = astronaut_add_sample(a, &s);
            if (src != OK) return src;
        }
    }

    if (has_rad_mision && explicit_rad_mision > a->radiacion_mision_msv) {
        a->radiacion_mision_msv = explicit_rad_mision;
    }

    /* Si no trajo ninguna muestra en history[], creamos la muestra inicial Dia 1 */
    if (a->history.count == 0) {
        DailyHealthSample s0;
        sample_from_baseline(&s0, &a->baseline, 1);
        StatusCode src0 = astronaut_add_sample(a, &s0);
        if (src0 != OK) return src0;
    }

    return OK;
}

/* =========================================================================
 * Mission & MissionHealthSummary <-> JSON
 * ========================================================================= */
cJSON *mission_to_json(const Mission *m) {
    if (!m) return NULL;
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;

    cJSON_AddStringToObject(o, "id", m->id);
    cJSON_AddStringToObject(o, "nombre", m->nombre);
    cJSON_AddStringToObject(o, "destino", m->destino);
    cJSON_AddNumberToObject(o, "dia_actual", m->dia_actual);
    cJSON_AddNumberToObject(o, "crew_count", m->crew_count);
    cJSON_AddItemToObject(o, "env", cabin_to_json(&m->env));

    cJSON *crew_arr = cJSON_AddArrayToObject(o, "crew_ids");
    for (int i = 0; i < m->crew_count; i++) {
        cJSON_AddItemToArray(crew_arr, cJSON_CreateString(m->crew_ids[i]));
    }
    return o;
}

cJSON *mission_health_summary_to_json(const MissionHealthSummary *sum) {
    if (!sum) return NULL;
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;

    cJSON_AddNumberToObject(o, "mission_health_avg", sum->mission_health_avg);
    cJSON_AddNumberToObject(o, "crew_ok_count", sum->crew_ok_count);
    cJSON_AddNumberToObject(o, "crew_caution_count", sum->crew_caution_count);
    cJSON_AddNumberToObject(o, "crew_critical_count", sum->crew_critical_count);
    cJSON_AddNumberToObject(o, "crew_very_critical_count", sum->crew_very_critical_count);
    cJSON_AddStringToObject(o, "priority_astronaut_id", sum->priority_astronaut_id);
    cJSON_AddNumberToObject(o, "fc_avg", sum->fc_avg);
    cJSON_AddNumberToObject(o, "spo2_avg", sum->spo2_avg);
    cJSON_AddNumberToObject(o, "sueno_avg_h", sum->sueno_avg_h);
    cJSON_AddNumberToObject(o, "estres_avg", sum->estres_avg);
    cJSON_AddNumberToObject(o, "rad_mision_avg_msv", sum->rad_mision_avg_msv);

    cJSON *haz_arr = cJSON_AddArrayToObject(o, "hazard_status");
    for (int i = 0; i < NASA_HAZARD_COUNT; i++) {
        cJSON_AddItemToArray(haz_arr, cJSON_CreateNumber((int)sum->hazard_status[i]));
    }
    return o;
}

StatusCode mission_from_json(Mission *m, const cJSON *obj) {
    if (!m || !cJSON_IsObject(obj)) return ERR_PARSE;

    char id_buf[32] = {0};
    json_get_str(obj, "id", id_buf, sizeof(id_buf));
    if (!is_valid_id(id_buf)) return ERR_INVALID_ID;

    const cJSON *nom_it = cJSON_GetObjectItemCaseSensitive(obj, "nombre");
    if (nom_it && (!cJSON_IsString(nom_it) || !nom_it->valuestring || nom_it->valuestring[0] == '\0')) {
        return ERR_PARSE;
    }
    const cJSON *dst_it = cJSON_GetObjectItemCaseSensitive(obj, "destino");
    if (dst_it && (!cJSON_IsString(dst_it) || !dst_it->valuestring || dst_it->valuestring[0] == '\0')) {
        return ERR_PARSE;
    }

    mission_init(m, id_buf, NULL, NULL);
    json_get_str(obj, "nombre", m->nombre, sizeof(m->nombre));
    json_get_str(obj, "destino", m->destino, sizeof(m->destino));
    json_get_int(obj, "dia_actual", &m->dia_actual);
    if (m->dia_actual < 1 || m->dia_actual > 10000) return ERR_INVALID_RANGE;

    const cJSON *env_obj = cJSON_GetObjectItemCaseSensitive(obj, "env");
    if (env_obj) {
        if (!cJSON_IsObject(env_obj)) return ERR_PARSE;
        cabin_from_json_partial(&m->env, env_obj);
        StatusCode vrc = validate_cabin(&m->env);
        if (vrc != OK) return vrc;
    }

    const cJSON *crew_arr = cJSON_GetObjectItemCaseSensitive(obj, "crew_ids");
    if (crew_arr) {
        if (!cJSON_IsArray(crew_arr)) return ERR_PARSE;
        const cJSON *elem = NULL;
        cJSON_ArrayForEach(elem, crew_arr) {
            if (!cJSON_IsString(elem) || !is_valid_id(elem->valuestring)) {
                return ERR_INVALID_ID;
            }
            StatusCode crc = mission_add_crew(m, elem->valuestring);
            if (crc != OK) return crc;
        }
    }
    return OK;
}

/* =========================================================================
 * Lectura y Escritura Atomica en Disco
 * ========================================================================= */
static cJSON *read_json_file(const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long fsize = ftell(fp);
    if (fsize <= 0 || fsize > 4 * 1024 * 1024) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    char *buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);
    buf[nread] = '\0';

    cJSON *root = cJSON_ParseWithLength(buf, nread);
    free(buf);
    return root;
}

static StatusCode write_json_atomic(const char *filepath, const cJSON *json) {
    if (!filepath || !json) return ERR_IO;
    char *printed = cJSON_Print(json);
    if (!printed) return ERR_IO;

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", filepath);

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) {
        free(printed);
        return ERR_IO;
    }

    size_t len = strlen(printed);
    size_t nwritten = fwrite(printed, 1, len, fp);
    fputc('\n', fp);
    int flush_rc = fflush(fp);
    int close_rc = fclose(fp);
    free(printed);

    if (nwritten != len || flush_rc != 0 || close_rc != 0) {
        unlink(tmp_path);
        return ERR_IO;
    }

    if (rename(tmp_path, filepath) != 0) {
        unlink(tmp_path);
        return ERR_IO;
    }
    return OK;
}

static void ensure_dir_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        mkdir(path, 0755);
    }
}

static int b64_val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

StatusCode storage_save_custom_avatar(const char *id_hint, const char *data_url, char *out_rel_path, size_t out_size) {
    if (!data_url || !out_rel_path || out_size < 32) return ERR_PARSE;
    if (strncmp(data_url, "data:image/", 11) != 0) return ERR_PARSE;

    const char *mime_start = data_url + 11;
    const char *semi = strchr(mime_start, ';');
    const char *comma = strchr(mime_start, ',');
    if (!semi || !comma || semi > comma) return ERR_PARSE;
    if (strncmp(semi, ";base64,", 8) != 0) return ERR_PARSE;

    size_t mime_len = (size_t)(semi - mime_start);
    const char *ext = NULL;
    if (mime_len == 3 && strncmp(mime_start, "png", 3) == 0) ext = "png";
    else if (mime_len == 4 && strncmp(mime_start, "jpeg", 4) == 0) ext = "jpg";
    else if (mime_len == 3 && strncmp(mime_start, "jpg", 3) == 0) ext = "jpg";
    else if (mime_len == 4 && strncmp(mime_start, "webp", 4) == 0) ext = "webp";
    else if (mime_len == 3 && strncmp(mime_start, "gif", 3) == 0) ext = "gif";
    else if (mime_len == 7 && strncmp(mime_start, "svg+xml", 7) == 0) ext = "svg";
    else return ERR_INVALID_RANGE;

    const char *b64 = comma + 1;
    size_t b64_len = strlen(b64);
    if (b64_len < 4 || b64_len > (size_t)(3 * 1024 * 1024)) return ERR_INVALID_RANGE;

    size_t max_decoded = (b64_len / 4) * 3 + 4;
    unsigned char *decoded = (unsigned char *)malloc(max_decoded);
    if (!decoded) return ERR_IO;

    size_t out_len = 0;
    int val = 0, valb = -8;
    for (size_t i = 0; i < b64_len; i++) {
        unsigned char c = (unsigned char)b64[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '=') break;
        int d = b64_val(c);
        if (d < 0) {
            free(decoded);
            return ERR_PARSE;
        }
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            decoded[out_len++] = (unsigned char)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }

    if (out_len == 0 || out_len > (size_t)(2 * 1024 * 1024)) {
        free(decoded);
        return ERR_INVALID_RANGE;
    }

    char safe_id[32];
    const char *src_id = (id_hint && is_valid_id(id_hint)) ? id_hint : "user";
    safe_strcpy(safe_id, sizeof(safe_id), src_id);

    ensure_dir_exists("web");
    ensure_dir_exists("web/assets");
    ensure_dir_exists("web/assets/avatars");

    char rel_path[256];
    snprintf(rel_path, sizeof(rel_path), "assets/avatars/custom_%s.%s", safe_id, ext);

    char full_path[512];
    snprintf(full_path, sizeof(full_path), "web/%s", rel_path);

    char tmp_path[520];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", full_path);

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) {
        free(decoded);
        return ERR_IO;
    }
    size_t nw = fwrite(decoded, 1, out_len, fp);
    int fl = fflush(fp);
    int cl = fclose(fp);
    free(decoded);

    if (nw != out_len || fl != 0 || cl != 0) {
        unlink(tmp_path);
        return ERR_IO;
    }
    if (rename(tmp_path, full_path) != 0) {
        unlink(tmp_path);
        return ERR_IO;
    }

    safe_strcpy(out_rel_path, out_size, rel_path);
    return OK;
}

StatusCode storage_save_defaults(const AppState *state) {
    if (!state) return ERR_IO;
    ensure_dir_exists(state->data_dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/defaults.json", state->data_dir);

    cJSON *json = baseline_to_json(&state->defaults, 0);
    if (!json) return ERR_IO;
    StatusCode rc = write_json_atomic(path, json);
    cJSON_Delete(json);
    return rc;
}

StatusCode storage_save_astronaut(const AppState *state, const Astronaut *a) {
    if (!state || !a || !is_valid_id(a->id)) return ERR_INVALID_ID;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/astronauts", state->data_dir);
    ensure_dir_exists(state->data_dir);
    ensure_dir_exists(dir);

    char path[512];
    snprintf(path, sizeof(path), "%s/astronauts/%s.json", state->data_dir, a->id);

    cJSON *json = astronaut_to_json(a, 0);
    if (!json) return ERR_IO;
    StatusCode rc = write_json_atomic(path, json);
    cJSON_Delete(json);
    return rc;
}

StatusCode storage_delete_astronaut_file(const AppState *state, const char *astronaut_id) {
    if (!state || !is_valid_id(astronaut_id)) return ERR_INVALID_ID;
    char path[512];
    snprintf(path, sizeof(path), "%s/astronauts/%s.json", state->data_dir, astronaut_id);
    if (unlink(path) != 0) return ERR_IO;
    return OK;
}

StatusCode storage_save_mission(const AppState *state, const Mission *m) {
    if (!state || !m || !is_valid_id(m->id)) return ERR_INVALID_ID;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/missions", state->data_dir);
    ensure_dir_exists(state->data_dir);
    ensure_dir_exists(dir);

    char path[512];
    snprintf(path, sizeof(path), "%s/missions/%s.json", state->data_dir, m->id);

    cJSON *json = mission_to_json(m);
    if (!json) return ERR_IO;
    StatusCode rc = write_json_atomic(path, json);
    cJSON_Delete(json);
    return rc;
}

static int cmp_str_ptr(const void *a, const void *b) {
    const char *sa = *(const char * const *)a;
    const char *sb = *(const char * const *)b;
    return strcmp(sa, sb);
}

static int has_json_ext(const char *name) {
    if (!name) return 0;
    size_t len = strlen(name);
    if (len <= 5) return 0;
    return (strcmp(name + len - 5, ".json") == 0);
}

StatusCode storage_load_all(AppState *state, const char *data_dir) {
    if (!state) return ERR_IO;
    memset(state, 0, sizeof(*state));
    safe_strcpy(state->data_dir, sizeof(state->data_dir), (data_dir && data_dir[0]) ? data_dir : "data");

    /* 1. Cargar defaults.json */
    baseline_init_nasa_defaults(&state->defaults);
    char def_path[512];
    snprintf(def_path, sizeof(def_path), "%s/defaults.json", state->data_dir);
    cJSON *def_json = read_json_file(def_path);
    if (def_json) {
        baseline_from_json(&state->defaults, def_json);
        cJSON_Delete(def_json);
    } else {
        storage_save_defaults(state);
    }

    /* 2. Cargar misiones desde el subdirectorio missions/ */
    char mis_dir[512];
    snprintf(mis_dir, sizeof(mis_dir), "%s/missions", state->data_dir);
    DIR *dm = opendir(mis_dir);
    if (dm) {
        char *names[MAX_MISSIONS * 2];
        int ncount = 0;
        struct dirent *de;
        while ((de = readdir(dm)) != NULL && ncount < MAX_MISSIONS * 2) {
            if (has_json_ext(de->d_name)) {
                size_t l = strlen(de->d_name) + 1;
                names[ncount] = (char *)malloc(l);
                if (names[ncount]) {
                    memcpy(names[ncount], de->d_name, l);
                    ncount++;
                }
            }
        }
        closedir(dm);
        if (ncount > 1) {
            qsort(names, (size_t)ncount, sizeof(char *), cmp_str_ptr);
        }
        for (int i = 0; i < ncount; i++) {
            if (state->mission_count < MAX_MISSIONS) {
                char fpath[1024];
                snprintf(fpath, sizeof(fpath), "%s/%s", mis_dir, names[i]);
                cJSON *mj = read_json_file(fpath);
                if (mj) {
                    Mission m;
                    if (mission_from_json(&m, mj) == OK) {
                        state->missions[state->mission_count++] = m;
                    }
                    cJSON_Delete(mj);
                }
            }
            free(names[i]);
        }
    }

    /* Si no habia misiones en disco, crear mision por defecto artemis_sim */
    if (state->mission_count == 0) {
        mission_init(&state->missions[0],
                     "artemis_sim",
                     "Artemis IV / Transito a Marte — Sol 14",
                     "Transito a Marte");
        state->mission_count = 1;
        storage_save_mission(state, &state->missions[0]);
    }

    /* 3. Cargar astronautas desde el subdirectorio astronauts/ */
    char ast_dir[512];
    snprintf(ast_dir, sizeof(ast_dir), "%s/astronauts", state->data_dir);
    DIR *da = opendir(ast_dir);
    if (da) {
        char *names[MAX_ASTRONAUTS * 2];
        int ncount = 0;
        struct dirent *de;
        while ((de = readdir(da)) != NULL && ncount < MAX_ASTRONAUTS * 2) {
            if (has_json_ext(de->d_name)) {
                size_t l = strlen(de->d_name) + 1;
                names[ncount] = (char *)malloc(l);
                if (names[ncount]) {
                    memcpy(names[ncount], de->d_name, l);
                    ncount++;
                }
            }
        }
        closedir(da);
        if (ncount > 1) {
            qsort(names, (size_t)ncount, sizeof(char *), cmp_str_ptr);
        }
        for (int i = 0; i < ncount; i++) {
            if (state->astronaut_count < MAX_ASTRONAUTS) {
                char fpath[1024];
                snprintf(fpath, sizeof(fpath), "%s/%s", ast_dir, names[i]);
                cJSON *aj = read_json_file(fpath);
                if (aj) {
                    Astronaut a;
                    if (astronaut_from_json(&a, aj, &state->defaults) == OK) {
                        state->astronauts[state->astronaut_count++] = a;
                        Mission *m = state_find_mission(state, a.mission_id);
                        if (m) {
                            mission_add_crew(m, a.id);
                        }
                    }
                    cJSON_Delete(aj);
                }
            }
            free(names[i]);
        }
    }

    /* 4. Evaluar a toda la tripulacion */
    evaluate_all(state);
    return OK;
}

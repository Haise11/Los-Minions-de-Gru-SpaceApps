#include "astronaut.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void safe_strcpy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

int is_valid_id(const char *id) {
    if (!id) return 0;
    size_t len = strlen(id);
    if (len == 0 || len > 15) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)id[i];
        if (!isalnum(c) && c != '_' && c != '-') {
            return 0;
        }
    }
    return 1;
}

int is_valid_sex(const char *sexo) {
    if (!sexo || sexo[0] == '\0') return 0;
    if (strcmp(sexo, "F") == 0 || strcmp(sexo, "f") == 0 ||
        strcmp(sexo, "Femenino") == 0 || strcmp(sexo, "femenino") == 0 ||
        strcmp(sexo, "Mujer") == 0 || strcmp(sexo, "mujer") == 0 ||
        strcmp(sexo, "M") == 0 || strcmp(sexo, "m") == 0 ||
        strcmp(sexo, "Masculino") == 0 || strcmp(sexo, "masculino") == 0 ||
        strcmp(sexo, "Hombre") == 0 || strcmp(sexo, "hombre") == 0) {
        return 1;
    }
    return 0;
}

void normalize_sex_str(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src || src[0] == '\0') {
        safe_strcpy(dst, dst_size, "M");
        return;
    }
    unsigned char first = (unsigned char)src[0];
    if (first == 'F' || first == 'f' || strcmp(src, "Mujer") == 0 || strcmp(src, "mujer") == 0) {
        safe_strcpy(dst, dst_size, "F");
    } else {
        safe_strcpy(dst, dst_size, "M");
    }
}

void baseline_init_nasa_defaults(AstronautBaseline *b) {
    if (!b) return;
    memset(b, 0, sizeof(*b));
    b->fc_base                  = 70.0f;
    b->pa_sis_base              = 120.0f;
    b->pa_dia_base              = 80.0f;
    b->spo2_base                = 98.0f;
    b->temp_base                = 36.6f;
    b->glucosa_base             = 90.0f;
    b->peso_base                = 72.0f;
    b->sueno_base_h             = 7.5f;
    b->animo_base               = 8;
    b->estres_base              = 3;
    b->cognitivo_base_ms        = 260.0f;
    b->calorias_objetivo_kcal   = 2500;
    b->hidratacion_objetivo_ml  = 2500;
    b->apetito_base             = 4;
    b->vision_base              = 1.0f;
    b->densidad_osea_base_pct   = 100.0f;
    b->ejercicio_objetivo_min   = 90;
    b->pre_resp_o2_objetivo_min = 120;
    b->dosis_diaria_nominal_msv = 0.5f;
}

void astronaut_init_from_defaults(Astronaut *a,
                                  const char *id,
                                  const char *nombre,
                                  const char *rol,
                                  const char *mission_id,
                                  const char *avatar,
                                  const AstronautBaseline *defaults) {
    if (!a) return;
    memset(a, 0, sizeof(*a));

    safe_strcpy(a->id, sizeof(a->id), id ? id : "astro_new");
    safe_strcpy(a->nombre, sizeof(a->nombre), (nombre && nombre[0]) ? nombre : "Tripulante");
    safe_strcpy(a->rol, sizeof(a->rol), (rol && rol[0]) ? rol : "Especialista de Mision");
    safe_strcpy(a->sexo, sizeof(a->sexo), "M");
    safe_strcpy(a->mission_id, sizeof(a->mission_id), (mission_id && mission_id[0]) ? mission_id : "artemis_sim");
    safe_strcpy(a->avatar, sizeof(a->avatar), (avatar && avatar[0]) ? avatar : "assets/avatars/astro_01.svg");

    if (defaults) {
        a->baseline = *defaults;
    } else {
        baseline_init_nasa_defaults(&a->baseline);
    }

    a->radiacion_previa_msv = 12.0f;
    a->radiacion_mision_msv = 0.0f;
    a->history.head         = 0;
    a->history.count        = 0;

    a->report.global_status = ALERT_OK;
    a->report.health_score  = 100;
    a->report.eva_aptitude  = APT_GO;
    safe_strcpy(a->report.resumen_estado, sizeof(a->report.resumen_estado), "Nominal");
}

void sample_from_baseline(DailyHealthSample *s, const AstronautBaseline *b, int dia_mision) {
    if (!s) return;
    AstronautBaseline local_b;
    if (!b) {
        baseline_init_nasa_defaults(&local_b);
        b = &local_b;
    }
    memset(s, 0, sizeof(*s));
    s->dia_mision       = (dia_mision > 0) ? dia_mision : 1;
    s->timestamp_unix   = (long)time(NULL);
    s->fc               = b->fc_base;
    s->pa_sis           = b->pa_sis_base;
    s->pa_dia           = b->pa_dia_base;
    s->spo2             = b->spo2_base;
    s->temp_c           = b->temp_base;
    s->glucosa_mg_dl    = b->glucosa_base;
    s->peso_kg          = b->peso_base;
    s->dosis_dia_msv    = b->dosis_diaria_nominal_msv;
    s->vision_actual    = b->vision_base;
    s->dolor_cabeza     = 0;
    s->mareo_vestibular = 0;
    s->ejercicio_min    = b->ejercicio_objetivo_min;
    s->tuvo_eva         = 0;
    s->pre_resp_o2_min  = b->pre_resp_o2_objetivo_min;
    s->sintomas_flags   = SYM_VIT_D_TAKEN;
    s->sueno_horas      = b->sueno_base_h;
    s->sueno_calidad    = 4;
    s->animo            = b->animo_base;
    s->estres           = b->estres_base;
    s->cognitivo_ms     = b->cognitivo_base_ms;
    s->cohesion_equipo  = 4;
    s->apetito_comida   = b->apetito_base;
    s->calorias_kcal    = b->calorias_objetivo_kcal;
    s->hidratacion_ml   = b->hidratacion_objetivo_ml;
    s->fase_ciclo       = CYCLE_NA;
    s->dia_ciclo        = 0;
    s->colico_menstrual = 0;
    s->flujo_menstrual  = 0;
    s->suplemento_hierro = 0;
}

void sample_prepare_partial_base(DailyHealthSample *out, const Astronaut *a, int dia_mision) {
    if (!out || !a) return;
    if (dia_mision > 0 && a->history.count > 0) {
        for (int i = a->history.count - 1; i >= 0; i--) {
            const DailyHealthSample *existing = ring_buffer_get_chronological(&a->history, i);
            if (existing && existing->dia_mision == dia_mision) {
                *out = *existing;
                out->timestamp_unix = (long)time(NULL);
                return;
            }
        }
    }
    const DailyHealthSample *latest = astronaut_get_latest_sample(a);
    if (latest) {
        *out = *latest;
        if (dia_mision > 0) {
            if (dia_mision != latest->dia_mision) {
                /* Nuevo dia: no arrastrar evento EVA puntual del dia anterior */
                out->tuvo_eva = 0;
            }
            out->dia_mision = dia_mision;
        } else {
            out->dia_mision = latest->dia_mision + 1;
            out->tuvo_eva   = 0;
        }
        out->timestamp_unix = (long)time(NULL);
    } else {
        sample_from_baseline(out, &a->baseline, (dia_mision > 0) ? dia_mision : 1);
    }
}

static int is_valid_float(float v, float min_v, float max_v) {
    if (isnan(v) || isinf(v)) return 0;
    return (v >= min_v && v <= max_v);
}

StatusCode validate_baseline(const AstronautBaseline *b) {
    if (!b) return ERR_INVALID_RANGE;
    if (!is_valid_float(b->fc_base, 30.0f, 200.0f))                  return ERR_INVALID_RANGE;
    if (!is_valid_float(b->pa_sis_base, 60.0f, 220.0f))              return ERR_INVALID_RANGE;
    if (!is_valid_float(b->pa_dia_base, 35.0f, 140.0f))              return ERR_INVALID_RANGE;
    if (b->pa_dia_base >= b->pa_sis_base)                            return ERR_INVALID_RANGE;
    if (!is_valid_float(b->spo2_base, 70.0f, 100.0f))                return ERR_INVALID_RANGE;
    if (!is_valid_float(b->temp_base, 34.0f, 40.0f))                 return ERR_INVALID_RANGE;
    if (!is_valid_float(b->glucosa_base, 40.0f, 300.0f))             return ERR_INVALID_RANGE;
    if (!is_valid_float(b->peso_base, 25.0f, 250.0f))                return ERR_INVALID_RANGE;
    if (!is_valid_float(b->sueno_base_h, 1.0f, 16.0f))               return ERR_INVALID_RANGE;
    if (b->animo_base < 1 || b->animo_base > 10)                     return ERR_INVALID_RANGE;
    if (b->estres_base < 1 || b->estres_base > 10)                   return ERR_INVALID_RANGE;
    if (!is_valid_float(b->cognitivo_base_ms, 80.0f, 2000.0f))       return ERR_INVALID_RANGE;
    if (b->calorias_objetivo_kcal < 500 || b->calorias_objetivo_kcal > 8000)   return ERR_INVALID_RANGE;
    if (b->hidratacion_objetivo_ml < 500 || b->hidratacion_objetivo_ml > 8000) return ERR_INVALID_RANGE;
    if (b->apetito_base < 1 || b->apetito_base > 5)                 return ERR_INVALID_RANGE;
    if (b->ejercicio_objetivo_min < 0 || b->ejercicio_objetivo_min > 600)      return ERR_INVALID_RANGE;
    if (b->pre_resp_o2_objetivo_min < 0 || b->pre_resp_o2_objetivo_min > 600)  return ERR_INVALID_RANGE;
    if (!is_valid_float(b->vision_base, 0.1f, 2.5f))                 return ERR_INVALID_RANGE;
    if (!is_valid_float(b->densidad_osea_base_pct, 20.0f, 150.0f))   return ERR_INVALID_RANGE;
    if (!is_valid_float(b->dosis_diaria_nominal_msv, 0.0f, 100.0f))  return ERR_INVALID_RANGE;
    return OK;
}

StatusCode validate_sample(const DailyHealthSample *s) {
    if (!s) return ERR_INVALID_RANGE;
    if (s->dia_mision < 1 || s->dia_mision > 10000) return ERR_INVALID_RANGE;

    /* Rangos fisiologicos y fisicos obligatorios en functions.md */
    if (!is_valid_float(s->temp_c, 30.0f, 43.0f))         return ERR_INVALID_RANGE;
    if (!is_valid_float(s->spo2, 40.0f, 100.0f))          return ERR_INVALID_RANGE;
    if (!is_valid_float(s->fc, 25.0f, 230.0f))            return ERR_INVALID_RANGE;
    if (!is_valid_float(s->pa_sis, 50.0f, 250.0f))        return ERR_INVALID_RANGE;
    if (!is_valid_float(s->pa_dia, 30.0f, 150.0f))        return ERR_INVALID_RANGE;
    if (s->pa_dia >= s->pa_sis)                           return ERR_INVALID_RANGE;
    if (!is_valid_float(s->sueno_horas, 0.0f, 24.0f))     return ERR_INVALID_RANGE;

    /* Validaciones complementarias de seguridad */
    if (!is_valid_float(s->glucosa_mg_dl, 20.0f, 600.0f)) return ERR_INVALID_RANGE;
    if (!is_valid_float(s->peso_kg, 20.0f, 250.0f))       return ERR_INVALID_RANGE;
    if (!is_valid_float(s->dosis_dia_msv, 0.0f, 5000.0f)) return ERR_INVALID_RANGE;
    if (!is_valid_float(s->vision_actual, 0.05f, 2.5f))   return ERR_INVALID_RANGE;
    if (!is_valid_float(s->cognitivo_ms, 50.0f, 5000.0f)) return ERR_INVALID_RANGE;

    if (s->dolor_cabeza < 0 || s->dolor_cabeza > 10)         return ERR_INVALID_RANGE;
    if (s->mareo_vestibular < 0 || s->mareo_vestibular > 10) return ERR_INVALID_RANGE;
    if (s->ejercicio_min < 0 || s->ejercicio_min > 600)      return ERR_INVALID_RANGE;
    if (s->tuvo_eva < 0 || s->tuvo_eva > 1)                  return ERR_INVALID_RANGE;
    if (s->pre_resp_o2_min < 0 || s->pre_resp_o2_min > 600)  return ERR_INVALID_RANGE;
    if (s->sueno_calidad < 1 || s->sueno_calidad > 5)        return ERR_INVALID_RANGE;
    if (s->animo < 1 || s->animo > 10)                       return ERR_INVALID_RANGE;
    if (s->estres < 1 || s->estres > 10)                     return ERR_INVALID_RANGE;
    if (s->cohesion_equipo < 1 || s->cohesion_equipo > 5)    return ERR_INVALID_RANGE;
    if (s->apetito_comida < 1 || s->apetito_comida > 5)      return ERR_INVALID_RANGE;
    if (s->calorias_kcal < 0 || s->calorias_kcal > 10000)    return ERR_INVALID_RANGE;
    if (s->hidratacion_ml < 0 || s->hidratacion_ml > 10000)  return ERR_INVALID_RANGE;
    if (s->sintomas_flags > 0x1FFu)                          return ERR_INVALID_RANGE;

    if (s->fase_ciclo < 0 || s->fase_ciclo > 5)              return ERR_INVALID_RANGE;
    if (s->dia_ciclo < 0 || s->dia_ciclo > 60)               return ERR_INVALID_RANGE;
    if (s->colico_menstrual < 0 || s->colico_menstrual > 10) return ERR_INVALID_RANGE;
    if (s->flujo_menstrual < 0 || s->flujo_menstrual > 3)    return ERR_INVALID_RANGE;
    if (s->suplemento_hierro < 0 || s->suplemento_hierro > 1) return ERR_INVALID_RANGE;

    return OK;
}

void ring_buffer_push_raw(SampleRingBuffer *rb, const DailyHealthSample *s) {
    if (!rb || !s) return;
    rb->items[rb->head] = *s;
    rb->head = (rb->head + 1) % MAX_HISTORY_SAMPLES;
    if (rb->count < MAX_HISTORY_SAMPLES) {
        rb->count++;
    }
}

StatusCode astronaut_add_sample(Astronaut *a, const DailyHealthSample *s) {
    if (!a || !s) return ERR_INVALID_RANGE;
    StatusCode rc = validate_sample(s);
    if (rc != OK) return rc;

    SampleRingBuffer *rb = &a->history;
    /* Si ya existe una muestra en el mismo dia_mision (> 0) en cualquier posicion
     * del buffer circular, la actualizamos in-place ajustando el diferencial de dosis */
    if (rb->count > 0 && s->dia_mision > 0) {
        int oldest_idx = (rb->head - rb->count + MAX_HISTORY_SAMPLES) % MAX_HISTORY_SAMPLES;
        for (int i = rb->count - 1; i >= 0; i--) {
            int real_idx = (oldest_idx + i) % MAX_HISTORY_SAMPLES;
            if (rb->items[real_idx].dia_mision == s->dia_mision) {
                float delta_rad = s->dosis_dia_msv - rb->items[real_idx].dosis_dia_msv;
                rb->items[real_idx] = *s;
                a->radiacion_mision_msv += delta_rad;
                if (a->radiacion_mision_msv < 0.0f) a->radiacion_mision_msv = 0.0f;
                return OK;
            }
        }

        /* Si el buffer esta lleno (90 muestras) y se envia un dia_mision anterior al mas antiguo
         * en la ventana, acumulamos la dosis de mision sin desalojar muestras mas recientes */
        if (rb->count == MAX_HISTORY_SAMPLES &&
            s->dia_mision < rb->items[oldest_idx].dia_mision) {
            a->radiacion_mision_msv += s->dosis_dia_msv;
            return OK;
        }
    }

    ring_buffer_push_raw(rb, s);
    a->radiacion_mision_msv += s->dosis_dia_msv;

    /* Mantener orden cronologico ascendente por dia_mision ante inserciones no monotonicas */
    if (rb->count > 1) {
        int oldest_idx = (rb->head - rb->count + MAX_HISTORY_SAMPLES) % MAX_HISTORY_SAMPLES;
        for (int i = rb->count - 1; i > 0; i--) {
            int curr_real = (oldest_idx + i) % MAX_HISTORY_SAMPLES;
            int prev_real = (oldest_idx + i - 1) % MAX_HISTORY_SAMPLES;
            if (rb->items[curr_real].dia_mision < rb->items[prev_real].dia_mision) {
                DailyHealthSample tmp = rb->items[curr_real];
                rb->items[curr_real]  = rb->items[prev_real];
                rb->items[prev_real]  = tmp;
            } else {
                break;
            }
        }
    }
    return OK;
}

const DailyHealthSample *astronaut_get_latest_sample(const Astronaut *a) {
    if (!a || a->history.count <= 0) return NULL;
    int last_idx = (a->history.head - 1 + MAX_HISTORY_SAMPLES) % MAX_HISTORY_SAMPLES;
    return &a->history.items[last_idx];
}

const DailyHealthSample *ring_buffer_get_chronological(const SampleRingBuffer *rb, int index) {
    if (!rb || index < 0 || index >= rb->count) return NULL;
    int oldest_idx = (rb->head - rb->count + MAX_HISTORY_SAMPLES) % MAX_HISTORY_SAMPLES;
    int real_idx = (oldest_idx + index) % MAX_HISTORY_SAMPLES;
    return &rb->items[real_idx];
}

int astronaut_compute_window_averages(const Astronaut *a,
                                      int window_days,
                                      float *out_sueno_avg,
                                      float *out_ejercicio_avg,
                                      float *out_calorias_avg,
                                      float *out_hidratacion_avg) {
    if (!a || a->history.count <= 0 || window_days <= 0) {
        if (a) {
            if (out_sueno_avg)       *out_sueno_avg       = a->baseline.sueno_base_h;
            if (out_ejercicio_avg)   *out_ejercicio_avg   = (float)a->baseline.ejercicio_objetivo_min;
            if (out_calorias_avg)    *out_calorias_avg    = (float)a->baseline.calorias_objetivo_kcal;
            if (out_hidratacion_avg) *out_hidratacion_avg = (float)a->baseline.hidratacion_objetivo_ml;
        }
        return 0;
    }

    const DailyHealthSample *latest = astronaut_get_latest_sample(a);
    int current_day = latest->dia_mision;

    float sum_sueno = 0.0f;
    float sum_ej    = 0.0f;
    float sum_cal   = 0.0f;
    float sum_hid   = 0.0f;
    int matched     = 0;

    for (int i = a->history.count - 1; i >= 0; i--) {
        const DailyHealthSample *s = ring_buffer_get_chronological(&a->history, i);
        if (!s) continue;
        if (current_day > 0 && s->dia_mision > 0) {
            if (s->dia_mision > current_day - window_days && s->dia_mision <= current_day) {
                sum_sueno += s->sueno_horas;
                sum_ej    += (float)s->ejercicio_min;
                sum_cal   += (float)s->calorias_kcal;
                sum_hid   += (float)s->hidratacion_ml;
                matched++;
            }
        }
    }

    /* Fallback si dia_mision no estaba numerado secuencialmente */
    if (matched == 0) {
        int start = a->history.count - window_days;
        if (start < 0) start = 0;
        for (int i = start; i < a->history.count; i++) {
            const DailyHealthSample *s = ring_buffer_get_chronological(&a->history, i);
            if (!s) continue;
            sum_sueno += s->sueno_horas;
            sum_ej    += (float)s->ejercicio_min;
            sum_cal   += (float)s->calorias_kcal;
            sum_hid   += (float)s->hidratacion_ml;
            matched++;
        }
    }

    if (matched > 0) {
        if (out_sueno_avg)       *out_sueno_avg       = sum_sueno / (float)matched;
        if (out_ejercicio_avg)   *out_ejercicio_avg   = sum_ej / (float)matched;
        if (out_calorias_avg)    *out_calorias_avg    = sum_cal / (float)matched;
        if (out_hidratacion_avg) *out_hidratacion_avg = sum_hid / (float)matched;
    }
    return matched;
}

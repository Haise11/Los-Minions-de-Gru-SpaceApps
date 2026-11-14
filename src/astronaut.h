#ifndef ASTRONAUT_H
#define ASTRONAUT_H

#include <stddef.h>
#include "health_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Validacion de IDs ([a-zA-Z0-9_-], 1..15 chars) contra path traversal */
int is_valid_id(const char *id);

/* Validacion y normalizacion de sexo biologico ("F" o "M") */
int  is_valid_sex(const char *sexo);
void normalize_sex_str(char *dst, size_t dst_size, const char *src);

/* Inicializa un AstronautBaseline con los valores de referencia oficial NASA */
void baseline_init_nasa_defaults(AstronautBaseline *b);

/* Inicializa un Astronaut con valores por defecto */
void astronaut_init_from_defaults(Astronaut *a,
                                  const char *id,
                                  const char *nombre,
                                  const char *rol,
                                  const char *mission_id,
                                  const char *avatar,
                                  const AstronautBaseline *defaults);

/* Genera una muestra nominal a partir del baseline */
void sample_from_baseline(DailyHealthSample *s, const AstronautBaseline *b, int dia_mision);

/* Prepara la base para un check-in parcial usando la ultima muestra o el baseline */
void sample_prepare_partial_base(DailyHealthSample *out, const Astronaut *a, int dia_mision);

/* Valida rangos fisiologicos y fisicos posibles */
StatusCode validate_baseline(const AstronautBaseline *b);
StatusCode validate_sample(const DailyHealthSample *s);

/* Inserta en el Ring Buffer en O(1) sin acumular ni validar (usado al cargar JSON) */
void ring_buffer_push_raw(SampleRingBuffer *rb, const DailyHealthSample *s);

/* Valida y registra una nueva muestra, actualizando radiacion_mision_msv */
StatusCode astronaut_add_sample(Astronaut *a, const DailyHealthSample *s);

/* Obtiene puntero a la muestra mas reciente (o NULL si count == 0) */
const DailyHealthSample *astronaut_get_latest_sample(const Astronaut *a);

/* Obtiene una muestra en orden cronologico (0 = mas antigua, count-1 = mas reciente) */
const DailyHealthSample *ring_buffer_get_chronological(const SampleRingBuffer *rb, int index);

/* Calcula promedios moviles para los ultimos window_days dias (por dia_mision) */
int astronaut_compute_window_averages(const Astronaut *a,
                                      int window_days,
                                      float *out_sueno_avg,
                                      float *out_ejercicio_avg,
                                      float *out_calorias_avg,
                                      float *out_hidratacion_avg);

#ifdef __cplusplus
}
#endif

#endif /* ASTRONAUT_H */

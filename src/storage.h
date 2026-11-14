#ifndef STORAGE_H
#define STORAGE_H

#include "health_types.h"
#include "../third_party/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Serializadores y deserializadores JSON compartidos con server.c */
cJSON *baseline_to_json(const AstronautBaseline *b, int include_rad_limit);
void   baseline_from_json(AstronautBaseline *b, const cJSON *obj);

cJSON *sample_to_json(const DailyHealthSample *s);
void   sample_from_json_partial(DailyHealthSample *s, const cJSON *obj);

cJSON *cabin_to_json(const CabinEnv *env);
void   cabin_from_json_partial(CabinEnv *env, const cJSON *obj);

cJSON *report_to_json(const EvaluationReport *rep);
cJSON *astronaut_to_json(const Astronaut *a, int include_report);
cJSON *astronaut_summary_to_json(const Astronaut *a);
StatusCode astronaut_from_json(Astronaut *a, const cJSON *obj, const AstronautBaseline *defaults);

cJSON *mission_to_json(const Mission *m);
cJSON *mission_health_summary_to_json(const MissionHealthSummary *sum);
StatusCode mission_from_json(Mission *m, const cJSON *obj);

/* Persistencia atomica en disco (archivos .json en data/) */
StatusCode storage_load_all(AppState *state, const char *data_dir);
StatusCode storage_save_defaults(const AppState *state);
StatusCode storage_save_astronaut(const AppState *state, const Astronaut *a);
StatusCode storage_delete_astronaut_file(const AppState *state, const char *astronaut_id);
StatusCode storage_save_mission(const AppState *state, const Mission *m);

/* Decodifica una imagen en formato data:image/...;base64,... y la guarda en web/assets/avatars/ */
StatusCode storage_save_custom_avatar(const char *id_hint,
                                      const char *data_url,
                                      char *out_avatar_path,
                                      size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */

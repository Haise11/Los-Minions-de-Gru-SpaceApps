#ifndef HEALTH_ENGINE_H
#define HEALTH_ENGINE_H

#include "health_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Evalua un astronauta contra su linea base, tendencias (3d/7d) y el CabinEnv */
void evaluate_astronaut(Astronaut *a, const CabinEnv *env);

/* Re-evalua todos los astronautas asignados a una mision */
void evaluate_mission_crew(AppState *state, const Mission *m);

/* Re-evalua toda la tripulacion registrada en AppState */
void evaluate_all(AppState *state);

/* Calcula el MissionHealthSummary (PROMEDIO DE SALUD y semaforo de 5 Peligros HRP) */
void evaluate_mission_summary(const AppState *state,
                              const Mission *m,
                              MissionHealthSummary *out_summary);

/* Inyecta uno de los 7 escenarios de prueba HRP en la mision activa */
StatusCode simulate_mission_scenario(AppState *state,
                                     Mission *m,
                                     const char *scenario_name);

/* Helpers de texto para serializacion y diagnostico */
const char *alert_severity_to_str(AlertSeverity sev);
const char *aptitude_to_str(OperationalAptitude apt);
const char *hazard_to_str(NasaHazard haz);
const char *body_system_to_str(BodySystem sys);
const char *gravity_mode_to_str(GravityMode g);
const char *hormonal_phase_to_str(HormonalCyclePhase phase);

#ifdef __cplusplus
}
#endif

#endif /* HEALTH_ENGINE_H */

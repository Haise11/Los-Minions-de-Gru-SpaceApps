#include "health_engine.h"
#include "astronaut.h"
#include "mission.h"

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

const char *alert_severity_to_str(AlertSeverity sev) {
    switch (sev) {
        case ALERT_OK:       return "OK";
        case ALERT_CAUTION:  return "CAUTION";
        case ALERT_CRITICAL: return "CRITICAL";
        default:             return "OK";
    }
}

const char *aptitude_to_str(OperationalAptitude apt) {
    switch (apt) {
        case APT_GO:         return "Apto";
        case APT_RESTRICTED: return "Restringido";
        case APT_NO_GO:      return "No apto";
        default:             return "Apto";
    }
}

const char *hazard_to_str(NasaHazard haz) {
    switch (haz) {
        case HAZARD_RADIATION:  return "HAZARD_RADIATION";
        case HAZARD_ISOLATION:  return "HAZARD_ISOLATION";
        case HAZARD_DISTANCE:   return "HAZARD_DISTANCE";
        case HAZARD_GRAVITY:    return "HAZARD_GRAVITY";
        case HAZARD_CLOSED_ENV: return "HAZARD_CLOSED_ENV";
        default:                return "HAZARD_GRAVITY";
    }
}

const char *body_system_to_str(BodySystem sys) {
    switch (sys) {
        case SYS_CARDIOVASCULAR:  return "SYS_CARDIOVASCULAR";
        case SYS_NEURO_OCULAR:    return "SYS_NEURO_OCULAR";
        case SYS_RESPIRATORY_EVA: return "SYS_RESPIRATORY_EVA";
        case SYS_MUSCULOSKELETAL: return "SYS_MUSCULOSKELETAL";
        case SYS_IMMUNE:          return "SYS_IMMUNE";
        case SYS_BEHAVIORAL:      return "SYS_BEHAVIORAL";
        case SYS_RADIATION:       return "SYS_RADIATION";
        default:                  return "SYS_CARDIOVASCULAR";
    }
}

const char *gravity_mode_to_str(GravityMode g) {
    switch (g) {
        case GRAV_MICRO:   return "GRAV_MICRO";
        case GRAV_MOON:    return "GRAV_MOON";
        case GRAV_MARS:    return "GRAV_MARS";
        case GRAV_EARTH:   return "GRAV_EARTH";
        case GRAV_DYNAMIC: return "GRAV_DYNAMIC";
        default:           return "GRAV_MICRO";
    }
}

static const char *gravity_mode_to_es(GravityMode g) {
    switch (g) {
        case GRAV_MICRO:   return "Microgravedad (0g)";
        case GRAV_MOON:    return "Gravedad Lunar (1/6g)";
        case GRAV_MARS:    return "Gravedad Marciana (3/8g)";
        case GRAV_EARTH:   return "Gravedad Terrestre (1g)";
        case GRAV_DYNAMIC: return "Cargas Dinamicas";
        default:           return "Microgravedad (0g)";
    }
}

const char *hormonal_phase_to_str(HormonalCyclePhase phase) {
    switch (phase) {
        case CYCLE_NA:         return "CYCLE_NA";
        case CYCLE_MENSTRUAL:  return "CYCLE_MENSTRUAL";
        case CYCLE_FOLLICULAR: return "CYCLE_FOLLICULAR";
        case CYCLE_OVULATORY:  return "CYCLE_OVULATORY";
        case CYCLE_LUTEAL:     return "CYCLE_LUTEAL";
        case CYCLE_SUPPRESSED: return "CYCLE_SUPPRESSED";
        default:               return "CYCLE_NA";
    }
}

static void push_alert(EvaluationReport *rep,
                       const char *id_regla,
                       NasaHazard hazard,
                       BodySystem system,
                       AlertSeverity severity,
                       const char *referencia_nasa,
                       const char *titulo,
                       const char *explicacion,
                       const char *contramedida) {
    if (!rep || rep->alert_count >= MAX_ALERTS) return;

    HealthAlert *al = &rep->alerts[rep->alert_count++];
    memset(al, 0, sizeof(*al));
    safe_strcpy(al->id_regla, sizeof(al->id_regla), id_regla);
    al->hazard   = hazard;
    al->system   = system;
    al->severity = severity;
    safe_strcpy(al->referencia_nasa, sizeof(al->referencia_nasa), referencia_nasa);
    safe_strcpy(al->titulo, sizeof(al->titulo), titulo);
    safe_strcpy(al->explicacion, sizeof(al->explicacion), explicacion);
    safe_strcpy(al->contramedida, sizeof(al->contramedida), contramedida);

    if ((int)system >= 0 && (int)system < BODY_SYSTEM_COUNT) {
        if (severity > rep->system_status[system]) {
            rep->system_status[system] = severity;
        }
    }
    if (severity > rep->global_status) {
        rep->global_status = severity;
    }
}

void evaluate_astronaut(Astronaut *a, const CabinEnv *env_in) {
    if (!a) return;

    CabinEnv default_env;
    const CabinEnv *env = env_in;
    if (!env) {
        cabin_init_nominal(&default_env);
        env = &default_env;
    }

    EvaluationReport *rep = &a->report;
    memset(rep, 0, sizeof(*rep));
    rep->global_status   = ALERT_OK;
    rep->health_score    = 100;
    rep->eva_aptitude    = APT_GO;
    rep->autonomous_mode = (env->latencia_com_seg > 5.0f) ? 1 : 0;
    safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), "Normal");

    DailyHealthSample fallback_sample;
    const DailyHealthSample *s = astronaut_get_latest_sample(a);
    if (!s) {
        sample_from_baseline(&fallback_sample, &a->baseline, 1);
        s = &fallback_sample;
    }

    const AstronautBaseline *b = &a->baseline;

    /* Paso 1: Promedios moviles de 3 y 7 dias (por dia_mision) */
    float sueno_3d = s->sueno_horas;
    float ej_3d    = (float)s->ejercicio_min;
    float cal_3d   = (float)s->calorias_kcal;
    float hid_3d   = (float)s->hidratacion_ml;
    astronaut_compute_window_averages(a, 3, &sueno_3d, &ej_3d, &cal_3d, &hid_3d);

    float sueno_7d = s->sueno_horas;
    float ej_7d    = (float)s->ejercicio_min;
    float cal_7d   = (float)s->calorias_kcal;
    float hid_7d   = (float)s->hidratacion_ml;
    astronaut_compute_window_averages(a, 7, &sueno_7d, &ej_7d, &cal_7d, &hid_7d);

    int vestibular_no_go = 0;

    /* =====================================================================
     * REGLA 1: Radiacion Espacial (HAZARD_RADIATION -> SYS_RADIATION)
     * ===================================================================== */
    if (env->evento_spe_activo == 1 || env->tasa_rad_msv_h >= 0.5f || s->dosis_dia_msv > 2.0f) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Pico de radiacion detectado: tormenta solar %s, tasa=%.2f mSv/h, dosis diaria=%.2f mSv.",
                 env->evento_spe_activo ? "activa" : "inactiva",
                 env->tasa_rad_msv_h, s->dosis_dia_msv);
        push_alert(rep,
                   "RAD_SPE_SPIKE",
                   HAZARD_RADIATION,
                   SYS_RADIATION,
                   ALERT_CRITICAL,
                   "Radi-N2 / CANS / Riesgo por Radiación",
                   "Tormenta Solar SPE / Pico Dosimetrico",
                   exp,
                   "1) Ingresar al refugio blindado y suspender EVA. 2) Activar dosimetria continua (Radi-N2/CANS). 3) Reforzar blindaje con bolsas de agua.");
    } else if (s->dosis_dia_msv >= 1.0f) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Dosis diaria elevada (%.2f mSv/dia vs nominal %.2f mSv/dia).",
                 s->dosis_dia_msv, b->dosis_diaria_nominal_msv);
        push_alert(rep,
                   "RAD_DAILY_ELEVATED",
                   HAZARD_RADIATION,
                   SYS_RADIATION,
                   ALERT_CAUTION,
                   "Radi-N2 / CANS / Riesgo por Radiación",
                   "Exposicion Diaria a Radiacion Elevada",
                   exp,
                   "1) Limitar tiempo en modulos externos. 2) Verificar lectura de neutrones (Radi-N2/CANS). 3) Rotar tareas hacia zona central.");
    }

    float total_career_rad = a->radiacion_previa_msv + a->radiacion_mision_msv;
    if (total_career_rad >= NASA_CAREER_RAD_LIMIT_MSV) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Dosis acumulada de carrera (%.1f mSv) alcanza/supera el limite de seguridad (%.0f mSv).",
                 total_career_rad, NASA_CAREER_RAD_LIMIT_MSV);
        push_alert(rep,
                   "RAD_CAREER_LIMIT",
                   HAZARD_RADIATION,
                   SYS_RADIATION,
                   ALERT_CRITICAL,
                   "Radi-N2 / CANS / Riesgo por Radiación",
                   "Limite de Radiacion de Carrera Alcanzado",
                   exp,
                   "1) Prohibir toda nueva salida EVA. 2) Reubicar al tripulante en sector de maximo blindaje. 3) Programar rotacion prioritaria.");
    } else if (total_career_rad >= 480.0f) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Dosis acumulada de carrera (%.1f mSv) supera el 80%% del limite de seguridad (480/%.0f mSv).",
                 total_career_rad, NASA_CAREER_RAD_LIMIT_MSV);
        push_alert(rep,
                   "RAD_CAREER_CAUTION",
                   HAZARD_RADIATION,
                   SYS_RADIATION,
                   ALERT_CAUTION,
                   "Radi-N2 / CANS / Riesgo por Radiación",
                   "Acercamiento a Limite de Radiacion (80%)",
                   exp,
                   "1) Restringir EVA no esenciales. 2) Reasignar turnos en zonas de menor tasa dosimetrica.");
    }

    /* =====================================================================
     * REGLA 2: SANS — Sindrome Neuro-Ocular (HAZARD_GRAVITY -> SYS_NEURO_OCULAR)
     * ===================================================================== */
    float vision_drop = b->vision_base - s->vision_actual;
    if (vision_drop >= 0.25f - 1e-4f) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Caida severa de agudeza visual (%.2f vs base %.2f), cefalea %d/10, PA %.0f/%.0f mmHg.",
                 s->vision_actual, b->vision_base, s->dolor_cabeza, s->pa_sis, s->pa_dia);
        push_alert(rep,
                   "SANS_CRITICAL",
                   HAZARD_GRAVITY,
                   SYS_NEURO_OCULAR,
                   ALERT_CRITICAL,
                   "Riesgo SANS / Bandas en Muslos / Presión Intracraneal",
                   "Sindrome Neuro-Ocular Crítico (SANS)",
                   exp,
                   "1) Colocar bandas de compresion en muslos (2-4 h diurnas). 2) Administrar Complejo B diario. 3) Realizar fondo de ojo / ecografia ocular (Tempus Pro).");
    } else if (vision_drop >= 0.15f - 1e-4f &&
               (s->dolor_cabeza >= 4 || s->pa_sis >= b->pa_sis_base + 20.0f - 1e-4f)) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Indicios de SANS: vision %.2f (base %.2f), dolor de cabeza %d/10, PA sistolica %.0f mmHg.%s",
                 s->vision_actual, b->vision_base, s->dolor_cabeza, s->pa_sis,
                 (s->sintomas_flags & SYM_THIGH_CUFF_ACTIVE) ? " [Banda en Muslos Activa]" : "");
        push_alert(rep,
                   "SANS_CAUTION",
                   HAZARD_GRAVITY,
                   SYS_NEURO_OCULAR,
                   ALERT_CAUTION,
                   "Riesgo SANS / Bandas en Muslos / Presión Intracraneal",
                   "Alerta Neuro-Ocular (SANS)",
                   exp,
                   "1) Activar bandas en muslos (2-4 h/dia). 2) Tomar Complejo B con comida. 3) Programar ecografia ocular / OCT (Tempus Pro).");
    }

    /* =====================================================================
     * REGLA 3: Hipoxia e Hipercapnia (HAZARD_CLOSED_ENV -> SYS_RESPIRATORY_EVA)
     * ===================================================================== */
    if (s->spo2 < 90.0f || env->po2_kpa < 15.0f) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Hipoxia critica detectada: SpO2=%.1f%% (base %.1f%%), pO2 cabina=%.1f kPa.",
                 s->spo2, b->spo2_base, env->po2_kpa);
        push_alert(rep,
                   "ENV_HYPOXIA_CRIT",
                   HAZARD_CLOSED_ENV,
                   SYS_RESPIRATORY_EVA,
                   ALERT_CRITICAL,
                   "Riesgo de Hipoxia / Tempus Pro",
                   "Hipoxia Critica / Caida de O2 en Cabina",
                   exp,
                   "1) Administrar O2 suplementario al 100%. 2) Suspender toda carga fisica. 3) Revisar valvulas pO2 del ECLSS de inmediato.");
    } else if (s->spo2 < 94.0f || env->po2_kpa < 18.0f) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Saturacion u oxigeno ambiental reducido: SpO2=%.1f%%, pO2 cabina=%.1f kPa.",
                 s->spo2, env->po2_kpa);
        push_alert(rep,
                   "ENV_HYPOXIA_CAUT",
                   HAZARD_CLOSED_ENV,
                   SYS_RESPIRATORY_EVA,
                   ALERT_CAUTION,
                   "Riesgo de Hipoxia / Tempus Pro",
                   "Precaucion por Hipoxia / Baja pO2",
                   exp,
                   "1) Aplicar O2 suplementario y bajar ritmo fisico. 2) Revisar flujo O2 del ECLSS. 3) Vigilar SpO2 continuo en Tempus Pro.");
    }

    if (env->pco2_kpa > 0.8f) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Hipercapnia critica: pCO2=%.2f kPa (>0.8 kPa), cefalea %d/10, reflejos %.0f ms.",
                 env->pco2_kpa, s->dolor_cabeza, s->cognitivo_ms);
        push_alert(rep,
                   "ENV_HYPERCAPNIA_CRIT",
                   HAZARD_CLOSED_ENV,
                   SYS_RESPIRATORY_EVA,
                   ALERT_CRITICAL,
                   "Entornos Cerrados Hostiles / ECLSS",
                   "Hipercapnia Critica por Acumulacion de CO2",
                   exp,
                   "1) Ventilar modulo y aislar fuente de CO2. 2) Activar respaldo LiOH si falla CDRA/CAMRAS. 3) Usar mascarilla de O2 si persiste cefalea.");
    } else if (env->pco2_kpa > 0.5f &&
               (s->dolor_cabeza >= 4 || s->cognitivo_ms > b->cognitivo_base_ms * 1.20f)) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "CO2 sobre nominal NASA (~0.40 kPa): %.2f kPa con cefalea (%d/10) y reflejos %.0f ms (base %.0f ms).",
                 env->pco2_kpa, s->dolor_cabeza, s->cognitivo_ms, b->cognitivo_base_ms);
        push_alert(rep,
                   "ENV_HYPERCAPNIA_CAUT",
                   HAZARD_CLOSED_ENV,
                   SYS_RESPIRATORY_EVA,
                   ALERT_CAUTION,
                   "Entornos Cerrados Hostiles / ECLSS (CDRA / CAMRAS)",
                   "Hipercapnia / Revisar Depurador de CO2",
                   exp,
                   "1) Inspeccionar depurador nominal CDRA/CAMRAS y ventilacion. 2) Hidratar y re-medir pCO2 antes de atribuir sintomas a fatiga.");
    }

    /* =====================================================================
     * REGLA 4: Enfermedad por Descompresion — DCS (HAZARD_CLOSED_ENV -> SYS_RESPIRATORY_EVA)
     * ===================================================================== */
    if (s->tuvo_eva == 1 &&
        (s->pre_resp_o2_min < b->pre_resp_o2_objetivo_min || (s->sintomas_flags & SYM_EVA_JOINT_PAIN))) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Alerta post-EVA: pre-respiracion O2=%d min (protocolo ISLE >=%d min; reposo 240 min)%s.",
                 s->pre_resp_o2_min, b->pre_resp_o2_objetivo_min,
                 (s->sintomas_flags & SYM_EVA_JOINT_PAIN) ? ", dolor articular reportado" : "");
        push_alert(rep,
                   "EVA_DCS_RISK",
                   HAZARD_CLOSED_ENV,
                   SYS_RESPIRATORY_EVA,
                   ALERT_CRITICAL,
                   "Riesgo de Enfermedad Descompresiva / EVA (ISLE)",
                   "Riesgo Critico de Descompresion (DCS Post-EVA)",
                   exp,
                   "1) Administrar O2 al 100% inmediato. 2) Inmovilizar articulación afectada. 3) Examen neurológico y preparar recompresión en esclusa/traje.");
    }

    /* =====================================================================
     * REGLA 5: Cardio, Hemostasia Venosa, Oseo, Vestibular y Cargas Dinamicas (HAZARD_GRAVITY)
     * ===================================================================== */
    int low_hydration = (b->hidratacion_objetivo_ml > 0 &&
                         (s->hidratacion_ml < (int)(0.75f * (float)b->hidratacion_objetivo_ml) ||
                          hid_3d < 0.75f * (float)b->hidratacion_objetivo_ml));
    int ortho_dizzy   = (s->sintomas_flags & SYM_ORTHOSTATIC_DIZZINESS) ? 1 : 0;

    if (s->pa_sis < 85.0f || s->fc > 130.0f) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Inestabilidad hemodinamica critica: FC=%.0f lpm (base %.0f), PA=%.0f/%.0f mmHg.",
                 s->fc, b->fc_base, s->pa_sis, s->pa_dia);
        push_alert(rep,
                   "GRAV_CARDIO_CRIT",
                   HAZARD_GRAVITY,
                   SYS_CARDIOVASCULAR,
                   ALERT_CRITICAL,
                   "Adaptaciones Cardiovasculares / Hemostasia Venosa / Tempus Pro",
                   "Inestabilidad Cardiovascular Critica",
                   exp,
                   "1) Posición supina o piernas elevadas. 2) Carga rápida de fluidos y electrolitos. 3) ECG y ultrasonido vascular con Tempus Pro.");
    } else if (s->fc > b->fc_base + 25.0f || s->fc > 105.0f || s->fc < 45.0f ||
               s->pa_sis < 90.0f || s->pa_sis > 140.0f || ortho_dizzy || low_hydration) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Desviacion cardio/hemostasia: FC=%.0f lpm, PA=%.0f/%.0f mmHg, hidratacion=%d/%d ml (3d %.0f ml)%s.",
                 s->fc, s->pa_sis, s->pa_dia, s->hidratacion_ml, b->hidratacion_objetivo_ml, hid_3d,
                 ortho_dizzy ? ", mareo ortostatico" : "");
        push_alert(rep,
                   "GRAV_CARDIO_VENOUS",
                   HAZARD_GRAVITY,
                   SYS_CARDIOVASCULAR,
                   ALERT_CAUTION,
                   "Adaptaciones Cardiovasculares / Hemostasia Venosa / Tempus Pro",
                   "Adaptacion Cardiovascular / Hemostasia Venosa",
                   exp,
                   "1) Ingerir 350-500 ml de agua con electrolitos. 2) Usar prenda de compresión inferior. 3) Verificar flujo venoso en Tempus Pro.");
    }

    if (ej_7d < 45.0f) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Deficit severo de acondicionamiento fisico: promedio 7d = %.0f min/dia activos (meta %d min).",
                 ej_7d, b->ejercicio_objetivo_min);
        push_alert(rep,
                   "GRAV_BONE_CRIT",
                   HAZARD_GRAVITY,
                   SYS_MUSCULOSKELETAL,
                   ALERT_CRITICAL,
                   "ARED / CEVIS / Protocolo Zero-T2 (Sin Cinta)",
                   "Riesgo Severo de Perdida Osea y Muscular",
                   exp,
                   "1) Completar >= 90 min/día de ejercicio activo (ARED + CEVIS / protocolo sin cinta Zero-T2). 2) Tomar Vitamina D3 diaria.");
    } else if (ej_7d < 60.0f) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Promedio de ejercicio activo en 7 dias bajo el umbral protector: %.0f min/dia (meta %d min).",
                 ej_7d, b->ejercicio_objetivo_min);
        push_alert(rep,
                   "GRAV_BONE_CAUT",
                   HAZARD_GRAVITY,
                   SYS_MUSCULOSKELETAL,
                   ALERT_CAUTION,
                   "ARED / CEVIS / Protocolo Zero-T2 (Sin Cinta)",
                   "Desacondicionamiento Musculoesqueletico",
                   exp,
                   "1) Retomar >= 90 min/día en ARED / CEVIS (o protocolo Zero-T2). 2) Confirmar suplementación diaria de Vitamina D3.");
    }

    if (s->mareo_vestibular >= 5 ||
        (env->gravedad_actual == GRAV_DYNAMIC && (s->mareo_vestibular >= 4 || ej_7d < 60.0f))) {
        vestibular_no_go = 1;
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Riesgo sensoriomotor/SMS en %s: mareo vestibular %d/10, ejercicio 7d %.0f min/dia.",
                 gravity_mode_to_es(env->gravedad_actual), s->mareo_vestibular, ej_7d);
        push_alert(rep,
                   "GRAV_VESTIBULAR_DYNAMIC",
                   HAZARD_GRAVITY,
                   SYS_NEURO_OCULAR,
                   ALERT_CAUTION,
                   "Sensoriomotor, Mareo Espacial (SMS) y Cargas Dinámicas",
                   "Alteracion Vestibular / Mareo Espacial (No apto)",
                   exp,
                   "1) Diferir pilotaje manual y EVA por 24-48 h (No apto). 2) Fijar punto visual estable, evitar movimientos cefálicos bruscos e hidratar.");
    }

    /* =====================================================================
     * REGLA 6: Inmunidad, Microbioma y Metabolismo (HAZARD_CLOSED_ENV -> SYS_IMMUNE)
     * ===================================================================== */
    unsigned int immune_flags = s->sintomas_flags &
                                (SYM_RASH_ALLERGY | SYM_VIRAL_REACTIVATION | SYM_INFECTION_SIGNS);
    int glucose_out = (s->glucosa_mg_dl < 70.0f || s->glucosa_mg_dl > 140.0f);
    int fever_caut  = (s->temp_c >= 37.8f);
    int fever_crit  = (s->temp_c >= 39.0f);

    if (fever_crit || (fever_caut && (immune_flags != 0 || glucose_out)) ||
        s->glucosa_mg_dl < 55.0f || s->glucosa_mg_dl > 200.0f) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Respuesta inmune/metabolica critica: Temp=%.1f C, Glucosa=%.0f mg/dL, signos inmunes=%s.",
                 s->temp_c, s->glucosa_mg_dl, immune_flags ? "activos" : "ninguno");
        push_alert(rep,
                   "IMMUNE_CRITICAL",
                   HAZARD_CLOSED_ENV,
                   SYS_IMMUNE,
                   ALERT_CRITICAL,
                   "Biomarcadores Inmunes / Microbioma / Medidas Estándar",
                   "Compromiso Inmunologico / Metabolico Critico",
                   exp,
                   "1) Aislar preventivamente al tripulante y tomar saliva/sangre seriada. 2) Estabilizar glucemia. 3) Auditar filtros HEPA y cultivo Veggie.");
    } else if (fever_caut || immune_flags != 0 || glucose_out) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Alteracion inmune o metabolica: Temp=%.1f C, Glucosa=%.0f mg/dL, signos inmunes=%s.",
                 s->temp_c, s->glucosa_mg_dl, immune_flags ? "activos" : "ninguno");
        push_alert(rep,
                   "IMMUNE_CAUTION",
                   HAZARD_CLOSED_ENV,
                   SYS_IMMUNE,
                   ALERT_CAUTION,
                   "Biomarcadores Inmunes / Microbioma / Medidas Estándar",
                   "Alerta de Inmunidad / Microbioma / Glucemia",
                   exp,
                   "1) Reducir contacto cercano y tomar muestra de saliva seca. 2) Estabilizar glucemia. 3) Revisar higiene y ventilacion de cabina.");
    }

    /* =====================================================================
     * REGLA 7: Aislamiento, Sueno, Cohesion y Nutricion (HAZARD_ISOLATION -> SYS_BEHAVIORAL)
     * ===================================================================== */
    if (sueno_3d < 5.0f && s->estres >= 8) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Deuda aguda de sueno (prom 3d %.1f h) con estres severo (%d/10) y reflejos %.0f ms.",
                 sueno_3d, s->estres, s->cognitivo_ms);
        push_alert(rep,
                   "ISO_SLEEP_CRIT",
                   HAZARD_ISOLATION,
                   SYS_BEHAVIORAL,
                   ALERT_CRITICAL,
                   "ARCHeR / Privación de Sueño",
                   "Fatiga Cognitiva Critica y Deuda de Sueno",
                   exp,
                   "1) Suspender maniobras criticas (No apto). 2) Proteger bloque de 8 h de sueno y luz LED circadiana. 3) Aplicar soporte conductual.");
    } else if (sueno_3d < 6.0f &&
               (s->cognitivo_ms > b->cognitivo_base_ms * 1.15f || s->estres >= 7 || s->animo <= 4)) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Deficit de sueno 3d (%.1f h vs base %.1f h), estres %d/10, animo %d/10, reflejos %.0f ms.",
                 sueno_3d, b->sueno_base_h, s->estres, s->animo, s->cognitivo_ms);
        push_alert(rep,
                   "ISO_SLEEP_CAUT",
                   HAZARD_ISOLATION,
                   SYS_BEHAVIORAL,
                   ALERT_CAUTION,
                   "ARCHeR / Privación de Sueño",
                   "Deuda de Sueno y Fatiga Conductual (ARCHeR)",
                   exp,
                   "1) Ajustar iluminacion LED circadiana. 2) Descargar tareas no criticas. 3) Asignar pausa activa / cuidado del cultivo Veggie.");
    }

    if (s->cohesion_equipo <= 2) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Indice de cohesion de equipo bajo (%d/5) reportado en el control diario.",
                 s->cohesion_equipo);
        push_alert(rep,
                   "ISO_TEAMWORK",
                   HAZARD_ISOLATION,
                   SYS_BEHAVIORAL,
                   ALERT_CAUTION,
                   "Riesgo de Cohesión de Equipo / HFBP",
                   "Friccion o Baja Cohesion de Equipo",
                   exp,
                   "1) Convocar briefing breve de coordinacion. 2) Redistribuir carga operativa. 3) Programar actividad compartida de distension.");
    }

    if ((b->calorias_objetivo_kcal > 0 && cal_7d < 0.80f * (float)b->calorias_objetivo_kcal) ||
        s->apetito_comida <= 2 || s->peso_kg < b->peso_base * 0.95f) {
        char exp[192];
        snprintf(exp, sizeof(exp),
                 "Deficit nutricional: ingesta 7d %.0f/%d kcal, apetito %d/5, peso %.1f/%.1f kg.",
                 cal_7d, b->calorias_objetivo_kcal, s->apetito_comida, s->peso_kg, b->peso_base);
        push_alert(rep,
                   "ISO_NUTRITION",
                   HAZARD_ISOLATION,
                   SYS_BEHAVIORAL,
                   ALERT_CAUTION,
                   "Dieta Enriquecida / Apetito Nutricional",
                   "Deficit Calórico / Fatiga de Menu Espacial",
                   exp,
                   "1) Rotar a dieta enriquecida (omega-3/flavonoides). 2) Sumar porcion fresca del modulo Veggie. 3) Reforzar hidratacion diaria.");
    }

    /* =====================================================================
     * REGLA 9: Fisiologia Hormonal Femenina, Menstruacion y Anemia/Hemostasia
     * ===================================================================== */
    int is_female = (a->sexo[0] == 'F' || a->sexo[0] == 'f');
    if (is_female || s->fase_ciclo != CYCLE_NA || s->flujo_menstrual > 0 || s->colico_menstrual > 0) {
        if ((s->flujo_menstrual == 3 &&
             (s->fc > b->fc_base + 18.0f || s->pa_sis < 95.0f || ortho_dizzy || s->colico_menstrual >= 7)) ||
            s->colico_menstrual >= 8) {
            char exp[192];
            snprintf(exp, sizeof(exp),
                     "Menorragia/dismenorrea severa (dia %d, flujo %d/3, colico %d/10, FC %.0f lpm, PA %.0f/%.0f mmHg).",
                     s->dia_ciclo, s->flujo_menstrual, s->colico_menstrual, s->fc, s->pa_sis, s->pa_dia);
            push_alert(rep,
                       "HORMONAL_MENORRHAGIA_CRIT",
                       HAZARD_GRAVITY,
                       SYS_CARDIOVASCULAR,
                       ALERT_CRITICAL,
                       "Medidas Estándar / Adaptaciones Cardiovasculares / EVA",
                       "Menorragia / Dismenorrea Severa y Riesgo de Anemia",
                       exp,
                       "1) Suspender EVA (No apto). 2) Administrar hierro terapeutico + Complejo B. 3) Reponer fluidos/electrolitos y controlar ferritina.");
        } else if ((s->fase_ciclo == CYCLE_MENSTRUAL || s->flujo_menstrual >= 1 || s->colico_menstrual >= 4) &&
                   (s->suplemento_hierro == 0 || s->flujo_menstrual >= 2 || s->colico_menstrual >= 4 || low_hydration)) {
            char exp[192];
            snprintf(exp, sizeof(exp),
                     "Fase menstrual activa (dia %d, flujo %d/3, colico %d/10, hierro=%s).",
                     s->dia_ciclo, s->flujo_menstrual, s->colico_menstrual,
                     s->suplemento_hierro ? "Activo" : "Pendiente");
            push_alert(rep,
                       "HORMONAL_MENSTRUAL_CARE",
                       HAZARD_GRAVITY,
                       SYS_IMMUNE,
                       ALERT_CAUTION,
                       "Medidas Estándar / Complejo B / Dieta Enriquecida",
                       "Monitoreo Menstrual: Prevencion de Ferropenia y Dismenorrea",
                       exp,
                       "1) Activar Hierro + Complejo B con comida (sin cafe/te 1 h). 2) Sumar +350 ml de hidratacion. 3) Reevaluar flujo en proximo check-in.");
        }

        if (s->fase_ciclo == CYCLE_LUTEAL &&
            ((s->temp_c >= b->temp_base + 0.35f && s->temp_c < 37.8f) ||
             s->colico_menstrual >= 3 ||
             (s->dolor_cabeza >= 3 && s->sueno_horas < b->sueno_base_h - 0.8f) ||
             s->animo <= 5)) {
            char exp[192];
            snprintf(exp, sizeof(exp),
                     "Fase lutea (dia %d): Temp %.1f C (base %.1f C), sueno %.1f h, animo %d/10, cefalea %d/10.",
                     s->dia_ciclo, s->temp_c, b->temp_base, s->sueno_horas, s->animo, s->dolor_cabeza);
            push_alert(rep,
                       "HORMONAL_LUTEAL_EFFECT",
                       HAZARD_ISOLATION,
                       SYS_BEHAVIORAL,
                       ALERT_CAUTION,
                       "Medidas Estándar / ARCHeR / Complejo B",
                       "Ajuste Hormonal Fase Lutea (Termorregulacion y Sueno)",
                       exp,
                       "1) Ajustar ventilacion/termorregulacion por alza lutea (+0.3-0.5 C). 2) Reforzar Complejo B y luz circadiana. 3) Descartar pCO2 elevado.");
        }

        if (s->fase_ciclo == CYCLE_SUPPRESSED &&
            (low_hydration ||
             (b->hidratacion_objetivo_ml > 0 && s->hidratacion_ml < (int)(0.85f * (float)b->hidratacion_objetivo_ml)) ||
             ej_7d < 65.0f ||
             (s->sintomas_flags & SYM_VIT_D_TAKEN) == 0)) {
            char exp[192];
            snprintf(exp, sizeof(exp),
                     "Supresion hormonal activa con hidratacion %d/%d ml o ejercicio 7d %.0f min/dia.",
                     s->hidratacion_ml, b->hidratacion_objetivo_ml, ej_7d);
            push_alert(rep,
                       "HORMONAL_VENOUS_BONE",
                       HAZARD_GRAVITY,
                       SYS_CARDIOVASCULAR,
                       ALERT_CAUTION,
                       "Hemostasia Venosa / ARED / Protocolo Zero-T2",
                       "Supresion Hormonal: Vigilancia de Hemostasia Venosa y Osea",
                       exp,
                       "1) Mantener hidratación >= 100% para mitigar riesgo VTE (mayor con COC estrogénicos). 2) Controlar flujo yugular (Tempus Pro). 3) Cumplir ARED / Zero-T2 + Vitamina D3.");
        }
    }

    /* =====================================================================
     * PASO 3: Ordenamiento de alertas, calculo de health_score y eva_aptitude
     * ===================================================================== */
    /* Ordenar alertas: ALERT_CRITICAL primero, luego ALERT_CAUTION */
    for (int i = 0; i < rep->alert_count - 1; i++) {
        for (int j = i + 1; j < rep->alert_count; j++) {
            if (rep->alerts[j].severity > rep->alerts[i].severity) {
                HealthAlert tmp = rep->alerts[i];
                rep->alerts[i]  = rep->alerts[j];
                rep->alerts[j]  = tmp;
            }
        }
    }

    /* Calculo de health_score (0..100) */
    int score = 100;
    for (int i = 0; i < rep->alert_count; i++) {
        if (rep->alerts[i].severity == ALERT_CRITICAL) {
            score -= 25;
        } else if (rep->alerts[i].severity == ALERT_CAUTION) {
            score -= 10;
        }
    }

    /* Penalizacion fina (hasta 10 pts) por desviaciones leves respecto al baseline */
    int minor_penalty = 0;
    if (fabsf(s->fc - b->fc_base) > 10.0f) minor_penalty += 2;
    if (s->sueno_horas < b->sueno_base_h - 0.8f) minor_penalty += 2;
    if (s->estres > b->estres_base + 2) minor_penalty += 2;
    if (s->dolor_cabeza >= 2) minor_penalty += 2;
    if (s->colico_menstrual >= 2) minor_penalty += 2;
    if (b->hidratacion_objetivo_ml > 0 && s->hidratacion_ml < (int)(0.90f * (float)b->hidratacion_objetivo_ml)) {
        minor_penalty += 2;
    }
    if (minor_penalty > 10) minor_penalty = 10;
    score -= minor_penalty;

    if (score < 0) score = 0;
    if (score > 100) score = 100;
    rep->health_score = score;

    int crit_cnt = 0;
    for (int i = 0; i < rep->alert_count; i++) {
        if (rep->alerts[i].severity == ALERT_CRITICAL) {
            crit_cnt++;
        }
    }
    rep->critical_alert_count = crit_cnt;
    rep->is_very_critical = (rep->global_status == ALERT_CRITICAL && (rep->health_score <= 50 || crit_cnt >= 2)) ? 1 : 0;

    /* Determinar aptitud operativa EVA / Pilotaje Manual */
    if (rep->global_status == ALERT_CRITICAL ||
        env->evento_spe_activo == 1 ||
        s->mareo_vestibular >= 5 ||
        s->colico_menstrual >= 8 ||
        sueno_3d < 5.0f ||
        vestibular_no_go) {
        rep->eva_aptitude = APT_NO_GO;
    } else if (rep->global_status == ALERT_CAUTION || s->colico_menstrual >= 4) {
        rep->eva_aptitude = APT_RESTRICTED;
    } else {
        rep->eva_aptitude = APT_GO;
    }

    /* Construir resumen_estado breve para la tarjeta estilo Credenciales.jpeg / DibujodePagina.jpeg */
    if (rep->alert_count == 0) {
        safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), "Normal");
    } else {
        const HealthAlert *top = &rep->alerts[0];
        if (strcmp(top->id_regla, "ENV_HYPERCAPNIA_CAUT") == 0 ||
            strcmp(top->id_regla, "ENV_HYPERCAPNIA_CRIT") == 0) {
            safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), "Dolor de cabeza · Revisar CO2");
        } else if (strcmp(top->id_regla, "SANS_CAUTION") == 0 ||
                   strcmp(top->id_regla, "SANS_CRITICAL") == 0) {
            if (s->dolor_cabeza > 0) {
                safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), "Dolor de cabeza · Alerta SANS");
            } else {
                safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), "Alerta Neuro-Ocular SANS");
            }
        } else if (strcmp(top->id_regla, "RAD_SPE_SPIKE") == 0) {
            safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), "Alerta Radiacion SPE · Refugio");
        } else if (strcmp(top->id_regla, "ENV_HYPOXIA_CRIT") == 0 ||
                   strcmp(top->id_regla, "ENV_HYPOXIA_CAUT") == 0) {
            safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), "Alerta Hipoxia · Revisar O2/ECLSS");
        } else if (strcmp(top->id_regla, "EVA_DCS_RISK") == 0) {
            safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), "Riesgo DCS Post-EVA · O2 100%");
        } else if (strcmp(top->id_regla, "HORMONAL_MENORRHAGIA_CRIT") == 0) {
            safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), "Menorragia / Dismenorrea · No apto");
        } else if (strcmp(top->id_regla, "HORMONAL_MENSTRUAL_CARE") == 0) {
            safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), "Ciclo Menstrual · Suplementar Hierro");
        } else if (strcmp(top->id_regla, "HORMONAL_LUTEAL_EFFECT") == 0) {
            safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), "Fase Lútea · Ajuste Térmico/Sueño");
        } else if (strcmp(top->id_regla, "HORMONAL_VENOUS_BONE") == 0) {
            safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), "Supresión Hormonal · Vigilar Flujo");
        } else {
            safe_strcpy(rep->resumen_estado, sizeof(rep->resumen_estado), top->titulo);
        }
    }
}

void evaluate_mission_crew(AppState *state, const Mission *m) {
    if (!state || !m) return;
    for (int i = 0; i < state->astronaut_count; i++) {
        Astronaut *a = &state->astronauts[i];
        if (strcmp(a->mission_id, m->id) == 0 || mission_has_crew(m, a->id)) {
            evaluate_astronaut(a, &m->env);
        }
    }
}

void evaluate_all(AppState *state) {
    if (!state) return;
    for (int i = 0; i < state->astronaut_count; i++) {
        Astronaut *a = &state->astronauts[i];
        Mission *m   = state_find_mission(state, a->mission_id);
        evaluate_astronaut(a, m ? &m->env : NULL);
    }
}

void evaluate_mission_summary(const AppState *state,
                              const Mission *m,
                              MissionHealthSummary *out_summary) {
    if (!out_summary) return;
    memset(out_summary, 0, sizeof(*out_summary));
    if (!state || !m) {
        out_summary->mission_health_avg = 100;
        return;
    }

    int count          = 0;
    int sum_score      = 0;
    float sum_fc       = 0.0f;
    float sum_spo2     = 0.0f;
    float sum_sueno    = 0.0f;
    float sum_estres   = 0.0f;
    float sum_rad_mis  = 0.0f;
    int best_vc_score  = 101;
    int best_vc_crits  = -1;

    for (int i = 0; i < state->astronaut_count; i++) {
        const Astronaut *a = &state->astronauts[i];
        if (strcmp(a->mission_id, m->id) != 0 && !mission_has_crew(m, a->id)) {
            continue;
        }
        count++;
        sum_score += a->report.health_score;
        if (a->report.global_status == ALERT_CRITICAL) {
            out_summary->crew_critical_count++;
        } else if (a->report.global_status == ALERT_CAUTION) {
            out_summary->crew_caution_count++;
        } else {
            out_summary->crew_ok_count++;
        }
        if (a->report.is_very_critical) {
            out_summary->crew_very_critical_count++;
            if (a->report.health_score < best_vc_score ||
                (a->report.health_score == best_vc_score &&
                 a->report.critical_alert_count > best_vc_crits)) {
                best_vc_score = a->report.health_score;
                best_vc_crits = a->report.critical_alert_count;
                safe_strcpy(out_summary->priority_astronaut_id,
                            sizeof(out_summary->priority_astronaut_id),
                            a->id);
            }
        }

        const DailyHealthSample *s = astronaut_get_latest_sample(a);
        if (s) {
            sum_fc     += s->fc;
            sum_spo2   += s->spo2;
            sum_sueno  += s->sueno_horas;
            sum_estres += (float)s->estres;
        } else {
            sum_fc     += a->baseline.fc_base;
            sum_spo2   += a->baseline.spo2_base;
            sum_sueno  += a->baseline.sueno_base_h;
            sum_estres += (float)a->baseline.estres_base;
        }
        sum_rad_mis += a->radiacion_mision_msv;

        for (int k = 0; k < a->report.alert_count; k++) {
            NasaHazard hz = a->report.alerts[k].hazard;
            AlertSeverity sv = a->report.alerts[k].severity;
            if ((int)hz >= 0 && (int)hz < NASA_HAZARD_COUNT) {
                if (sv > out_summary->hazard_status[hz]) {
                    out_summary->hazard_status[hz] = sv;
                }
            }
        }
    }

    if (count > 0) {
        out_summary->mission_health_avg = (int)roundf((float)sum_score / (float)count);
        out_summary->fc_avg             = sum_fc / (float)count;
        out_summary->spo2_avg           = sum_spo2 / (float)count;
        out_summary->sueno_avg_h        = sum_sueno / (float)count;
        out_summary->estres_avg         = sum_estres / (float)count;
        out_summary->rad_mision_avg_msv = sum_rad_mis / (float)count;
    } else {
        out_summary->mission_health_avg = 100;
    }

    /* Estado del peligro Distancia de la Tierra (HAZARD_DISTANCE) */
    if (m->env.latencia_com_seg > 5.0f) {
        if (out_summary->hazard_status[HAZARD_DISTANCE] < ALERT_CAUTION) {
            out_summary->hazard_status[HAZARD_DISTANCE] = ALERT_CAUTION;
        }
    }
}

/* Restaura las ultimas muestras de un astronauta a su estado nominal para limpiar simulaciones previas */
static void reset_astronaut_recent_to_nominal(Astronaut *a, int current_day) {
    if (!a) return;
    if (a->history.count == 0) {
        DailyHealthSample s;
        sample_from_baseline(&s, &a->baseline, current_day > 0 ? current_day : 14);
        astronaut_add_sample(a, &s);
        return;
    }

    static const float fc_offsets[5]    = {-1.0f, 0.0f, 1.0f, -1.0f, 2.0f};
    static const float spo2_offsets[5]  = {0.0f, 0.1f, -0.1f, 0.1f, -0.2f};
    static const float sueno_offsets[5] = {0.1f, -0.1f, 0.2f, 0.0f, -0.2f};
    static const float rad_offsets[5]   = {-0.01f, 0.01f, 0.0f, -0.02f, 0.02f};
    static const int   estres_offsets[5]= {-1, 0, 1, 0, -1};
    static const float cog_offsets[5]   = {-3.0f, 1.0f, -2.0f, 2.0f, 0.0f};
    static const int   ej_offsets[5]    = {2, 0, 4, -2, 3};
    static const int   cal_offsets[5]   = {20, -10, 30, 0, -20};
    static const int   hid_offsets[5]   = {30, 0, 50, -20, 20};

    size_t id_len = strlen(a->id);
    int seed_offset = (id_len > 0) ? (int)(unsigned char)a->id[id_len - 1] : 0;

    int start_idx = a->history.count - 7;
    if (start_idx < 0) start_idx = 0;
    for (int i = start_idx; i < a->history.count; i++) {
        int oldest_idx = (a->history.head - a->history.count + MAX_HISTORY_SAMPLES) % MAX_HISTORY_SAMPLES;
        int real_idx   = (oldest_idx + i) % MAX_HISTORY_SAMPLES;
        DailyHealthSample *item = &a->history.items[real_idx];
        int day = item->dia_mision;
        long ts = item->timestamp_unix;
        float old_rad = item->dosis_dia_msv;

        sample_from_baseline(item, &a->baseline, day);
        item->timestamp_unix = ts;

        int k = (day + seed_offset) % 5;
        if (k < 0) k = -k;
        item->fc            += fc_offsets[k];
        item->spo2          += spo2_offsets[k];
        if (item->spo2 > 100.0f) item->spo2 = 100.0f;
        item->sueno_horas   += sueno_offsets[k];
        item->dosis_dia_msv += rad_offsets[k];
        if (item->dosis_dia_msv < 0.0f) item->dosis_dia_msv = 0.0f;
        item->estres        += estres_offsets[k];
        if (item->estres < 1) item->estres = 1;
        if (item->estres > 10) item->estres = 10;
        item->cognitivo_ms  += cog_offsets[k];
        item->ejercicio_min += ej_offsets[k];
        item->calorias_kcal += cal_offsets[k];
        item->hidratacion_ml += hid_offsets[k];
        if (a->sexo[0] == 'F' || a->sexo[0] == 'f') {
            item->fase_ciclo        = CYCLE_FOLLICULAR;
            item->dia_ciclo         = 6 + (day % 7);
            item->flujo_menstrual   = 0;
            item->colico_menstrual  = 0;
            item->suplemento_hierro = 1;
        }

        a->radiacion_mision_msv += (item->dosis_dia_msv - old_rad);
        if (a->radiacion_mision_msv < 0.0f) a->radiacion_mision_msv = 0.0f;
    }
}

StatusCode simulate_mission_scenario(AppState *state,
                                     Mission *m,
                                     const char *scenario_name) {
    if (!state || !m || !scenario_name) return ERR_INVALID_RANGE;

    /* Validar nombre de escenario antes de modificar estado */
    if (strcmp(scenario_name, "NOMINAL") != 0 &&
        strcmp(scenario_name, "SPE_STORM") != 0 &&
        strcmp(scenario_name, "HYPOXIA") != 0 &&
        strcmp(scenario_name, "HYPERCAPNIA") != 0 &&
        strcmp(scenario_name, "SANS_SLEEP") != 0 &&
        strcmp(scenario_name, "FATIGUE_ISOLATION") != 0 &&
        strcmp(scenario_name, "DCS_EVA") != 0 &&
        strcmp(scenario_name, "HORMONAL_FEMALE") != 0) {
        return ERR_INVALID_RANGE;
    }

    /* Preservar la latencia configurada (ej. modo Marte) y restablecer base nominal */
    float saved_latency = m->env.latencia_com_seg;
    cabin_init_nominal(&m->env);
    m->env.latencia_com_seg = saved_latency;

    for (int i = 0; i < state->astronaut_count; i++) {
        Astronaut *a = &state->astronauts[i];
        if (strcmp(a->mission_id, m->id) == 0 || mission_has_crew(m, a->id)) {
            reset_astronaut_recent_to_nominal(a, m->dia_actual);
        }
    }

    if (strcmp(scenario_name, "NOMINAL") == 0) {
        evaluate_mission_crew(state, m);
        return OK;
    }

    if (strcmp(scenario_name, "SPE_STORM") == 0) {
        m->env.evento_spe_activo = 1;
        m->env.tasa_rad_msv_h    = 1.85f;
        for (int i = 0; i < state->astronaut_count; i++) {
            Astronaut *a = &state->astronauts[i];
            if (strcmp(a->mission_id, m->id) != 0 && !mission_has_crew(m, a->id)) continue;
            DailyHealthSample s;
            sample_prepare_partial_base(&s, a, m->dia_actual);
            s.dosis_dia_msv = 3.20f;
            s.estres        = 6;
            astronaut_add_sample(a, &s);
        }
    } else if (strcmp(scenario_name, "HYPOXIA") == 0) {
        m->env.po2_kpa            = 14.4f;
        m->env.presion_cabina_kpa = 74.0f;
        for (int i = 0; i < state->astronaut_count; i++) {
            Astronaut *a = &state->astronauts[i];
            if (strcmp(a->mission_id, m->id) != 0 && !mission_has_crew(m, a->id)) continue;
            DailyHealthSample s;
            sample_prepare_partial_base(&s, a, m->dia_actual);
            s.spo2          = (i % 2 == 0) ? 88.5f : 91.5f;
            s.fc            = a->baseline.fc_base + 22.0f;
            s.cognitivo_ms  = a->baseline.cognitivo_base_ms * 1.18f;
            s.dolor_cabeza  = 5;
            astronaut_add_sample(a, &s);
        }
    } else if (strcmp(scenario_name, "HYPERCAPNIA") == 0) {
        m->env.pco2_kpa = 0.72f;
        for (int i = 0; i < state->astronaut_count; i++) {
            Astronaut *a = &state->astronauts[i];
            if (strcmp(a->mission_id, m->id) != 0 && !mission_has_crew(m, a->id)) continue;
            DailyHealthSample s;
            sample_prepare_partial_base(&s, a, m->dia_actual);
            s.dolor_cabeza = 6;
            s.cognitivo_ms = a->baseline.cognitivo_base_ms * 1.28f;
            s.sueno_calidad = 2;
            astronaut_add_sample(a, &s);
        }
    } else if (strcmp(scenario_name, "SANS_SLEEP") == 0) {
        for (int i = 0; i < state->astronaut_count; i++) {
            Astronaut *a = &state->astronauts[i];
            if (strcmp(a->mission_id, m->id) != 0 && !mission_has_crew(m, a->id)) continue;
            /* Modificar los ultimos 3 dias de sueno para disparar correlacion SANS + Sueno */
            int start_idx = a->history.count - 3;
            if (start_idx < 0) start_idx = 0;
            for (int k = start_idx; k < a->history.count; k++) {
                int oldest_idx = (a->history.head - a->history.count + MAX_HISTORY_SAMPLES) % MAX_HISTORY_SAMPLES;
                int real_idx   = (oldest_idx + k) % MAX_HISTORY_SAMPLES;
                a->history.items[real_idx].sueno_horas = 4.7f;
            }
            DailyHealthSample s;
            sample_prepare_partial_base(&s, a, m->dia_actual);
            s.vision_actual = a->baseline.vision_base - ((i == 0) ? 0.26f : 0.18f);
            s.dolor_cabeza  = 6;
            s.pa_sis        = a->baseline.pa_sis_base + 22.0f;
            s.sueno_horas   = 4.6f;
            s.estres        = 8;
            s.cognitivo_ms  = a->baseline.cognitivo_base_ms * 1.22f;
            astronaut_add_sample(a, &s);
        }
    } else if (strcmp(scenario_name, "FATIGUE_ISOLATION") == 0) {
        for (int i = 0; i < state->astronaut_count; i++) {
            Astronaut *a = &state->astronauts[i];
            if (strcmp(a->mission_id, m->id) != 0 && !mission_has_crew(m, a->id)) continue;
            int start_idx = a->history.count - 7;
            if (start_idx < 0) start_idx = 0;
            for (int k = start_idx; k < a->history.count; k++) {
                int oldest_idx = (a->history.head - a->history.count + MAX_HISTORY_SAMPLES) % MAX_HISTORY_SAMPLES;
                int real_idx   = (oldest_idx + k) % MAX_HISTORY_SAMPLES;
                a->history.items[real_idx].calorias_kcal = (int)(0.72f * (float)a->baseline.calorias_objetivo_kcal);
                if (k >= a->history.count - 3) {
                    a->history.items[real_idx].sueno_horas = 5.3f;
                }
            }
            DailyHealthSample s;
            sample_prepare_partial_base(&s, a, m->dia_actual);
            s.sueno_horas     = 5.1f;
            s.estres          = 8;
            s.animo           = 3;
            s.cohesion_equipo = 2;
            s.apetito_comida  = 2;
            s.calorias_kcal   = (int)(0.70f * (float)a->baseline.calorias_objetivo_kcal);
            s.cognitivo_ms    = a->baseline.cognitivo_base_ms * 1.20f;
            astronaut_add_sample(a, &s);
        }
    } else if (strcmp(scenario_name, "DCS_EVA") == 0) {
        for (int i = 0; i < state->astronaut_count; i++) {
            Astronaut *a = &state->astronauts[i];
            if (strcmp(a->mission_id, m->id) != 0 && !mission_has_crew(m, a->id)) continue;
            DailyHealthSample s;
            sample_prepare_partial_base(&s, a, m->dia_actual);
            if (i < 2) {
                s.tuvo_eva        = 1;
                s.pre_resp_o2_min = 75;
                s.sintomas_flags |= SYM_EVA_JOINT_PAIN;
                s.fc              = a->baseline.fc_base + 18.0f;
            }
            astronaut_add_sample(a, &s);
        }
    } else if (strcmp(scenario_name, "HORMONAL_FEMALE") == 0) {
        int fem_idx = 0;
        for (int i = 0; i < state->astronaut_count; i++) {
            Astronaut *a = &state->astronauts[i];
            if (strcmp(a->mission_id, m->id) != 0 && !mission_has_crew(m, a->id)) continue;
            if (a->sexo[0] == 'F' || a->sexo[0] == 'f') {
                DailyHealthSample s;
                sample_prepare_partial_base(&s, a, m->dia_actual);
                if (fem_idx == 0) {
                    /* Caso 1: Menstruacion activa con dismenorrea y pendiente suplemento de hierro */
                    s.fase_ciclo        = CYCLE_MENSTRUAL;
                    s.dia_ciclo         = 2;
                    s.flujo_menstrual   = 2;
                    s.colico_menstrual  = 5;
                    s.suplemento_hierro = 0;
                    s.fc                = a->baseline.fc_base + 12.0f;
                    s.hidratacion_ml    = (int)(0.82f * (float)a->baseline.hidratacion_objetivo_ml);
                } else {
                    /* Caso 2: Fase lutea con alza termica por progesterona, sueno reducido y cefalea */
                    s.fase_ciclo        = CYCLE_LUTEAL;
                    s.dia_ciclo         = 24;
                    s.flujo_menstrual   = 0;
                    s.colico_menstrual  = 4;
                    s.suplemento_hierro = 1;
                    s.temp_c            = a->baseline.temp_base + 0.5f;
                    s.sueno_horas       = a->baseline.sueno_base_h - 1.2f;
                    s.dolor_cabeza      = 4;
                    s.animo             = 5;
                }
                astronaut_add_sample(a, &s);
                fem_idx++;
            }
        }
        /* Si la mision no tenia ninguna tripulante femenina, inyectar en el primer tripulante */
        if (fem_idx == 0) {
            for (int i = 0; i < state->astronaut_count; i++) {
                Astronaut *a = &state->astronauts[i];
                if (strcmp(a->mission_id, m->id) != 0 && !mission_has_crew(m, a->id)) continue;
                DailyHealthSample s;
                sample_prepare_partial_base(&s, a, m->dia_actual);
                s.fase_ciclo        = CYCLE_MENSTRUAL;
                s.dia_ciclo         = 2;
                s.flujo_menstrual   = 2;
                s.colico_menstrual  = 5;
                s.suplemento_hierro = 0;
                astronaut_add_sample(a, &s);
                break;
            }
        }
    }

    evaluate_mission_crew(state, m);
    return OK;
}

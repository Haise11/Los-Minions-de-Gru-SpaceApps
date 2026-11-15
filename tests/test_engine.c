#define _POSIX_C_SOURCE 200809L
#include "astronaut.h"
#include "health_engine.h"
#include "health_types.h"
#include "mission.h"
#include "server.h"
#include "storage.h"
#include "../third_party/cJSON.h"
#include "../third_party/mongoose.h"

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    printf("  [TEST %02d] %-48s ... ", g_tests_run, #fn); \
    fflush(stdout); \
    fn(); \
    g_tests_passed++; \
    printf("PASS\n"); \
} while (0)

static int has_alert_rule(const EvaluationReport *rep, const char *rule_id, AlertSeverity expected_sev) {
    for (int i = 0; i < rep->alert_count; i++) {
        if (strcmp(rep->alerts[i].id_regla, rule_id) == 0 &&
            rep->alerts[i].severity == expected_sev) {
            return 1;
        }
    }
    return 0;
}

static void test_id_sanitization(void) {
    assert(is_valid_id("astro_01") == 1);
    assert(is_valid_id("artemis-sim_2") == 1);
    assert(is_valid_id("") == 0);
    assert(is_valid_id(NULL) == 0);
    assert(is_valid_id("../etc/passwd") == 0);
    assert(is_valid_id("astro/01") == 0);
    assert(is_valid_id("astro.json") == 0);
    assert(is_valid_id("id_demasiado_largo_12345") == 0);
}

static void test_sample_and_cabin_validation(void) {
    AstronautBaseline b;
    baseline_init_nasa_defaults(&b);

    DailyHealthSample s;
    sample_from_baseline(&s, &b, 1);
    assert(validate_sample(&s) == OK);

    /* Rangos invalidos fisiologicos */
    s.temp_c = 28.0f;
    assert(validate_sample(&s) == ERR_INVALID_RANGE);
    s.temp_c = 36.6f;

    s.spo2 = 35.0f;
    assert(validate_sample(&s) == ERR_INVALID_RANGE);
    s.spo2 = 101.0f;
    assert(validate_sample(&s) == ERR_INVALID_RANGE);
    s.spo2 = 98.0f;

    s.fc = 15.0f;
    assert(validate_sample(&s) == ERR_INVALID_RANGE);
    s.fc = 250.0f;
    assert(validate_sample(&s) == ERR_INVALID_RANGE);
    s.fc = 70.0f;

    s.pa_sis = 260.0f;
    assert(validate_sample(&s) == ERR_INVALID_RANGE);
    s.pa_sis = 120.0f;

    /* PA diastolica mayor o igual que sistolica debe fallar */
    s.pa_dia = 125.0f;
    assert(validate_sample(&s) == ERR_INVALID_RANGE);
    s.pa_dia = 80.0f;

    /* dia_mision <= 0 debe fallar */
    s.dia_mision = 0;
    assert(validate_sample(&s) == ERR_INVALID_RANGE);
    s.dia_mision = 1;

    s.sueno_horas = 25.0f;
    assert(validate_sample(&s) == ERR_INVALID_RANGE);
    s.sueno_horas = 7.5f;

    s.sintomas_flags = 0x200u; /* > 0x1FF (511) */
    assert(validate_sample(&s) == ERR_INVALID_RANGE);
    s.sintomas_flags = SYM_VIT_D_TAKEN | SYM_VEGGIE_FRESH_FOOD;
    assert(validate_sample(&s) == OK);

    CabinEnv env;
    cabin_init_nominal(&env);
    assert(validate_cabin(&env) == OK);

    env.presion_cabina_kpa = 10.0f;
    assert(validate_cabin(&env) == ERR_INVALID_RANGE);
    env.presion_cabina_kpa = 101.3f;

    env.po2_kpa = 3.0f;
    assert(validate_cabin(&env) == ERR_INVALID_RANGE);
    env.po2_kpa = 21.3f;

    env.pco2_kpa = 12.0f;
    assert(validate_cabin(&env) == ERR_INVALID_RANGE);
    env.pco2_kpa = 0.35f;
    assert(validate_cabin(&env) == OK);

    /* Validacion de Baseline */
    assert(validate_baseline(&b) == OK);
    b.fc_base = 20.0f;
    assert(validate_baseline(&b) == ERR_INVALID_RANGE);
    b.fc_base = 65.0f;
    b.pa_dia_base = 130.0f; /* >= pa_sis_base (118.0) */
    assert(validate_baseline(&b) == ERR_INVALID_RANGE);
    b.pa_dia_base = 76.0f;
    assert(validate_baseline(&b) == OK);
}

static void test_ring_buffer_and_radiation_accumulator(void) {
    Astronaut a;
    astronaut_init_from_defaults(&a, "astro_rb", "Test Buffer", "Piloto", "artemis_sim", NULL, NULL);
    a.radiacion_previa_msv = 10.0f;

    /* Insertar 110 dias (superando MAX_HISTORY_SAMPLES = 90) con 0.5 mSv/dia */
    for (int day = 1; day <= 110; day++) {
        DailyHealthSample s;
        sample_from_baseline(&s, &a.baseline, day);
        s.dosis_dia_msv = 0.5f;
        assert(astronaut_add_sample(&a, &s) == OK);
    }

    assert(a.history.count == MAX_HISTORY_SAMPLES);
    /* Acumulador independiente del tamano del buffer: 110 * 0.5 = 55.0 mSv */
    assert(fabsf(a.radiacion_mision_msv - 55.0f) < 0.01f);

    /* La muestra mas antigua en el buffer de 90 debe ser el dia 21, y la mas reciente el dia 110 */
    const DailyHealthSample *oldest = ring_buffer_get_chronological(&a.history, 0);
    const DailyHealthSample *latest = astronaut_get_latest_sample(&a);
    assert(oldest != NULL && oldest->dia_mision == 21);
    assert(latest != NULL && latest->dia_mision == 110);

    /* Actualizar el mismo dia 110 no debe duplicar el conteo ni distorsionar la dosis */
    DailyHealthSample update_s = *latest;
    update_s.dosis_dia_msv = 1.5f; /* +1.0 mSv respecto a 0.5 */
    assert(astronaut_add_sample(&a, &update_s) == OK);
    assert(a.history.count == MAX_HISTORY_SAMPLES);
    assert(fabsf(a.radiacion_mision_msv - 56.0f) < 0.01f);

    /* Actualizar un dia anterior (ej. dia 50) de forma idempotente sin alterar latest (dia 110) */
    DailyHealthSample earlier_s;
    sample_prepare_partial_base(&earlier_s, &a, 50);
    assert(earlier_s.dia_mision == 50);
    assert(fabsf(earlier_s.dosis_dia_msv - 0.5f) < 0.01f);
    earlier_s.dosis_dia_msv = 2.5f; /* +2.0 mSv respecto a 0.5 */
    earlier_s.fc = 88.0f;
    assert(astronaut_add_sample(&a, &earlier_s) == OK);
    assert(a.history.count == MAX_HISTORY_SAMPLES);
    assert(fabsf(a.radiacion_mision_msv - 58.0f) < 0.01f);
    assert(astronaut_get_latest_sample(&a)->dia_mision == 110);

    /* Verificar insercion fuera de orden (backfill de un dia omitido) mantiene orden cronologico */
    Astronaut a2;
    astronaut_init_from_defaults(&a2, "astro_ord", "Order Test", "Piloto", "artemis_sim", NULL, NULL);
    int days_seq[] = {1, 2, 4, 5, 3};
    for (int i = 0; i < 5; i++) {
        DailyHealthSample sd;
        sample_from_baseline(&sd, &a2.baseline, days_seq[i]);
        sd.dosis_dia_msv = 1.0f;
        assert(astronaut_add_sample(&a2, &sd) == OK);
    }
    assert(a2.history.count == 5);
    assert(fabsf(a2.radiacion_mision_msv - 5.0f) < 0.01f);
    for (int i = 0; i < 5; i++) {
        const DailyHealthSample *sc = ring_buffer_get_chronological(&a2.history, i);
        assert(sc != NULL && sc->dia_mision == i + 1);
    }
    assert(astronaut_get_latest_sample(&a2)->dia_mision == 5);
}

static void test_rule1_radiation_hazards(void) {
    Astronaut a;
    CabinEnv env;
    cabin_init_nominal(&env);
    astronaut_init_from_defaults(&a, "astro_rad", "Rad Tester", "Cientifico", "artemis_sim", NULL, NULL);

    DailyHealthSample s;
    sample_from_baseline(&s, &a.baseline, 1);
    astronaut_add_sample(&a, &s);

    /* Nominal */
    evaluate_astronaut(&a, &env);
    assert(a.report.global_status == ALERT_OK);
    assert(a.report.eva_aptitude == APT_GO);

    /* Tormenta solar SPE */
    env.evento_spe_activo = 1;
    env.tasa_rad_msv_h    = 0.85f;
    evaluate_astronaut(&a, &env);
    assert(a.report.global_status == ALERT_CRITICAL);
    assert(a.report.eva_aptitude == APT_NO_GO);
    assert(strcmp(aptitude_to_str(APT_GO), "Apto") == 0);
    assert(strcmp(aptitude_to_str(APT_RESTRICTED), "Restringido") == 0);
    assert(strcmp(aptitude_to_str(APT_NO_GO), "No apto") == 0);
    assert(has_alert_rule(&a.report, "RAD_SPE_SPIKE", ALERT_CRITICAL));

    /* Acumulado de carrera >= 480 mSv (Caution) y >= 600 mSv (Critical) */
    cabin_init_nominal(&env);
    a.radiacion_previa_msv = 490.0f;
    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "RAD_CAREER_CAUTION", ALERT_CAUTION));

    a.radiacion_previa_msv = 605.0f;
    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "RAD_CAREER_LIMIT", ALERT_CRITICAL));
    assert(a.report.global_status == ALERT_CRITICAL);
}

static void test_rule2_sans_neuro_ocular(void) {
    Astronaut a;
    CabinEnv env;
    cabin_init_nominal(&env);
    astronaut_init_from_defaults(&a, "astro_sans", "SANS Tester", "Medico", "artemis_sim", NULL, NULL);

    DailyHealthSample s;
    sample_from_baseline(&s, &a.baseline, 1);
    s.vision_actual = 0.84f; /* Caida de 0.16 respecto a 1.0 */
    s.dolor_cabeza  = 5;
    astronaut_add_sample(&a, &s);

    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "SANS_CAUTION", ALERT_CAUTION));
    assert(a.report.system_status[SYS_NEURO_OCULAR] == ALERT_CAUTION);

    /* Caida critica >= 0.25 */
    s.vision_actual = 0.74f;
    astronaut_add_sample(&a, &s);
    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "SANS_CRITICAL", ALERT_CRITICAL));
    assert(a.report.system_status[SYS_NEURO_OCULAR] == ALERT_CRITICAL);
}

static void test_rule3_hypoxia_and_hypercapnia(void) {
    Astronaut a;
    CabinEnv env;
    cabin_init_nominal(&env);
    astronaut_init_from_defaults(&a, "astro_env", "Env Tester", "Ingeniero", "artemis_sim", NULL, NULL);

    DailyHealthSample s;
    sample_from_baseline(&s, &a.baseline, 1);
    s.spo2 = 88.5f;
    astronaut_add_sample(&a, &s);

    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "ENV_HYPOXIA_CRIT", ALERT_CRITICAL));

    /* Restaurar SpO2 y probar Hipercapnia */
    s.spo2 = 98.0f;
    s.dolor_cabeza = 5;
    s.cognitivo_ms = 325.0f;
    astronaut_add_sample(&a, &s);
    env.pco2_kpa = 0.65f;

    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "ENV_HYPERCAPNIA_CAUT", ALERT_CAUTION));
    assert(strcmp(a.report.resumen_estado, "Dolor de cabeza · Revisar CO2") == 0);

    env.pco2_kpa = 0.90f;
    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "ENV_HYPERCAPNIA_CRIT", ALERT_CRITICAL));
}

static void test_rule4_dcs_eva(void) {
    Astronaut a;
    CabinEnv env;
    cabin_init_nominal(&env);
    astronaut_init_from_defaults(&a, "astro_dcs", "EVA Tester", "Especialista EVA", "artemis_sim", NULL, NULL);

    DailyHealthSample s;
    sample_from_baseline(&s, &a.baseline, 1);
    s.tuvo_eva        = 1;
    s.pre_resp_o2_min = 80; /* Menor a 120 min objetivo */
    s.sintomas_flags |= SYM_EVA_JOINT_PAIN;
    astronaut_add_sample(&a, &s);

    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "EVA_DCS_RISK", ALERT_CRITICAL));
    assert(a.report.eva_aptitude == APT_NO_GO);
    assert(strcmp(aptitude_to_str(APT_GO), "Apto") == 0);
    assert(strcmp(aptitude_to_str(APT_RESTRICTED), "Restringido") == 0);
    assert(strcmp(aptitude_to_str(APT_NO_GO), "No apto") == 0);
}

static void test_rule5_cardio_bone_vestibular(void) {
    Astronaut a;
    CabinEnv env;
    cabin_init_nominal(&env);
    astronaut_init_from_defaults(&a, "astro_grav", "Grav Tester", "Piloto", "artemis_sim", NULL, NULL);

    /* 7 dias con bajo ejercicio (50 min) y deshidratacion en el dia 7 */
    for (int d = 1; d <= 7; d++) {
        DailyHealthSample s;
        sample_from_baseline(&s, &a.baseline, d);
        s.ejercicio_min = 50;
        if (d == 7) {
            s.hidratacion_ml   = 1500; /* < 75% de 2500 ml */
            s.mareo_vestibular = 6;
        }
        astronaut_add_sample(&a, &s);
    }

    env.gravedad_actual = GRAV_MARS;
    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "GRAV_CARDIO_VENOUS", ALERT_CAUTION));
    assert(has_alert_rule(&a.report, "GRAV_BONE_CAUT", ALERT_CAUTION));
    assert(has_alert_rule(&a.report, "GRAV_VESTIBULAR_DYNAMIC", ALERT_CAUTION));
    assert(a.report.eva_aptitude == APT_NO_GO);
    assert(strcmp(aptitude_to_str(APT_GO), "Apto") == 0);
    assert(strcmp(aptitude_to_str(APT_RESTRICTED), "Restringido") == 0);
    assert(strcmp(aptitude_to_str(APT_NO_GO), "No apto") == 0);

    /* Verificar que el promedio movil de 3 dias de hidratacion (hid_3d < 75%) tambien activa GRAV_CARDIO_VENOUS
     * incluso si el dia mas reciente supera ligeramente el 75% (ej. 1900 ml sobre 2500 ml = 76%) */
    Astronaut a_hid;
    astronaut_init_from_defaults(&a_hid, "astro_hid", "Hid Tester", "Piloto", "artemis_sim", NULL, NULL);
    int hid_days[] = {1400, 1400, 1900}; /* Promedio 3d = 1566.7 ml (< 1875 ml = 75% de 2500 ml) */
    for (int d = 1; d <= 3; d++) {
        DailyHealthSample sh;
        sample_from_baseline(&sh, &a_hid.baseline, d);
        sh.hidratacion_ml = hid_days[d - 1];
        assert(astronaut_add_sample(&a_hid, &sh) == OK);
    }
    cabin_init_nominal(&env);
    evaluate_astronaut(&a_hid, &env);
    assert(has_alert_rule(&a_hid.report, "GRAV_CARDIO_VENOUS", ALERT_CAUTION));
}

static void test_rule6_immune_and_metabolic(void) {
    Astronaut a;
    CabinEnv env;
    cabin_init_nominal(&env);
    astronaut_init_from_defaults(&a, "astro_imm", "Immune Tester", "Biologo", "artemis_sim", NULL, NULL);

    DailyHealthSample s;
    sample_from_baseline(&s, &a.baseline, 1);
    s.glucosa_mg_dl   = 155.0f;
    s.sintomas_flags |= SYM_VIRAL_REACTIVATION;
    astronaut_add_sample(&a, &s);

    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "IMMUNE_CAUTION", ALERT_CAUTION));

    s.temp_c = 39.2f;
    astronaut_add_sample(&a, &s);
    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "IMMUNE_CRITICAL", ALERT_CRITICAL));
}

static void test_rule7_8_isolation_sleep_and_distance(void) {
    Astronaut a;
    CabinEnv env;
    cabin_init_nominal(&env);
    env.latencia_com_seg = 840.0f; /* Marte: 14 minutos */
    astronaut_init_from_defaults(&a, "astro_iso", "Iso Tester", "Comandante", "artemis_sim", NULL, NULL);

    for (int d = 1; d <= 7; d++) {
        DailyHealthSample s;
        sample_from_baseline(&s, &a.baseline, d);
        s.calorias_kcal = 1800; /* < 80% de 2500 */
        if (d >= 5) {
            s.sueno_horas = 4.5f;
        }
        if (d == 7) {
            s.estres          = 8;
            s.cohesion_equipo = 2;
            s.apetito_comida  = 2;
        }
        astronaut_add_sample(&a, &s);
    }

    evaluate_astronaut(&a, &env);
    assert(a.report.autonomous_mode == 1);
    assert(has_alert_rule(&a.report, "ISO_SLEEP_CRIT", ALERT_CRITICAL));
    assert(has_alert_rule(&a.report, "ISO_TEAMWORK", ALERT_CAUTION));
    assert(has_alert_rule(&a.report, "ISO_NUTRITION", ALERT_CAUTION));
    /* Verificar que las alertas CRITICAL estan ordenadas antes que las CAUTION */
    assert(a.report.alerts[0].severity == ALERT_CRITICAL);
}

static void test_seed_storage_and_simulations(void) {
    /* 1. Verificar que los archivos actuales en data/ cargan limpiamente sin errores */
    static AppState live_state;
    assert(storage_load_all(&live_state, "data") == OK);
    assert(live_state.mission_count >= 1);
    assert(live_state.astronaut_count >= 4);
    assert(state_find_mission(&live_state, "artemis_sim") != NULL);
    assert(state_find_astronaut(&live_state, "astro_01") != NULL);
    assert(state_find_astronaut(&live_state, "astro_02") != NULL);
    assert(state_find_astronaut(&live_state, "astro_03") != NULL);
    assert(state_find_astronaut(&live_state, "astro_04") != NULL);

    /* 2. Crear dataset semilla determinista en /tmp/space_health_test_seed y verificar round-trip en disco */
    static AppState seed_init;
    memset(&seed_init, 0, sizeof(seed_init));
    snprintf(seed_init.data_dir, sizeof(seed_init.data_dir), "/tmp/space_health_test_seed");
    baseline_init_nasa_defaults(&seed_init.defaults);
    storage_save_defaults(&seed_init);

    mission_init(&seed_init.missions[0], "artemis_sim", "Artemis IV / Mars — Sol 14", "Transito a Marte");
    seed_init.missions[0].dia_actual = 14;
    seed_init.mission_count = 1;

    const char *ids[4]    = {"astro_01", "astro_02", "astro_03", "astro_04"};
    const char *names[4]  = {"Cmdr. Elena Vance", "Dr. Kenji Sato", "Ing. Liam O'Connor", "Esp. Sofia Al-Mansoor"};
    const char *sexes[4]  = {"F", "M", "M", "F"};
    for (int i = 0; i < 4; i++) {
        Astronaut *a = &seed_init.astronauts[seed_init.astronaut_count++];
        astronaut_init_from_defaults(a, ids[i], names[i], "Tripulante", "artemis_sim", NULL, &seed_init.defaults);
        snprintf(a->sexo, sizeof(a->sexo), "%s", sexes[i]);
        for (int day = 1; day <= 14; day++) {
            DailyHealthSample s;
            sample_from_baseline(&s, &a->baseline, day);
            s.fc += (float)((day % 3) - 1);
            s.sueno_horas += (day % 2 == 0) ? 0.1f : -0.1f;
            if (i == 0 || i == 3) {
                s.fase_ciclo = CYCLE_FOLLICULAR;
                s.dia_ciclo = day;
                s.suplemento_hierro = 1;
            }
            if (i == 1 && day == 14) {
                s.vision_actual = 0.84f;
                s.dolor_cabeza  = 4;
            }
            assert(astronaut_add_sample(a, &s) == OK);
        }
        mission_add_crew(&seed_init.missions[0], a->id);
        storage_save_astronaut(&seed_init, a);
    }
    storage_save_mission(&seed_init, &seed_init.missions[0]);

    static AppState state;
    assert(storage_load_all(&state, "/tmp/space_health_test_seed") == OK);
    assert(state.mission_count == 1);
    assert(state.astronaut_count == 4);

    Mission *m = state_find_mission(&state, "artemis_sim");
    assert(m != NULL);
    assert(m->dia_actual == 14);
    assert(m->crew_count == 4);
    assert(fabsf(m->env.presion_cabina_kpa - 101.3f) < 0.1f);
    assert(fabsf(m->env.po2_kpa - 21.3f) < 0.1f);
    assert(fabsf(m->env.pco2_kpa - 0.35f) < 0.05f);
    assert(fabsf(m->env.latencia_com_seg - 1.3f) < 0.1f);
    assert(m->env.evento_spe_activo == 0);
    assert(m->env.gravedad_actual == GRAV_MICRO);

    Astronaut *a1 = state_find_astronaut(&state, "astro_01");
    Astronaut *a2 = state_find_astronaut(&state, "astro_02");
    Astronaut *a3 = state_find_astronaut(&state, "astro_03");
    Astronaut *a4 = state_find_astronaut(&state, "astro_04");
    assert(a1 != NULL && a2 != NULL && a3 != NULL && a4 != NULL);
    assert(strcmp(a1->sexo, "F") == 0 && strcmp(a2->sexo, "M") == 0);

    Astronaut *crew[4] = {a1, a2, a3, a4};
    for (int i = 0; i < 4; i++) {
        assert(crew[i]->history.count == 14);
        int varied_fc_or_sueno = 0;
        const DailyHealthSample *s8 = ring_buffer_get_chronological(&crew[i]->history, 7);
        assert(s8 != NULL && s8->dia_mision == 8);
        for (int idx = 8; idx < 14; idx++) {
            const DailyHealthSample *sk = ring_buffer_get_chronological(&crew[i]->history, idx);
            assert(sk != NULL && sk->dia_mision == idx + 1);
            if (fabsf(sk->fc - s8->fc) > 0.01f || fabsf(sk->sueno_horas - s8->sueno_horas) > 0.01f) {
                varied_fc_or_sueno = 1;
            }
        }
        assert(varied_fc_or_sueno == 1);
    }

    assert(a1->report.global_status == ALERT_OK);
    assert(a3->report.global_status == ALERT_OK);
    assert(a4->report.global_status == ALERT_OK);
    assert(a2->report.global_status == ALERT_CAUTION);
    assert(has_alert_rule(&a2->report, "SANS_CAUTION", ALERT_CAUTION));
    assert(strcmp(a2->report.resumen_estado, "Dolor de cabeza · Alerta SANS") == 0);

    MissionHealthSummary init_sum;
    evaluate_mission_summary(&state, m, &init_sum);
    assert(init_sum.crew_ok_count == 3);
    assert(init_sum.crew_caution_count == 1);
    assert(init_sum.crew_critical_count == 0);
    assert(init_sum.mission_health_avg == 97);

    cJSON *a2_json = astronaut_to_json(a2, 1);
    assert(a2_json != NULL);
    char *a2_str = cJSON_PrintUnformatted(a2_json);
    assert(a2_str != NULL);
    assert(strstr(a2_str, "\"SANS_CAUTION\"") != NULL);
    assert(strstr(a2_str, "Dolor de cabeza") != NULL);
    free(a2_str);
    cJSON_Delete(a2_json);

    /* Probar los 8 escenarios del simulador */
    const char *scenarios[] = {
        "SPE_STORM",
        "HYPOXIA",
        "HYPERCAPNIA",
        "SANS_SLEEP",
        "FATIGUE_ISOLATION",
        "DCS_EVA",
        "HORMONAL_FEMALE",
        "NOMINAL"
    };

    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
        assert(simulate_mission_scenario(&state, m, scenarios[i]) == OK);
        MissionHealthSummary sum;
        evaluate_mission_summary(&state, m, &sum);
        if (strcmp(scenarios[i], "NOMINAL") == 0) {
            assert(sum.crew_critical_count == 0);
            assert(sum.crew_caution_count == 0);
            assert(sum.crew_ok_count == 4);
            assert(sum.mission_health_avg >= 95);

            for (int c = 0; c < 4; c++) {
                int varied_fc_sueno = 0;
                int varied_estres   = 0;
                const DailyHealthSample *first_s = ring_buffer_get_chronological(&crew[c]->history, 7);
                assert(first_s != NULL);
                for (int idx = 8; idx < 14; idx++) {
                    const DailyHealthSample *cur_s = ring_buffer_get_chronological(&crew[c]->history, idx);
                    assert(cur_s != NULL);
                    if (fabsf(cur_s->fc - first_s->fc) > 0.01f ||
                        fabsf(cur_s->sueno_horas - first_s->sueno_horas) > 0.01f) {
                        varied_fc_sueno = 1;
                    }
                    if (cur_s->estres != first_s->estres) {
                        varied_estres = 1;
                    }
                }
                assert(varied_fc_sueno == 1);
                assert(varied_estres == 1);
            }
        } else {
            assert(sum.crew_critical_count > 0 || sum.crew_caution_count > 0);
        }
    }

    /* Limpiar directorio temporal de seed */
    unlink("/tmp/space_health_test_seed/astronauts/astro_01.json");
    unlink("/tmp/space_health_test_seed/astronauts/astro_02.json");
    unlink("/tmp/space_health_test_seed/astronauts/astro_03.json");
    unlink("/tmp/space_health_test_seed/astronauts/astro_04.json");
    unlink("/tmp/space_health_test_seed/missions/artemis_sim.json");
    unlink("/tmp/space_health_test_seed/defaults.json");
    rmdir("/tmp/space_health_test_seed/astronauts");
    rmdir("/tmp/space_health_test_seed/missions");
    rmdir("/tmp/space_health_test_seed");
}

static void test_rule9_female_hormonal_physiology(void) {
    /* 1. Validacion y normalizacion de sexo biologico */
    assert(is_valid_sex("F") == 1);
    assert(is_valid_sex("femenino") == 1);
    assert(is_valid_sex("Mujer") == 1);
    assert(is_valid_sex("M") == 1);
    assert(is_valid_sex("masculino") == 1);
    assert(is_valid_sex("Hombre") == 1);
    assert(is_valid_sex("X") == 0);

    char sx[16];
    normalize_sex_str(sx, sizeof(sx), "mujer");
    assert(strcmp(sx, "F") == 0);
    normalize_sex_str(sx, sizeof(sx), "masculino");
    assert(strcmp(sx, "M") == 0);

    Astronaut a;
    CabinEnv env;
    cabin_init_nominal(&env);
    astronaut_init_from_defaults(&a, "astro_fem", "Dra. Valentina", "Exobiologia", "artemis_sim", NULL, NULL);
    snprintf(a.sexo, sizeof(a.sexo), "F");

    /* 2. Menstruacion sin suplemento de hierro -> HORMONAL_MENSTRUAL_CARE (CAUTION) */
    DailyHealthSample s;
    sample_from_baseline(&s, &a.baseline, 1);
    s.fase_ciclo = CYCLE_MENSTRUAL;
    s.dia_ciclo = 2;
    s.flujo_menstrual = 2;
    s.colico_menstrual = 5;
    s.suplemento_hierro = 0;
    assert(validate_sample(&s) == OK);
    assert(astronaut_add_sample(&a, &s) == OK);
    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "HORMONAL_MENSTRUAL_CARE", ALERT_CAUTION));
    assert(a.report.eva_aptitude == APT_RESTRICTED);

    /* 3. Menorragia con taquicardia compensatoria y colico severo -> HORMONAL_MENORRHAGIA_CRIT + NO-GO */
    sample_from_baseline(&s, &a.baseline, 2);
    s.fase_ciclo = CYCLE_MENSTRUAL;
    s.dia_ciclo = 3;
    s.flujo_menstrual = 3;
    s.colico_menstrual = 8;
    s.fc = a.baseline.fc_base + 22.0f;
    s.suplemento_hierro = 0;
    assert(astronaut_add_sample(&a, &s) == OK);
    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "HORMONAL_MENORRHAGIA_CRIT", ALERT_CRITICAL));
    assert(a.report.eva_aptitude == APT_NO_GO);
    assert(strcmp(aptitude_to_str(APT_GO), "Apto") == 0);
    assert(strcmp(aptitude_to_str(APT_RESTRICTED), "Restringido") == 0);
    assert(strcmp(aptitude_to_str(APT_NO_GO), "No apto") == 0);

    /* 4. Fase lutea con termogenesis fisiologica -> HORMONAL_LUTEAL_EFFECT */
    sample_from_baseline(&s, &a.baseline, 3);
    s.fase_ciclo = CYCLE_LUTEAL;
    s.dia_ciclo = 21;
    s.temp_c = a.baseline.temp_base + 0.45f; /* ~37.05 C (< 37.8 C) */
    s.flujo_menstrual = 0;
    s.colico_menstrual = 2;
    assert(astronaut_add_sample(&a, &s) == OK);
    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "HORMONAL_LUTEAL_EFFECT", ALERT_CAUTION));

    /* 5. Supresion hormonal sin Vitamina D e hidratacion reducida -> HORMONAL_VENOUS_BONE */
    sample_from_baseline(&s, &a.baseline, 4);
    s.fase_ciclo = CYCLE_SUPPRESSED;
    s.dia_ciclo = 10;
    s.sintomas_flags &= ~SYM_VIT_D_TAKEN;
    s.hidratacion_ml = (int)(0.78f * (float)a.baseline.hidratacion_objetivo_ml);
    assert(astronaut_add_sample(&a, &s) == OK);
    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "HORMONAL_VENOUS_BONE", ALERT_CAUTION));

    /* 6. Simulacion de escenario HORMONAL_FEMALE */
    AppState st;
    memset(&st, 0, sizeof(st));
    baseline_init_nasa_defaults(&st.defaults);
    mission_init(&st.missions[0], "m_fem", "Mision Fem", "Orbita");
    st.mission_count = 1;
    st.astronauts[0] = a;
    st.astronaut_count = 1;
    mission_add_crew(&st.missions[0], a.id);
    assert(simulate_mission_scenario(&st, &st.missions[0], "HORMONAL_FEMALE") == OK);
    assert(has_alert_rule(&st.astronauts[0].report, "HORMONAL_MENORRHAGIA_CRIT", ALERT_CRITICAL) ||
           has_alert_rule(&st.astronauts[0].report, "HORMONAL_MENSTRUAL_CARE", ALERT_CAUTION));

    /* 7. Prioridad Total (MUY CRITICO): 2+ alertas criticas o score <= 50 */
    DailyHealthSample s_vc;
    sample_from_baseline(&s_vc, &st.astronauts[0].baseline, st.missions[0].dia_actual + 1);
    s_vc.fase_ciclo = CYCLE_MENSTRUAL;
    s_vc.dia_ciclo = 2;
    s_vc.flujo_menstrual = 3;
    s_vc.colico_menstrual = 8;
    s_vc.fc = st.astronauts[0].baseline.fc_base + 22.0f;
    s_vc.suplemento_hierro = 0;
    s_vc.spo2 = 86.0f; /* Suma ENV_HYPOXIA_CRIT + HORMONAL_MENORRHAGIA_CRIT */
    assert(astronaut_add_sample(&st.astronauts[0], &s_vc) == OK);
    evaluate_astronaut(&st.astronauts[0], &st.missions[0].env);
    assert(st.astronauts[0].report.global_status == ALERT_CRITICAL);
    assert(st.astronauts[0].report.critical_alert_count >= 2);
    assert(st.astronauts[0].report.is_very_critical == 1);

    MissionHealthSummary msum;
    evaluate_mission_summary(&st, &st.missions[0], &msum);
    assert(msum.crew_very_critical_count == 1);
    assert(strcmp(msum.priority_astronaut_id, "astro_fem") == 0);
}

/* Helper para enviar una peticion HTTP local a un mg_mgr en el mismo hilo */
static int send_local_http_req(struct mg_mgr *mgr,
                               int port,
                               const char *method,
                               const char *uri,
                               const char *body,
                               char *out_resp,
                               size_t out_cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    mg_mgr_poll(mgr, 10);

    char req[4096];
    size_t blen = body ? strlen(body) : 0;
    int rlen = snprintf(req, sizeof(req),
                        "%s %s HTTP/1.1\r\n"
                        "Host: localhost:%d\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: %zu\r\n"
                        "\r\n"
                        "%s",
                        method, uri, port, blen, body ? body : "");
    send(fd, req, (size_t)rlen, 0);

    size_t total = 0;
    for (int iter = 0; iter < 20; iter++) {
        mg_mgr_poll(mgr, 5);
        ssize_t n = recv(fd, out_resp + total, out_cap - total - 1, 0);
        if (n > 0) {
            total += (size_t)n;
        }
    }
    out_resp[total] = '\0';
    close(fd);

    int status = 0;
    if (strncmp(out_resp, "HTTP/1.1 ", 9) == 0) {
        status = atoi(out_resp + 9);
    }
    return status;
}

static void test_http_rest_api_and_static_files(void) {
    static AppState state;
    assert(storage_load_all(&state, "/tmp/space_health_test_api") == OK);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    int port = 18942;
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d", port);
    assert(mg_http_listen(&mgr, url, server_http_handler, &state) != NULL);

    static char resp[262144];

    /* 1. Archivos estaticos */
    assert(send_local_http_req(&mgr, port, "GET", "/", NULL, resp, sizeof(resp)) == 200);
    assert(strlen(resp) < sizeof(resp) - 100);
    assert(strstr(resp, "P.U.L.S.O.") != NULL);
    assert(send_local_http_req(&mgr, port, "GET", "/styles.css", NULL, resp, sizeof(resp)) == 200);
    assert(send_local_http_req(&mgr, port, "GET", "/app.js", NULL, resp, sizeof(resp)) == 200);
    assert(send_local_http_req(&mgr, port, "GET", "/assets/avatars/astro_01.svg", NULL, resp, sizeof(resp)) == 200);

    /* 2. GET /api/defaults y GET /api/glossary */
    assert(send_local_http_req(&mgr, port, "GET", "/api/defaults", NULL, resp, sizeof(resp)) == 200);
    assert(strstr(resp, "NASA_CAREER_RAD_LIMIT_MSV") != NULL);

    assert(send_local_http_req(&mgr, port, "GET", "/api/glossary", NULL, resp, sizeof(resp)) == 200);
    assert(strlen(resp) < sizeof(resp) - 100);
    assert(strstr(resp, "\"items\"") != NULL);
    assert(strstr(resp, "\"sources\"") != NULL);
    assert(strstr(resp, "\"MEASURES\"") != NULL);
    assert(strstr(resp, "\"ACRONYMS\"") != NULL);
    assert(strstr(resp, "\"source_title\"") != NULL);
    assert(strstr(resp, "HORMONAL_MENORRHAGIA_CRIT") != NULL);
    assert(strstr(resp, "SYM_THIGH_CUFF_ACTIVE") != NULL);
    assert(strstr(resp, "CDRA") != NULL);
    assert(strstr(resp, "CAMRAS") != NULL);
    assert(strstr(resp, "\"system\":\"RADIATION\"") != NULL);
    assert(strstr(resp, "Ánimo y estrés: escala 1–10") != NULL);
    assert(strstr(resp, "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/") != NULL);

    /* 2b. Subrutas API desconocidas deben retornar 404 Not Found (no 400 Bad Request) */
    assert(send_local_http_req(&mgr, port, "GET", "/api/missions/artemis_sim/unknown", NULL, resp, sizeof(resp)) == 404);
    assert(send_local_http_req(&mgr, port, "GET", "/api/astronauts/astro_01/unknown", NULL, resp, sizeof(resp)) == 404);

    /* 2c. Cuerpos JSON que no sean objetos (arrays o primitivos) deben rechazarse con 400 Bad Request */
    assert(send_local_http_req(&mgr, port, "PUT", "/api/missions/artemis_sim/env",
                               "[1, 2, 3]", resp, sizeof(resp)) == 400);
    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts",
                               "\"not_an_object\"", resp, sizeof(resp)) == 400);

    /* 3. POST /api/astronauts (crear y validar rechazo de ID, baseline, tipos o historial invalidos) */
    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts",
                               "{\"id\":\"../bad_id\",\"nombre\":\"Bad\"}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts",
                               "{\"id\":\"astro_badm\",\"nombre\":\"Bad Mission Type\",\"mission_id\":12345}",
                               resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts",
                               "{\"id\":\"astro_badb\",\"nombre\":\"Bad Base\",\"baseline\":{\"fc_base\":-20}}",
                               resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts",
                               "{\"id\":\"astro_badh\",\"nombre\":\"Bad Hist\",\"history\":[{\"dia_mision\":1,\"spo2\":999}]}",
                               resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts",
                               "{\"id\":\"astro_t1\",\"nombre\":\"Cmdr. Test\",\"rol\":\"Piloto\",\"sexo\":\"F\",\"mission_id\":\"artemis_sim\"}",
                               resp, sizeof(resp)) == 201);
    assert(strstr(resp, "\"sexo\":\"F\"") != NULL);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts",
                               "{\"id\":\"astro_bads\",\"nombre\":\"Bad Sex\",\"sexo\":\"X\"}",
                               resp, sizeof(resp)) == 400);

    /* 3b. POST /api/avatars (subida personalizada Base64 + rechazo formato invalido) */
    assert(send_local_http_req(&mgr, port, "POST", "/api/avatars",
                               "{\"id\":\"astro_t1\",\"data_url\":\"not_a_data_url\"}",
                               resp, sizeof(resp)) == 400);

    /* SVG minimo valido en base64: '<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4"/>' */
    assert(send_local_http_req(&mgr, port, "POST", "/api/avatars",
                               "{\"id\":\"astro_t1\",\"data_url\":\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSI0IiBoZWlnaHQ9IjQiLz4=\"}",
                               resp, sizeof(resp)) == 201);
    assert(strstr(resp, "assets/avatars/custom_astro_t1.svg") != NULL);

    /* Verificar muestra hormonal + rechazo fase_ciclo fuera de rango */
    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":14,\"fase_ciclo\":9}",
                               resp, sizeof(resp)) == 400);
    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":14,\"fase_ciclo\":1,\"dia_ciclo\":2,\"flujo_menstrual\":3,\"colico_menstrual\":8,\"suplemento_hierro\":0}",
                               resp, sizeof(resp)) == 201);
    assert(strstr(resp, "HORMONAL_MENORRHAGIA_CRIT") != NULL);

    /* 4. POST /api/astronauts/astro_t1/samples (check-in parcial, rechazo fuera de rango y rechazo de tipo erroneo) */
    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "[1, 2, 3]", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":15,\"temp_c\":49.5}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":15,\"spo2\":\"invalid_type\"}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":15,\"spo2\":null}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":15,\"animo\":true}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":15,\"animo\":7.5}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":15,\"sintomas_flags\":-5}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":15,\"sintomas_flags\":null}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":15,\"sintomas_flags\":\"bad_flags\"}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":15,\"sintomas_flags\":1024}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":15,\"spo2\":88.0,\"dolor_cabeza\":6}", resp, sizeof(resp)) == 201);
    assert(strstr(resp, "ENV_HYPOXIA_CRIT") != NULL);

    /* 4b. PUT /api/astronauts/astro_t1 (rechazo de baseline invalida, tipos invalidos o radiacion negativa) */
    assert(send_local_http_req(&mgr, port, "PUT", "/api/astronauts/astro_t1",
                               "{\"radiacion_previa_msv\":-10}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "PUT", "/api/astronauts/astro_t1",
                               "{\"nombre\":123}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "PUT", "/api/astronauts/astro_t1",
                               "{\"baseline\":{\"pa_sis_base\":110,\"pa_dia_base\":120}}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "PUT", "/api/astronauts/astro_t1",
                               "{\"radiacion_mision_msv\":-5}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "PUT", "/api/astronauts/astro_t1",
                               "{\"nombre\":\"Dra. Test Editada\",\"sexo\":\"F\",\"rol\":\"Oficial Medica\",\"radiacion_previa_msv\":22.5,\"radiacion_mision_msv\":3.25,\"baseline\":{\"fc_base\":68}}",
                               resp, sizeof(resp)) == 200);
    assert(strstr(resp, "Dra. Test Editada") != NULL);
    assert(strstr(resp, "\"sexo\":\"F\"") != NULL);

    /* 5. POST /api/missions y PUT /api/missions/artemis_sim/env (validaciones y cambio de mision en re-import) */
    assert(send_local_http_req(&mgr, port, "POST", "/api/missions",
                               "{\"id\":\"bad_env_m\",\"nombre\":\"Bad Env\",\"env\":{\"po2_kpa\":2.0}}",
                               resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "POST", "/api/missions",
                               "{\"id\":\"mars_m2\",\"nombre\":\"Mars Sim 2\",\"dia_actual\":10}",
                               resp, sizeof(resp)) == 201);

    /* Re-importar astro_t1 cambiando mission_id a mars_m2 debe removerlo de artemis_sim */
    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts",
                               "{\"id\":\"astro_t1\",\"nombre\":\"Cmdr. Test\",\"rol\":\"Piloto\",\"mission_id\":\"mars_m2\"}",
                               resp, sizeof(resp)) == 201);
    Mission *m_art = state_find_mission(&state, "artemis_sim");
    Mission *m_mars = state_find_mission(&state, "mars_m2");
    assert(m_art != NULL && !mission_has_crew(m_art, "astro_t1"));
    assert(m_mars != NULL && mission_has_crew(m_mars, "astro_t1"));

    assert(send_local_http_req(&mgr, port, "PUT", "/api/missions/artemis_sim/env",
                               "{\"dia_actual\":-3}", resp, sizeof(resp)) == 400);

    assert(send_local_http_req(&mgr, port, "PUT", "/api/missions/artemis_sim/env",
                               "{\"latencia_com_seg\":840}", resp, sizeof(resp)) == 200);
    assert(strstr(resp, "\"autonomous_mode\":1") != NULL);

    /* 5b. Verificar Prioridad Total (MUY CRITICO) en API REST de muestra y resumen de mision */
    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":16,\"spo2\":85.0,\"tuvo_eva\":1,\"pre_resp_o2_min\":60,\"sintomas_flags\":8}",
                               resp, sizeof(resp)) == 201);
    assert(strstr(resp, "\"is_very_critical\":1") != NULL);
    assert(strstr(resp, "\"critical_alert_count\":") != NULL);

    assert(send_local_http_req(&mgr, port, "GET", "/api/missions/mars_m2", NULL, resp, sizeof(resp)) == 200);
    assert(strstr(resp, "\"crew_very_critical_count\":1") != NULL);
    assert(strstr(resp, "\"priority_astronaut_id\":\"astro_t1\"") != NULL);
    assert(strstr(resp, "\"fase_ciclo\":") != NULL);

    /* 5c. Verificar dia_descanso en API REST de muestra */
    assert(send_local_http_req(&mgr, port, "POST", "/api/astronauts/astro_t1/samples",
                               "{\"dia_mision\":17,\"dia_descanso\":1,\"ejercicio_min\":0}",
                               resp, sizeof(resp)) == 201);
    assert(strstr(resp, "\"dia_descanso\":1") != NULL);

    /* 6. GET /api/astronauts/astro_t1/export */
    assert(send_local_http_req(&mgr, port, "GET", "/api/astronauts/astro_t1/export", NULL, resp, sizeof(resp)) == 200);
    assert(strstr(resp, "Content-Disposition: attachment") != NULL);

    /* 7. DELETE /api/astronauts/astro_t1 */
    assert(send_local_http_req(&mgr, port, "DELETE", "/api/astronauts/astro_t1", NULL, resp, sizeof(resp)) == 200);
    assert(send_local_http_req(&mgr, port, "GET", "/api/astronauts/astro_t1", NULL, resp, sizeof(resp)) == 404);

    mg_mgr_free(&mgr);

    /* Limpiar directorio temporal de pruebas y avatar de prueba */
    unlink("web/assets/avatars/custom_astro_t1.svg");
    unlink("/tmp/space_health_test_api/missions/artemis_sim.json");
    unlink("/tmp/space_health_test_api/missions/mars_m2.json");
    unlink("/tmp/space_health_test_api/defaults.json");
    rmdir("/tmp/space_health_test_api/astronauts");
    rmdir("/tmp/space_health_test_api/missions");
    rmdir("/tmp/space_health_test_api");
}

static void test_rest_day_and_microgravity_sms(void) {
    CabinEnv env;
    cabin_init_nominal(&env);
    env.gravedad_actual = GRAV_MICRO;

    Astronaut a;
    astronaut_init_from_defaults(&a, "astro_rest", "Esp. Descanso", "Especialista", "artemis_sim", NULL, NULL);

    /* Validar limites de dia_descanso */
    DailyHealthSample bad_s;
    sample_from_baseline(&bad_s, &a.baseline, 1);
    bad_s.dia_descanso = 2;
    assert(validate_sample(&bad_s) == ERR_INVALID_RANGE);
    bad_s.dia_descanso = -1;
    assert(validate_sample(&bad_s) == ERR_INVALID_RANGE);

    /* 5 dias activos (90 min) + 2 dias de descanso programado (0 min): promedio activo se mantiene en 90 min */
    for (int d = 1; d <= 7; d++) {
        DailyHealthSample s;
        sample_from_baseline(&s, &a.baseline, d);
        if (d == 3 || d == 7) {
            s.dia_descanso = 1;
            s.ejercicio_min = 0;
        } else {
            s.dia_descanso = 0;
            s.ejercicio_min = 90;
        }
        assert(astronaut_add_sample(&a, &s) == OK);
    }

    float ej_7d = 0.0f;
    astronaut_compute_window_averages(&a, 7, NULL, &ej_7d, NULL, NULL);
    assert(ej_7d > 89.5f && ej_7d < 90.5f);
    evaluate_astronaut(&a, &env);
    assert(!has_alert_rule(&a.report, "GRAV_BONE_CAUT", ALERT_CAUTION));
    assert(!has_alert_rule(&a.report, "GRAV_BONE_CRIT", ALERT_CRITICAL));

    /* Al sumar un 3er dia de descanso en la misma ventana (dia 8 con dia 2..8 = 3 descansos), si se penaliza */
    DailyHealthSample s8;
    sample_from_baseline(&s8, &a.baseline, 8);
    s8.dia_descanso = 1;
    s8.ejercicio_min = 0;
    assert(astronaut_add_sample(&a, &s8) == OK);
    astronaut_compute_window_averages(&a, 7, NULL, &ej_7d, NULL, NULL);
    assert(ej_7d < 60.0f);
    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "GRAV_BONE_CAUT", ALERT_CAUTION));

    /* Mareo espacial (SMS) severo en microgravedad 0g (>= 5) debe disparar alerta vestibular y NO-GO */
    DailyHealthSample s_sms;
    sample_from_baseline(&s_sms, &a.baseline, 9);
    s_sms.mareo_vestibular = 5;
    assert(astronaut_add_sample(&a, &s_sms) == OK);
    evaluate_astronaut(&a, &env);
    assert(has_alert_rule(&a.report, "GRAV_VESTIBULAR_DYNAMIC", ALERT_CAUTION));
    assert(a.report.eva_aptitude == APT_NO_GO);
}

static void test_countermeasure_step_format_and_cycle_alignment(void) {
    static AppState st;
    assert(storage_load_all(&st, "data") == OK);
    assert(st.astronaut_count == 4);

    /* Verificar alineacion dia_ciclo <-> fase_ciclo en tripulantes femeninas semilla */
    for (int i = 0; i < st.astronaut_count; i++) {
        const Astronaut *a = &st.astronauts[i];
        if (a->sexo[0] == 'F') {
            for (int j = 0; j < a->history.count; j++) {
                const DailyHealthSample *s = ring_buffer_get_chronological(&a->history, j);
                assert(s != NULL);
                if (s->dia_ciclo >= 1 && s->dia_ciclo <= 5) assert(s->fase_ciclo == CYCLE_MENSTRUAL);
                else if (s->dia_ciclo >= 6 && s->dia_ciclo <= 13) assert(s->fase_ciclo == CYCLE_FOLLICULAR);
                else if (s->dia_ciclo >= 14 && s->dia_ciclo <= 16) assert(s->fase_ciclo == CYCLE_OVULATORY);
                else if (s->dia_ciclo >= 17 && s->dia_ciclo <= 28) assert(s->fase_ciclo == CYCLE_LUTEAL);
            }
        }
    }

    /* Verificar que todas las alertas en todos los escenarios tengan contramedida en pasos 1) ... */
    const char *scenarios[] = {
        "SPE_STORM", "HYPOXIA", "HYPERCAPNIA", "SANS_SLEEP",
        "FATIGUE_ISOLATION", "DCS_EVA", "HORMONAL_FEMALE"
    };
    Mission *m = state_find_mission(&st, "artemis_sim");
    assert(m != NULL);

    for (size_t k = 0; k < sizeof(scenarios) / sizeof(scenarios[0]); k++) {
        assert(simulate_mission_scenario(&st, m, scenarios[k]) == OK);
        int total_alerts = 0;
        for (int i = 0; i < st.astronaut_count; i++) {
            const EvaluationReport *rep = &st.astronauts[i].report;
            for (int a = 0; a < rep->alert_count; a++) {
                total_alerts++;
                const char *cm = rep->alerts[a].contramedida;
                assert(strncmp(cm, "1)", 2) == 0);
                assert(strlen(cm) < sizeof(rep->alerts[a].contramedida) - 4);
            }
        }
        assert(total_alerts > 0);
    }
}

int main(void) {
    printf("===================================================================\n");
    printf("  Running NASA HRP Health Engine, Storage & HTTP REST Tests (C11)\n");
    printf("===================================================================\n");

    RUN_TEST(test_id_sanitization);
    RUN_TEST(test_sample_and_cabin_validation);
    RUN_TEST(test_ring_buffer_and_radiation_accumulator);
    RUN_TEST(test_rule1_radiation_hazards);
    RUN_TEST(test_rule2_sans_neuro_ocular);
    RUN_TEST(test_rule3_hypoxia_and_hypercapnia);
    RUN_TEST(test_rule4_dcs_eva);
    RUN_TEST(test_rule5_cardio_bone_vestibular);
    RUN_TEST(test_rule6_immune_and_metabolic);
    RUN_TEST(test_rule7_8_isolation_sleep_and_distance);
    RUN_TEST(test_rule9_female_hormonal_physiology);
    RUN_TEST(test_rest_day_and_microgravity_sms);
    RUN_TEST(test_seed_storage_and_simulations);
    RUN_TEST(test_http_rest_api_and_static_files);
    RUN_TEST(test_countermeasure_step_format_and_cycle_alignment);

    printf("-------------------------------------------------------------------\n");
    printf("  RESULTS: %d / %d tests passed successfully!\n", g_tests_passed, g_tests_run);
    printf("===================================================================\n");
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}

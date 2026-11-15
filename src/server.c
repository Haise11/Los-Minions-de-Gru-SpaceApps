#include "server.h"
#include "astronaut.h"
#include "health_engine.h"
#include "mission.h"
#include "storage.h"
#include "../third_party/cJSON.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_HEADERS \
    "Content-Type: application/json; charset=utf-8\r\n" \
    "Cache-Control: no-store\r\n" \
    "Access-Control-Allow-Origin: *\r\n" \
    "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n" \
    "Access-Control-Allow-Headers: Content-Type\r\n"

static void safe_strcpy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static void reply_json_obj(struct mg_connection *c, int status_code, cJSON *obj) {
    if (!obj) {
        mg_http_reply(c, 500, JSON_HEADERS, "{\"error\":\"Internal serialization error\"}\n");
        return;
    }
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!str) {
        mg_http_reply(c, 500, JSON_HEADERS, "{\"error\":\"Out of memory\"}\n");
        return;
    }
    mg_http_reply(c, status_code, JSON_HEADERS, "%s\n", str);
    free(str);
}

static void reply_error(struct mg_connection *c, int status_code, StatusCode err_code, const char *msg) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", msg ? msg : "Error");
    cJSON_AddNumberToObject(o, "code", (int)err_code);
    reply_json_obj(c, status_code, o);
}

static int method_is(const struct mg_http_message *hm, const char *m) {
    return mg_strcmp(hm->method, mg_str(m)) == 0;
}

static cJSON *parse_req_body(const struct mg_http_message *hm) {
    if (!hm || !hm->body.buf || hm->body.len == 0) return NULL;
    cJSON *root = cJSON_ParseWithLength(hm->body.buf, hm->body.len);
    if (!root) return NULL;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static void copy_cap(const struct mg_str *cap, char *dst, size_t dst_size) {
    if (!cap || !cap->buf || cap->len == 0 || dst_size == 0) {
        if (dst && dst_size > 0) dst[0] = '\0';
        return;
    }
    size_t n = cap->len < (dst_size - 1) ? cap->len : (dst_size - 1);
    memcpy(dst, cap->buf, n);
    dst[n] = '\0';
}

/* Construye el objeto completo para GET /api/missions/:id */
static cJSON *build_mission_detail_json(const AppState *state, const Mission *m) {
    cJSON *o = mission_to_json(m);
    if (!o) return NULL;

    int autonomous = (m->env.latencia_com_seg > 5.0f) ? 1 : 0;
    cJSON_AddNumberToObject(o, "autonomous_mode", autonomous);

    MissionHealthSummary sum;
    evaluate_mission_summary(state, m, &sum);
    cJSON_AddItemToObject(o, "summary", mission_health_summary_to_json(&sum));
    cJSON_AddNumberToObject(o, "mission_health_avg", sum.mission_health_avg);

    cJSON *crew_arr = cJSON_AddArrayToObject(o, "crew");
    for (int i = 0; i < state->astronaut_count; i++) {
        const Astronaut *a = &state->astronauts[i];
        if (strcmp(a->mission_id, m->id) == 0 || mission_has_crew(m, a->id)) {
            cJSON_AddItemToArray(crew_arr, astronaut_summary_to_json(a));
        }
    }
    return o;
}

static void add_glossary_item(cJSON *arr, const char *id, const char *category,
                              const char *code, const char *title, const char *severity,
                              const char *hazard, const char *system_body,
                              const char *thresholds, const char *summary,
                              const char *protocol, const char *source_title,
                              const char *source_url, const char *keywords) {
    cJSON *it = cJSON_CreateObject();
    cJSON_AddStringToObject(it, "id", id);
    cJSON_AddStringToObject(it, "category", category);
    cJSON_AddStringToObject(it, "code", code);
    cJSON_AddStringToObject(it, "title", title);
    cJSON_AddStringToObject(it, "severity", severity);
    cJSON_AddStringToObject(it, "hazard", hazard);
    cJSON_AddStringToObject(it, "system", system_body);
    cJSON_AddStringToObject(it, "thresholds", thresholds);
    cJSON_AddStringToObject(it, "summary", summary);
    cJSON_AddStringToObject(it, "protocol", protocol);
    cJSON_AddStringToObject(it, "source_title", source_title);
    cJSON_AddStringToObject(it, "source_url", source_url);
    cJSON_AddStringToObject(it, "keywords", keywords ? keywords : "");
    cJSON_AddItemToArray(arr, it);
}

static cJSON *build_glossary_json(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", "NASA-HRP-STD-3001-2026");
    cJSON_AddStringToObject(root, "title", "Glosario Clínico, Medidas, Siglas y Protocolos NASA HRP / CSA");

    cJSON *sources = cJSON_AddArrayToObject(root, "sources");
    const struct {
        const char *id;
        const char *agency;
        const char *title;
        const char *url;
    } refs[] = {
        {"HRP_5_HAZARDS", "NASA HRP", "5 Hazards of Human Spaceflight", "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/"},
        {"NASA_BODY_SPACE", "NASA", "The Human Body in Space", "https://www.nasa.gov/humans-in-space/the-human-body-in-space/"},
        {"CSA_EFFECTS", "CSA / ASC", "Living in Space: Effects of Space on the Body", "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp"},
        {"CSA_RADIATION", "CSA / ASC", "Space Radiation and Its Effects on the Human Body", "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp"},
        {"NASA_COUNTERMEASURES", "NASA", "Exercising in Space (Countermeasures: ARED, T2, CEVIS)", "https://www.nasa.gov/AtoZ/exercising-in-space/"},
        {"NASA_RESEARCH", "NASA HRP", "Human Research Program Architecture & Standards", "https://www.nasa.gov/hrp/research/"}
    };
    for (size_t i = 0; i < sizeof(refs) / sizeof(refs[0]); i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "id", refs[i].id);
        cJSON_AddStringToObject(s, "agency", refs[i].agency);
        cJSON_AddStringToObject(s, "title", refs[i].title);
        cJSON_AddStringToObject(s, "url", refs[i].url);
        cJSON_AddItemToArray(sources, s);
    }

    cJSON *items = cJSON_AddArrayToObject(root, "items");

    /* 1. LOS 5 PELIGROS DEL NASA HRP */
    add_glossary_item(items, "haz_radiation", "HAZARDS", "PELIGRO 1 · RADIACIÓN",
        "Radiación Espacial (Rayos Cósmicos Galácticos y Tormentas Solares SPE)", "CRITICAL",
        "RADIATION", "IMMUNE",
        "Normal < 1.0 mSv/día · Precaución >= 1.0 mSv/día o >= 480 mSv en carrera · Crítico > 2.0 mSv/día, tasa >= 0.5 mSv/h, tormenta solar activa o >= 600 mSv acumulados.",
        "Fuera de la magnetósfera terrestre, la tripulación enfrenta Rayos Cósmicos Galácticos (GCR) y Eventos de Partículas Solares (SPE). Provoca daño al ADN, caída inmunitaria aguda, cataratas tempranas y riesgo oncológico y cardiovascular acumulado (límite estándar NASA: 600 mSv de carrera).",
        "Despliegue inmediato a refugio blindado contra tormentas solares, suspensión total de EVA (No apto), dosimetría neutrónica continua (Radi-N2 / CANS) y refuerzo de blindaje con bolsas de agua.",
        "CSA — Space Radiation and Its Effects on the Human Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp",
        "RAD_SPE_SPIKE RAD_CAREER_LIMIT");

    add_glossary_item(items, "haz_gravity", "HAZARDS", "PELIGRO 2 · GRAVEDAD",
        "Campos de Gravedad Alterados (Microgravedad 0g, Lunar 1/6g, Marciana 3/8g)", "CAUTION",
        "GRAVITY", "CARDIOVASCULAR",
        "Visión SANS: caída >= 0.15 (Precaución) o >= 0.25 (Crítico) · Ejercicio 7d < 60 min/día (Precaución) o < 45 min/día (Crítico) · Mareo vestibular >= 5/10 bajo gravedad.",
        "La microgravedad desplaza fluidos hacia la cabeza (favoreciendo el síndrome neuro-ocular SANS y estasis venosa yugular), contrae el volumen plasmático, acelera la pérdida ósea (1–1.5 %/mes sin ejercicio) y provoca desorientación sensoriomotora en transiciones gravitatorias.",
        "Cumplir entrenamiento diario en Zero-T2 / ARED (meta 90 min/día), suplementación con Vitamina D3, manguitos de compresión en muslos (Thigh Cuff) + Complejo B ante riesgo SANS e hidratación dirigida.",
        "NASA — The Human Body in Space",
        "https://www.nasa.gov/humans-in-space/the-human-body-in-space/",
        "GRAV_BONE_CAUT GRAV_BONE_CRIT SYM_VIT_D_TAKEN");

    add_glossary_item(items, "haz_hostile_env", "HAZARDS", "PELIGRO 3 · AMBIENTE HOSTIL",
        "Ambientes Hostiles y Cerrados (Soporte Vital ECLSS y Microecosistema)", "CRITICAL",
        "HOSTILE_ENV", "RESPIRATORY",
        "CO2 normal <= 0.5 kPa · Precaución > 0.5 kPa (+ cefalea >= 4 o reflejos lentos) · Crítico > 0.8 kPa | SpO2 < 94 % o pO2 < 18 kPa (Precaución) · SpO2 < 90 % o pO2 < 15 kPa (Crítico).",
        "El hábitat presurizado depende íntegramente del soporte vital ECLSS. Sin convección natural en 0g se forman bolsas de CO2 que inducen hipercapnia (cefalea y lentitud cognitiva). La caída de O2 produce hipoxia tisular y la flora cerrada eleva el riesgo alérgico e infeccioso.",
        "Inspeccionar depurador de CO2 (cartuchos LiOH), verificar presión parcial de O2 en cabina, administrar O2 al 100 % ante hipoxia o síntomas post-EVA y auditar filtros HEPA.",
        "NASA HRP — 5 Hazards of Human Spaceflight",
        "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/",
        "ENV_HYPERCAPNIA ENV_HYPOXIA");

    add_glossary_item(items, "haz_isolation", "HAZARDS", "PELIGRO 4 · AISLAMIENTO",
        "Aislamiento, Confinamiento y Desincronización Circadiana", "CAUTION",
        "ISOLATION", "BEHAVIORAL",
        "Sueño promedio 3 días < 6.0 h (+ reflejos lentos, estrés >= 7 o ánimo <= 4) -> Precaución · Sueño < 5.0 h + Estrés >= 8 -> Crítico.",
        "Ciclos luz/oscuridad no terrestres, confinamiento prolongado y sobrecarga operativa alteran el ritmo circadiano y el desempeño psicomotor. Produce deuda crónica de sueño, deterioro de la cohesión de tripulación (<= 2/5), fatiga de menú y elevación de cortisol.",
        "Aplicar iluminación circadiana y protocolo conductual ARCHeR, proteger bloques de 8 horas de sueño, rotar dieta fresca (Veggie Fresh) y redistribuir carga de trabajo.",
        "CSA — Effects of Space on the Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp",
        "ISO_SLEEP_COGNITIVE ARCHER");

    add_glossary_item(items, "haz_distance", "HAZARDS", "PELIGRO 5 · DISTANCIA",
        "Distancia de la Tierra y Autonomía Médica de Misión Profunda", "CAUTION",
        "DISTANCE", "BEHAVIORAL",
        "Órbita baja: < 1 s · Luna (Artemis): ~1.3 s · Marte/Tránsito: 180–1320 s · Modo Autónomo a bordo activo automáticamente si la latencia supera 5.0 s.",
        "En misiones de espacio profundo no existe evacuación inmediata ni telemedicina en vivo. La tripulación debe ejecutar triaje y contramedidas de forma autónoma mediante evaluación clínica local y diagnóstico portátil (Tempus Pro).",
        "Operar en Modo Autónomo a bordo cuando el retardo de comunicaciones supere 5.0 s, seguir las alertas clínicas locales y exportar bitácoras para sincronización diferida con Tierra.",
        "NASA HRP — Human Research Program Research",
        "https://www.nasa.gov/hrp/research/",
        "AUTONOMOUS_MODE");

    /* 2. PROTOCOLOS Y CONTRAMEDIDAS */
    add_glossary_item(items, "prot_ared_zerot2", "PROTOCOLS", "CONTRAMEDIDA 01 · EJERCICIO",
        "Protocolo Osteomuscular y Cardiovascular: ARED + Zero-T2 + Vitamina D", "OK",
        "GRAVITY", "MUSCULOSKELETAL",
        "Meta diaria: 90 min/día · Precaución si promedio 7 días < 60 min/día · Crítico si promedio 7 días < 45 min/día.",
        "La pérdida de densidad ósea y masa muscular en microgravedad exige carga resistiva de alta intensidad (ARED) combinada con carrera aeróbica sujeta con arnés en Zero-T2 y suplementación diaria de Vitamina D3.",
        "Completar >= 60–90 min/día en ARED/Zero-T2, registrar toma diaria de Vitamina D3 y sostener la meta calórica y de hidratación.",
        "NASA — Exercising in Space",
        "https://www.nasa.gov/AtoZ/exercising-in-space/",
        "GRAV_BONE_CAUT GRAV_BONE_CRIT SYM_VIT_D_TAKEN");

    add_glossary_item(items, "prot_sans_cuff", "PROTOCOLS", "CONTRAMEDIDA 02 · SANS",
        "Protocolo Neuro-Ocular SANS: Manguitos en Muslos + Complejo B + Tempus Pro", "CAUTION",
        "GRAVITY", "OCULAR",
        "Precaución: caída visual >= 0.15 (+ cefalea >= 4 o presión sistólica >= base + 20 mmHg) · Crítico: caída visual >= 0.25.",
        "El desplazamiento cefálico de fluidos eleva la presión venosa craneal y produce cambios neuro-oculares (SANS). Se contrarresta con manguitos de compresión en muslos (Thigh Cuff) para retener fluidos en extremidades inferiores junto a suplementación con Complejo B.",
        "Activar Thigh Cuff, administrar Complejo B y realizar evaluación de fondo de ojo y vaina del nervio óptico con el monitor Tempus Pro.",
        "NASA — The Human Body in Space (SANS)",
        "https://www.nasa.gov/humans-in-space/the-human-body-in-space/",
        "SANS_CAUTION SANS_CRITICAL SYM_THIGH_CUFF_ACTIVE SYM_B_COMPLEX_TAKEN");

    add_glossary_item(items, "prot_storm_shelter", "PROTOCOLS", "CONTRAMEDIDA 03 · REFUGIO",
        "Refugio Blindado contra Tormenta de Partículas Solares (SPE)", "CRITICAL",
        "RADIATION", "IMMUNE",
        "Se activa ante alerta de tormenta solar en cabina, tasa externa >= 0.5 mSv/h o dosis diaria superior a 2.0 mSv.",
        "Durante un evento SPE o pico de radiación solar, los protones energéticos elevan rápidamente la dosis absorbida. El refugio temporal rodeado de agua potable y polietileno atenúa la radiación secundaria.",
        "Suspender cualquier EVA de inmediato (No apto), trasladar a toda la tripulación al refugio blindado, mantener dosimetría activa (Radi-N2 / CANS) y vigilar síntomas tempranos.",
        "CSA — Space Radiation and Its Effects on the Human Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp",
        "RAD_SPE_SPIKE");

    add_glossary_item(items, "prot_eclss_dcs", "PROTOCOLS", "CONTRAMEDIDA 04 · ECLSS Y DCS",
        "Purga Atmosférica ECLSS y Pre-Respiración de O2 contra DCS Post-EVA", "CRITICAL",
        "HOSTILE_ENV", "RESPIRATORY",
        "Cabina: CO2 > 0.5 / > 0.8 kPa o pO2 < 18.0 / < 15.0 kPa | Riesgo DCS: EVA realizada con pre-respiración de O2 < 120 min o dolor articular posterior.",
        "En caminatas espaciales (EVA), el traje trabaja a menor presión; una pre-respiración insuficiente (< 120 min) deja nitrógeno disuelto capaz de formar burbujas articulares o neurológicas (enfermedad descompresiva DCS).",
        "Ante falla ECLSS: revisar depurador de CO2 y reservas de O2. Ante sospecha de DCS post-EVA: administrar O2 al 100 %, reposo articular, examen neurológico y recompresión en esclusa.",
        "NASA HRP — 5 Hazards of Human Spaceflight",
        "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/",
        "EVA_DCS_RISK SYM_EVA_JOINT_PAIN");

    /* 3. SÍNTOMAS Y CONDICIONES CLÍNICAS */
    add_glossary_item(items, "sym_immune_flags", "SYMPTOMS", "CLÍNICA 01 · INMUNIDAD",
        "Sarpullido Alérgico, Reactivación Viral e Infección Febril", "CAUTION",
        "HOSTILE_ENV", "IMMUNE",
        "Precaución ante signos cutáneos/virales aislados · Crítico si Temperatura >= 39.0 °C o >= 37.8 °C acompañada de infección o glucemia alterada.",
        "El estrés fisiológico, la desregulación inmunitaria y el aire recirculado favorecen dermatitis de contacto, reactivación de virus latentes y cuadros infecciosos en hábitats cerrados.",
        "Tomar muestra de saliva/sangre (Standard Measures), aislar preventivamente si hay fiebre, verificar glucemia (70–140 mg/dL) y revisar filtros HEPA.",
        "NASA HRP — Human Research Program Research",
        "https://www.nasa.gov/hrp/research/",
        "SYM_RASH_ALLERGY SYM_VIRAL_REACTIVATION SYM_INFECTION_SIGNS IMMUNE_CRITICAL");

    add_glossary_item(items, "sym_eva_ortho", "SYMPTOMS", "CLÍNICA 02 · EVA Y ORTOSTATISMO",
        "Dolor Articular Post-EVA y Mareo Ortostático", "CRITICAL",
        "GRAVITY", "CARDIOVASCULAR",
        "Dolor articular tras caminata espacial activa alerta crítica de descompresión · Mareo ortostático dispara alerta cardiovascular o agrava pérdida menstrual abundante.",
        "El dolor articular tras una EVA sugiere formación de burbujas de nitrógeno en tejidos. El mareo ortostático refleja reducción de volumen plasmático (~15 %) e intolerancia hemodinámica en transiciones de gravedad o pérdida sanguínea.",
        "Para dolor articular post-EVA: O2 al 100 % y protocolo de recompresión. Para mareo ortostático: posición horizontal, hidratación con electrolitos y control vascular con Tempus Pro.",
        "CSA — Living in Space: Effects of Space on the Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp",
        "SYM_EVA_JOINT_PAIN SYM_ORTHOSTATIC_DIZZINESS GRAV_CARDIO_VENOUS");

    add_glossary_item(items, "sym_countermeasures_bits", "SYMPTOMS", "CLÍNICA 03 · ADHERENCIA",
        "Contramedidas Diarias: Bandas en Muslos, Complejo B, Vitamina D3 y Dieta Vegetal Fresca", "OK",
        "GRAVITY", "MUSCULOSKELETAL",
        "Registro diario recomendado de adherencia preventiva según estado neuro-ocular, óseo y nutricional.",
        "El registro diario verifica cuatro contramedidas esenciales: manguitos de compresión en muslos (SANS), Complejo B (cofactor vascular y hematopoyético), Vitamina D3 (protección ósea) y vegetales frescos de cultivo a bordo.",
        "Mantener Vitamina D3 y Dieta Vegetal Fresca de forma continua; añadir Bandas en Muslos y Complejo B ante riesgo neuro-ocular SANS o ferropenia.",
        "NASA — Ejercitación y Nutrición Espacial",
        "https://www.nasa.gov/AtoZ/exercising-in-space/",
        "SYM_THIGH_CUFF_ACTIVE SYM_B_COMPLEX_TAKEN SYM_VIT_D_TAKEN SYM_VEGGIE_FRESH_FOOD");

    /* 4. FISIOLOGÍA FEMENINA Y CICLO HORMONAL */
    add_glossary_item(items, "fem_menstrual_microg", "HORMONAL", "SALUD FEMENINA 01 · CICLO",
        "Fisiología Menstrual en Microgravedad y Fases del Ciclo Hormonal", "OK",
        "GRAVITY", "IMMUNE",
        "Fases: No Aplica · Menstrual (días 1–5) · Folicular (6–12) · Ovulatoria (13–15) · Lútea (16–28) · Suprimido (LARC/ACO).",
        "La evidencia médica espacial confirma que el flujo menstrual en 0g sigue un curso anterógrado normal por contractilidad uterina. Su seguimiento diario permite contextualizar temperatura basal, hidratación, balance de hierro y confort operativo.",
        "Registrar fase y día del ciclo, intensidad de flujo (Nulo a Abundante) y nivel de cólico (0–10) en tripulación femenina, asegurando insumos de higiene espacial e hidratación.",
        "NASA — The Human Body in Space",
        "https://www.nasa.gov/humans-in-space/the-human-body-in-space/",
        "HORMONAL_FEMALE CYCLE_MENSTRUAL CYCLE_FOLLICULAR");

    add_glossary_item(items, "fem_iron_anemia", "HORMONAL", "SALUD FEMENINA 02 · HIERRO",
        "Balance de Hierro, Prevención de Anemia Espacial y Menorragia", "CRITICAL",
        "GRAVITY", "CARDIOVASCULAR",
        "Precaución: fase menstrual o flujo moderado/cólico >= 4 sin hierro o baja hidratación · Crítico (No apto): flujo abundante con taquicardia, hipotensión (< 95 mmHg), mareo ortostático o cólico >= 8/10.",
        "En el espacio se produce hemólisis sostenida ('anemia espacial') y contracción plasmática. En tripulantes menstruantes, la pérdida adicional de hierro puede desencadenar ferropenia, taquicardia compensatoria e intolerancia ortostática en EVA.",
        "Indicar suplementación de Hierro + Complejo B y refuerzo hídrico (+350–500 ml). Ante menorragia o dolor intenso (>= 8/10), suspender EVA (No apto) y vigilar hemodinamia.",
        "CSA — Effects of Space on the Body (Cardiovascular & Blood Volume)",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp",
        "HORMONAL_MENSTRUAL_CARE HORMONAL_MENORRHAGIA_CRIT");

    add_glossary_item(items, "fem_luteal_thermo", "HORMONAL", "SALUD FEMENINA 03 · FASE LÚTEA",
        "Fase Lútea en Microgravedad: Termogénesis y Sueño", "CAUTION",
        "ISOLATION", "BEHAVIORAL",
        "Precaución durante fase lútea ante alza térmica fisiológica (+0.35 °C respecto a base y < 37.8 °C), cólico >= 3, cefalea con sueño reducido o descenso anímico.",
        "La progesterona en fase lútea eleva el punto de ajuste de temperatura basal (+0.3 a +0.5 °C, rango subfebril no infeccioso), aumenta la ventilación minuto y puede fragmentar el sueño.",
        "Distinguir la termogénesis lútea (< 37.8 °C) de fiebre infecciosa, ajustar enfriamiento personal, proteger horas de sueño y descartar acumulación de CO2 si hay cefalea.",
        "NASA HRP — Human Research Program Research",
        "https://www.nasa.gov/hrp/research/",
        "HORMONAL_LUTEAL_EFFECT");

    add_glossary_item(items, "fem_suppression_vte", "HORMONAL", "SALUD FEMENINA 04 · SUPRESIÓN Y VTE",
        "Supresión Menstrual (LARC/ACO), Hemostasia Venosa y Densidad Ósea", "CAUTION",
        "GRAVITY", "CARDIOVASCULAR",
        "Precaución en ciclo suprimido si hidratación < 85 % de la meta, ejercicio 7 días < 65 min/día o falta de Vitamina D3.",
        "Para suprimir el ciclo en misiones largas se utilizan anticonceptivos combinados o dispositivos LARC. En 0g, el estrógeno exógeno sumado a estasis yugular exige vigilar riesgo tromboembólico venoso y reforzar la protección ósea.",
        "Sostener hidratación >= 100 % de la meta, cumplir carga en ARED/Zero-T2 (>= 65–90 min/día) + Vitamina D3 y controlar flujo venoso yugular con Tempus Pro.",
        "NASA HRP — Cardiovascular & Bone Countermeasures",
        "https://www.nasa.gov/hrp/research/",
        "HORMONAL_VENOUS_BONE");

    /* 5. REGLAS Y UMBRALES */
    add_glossary_item(items, "thresh_score_aptitude", "THRESHOLDS", "UMBRAL 01 · PUNTAJE Y APTITUD",
        "Cálculo de Puntaje de Salud (0–100) y Aptitud EVA (Apto / Restringido / No apto)", "OK",
        "DISTANCE", "CARDIOVASCULAR",
        "Parte de 100 pts: -25 por cada alerta Crítica, -10 por cada Precaución y hasta -10 por desvíos leves · No apto ante cualquier Crítico, tormenta solar, mareo >= 5, cólico >= 8 o sueño < 5.0 h.",
        "El evaluador clínico a bordo analiza en tiempo real signos vitales, dosis de radiación, atmósfera de cabina, sueño/cognición y salud hormonal, calculando un puntaje integrado de 0 a 100 y la aptitud inmediata para caminata espacial.",
        "Estado Urgencia: paciente Crítico con puntaje <= 50 o >= 2 alertas críticas simultáneas requiere atención médica prioritaria inmediata.",
        "NASA HRP — 5 Hazards of Human Spaceflight",
        "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/",
        "URGENCIA APTO RESTRINGIDO NO APTO");

    /* 6. DEFINICIONES DE MEDIDAS Y UNIDADES */
    add_glossary_item(items, "meas_vitals_cardio", "MEASURES", "MEDIDAS 01 · SIGNOS VITALES",
        "Frecuencia Cardíaca (lpm), Presión Arterial (mmHg), SpO2 (%), Temperatura (°C) y Glucosa (mg/dL)", "OK",
        "GRAVITY", "CARDIOVASCULAR",
        "FC reposo típico: 50–80 lpm · PA sistólica/diastólica: ~110–130 / 70–85 mmHg · SpO2 normal: >= 95 % · Temp oral: 36.2–37.2 °C · Glucemia: 70–140 mg/dL.",
        "FC = frecuencia cardíaca en lpm (latidos por minuto). PA = presión arterial en mmHg (milímetros de mercurio; sistólica de contracción / diastólica de reposo). SpO2 (%) = saturación arterial de oxígeno. °C = temperatura corporal en grados Celsius. mg/dL = glucosa por decilitro de sangre.",
        "Comparar cada medición diaria contra la línea base individual previa al vuelo para detectar hipoxia, hipotensión ortostática, fiebre o estrés metabólico.",
        "NASA — The Human Body in Space",
        "https://www.nasa.gov/humans-in-space/the-human-body-in-space/",
        "FC PA PAS PAD SPO2 TEMP GLUCOSA");

    add_glossary_item(items, "meas_vision_rad", "MEASURES", "MEDIDAS 02 · VISIÓN Y RADIACIÓN",
        "Agudeza Visual (escala decimal), Dosis Diaria y Acumulada (mSv) y Tasa de Dosis (mSv/h)", "OK",
        "RADIATION", "OCULAR",
        "Agudeza base: 1.00 (equivalente 20/20); caída >= 0.15 alerta SANS · Dosis diaria normal < 1.0 mSv · Límite NASA de carrera: 600 mSv.",
        "La agudeza visual se mide en escala decimal (1.00 = visión normal 20/20; valores menores indican pérdida de nitidez por edema ocular SANS). mSv (milisievert) mide dosis equivalente de radiación ionizante ponderada por daño biológico; mSv/h mide la intensidad horaria durante tormentas solares.",
        "Monitorear desviación visual respecto a la base individual y registrar dosimetría diaria y acumulada de misión/carrera tras cada ciclo.",
        "CSA — Space Radiation and Its Effects on the Human Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp",
        "AGUDEZA VISUAL MSV MSV_H");

    add_glossary_item(items, "meas_cabin_kpa", "MEASURES", "MEDIDAS 03 · ATMÓSFERA Y GRAVEDAD",
        "Presiones Parciales de Cabina (kPa) y Nivel de Gravedad (g)", "OK",
        "HOSTILE_ENV", "RESPIRATORY",
        "Presión total estándar: 101.3 kPa (1 atm) · pO2 normal: ~21.0 kPa · pCO2 normal: <= 0.5 kPa · Gravedad: 0g, 1/6g (0.16g), 3/8g (0.38g), 1g.",
        "kPa (kilopascal) mide presión atmosférica y presión parcial de cada gas (101.3 kPa equivale a 1 atmósfera al nivel del mar). La fracción de g compara la aceleración gravitatoria local con la gravedad terrestre estándar (9.81 m/s²).",
        "Controlar que el CO2 no supere 0.5 kPa y el oxígeno se mantenga sobre 18 kPa para evitar hipercapnia e hipoxia.",
        "NASA HRP — 5 Hazards of Human Spaceflight",
        "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/",
        "KPA PO2 PCO2 GRAVEDAD");

    add_glossary_item(items, "meas_wellbeing_scales", "MEASURES", "MEDIDAS 04 · SUEÑO Y ESCALAS SUBJETIVAS",
        "Horas de Sueño (h), Escalas Clínicas (0–10 y 1–5) y Tiempo de Reacción Cognitiva (ms)", "OK",
        "ISOLATION", "BEHAVIORAL",
        "Sueño recomendado: 7.5–8.5 h/día · Estrés y dolor/mareo/cólico: escala 0–10 · Ánimo y cohesión grupal: escala 1–5 · Reacción cognitiva PVT: ~220–300 ms.",
        "ms (milisegundos) mide la latencia de respuesta psicomotora en prueba de vigilancia visual (PVT). Las escalas 0–10 gradúan intensidad sintomática (0 = ausente, 10 = máximo), mientras las escalas 1–5 evalúan bienestar emocional y cohesión de tripulación (5 = óptimo).",
        "Un aumento > 15–20 % del tiempo de reacción en ms respecto a la base individual alerta sobre fatiga acumulada o hipercapnia.",
        "CSA — Living in Space: Effects of Space on the Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp",
        "PVT MS SUENO ESTRES ANIMO");

    add_glossary_item(items, "meas_metabolic_eva", "MEASURES", "MEDIDAS 05 · METABOLISMO, EJERCICIO Y EVA",
        "Hidratación (ml), Energía (kcal), Peso (kg), Ejercicio (min) y Pre-Respiración O2 (min)", "OK",
        "GRAVITY", "MUSCULOSKELETAL",
        "Hidratación objetivo: ~2400–2800 ml/día · Calorías: ~2300–3100 kcal/día · Ejercicio meta: 90 min/día · Pre-respiración O2 EVA: >= 120 min.",
        "ml (mililitros) registra ingesta hídrica diaria para sostener volumen plasmático. kcal (kilocalorías) cuantifica ingesta energética. min (minutos) mide tiempo efectivo de contramedida física diaria y duración de purga de nitrógeno con oxígeno puro previa a una EVA.",
        "Verificar hidratación y calorías suficientes antes de entrenamientos intensos o actividades extravehiculares.",
        "NASA — Exercising in Space",
        "https://www.nasa.gov/AtoZ/exercising-in-space/",
        "ML KCAL KG MIN PRE_RESP");

    add_glossary_item(items, "meas_ops_units", "MEASURES", "MEDIDAS 06 · TIEMPO OPERATIVO Y SEVERIDAD",
        "Jornada de Registro, Latencia de Señal (s), Puntaje (0–100 pts) y Niveles de Severidad", "OK",
        "DISTANCE", "BEHAVIORAL",
        "Jornada = día operativo secuencial · Latencia (s) = retardo de señal en segundos · Puntaje: 0–100 pts · Niveles: NORMAL / PRECAUCIÓN / CRÍTICO / URGENCIA.",
        "La jornada identifica el día cronológico del registro clínico. La latencia en segundos refleja el retardo de comunicaciones. Los niveles de severidad clasifican el estado médico: Normal (sin anomalías), Precaución (seguimiento), Crítico (No apto) y Urgencia (atención prioritaria).",
        "En nivel Urgencia (puntaje <= 50 o >= 2 alertas críticas), el sistema destaca al paciente mediante un atajo flotante directo.",
        "NASA HRP — Human Research Program Architecture",
        "https://www.nasa.gov/hrp/research/",
        "SOL DIA LATENCIA PUNTAJE URGENCIA");

    /* 7. SIGLAS Y ACRÓNIMOS */
    add_glossary_item(items, "acro_agencies_ops", "ACRONYMS", "SIGLAS 01 · AGENCIAS Y APTITUD EVA",
        "CSA / ASC · EVA · Apto / Restringido / No apto · LEO", "OK",
        "DISTANCE", "CARDIOVASCULAR",
        "Agencias espaciales, actividades extravehiculares y semáforo operativo de tripulación.",
        "CSA / ASC: Agencia Espacial Canadiense. EVA: Actividad Extravehicular (caminata espacial). Apto / Restringido / No apto: niveles de autorización médica para salida EVA. LEO: Órbita Terrestre Baja.",
        "Consultar siempre la aptitud EVA antes de autorizar salida por esclusa o pilotaje manual.",
        "NASA HRP — 5 Hazards of Human Spaceflight",
        "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/",
        "NASA HRP CSA ASC EVA LEO");

    add_glossary_item(items, "acro_env_rad", "ACRONYMS", "SIGLAS 02 · SOPORTE VITAL Y RADIACIÓN",
        "ECLSS · LiOH · HEPA · SPE · GCR", "OK",
        "HOSTILE_ENV", "RESPIRATORY",
        "Sistemas ambientales cerrados y fuentes de radiación ionizante espacial.",
        "ECLSS: Sistema de Control Ambiental y Soporte Vital. LiOH: Hidróxido de Litio (cartuchos depuradores de CO2). HEPA: Filtro de Aire de Alta Eficiencia para partículas y microbios. SPE: Evento de Partículas Solares (tormenta solar). GCR: Rayos Cósmicos Galácticos.",
        "Ante falla ECLSS o alerta SPE, priorizar purga de cabina o refugio blindado respectivamente.",
        "CSA — Space Radiation and Its Effects on the Human Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp",
        "ECLSS LIOH HEPA SPE GCR");

    add_glossary_item(items, "acro_syndromes", "ACRONYMS", "SIGLAS 03 · SÍNDROMES CLÍNICOS",
        "SANS · DCS · VTE / TEV", "OK",
        "GRAVITY", "OCULAR",
        "Cuadros médicos característicos de vuelos espaciales tripulados.",
        "SANS: Síndrome Neuro-Ocular Asociado a Vuelos Espaciales (cambios visuales y de nervio óptico por desplazamiento de fluidos). DCS: Enfermedad Descompresiva (burbujas de nitrógeno tras EVA o despresurización). VTE / TEV: Tromboembolismo Venoso.",
        "Evaluar síntomas visuales, articulares y venosos ante cualquier cambio de presión o gravedad.",
        "NASA — The Human Body in Space",
        "https://www.nasa.gov/humans-in-space/the-human-body-in-space/",
        "SANS DCS VTE TEV");

    add_glossary_item(items, "acro_hardware_ex", "ACRONYMS", "SIGLAS 04 · HARDWARE Y CONTRAMEDIDAS",
        "ARED · Zero-T2 · CEVIS · Thigh Cuff · Veggie", "OK",
        "GRAVITY", "MUSCULOSKELETAL",
        "Equipamiento de ejercicio, compresión vascular y soporte nutricional a bordo.",
        "ARED: Dispositivo Avanzado de Ejercicio Resistivo (fuerza ósea y muscular en 0g). Zero-T2: Cinta de correr con arnés de carga axial. CEVIS: Cicloergómetro con aislamiento de vibración. Thigh Cuff: Manguitos compresivos de muslos contra SANS. Veggie: Sistema de cultivo de vegetales frescos en órbita.",
        "Combinar carga mecánica diaria con nutrición fresca y compresión vascular cuando esté indicada.",
        "NASA — Exercising in Space",
        "https://www.nasa.gov/AtoZ/exercising-in-space/",
        "ARED ZERO-T2 CEVIS THIGH CUFF VEGGIE");

    add_glossary_item(items, "acro_diag_psych", "ACRONYMS", "SIGLAS 05 · DIAGNÓSTICO Y CONDUCTA",
        "Tempus Pro · Radi-N2 · CANS · ARCHeR · HFBP · PVT · Standard Measures", "OK",
        "ISOLATION", "BEHAVIORAL",
        "Instrumentación médica portátil, dosimetría y protocolos conductuales HRP.",
        "Tempus Pro: Monitor multiparamétrico y ecógrafo médico portátil para autonomía profunda. Radi-N2 / CANS: Dosímetros de neutrones y radiación cósmica de la CSA. ARCHeR / HFBP: Protocolos de salud conductual y desempeño en aislamiento. PVT: Prueba de Vigilancia Psicomotora. Standard Measures: Panel de muestreo biomédico estándar NASA.",
        "Emplear Tempus Pro y PVT durante chequeos autónomos sin enlace terrestre continuo.",
        "NASA HRP — Human Research Program Research",
        "https://www.nasa.gov/hrp/research/",
        "TEMPUS RADI-N2 CANS ARCHER HFBP PVT STANDARD MEASURES");

    add_glossary_item(items, "acro_hormonal_sup", "ACRONYMS", "SIGLAS 06 · FARMACOLOGÍA Y SALUD FEMENINA",
        "LARC · ACO / COC · Fe · Vitamina D3 · Complejo B", "OK",
        "GRAVITY", "IMMUNE",
        "Siglas de supresión hormonal y micronutrientes clave en vuelos espaciales.",
        "LARC: Anticoncepción Reversible de Larga Duración. ACO / COC: Anticonceptivos Orales Combinados. Fe: Símbolo químico del Hierro (suplementación frente a hemólisis espacial y sangrado menstrual). Vitamina D3 y Complejo B: Cofactores óseos, vasculares y metabólicos.",
        "Individualizar suplementación de hierro, vitamina D3 y complejo B según sexo, fase hormonal y carga física.",
        "CSA — Effects of Space on the Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp",
        "LARC ACO COC FE VITAMINA D COMPLEJO B");

    return root;
}

void server_http_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev != MG_EV_HTTP_MSG) return;

    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    AppState *state = (AppState *)c->fn_data;
    if (!hm || !state) return;

    if (method_is(hm, "OPTIONS")) {
        mg_http_reply(c, 204, JSON_HEADERS, "");
        return;
    }

    struct mg_str caps[4];

    /* 1. GET /api/defaults */
    if (mg_http_match_uri(hm, "/api/defaults")) {
        if (!method_is(hm, "GET")) {
            reply_error(c, 405, ERR_INVALID_RANGE, "Método no permitido");
            return;
        }
        reply_json_obj(c, 200, baseline_to_json(&state->defaults, 1));
        return;
    }

    /* 1b. GET /api/glossary */
    if (mg_http_match_uri(hm, "/api/glossary")) {
        if (!method_is(hm, "GET")) {
            reply_error(c, 405, ERR_INVALID_RANGE, "Método no permitido");
            return;
        }
        reply_json_obj(c, 200, build_glossary_json());
        return;
    }

    /* 1c. POST /api/avatars */
    if (mg_http_match_uri(hm, "/api/avatars")) {
        if (!method_is(hm, "POST")) {
            reply_error(c, 405, ERR_INVALID_RANGE, "Método no permitido");
            return;
        }
        cJSON *body = parse_req_body(hm);
        if (!body) {
            reply_error(c, 400, ERR_PARSE, "JSON invalido en el cuerpo de la peticion");
            return;
        }
        const cJSON *id_it = cJSON_GetObjectItemCaseSensitive(body, "id");
        const cJSON *du_it = cJSON_GetObjectItemCaseSensitive(body, "data_url");
        if (!cJSON_IsString(du_it) || !du_it->valuestring) {
            cJSON_Delete(body);
            reply_error(c, 400, ERR_PARSE, "Campo data_url requerido (data:image/...;base64,...)");
            return;
        }
        const char *id_hint = (cJSON_IsString(id_it) && id_it->valuestring) ? id_it->valuestring : "custom";
        char rel_path[256] = {0};
        StatusCode rc = storage_save_custom_avatar(id_hint, du_it->valuestring, rel_path, sizeof(rel_path));
        if (rc != OK) {
            cJSON_Delete(body);
            reply_error(c, 400, rc, "Formato de imagen no soportado o base64 invalido (use PNG, JPG, WEBP, GIF o SVG <= 2MB)");
            return;
        }

        /* Si el ID corresponde a un astronauta existente, actualizar su avatar automáticamente */
        if (is_valid_id(id_hint)) {
            Astronaut *existing_a = state_find_astronaut(state, id_hint);
            if (existing_a) {
                safe_strcpy(existing_a->avatar, sizeof(existing_a->avatar), rel_path);
                storage_save_astronaut(state, existing_a);
            }
        }
        cJSON_Delete(body);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "avatar", rel_path);
        cJSON_AddStringToObject(resp, "status", "saved");
        reply_json_obj(c, 201, resp);
        return;
    }

    /* 2 & 3. GET / POST /api/missions */
    if (mg_http_match_uri(hm, "/api/missions")) {
        if (method_is(hm, "GET")) {
            cJSON *arr = cJSON_CreateArray();
            for (int i = 0; i < state->mission_count; i++) {
                const Mission *m = &state->missions[i];
                MissionHealthSummary sum;
                evaluate_mission_summary(state, m, &sum);
                cJSON *mo = mission_to_json(m);
                cJSON_AddNumberToObject(mo, "autonomous_mode", (m->env.latencia_com_seg > 5.0f) ? 1 : 0);
                cJSON_AddNumberToObject(mo, "mission_health_avg", sum.mission_health_avg);
                cJSON_AddItemToArray(arr, mo);
            }
            reply_json_obj(c, 200, arr);
            return;
        }
        if (method_is(hm, "POST")) {
            cJSON *body = parse_req_body(hm);
            if (!body) {
                reply_error(c, 400, ERR_PARSE, "JSON invalido en el cuerpo de la peticion");
                return;
            }
            Mission new_m;
            StatusCode rc = mission_from_json(&new_m, body);
            cJSON_Delete(body);
            if (rc != OK) {
                reply_error(c, 400, rc, "ID de mision o parametros invalidos");
                return;
            }
            Mission *existing = state_find_mission(state, new_m.id);
            if (existing) {
                if (new_m.crew_count == 0 && existing->crew_count > 0) {
                    new_m.crew_count = existing->crew_count;
                    memcpy(new_m.crew_ids, existing->crew_ids, sizeof(existing->crew_ids));
                }
                *existing = new_m;
                evaluate_mission_crew(state, existing);
                storage_save_mission(state, existing);
                reply_json_obj(c, 201, build_mission_detail_json(state, existing));
                return;
            }
            if (state->mission_count >= MAX_MISSIONS) {
                reply_error(c, 422, ERR_LIMIT_REACHED, "Limite maximo de misiones alcanzado");
                return;
            }
            state->missions[state->mission_count] = new_m;
            Mission *saved = &state->missions[state->mission_count++];
            evaluate_mission_crew(state, saved);
            storage_save_mission(state, saved);
            reply_json_obj(c, 201, build_mission_detail_json(state, saved));
            return;
        }
        reply_error(c, 405, ERR_INVALID_RANGE, "Método no permitido");
        return;
    }

    /* 5. PUT /api/missions/:id/env */
    if (mg_match(hm->uri, mg_str("/api/missions/*/env"), caps)) {
        if (!method_is(hm, "PUT") && !method_is(hm, "POST")) {
            reply_error(c, 405, ERR_INVALID_RANGE, "Método no permitido");
            return;
        }
        char mid[32];
        copy_cap(&caps[0], mid, sizeof(mid));
        if (!is_valid_id(mid)) {
            reply_error(c, 400, ERR_INVALID_ID, "ID de mision invalido");
            return;
        }
        Mission *m = state_find_mission(state, mid);
        if (!m) {
            reply_error(c, 404, ERR_NOT_FOUND, "Mision no encontrada");
            return;
        }
        cJSON *body = parse_req_body(hm);
        if (!body) {
            reply_error(c, 400, ERR_PARSE, "JSON invalido");
            return;
        }
        CabinEnv candidate = m->env;
        cabin_from_json_partial(&candidate, body);
        const cJSON *dia_it = cJSON_GetObjectItemCaseSensitive(body, "dia_actual");
        int new_dia = m->dia_actual;
        if (dia_it) {
            if (!cJSON_IsNumber(dia_it) || floor(dia_it->valuedouble) != dia_it->valuedouble ||
                dia_it->valueint < 1 || dia_it->valueint > 10000) {
                cJSON_Delete(body);
                reply_error(c, 400, ERR_INVALID_RANGE, "dia_actual fuera de rango (1..10000)");
                return;
            }
            new_dia = dia_it->valueint;
        }
        cJSON_Delete(body);

        StatusCode vrc = validate_cabin(&candidate);
        if (vrc != OK) {
            reply_error(c, 400, vrc, "Parametros de entorno de cabina fuera de rango fisico");
            return;
        }
        m->env = candidate;
        m->dia_actual = new_dia;
        evaluate_mission_crew(state, m);
        storage_save_mission(state, m);
        reply_json_obj(c, 200, build_mission_detail_json(state, m));
        return;
    }

    /* 6. POST /api/missions/:id/simulate */
    if (mg_match(hm->uri, mg_str("/api/missions/*/simulate"), caps)) {
        if (!method_is(hm, "POST")) {
            reply_error(c, 405, ERR_INVALID_RANGE, "Método no permitido");
            return;
        }
        char mid[32];
        copy_cap(&caps[0], mid, sizeof(mid));
        if (!is_valid_id(mid)) {
            reply_error(c, 400, ERR_INVALID_ID, "ID de mision invalido");
            return;
        }
        Mission *m = state_find_mission(state, mid);
        if (!m) {
            reply_error(c, 404, ERR_NOT_FOUND, "Mision no encontrada");
            return;
        }
        cJSON *body = parse_req_body(hm);
        if (!body) {
            reply_error(c, 400, ERR_PARSE, "JSON invalido");
            return;
        }
        const cJSON *sc_it = cJSON_GetObjectItemCaseSensitive(body, "scenario");
        char scenario[64] = {0};
        if (cJSON_IsString(sc_it) && sc_it->valuestring) {
            safe_strcpy(scenario, sizeof(scenario), sc_it->valuestring);
        }
        cJSON_Delete(body);

        StatusCode src = simulate_mission_scenario(state, m, scenario);
        if (src != OK) {
            reply_error(c, 400, src, "Escenario de simulacion desconocido");
            return;
        }
        storage_save_mission(state, m);
        for (int i = 0; i < state->astronaut_count; i++) {
            Astronaut *a = &state->astronauts[i];
            if (strcmp(a->mission_id, m->id) == 0 || mission_has_crew(m, a->id)) {
                storage_save_astronaut(state, a);
            }
        }
        cJSON *resp = build_mission_detail_json(state, m);
        cJSON_AddStringToObject(resp, "scenario_applied", scenario);
        reply_json_obj(c, 200, resp);
        return;
    }

    /* 4. GET /api/missions/:id */
    if (mg_match(hm->uri, mg_str("/api/missions/*"), caps)) {
        if (!method_is(hm, "GET")) {
            reply_error(c, 405, ERR_INVALID_RANGE, "Método no permitido");
            return;
        }
        char mid[32];
        copy_cap(&caps[0], mid, sizeof(mid));
        if (!is_valid_id(mid)) {
            reply_error(c, 400, ERR_INVALID_ID, "ID de mision invalido");
            return;
        }
        Mission *m = state_find_mission(state, mid);
        if (!m) {
            reply_error(c, 404, ERR_NOT_FOUND, "Mision no encontrada");
            return;
        }
        reply_json_obj(c, 200, build_mission_detail_json(state, m));
        return;
    }

    /* 7 & 8. GET / POST /api/astronauts */
    if (mg_http_match_uri(hm, "/api/astronauts")) {
        if (method_is(hm, "GET")) {
            cJSON *arr = cJSON_CreateArray();
            for (int i = 0; i < state->astronaut_count; i++) {
                cJSON_AddItemToArray(arr, astronaut_summary_to_json(&state->astronauts[i]));
            }
            reply_json_obj(c, 200, arr);
            return;
        }
        if (method_is(hm, "POST")) {
            cJSON *body = parse_req_body(hm);
            if (!body) {
                reply_error(c, 400, ERR_PARSE, "JSON invalido en el cuerpo de la peticion");
                return;
            }
            Astronaut new_a;
            StatusCode rc = astronaut_from_json(&new_a, body, &state->defaults);
            cJSON_Delete(body);
            if (rc != OK) {
                reply_error(c, 400, rc, "ID de astronauta invalido (solo [a-zA-Z0-9_-], 1..15 caracteres) o parametros fuera de rango");
                return;
            }

            Mission *dest_m = state_find_mission(state, new_a.mission_id);
            if (!dest_m && state->mission_count > 0) {
                dest_m = &state->missions[0];
                safe_strcpy(new_a.mission_id, sizeof(new_a.mission_id), dest_m->id);
            }
            if (dest_m && !mission_has_crew(dest_m, new_a.id) && dest_m->crew_count >= MAX_CREW_PER_MISSION) {
                reply_error(c, 422, ERR_LIMIT_REACHED, "Limite maximo de tripulantes por mision alcanzado");
                return;
            }

            Astronaut *target = state_find_astronaut(state, new_a.id);
            if (target) {
                /* Si ya existia (ej. re-importando un archivo .json desde CARGAR ARCHIVO),
                 * si cambia de mision lo removemos de la mision anterior para evitar duplicados */
                if (strcmp(target->mission_id, new_a.mission_id) != 0) {
                    Mission *old_m = state_find_mission(state, target->mission_id);
                    if (old_m) {
                        mission_remove_crew(old_m, target->id);
                        storage_save_mission(state, old_m);
                    }
                }
                *target = new_a;
            } else {
                if (state->astronaut_count >= MAX_ASTRONAUTS) {
                    reply_error(c, 422, ERR_LIMIT_REACHED, "Limite maximo de astronautas alcanzado");
                    return;
                }
                state->astronauts[state->astronaut_count] = new_a;
                target = &state->astronauts[state->astronaut_count++];
            }

            if (dest_m) {
                mission_add_crew(dest_m, target->id);
                storage_save_mission(state, dest_m);
            }
            evaluate_astronaut(target, dest_m ? &dest_m->env : NULL);
            storage_save_astronaut(state, target);
            reply_json_obj(c, 201, astronaut_to_json(target, 1));
            return;
        }
        reply_error(c, 405, ERR_INVALID_RANGE, "Método no permitido");
        return;
    }

    /* 12. POST /api/astronauts/:id/samples */
    if (mg_match(hm->uri, mg_str("/api/astronauts/*/samples"), caps)) {
        if (!method_is(hm, "POST")) {
            reply_error(c, 405, ERR_INVALID_RANGE, "Método no permitido");
            return;
        }
        char aid[32];
        copy_cap(&caps[0], aid, sizeof(aid));
        if (!is_valid_id(aid)) {
            reply_error(c, 400, ERR_INVALID_ID, "ID de astronauta invalido");
            return;
        }
        Astronaut *a = state_find_astronaut(state, aid);
        if (!a) {
            reply_error(c, 404, ERR_NOT_FOUND, "Astronauta no encontrado");
            return;
        }
        cJSON *body = parse_req_body(hm);
        if (!body) {
            reply_error(c, 400, ERR_PARSE, "JSON invalido");
            return;
        }

        Mission *m = state_find_mission(state, a->mission_id);
        const DailyHealthSample *latest = astronaut_get_latest_sample(a);
        int default_day = m ? m->dia_actual : (latest ? latest->dia_mision : 1);
        const cJSON *dia_it = cJSON_GetObjectItemCaseSensitive(body, "dia_mision");
        int target_day = (cJSON_IsNumber(dia_it) && dia_it->valueint > 0) ? dia_it->valueint : default_day;

        DailyHealthSample sample;
        sample_prepare_partial_base(&sample, a, target_day);
        sample_from_json_partial(&sample, body);
        cJSON_Delete(body);

        StatusCode rc = astronaut_add_sample(a, &sample);
        if (rc != OK) {
            reply_error(c, 400, rc, "Valores fisiologicos de la muestra fuera de rango");
            return;
        }

        if (m && sample.dia_mision > m->dia_actual) {
            m->dia_actual = sample.dia_mision;
            storage_save_mission(state, m);
        }

        evaluate_astronaut(a, m ? &m->env : NULL);
        storage_save_astronaut(state, a);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddItemToObject(resp, "sample", sample_to_json(&sample));
        cJSON_AddItemToObject(resp, "report", report_to_json(&a->report));
        cJSON_AddItemToObject(resp, "astronaut", astronaut_to_json(a, 1));
        reply_json_obj(c, 201, resp);
        return;
    }

    /* 13. GET /api/astronauts/:id/export */
    if (mg_match(hm->uri, mg_str("/api/astronauts/*/export"), caps)) {
        if (!method_is(hm, "GET")) {
            reply_error(c, 405, ERR_INVALID_RANGE, "Método no permitido");
            return;
        }
        char aid[32];
        copy_cap(&caps[0], aid, sizeof(aid));
        if (!is_valid_id(aid)) {
            reply_error(c, 400, ERR_INVALID_ID, "ID de astronauta invalido");
            return;
        }
        Astronaut *a = state_find_astronaut(state, aid);
        if (!a) {
            reply_error(c, 404, ERR_NOT_FOUND, "Astronauta no encontrado");
            return;
        }

        cJSON *export_obj = astronaut_to_json(a, 1);
        char *pretty = cJSON_Print(export_obj);
        cJSON_Delete(export_obj);
        if (!pretty) {
            reply_error(c, 500, ERR_IO, "Error al serializar bitacora");
            return;
        }

        char headers[512];
        snprintf(headers, sizeof(headers),
                 "Content-Type: application/json; charset=utf-8\r\n"
                 "Content-Disposition: attachment; filename=\"%s_bitacora_hrp.json\"\r\n"
                 "Cache-Control: no-store\r\n",
                 a->id);
        mg_http_reply(c, 200, headers, "%s\n", pretty);
        free(pretty);
        return;
    }

    /* 9, 10 & 11. GET / PUT / DELETE /api/astronauts/:id */
    if (mg_match(hm->uri, mg_str("/api/astronauts/*"), caps)) {
        char aid[32];
        copy_cap(&caps[0], aid, sizeof(aid));
        if (!is_valid_id(aid)) {
            reply_error(c, 400, ERR_INVALID_ID, "ID de astronauta invalido");
            return;
        }
        Astronaut *a = state_find_astronaut(state, aid);
        if (!a) {
            reply_error(c, 404, ERR_NOT_FOUND, "Astronauta no encontrado");
            return;
        }

        if (method_is(hm, "GET")) {
            reply_json_obj(c, 200, astronaut_to_json(a, 1));
            return;
        }

        if (method_is(hm, "PUT")) {
            cJSON *body = parse_req_body(hm);
            if (!body) {
                reply_error(c, 400, ERR_PARSE, "JSON invalido");
                return;
            }

            const cJSON *rprev = cJSON_GetObjectItemCaseSensitive(body, "radiacion_previa_msv");
            if (rprev) {
                if (!cJSON_IsNumber(rprev) || isnan(rprev->valuedouble) || isinf(rprev->valuedouble) ||
                    rprev->valuedouble < 0.0 || rprev->valuedouble > 10000.0) {
                    cJSON_Delete(body);
                    reply_error(c, 400, ERR_INVALID_RANGE, "radiacion_previa_msv fuera de rango");
                    return;
                }
            }

            const cJSON *rmis = cJSON_GetObjectItemCaseSensitive(body, "radiacion_mision_msv");
            if (rmis) {
                if (!cJSON_IsNumber(rmis) || isnan(rmis->valuedouble) || isinf(rmis->valuedouble) ||
                    rmis->valuedouble < 0.0 || rmis->valuedouble > 10000.0) {
                    cJSON_Delete(body);
                    reply_error(c, 400, ERR_INVALID_RANGE, "radiacion_mision_msv fuera de rango");
                    return;
                }
            }

            const cJSON *mid_it = cJSON_GetObjectItemCaseSensitive(body, "mission_id");
            if (mid_it) {
                if (!cJSON_IsString(mid_it) || !is_valid_id(mid_it->valuestring)) {
                    cJSON_Delete(body);
                    reply_error(c, 400, ERR_INVALID_ID, "mission_id invalido");
                    return;
                }
                Mission *chk_m = state_find_mission(state, mid_it->valuestring);
                if (chk_m && !mission_has_crew(chk_m, a->id) && chk_m->crew_count >= MAX_CREW_PER_MISSION) {
                    cJSON_Delete(body);
                    reply_error(c, 422, ERR_LIMIT_REACHED, "Limite maximo de tripulantes por mision alcanzado");
                    return;
                }
            }

            const cJSON *nom = cJSON_GetObjectItemCaseSensitive(body, "nombre");
            if (nom && (!cJSON_IsString(nom) || !nom->valuestring || nom->valuestring[0] == '\0')) {
                cJSON_Delete(body);
                reply_error(c, 400, ERR_PARSE, "nombre invalido");
                return;
            }

            const cJSON *rol = cJSON_GetObjectItemCaseSensitive(body, "rol");
            if (rol && (!cJSON_IsString(rol) || !rol->valuestring || rol->valuestring[0] == '\0')) {
                cJSON_Delete(body);
                reply_error(c, 400, ERR_PARSE, "rol invalido");
                return;
            }

            const cJSON *ava = cJSON_GetObjectItemCaseSensitive(body, "avatar");
            char resolved_avatar[256] = {0};
            if (ava) {
                if (!cJSON_IsString(ava) || !ava->valuestring || ava->valuestring[0] == '\0') {
                    cJSON_Delete(body);
                    reply_error(c, 400, ERR_PARSE, "avatar invalido");
                    return;
                }
                if (strncmp(ava->valuestring, "data:image/", 11) == 0) {
                    StatusCode arc = storage_save_custom_avatar(a->id, ava->valuestring, resolved_avatar, sizeof(resolved_avatar));
                    if (arc != OK) {
                        cJSON_Delete(body);
                        reply_error(c, 400, arc, "Formato de imagen de avatar invalido");
                        return;
                    }
                } else {
                    safe_strcpy(resolved_avatar, sizeof(resolved_avatar), ava->valuestring);
                }
            }

            const cJSON *sex = cJSON_GetObjectItemCaseSensitive(body, "sexo");
            if (sex) {
                if (!cJSON_IsString(sex) || !is_valid_sex(sex->valuestring)) {
                    cJSON_Delete(body);
                    reply_error(c, 400, ERR_INVALID_RANGE, "sexo invalido (use F o M)");
                    return;
                }
            }

            AstronautBaseline candidate_base = a->baseline;
            const cJSON *b_obj = cJSON_GetObjectItemCaseSensitive(body, "baseline");
            if (b_obj) {
                if (!cJSON_IsObject(b_obj)) {
                    cJSON_Delete(body);
                    reply_error(c, 400, ERR_PARSE, "baseline debe ser un objeto JSON");
                    return;
                }
                baseline_from_json(&candidate_base, b_obj);
                if (validate_baseline(&candidate_base) != OK) {
                    cJSON_Delete(body);
                    reply_error(c, 400, ERR_INVALID_RANGE, "Parametros de baseline fuera de rango fisiologico");
                    return;
                }
            }

            if (nom) safe_strcpy(a->nombre, sizeof(a->nombre), nom->valuestring);
            if (rol) safe_strcpy(a->rol, sizeof(a->rol), rol->valuestring);
            if (ava) safe_strcpy(a->avatar, sizeof(a->avatar), resolved_avatar);
            if (sex) normalize_sex_str(a->sexo, sizeof(a->sexo), sex->valuestring);

            if (rprev) {
                a->radiacion_previa_msv = (float)rprev->valuedouble;
            }
            if (rmis) {
                a->radiacion_mision_msv = (float)rmis->valuedouble;
            }

            if (mid_it && strcmp(a->mission_id, mid_it->valuestring) != 0) {
                Mission *old_m = state_find_mission(state, a->mission_id);
                if (old_m) {
                    mission_remove_crew(old_m, a->id);
                    storage_save_mission(state, old_m);
                }
                safe_strcpy(a->mission_id, sizeof(a->mission_id), mid_it->valuestring);
                Mission *new_m = state_find_mission(state, a->mission_id);
                if (new_m) {
                    mission_add_crew(new_m, a->id);
                    storage_save_mission(state, new_m);
                }
            }

            a->baseline = candidate_base;
            cJSON_Delete(body);

            Mission *m = state_find_mission(state, a->mission_id);
            evaluate_astronaut(a, m ? &m->env : NULL);
            storage_save_astronaut(state, a);
            reply_json_obj(c, 200, astronaut_to_json(a, 1));
            return;
        }

        if (method_is(hm, "DELETE")) {
            char deleted_id[16];
            safe_strcpy(deleted_id, sizeof(deleted_id), a->id);

            for (int m_idx = 0; m_idx < state->mission_count; m_idx++) {
                if (mission_remove_crew(&state->missions[m_idx], deleted_id) == OK) {
                    storage_save_mission(state, &state->missions[m_idx]);
                }
            }
            storage_delete_astronaut_file(state, deleted_id);

            int idx = (int)(a - state->astronauts);
            for (int i = idx; i < state->astronaut_count - 1; i++) {
                state->astronauts[i] = state->astronauts[i + 1];
            }
            state->astronaut_count--;

            cJSON *resp = cJSON_CreateObject();
            cJSON_AddStringToObject(resp, "status", "deleted");
            cJSON_AddStringToObject(resp, "id", deleted_id);
            reply_json_obj(c, 200, resp);
            return;
        }

        reply_error(c, 405, ERR_INVALID_RANGE, "Método no permitido");
        return;
    }

    /* Si es otra ruta bajo /api/, devolver 404 JSON */
    if (mg_http_match_uri(hm, "/api/#")) {
        reply_error(c, 404, ERR_NOT_FOUND, "Endpoint API no encontrado");
        return;
    }

    /* Rutas estaticas: servir desde web/ */
    struct mg_http_serve_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.root_dir = "web";
    mg_http_serve_dir(c, hm, &opts);
}

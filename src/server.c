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
        "Radiación Espacial (Rayos Cósmicos y Tormentas Solares)", "CRITICAL",
        "RADIATION", "RADIATION",
        "En órbita baja (ISS): ~0.4–1.0 mSv por día · Lejos de la Tierra: ~1.3 mSv por día · Aviso amarillo desde 1.0 mSv/día o 480 mSv acumulados · Alerta roja si supera 2.0 mSv/día, hay tormenta solar o llega al tope de 600 mSv en toda su carrera.",
        "Aquí en la Tierra el campo magnético nos protege como un escudo, pero en el espacio estamos mucho más expuestos a partículas que vienen del Sol (tormentas solares SPE) y de fuera del sistema solar (rayos cósmicos GCR). Con el tiempo esta radiación puede dañar células y defensas, irritar los ojos o aumentar riesgos médicos a largo plazo.",
        "1) Si salta alerta de tormenta solar, entra enseguida al sector más protegido de la nave y cancela cualquier caminata afuera. 2) Usa bolsas de agua o víveres alrededor del refugio porque frenan muy bien la radiación. 3) Mantén encendidos los detectores Radi-N2 y CANS para seguir la dosis en vivo.",
        "CSA — Space Radiation and Its Effects on the Human Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp",
        "RAD_SPE_SPIKE RAD_CAREER_LIMIT");

    add_glossary_item(items, "haz_gravity", "HAZARDS", "PELIGRO 2 · GRAVEDAD",
        "Cambios de Gravedad (Ingravidez 0g, Luna y Marte)", "CAUTION",
        "GRAVITY", "CARDIOVASCULAR",
        "Vista (SANS): baja >= 0.15 (aviso) o >= 0.25 (alerta roja) · Ejercicio semanal: aviso si promedias menos de 60 min/día o alerta roja bajo 45 min/día · Mareo espacial: alerta si marca 5/10 o más.",
        "Cuando no hay gravedad (0g), la sangre y los líquidos del cuerpo dejan de bajar a las piernas y se acumulan hacia la cara y la cabeza. Eso puede apretar un poco los ojos y cambiar la vista (SANS), hacer que los huesos pierdan calcio (1–1.5 % al mes si no se hace fuerza) y provocar mareos parecidos al mareo de viaje al llegar al espacio o volver a pisar suelo.",
        "1) Haz por lo menos 90 minutos de ejercicio real al día (pesas en ARED, bicicleta CEVIS o rutina Zero-T2) para que huesos y corazón sigan fuertes. 2) Toma Vitamina D3 todos los días. 3) Si notas presión en la cabeza o vista rara, usa las bandas en los muslos y Complejo B.",
        "NASA — The Human Body in Space",
        "https://www.nasa.gov/humans-in-space/the-human-body-in-space/",
        "GRAV_BONE_CAUT GRAV_BONE_CRIT SYM_VIT_D_TAKEN");

    add_glossary_item(items, "haz_hostile_env", "HAZARDS", "PELIGRO 3 · AIRE Y CABINA",
        "Vivir en una Nave Cerrada (Oxígeno, CO2 y Soporte Vital)", "CRITICAL",
        "HOSTILE_ENV", "RESPIRATORY",
        "CO2 normal: <= 0.40 kPa · Aviso si pasa de 0.50 kPa con dolor de cabeza o reflejos lentos · Alerta roja si supera 0.80 kPa | Oxígeno normal (SpO2): 95–100 %, aviso bajo 94 % y alerta roja bajo 90 %.",
        "En una nave espacial no se puede abrir la ventana, así que el sistema de aire (ECLSS) tiene que limpiar el aire todo el tiempo. Como en gravedad cero el aire caliente no sube solo, el CO2 que exhalamos puede quedar flotando en bolsas invisibles cerca de la cara, causando dolor de cabeza y pesadez mental. Para limpiarlo se usan los equipos CDRA o CAMRAS, y los cartuchos LiOH quedan guardados por si hay emergencia.",
        "1) Si sube el CO2, revisa primero los limpiadores de aire CDRA/CAMRAS y que los ventiladores estén moviendo el aire; usa cartuchos LiOH si el sistema principal falla. 2) Si falta oxígeno o baja la saturación, ponte mascarilla de oxígeno al 100 % y descansa.",
        "NASA HRP — 5 Hazards of Human Spaceflight",
        "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/",
        "ENV_HYPERCAPNIA ENV_HYPOXIA CDRA CAMRAS LIOH");

    add_glossary_item(items, "haz_isolation", "HAZARDS", "PELIGRO 4 · AISLAMIENTO",
        "Encierro, Poco Sueño y Ánimo en Misiones Largas", "CAUTION",
        "ISOLATION", "BEHAVIORAL",
        "Sueño promedio de 3 días menor a 6 horas (+ reflejos lentos, estrés >= 7 o ánimo <= 4) activa aviso amarillo · Menos de 5 horas de sueño con estrés >= 8 pasa a alerta roja.",
        "Pasar meses encerrado con el mismo equipo, lejos de casa y viendo amanecer cada 90 minutos (o sin día y noche naturales) desordena el reloj interno del cuerpo. Si duermes mal varios días seguidos baja tu concentración, te irritas más fácil, puedes perder el apetito por la comida espacial de siempre y aparecen roces en la convivencia del grupo.",
        "1) Cuida un horario fijo de 7.5 a 8.5 horas de sueño usando luces suaves antes de dormir. 2) Si estás agotado, deja las tareas secundarias para otro día. 3) Agrega vegetales frescos del cultivo Veggie y busca momentos tranquilos para conversar con el equipo.",
        "CSA — Effects of Space on the Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp",
        "ISO_SLEEP_COGNITIVE ARCHER");

    add_glossary_item(items, "haz_distance", "HAZARDS", "PELIGRO 5 · DISTANCIA",
        "Estar Lejos de la Tierra y Atender Urgencias Sin Ayuda en Vivo", "CAUTION",
        "DISTANCE", "BEHAVIORAL",
        "Órbita cercana: menos de 1 segundo de retraso · Luna: ~1.3 segundos · Viaje a Marte: de 3 a 22 minutos solo de ida · Si la señal tarda más de 5 segundos, la nave activa el Modo Autónomo.",
        "Cuando viajas a la Luna o a Marte ya no puedes llamar al médico de la Tierra y esperar respuesta al instante, ni volver rápido en una cápsula de rescate. Por eso la tripulación y el sistema de a bordo tienen que detectar problemas solos, guiarse paso a paso con esta consola y usar equipos portátiles como Tempus Pro.",
        "1) Cuando la señal tarde más de 5 segundos, apóyate en las alertas y pasos guiados locales de P.U.L.S.O. sin esperar confirmación terrestre. 2) Usa el monitor portátil Tempus Pro para chequeos y guarda las bitácoras para enviarlas después.",
        "NASA HRP — Human Research Program Research",
        "https://www.nasa.gov/hrp/research/",
        "AUTONOMOUS_MODE");

    /* 2. PROTOCOLOS Y CONTRAMEDIDAS */
    add_glossary_item(items, "prot_ared_zerot2", "PROTOCOLS", "CONTRAMEDIDA 01 · EJERCICIO",
        "Rutina Diaria de Ejercicio (ARED, Bici CEVIS, Zero-T2) + Vitamina D3", "OK",
        "GRAVITY", "MUSCULOSKELETAL",
        "Objetivo ideal: 90 min diarios de ejercicio real (dentro del bloque de 2 a 2.5 horas en estación) · Aviso si promedias menos de 60 min/día en la semana · Alerta roja bajo 45 min/día (puedes marcar 1 o 2 días de descanso por semana sin que te baje el promedio).",
        "Sin gravedad, los músculos y huesos creen que ya no hacen falta y empiezan a perder fuerza rápido. Para evitarlo se combinan pesas sin peso real (máquina ARED con cilindros de vacío), bicicleta fija (CEVIS) y el protocolo Zero-T2 ('sin cinta de correr'), pensado para cápsulas chicas de espacio profundo donde no entra una cinta como la T2/COLBERT de la ISS. La Vitamina D3 ayuda a que el calcio siga entrando al hueso ya que allí no tomas sol.",
        "1) Completa tus 90 minutos activos diarios combinando fuerza (ARED/Zero-T2) y cardio (CEVIS). 2) Toma 1 dosis de Vitamina D3 con la comida principal. 3) Usa tu día de descanso programado (1 o 2 por semana) cuando sientas fatiga muscular.",
        "NASA — Exercising in Space",
        "https://www.nasa.gov/AtoZ/exercising-in-space/",
        "GRAV_BONE_CAUT GRAV_BONE_CRIT SYM_VIT_D_TAKEN ZERO-T2");

    add_glossary_item(items, "prot_sans_cuff", "PROTOCOLS", "CONTRAMEDIDA 02 · VISTA Y CABEZA",
        "Cuidar la Vista en Gravedad Cero (SANS): Bandas en Muslos + Complejo B", "CAUTION",
        "GRAVITY", "OCULAR",
        "Aviso amarillo si la vista baja >= 0.15 puntos junto con dolor de cabeza (>= 4/10) o presión alta · Alerta roja si la vista baja >= 0.25 puntos respecto a tu medida normal.",
        "En el espacio, al subir más líquido hacia la cabeza, aumenta la presión detrás de los ojos y puede aplanar un poco el globo ocular o hinchar el nervio óptico (esto se llama SANS y suele volver más difícil enfocar de cerca). Las bandas elásticas en los muslos retienen parte del líquido abajo en las piernas, y el Complejo B cuida los vasos sanguíneos y nervios.",
        "1) Ponte las bandas en la parte alta de ambos muslos durante 2 a 4 horas de día (quítatelas al dormir o si sientes hormigueo). 2) Toma Complejo B con comida. 3) Hazte un chequeo ocular con el ecógrafo Tempus Pro.",
        "NASA — The Human Body in Space (SANS)",
        "https://www.nasa.gov/humans-in-space/the-human-body-in-space/",
        "SANS_CAUTION SANS_CRITICAL SYM_THIGH_CUFF_ACTIVE SYM_B_COMPLEX_TAKEN");

    add_glossary_item(items, "prot_storm_shelter", "PROTOCOLS", "CONTRAMEDIDA 03 · REFUGIO SOLAR",
        "Cómo Protegerse en el Refugio Durante una Tormenta Solar", "CRITICAL",
        "RADIATION", "RADIATION",
        "Se activa apenas suena alarma de tormenta solar (SPE), cuando la radiación por hora sube de 0.5 mSv/h o la dosis del día pasa de 2.0 mSv.",
        "Cuando el Sol lanza una llamarada fuerte (SPE), llegan partículas cargadas con mucha energía en poco tiempo. Como el metal por sí solo no frena todo, los materiales con mucho hidrógeno —como las bolsas de agua potable, la comida y los depósitos de plástico grueso— forman el mejor escudo improvisado dentro de la parte central de la nave.",
        "1) Cancela al instante cualquier caminata espacial afuera. 2) Junta a toda la tripulación en el centro mejor blindado rodeado de bolsas de agua. 3) Sigue las lecturas de Radi-N2 y CANS hasta recibir confirmación de que bajó el pico.",
        "CSA — Space Radiation and Its Effects on the Human Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp",
        "RAD_SPE_SPIKE");

    add_glossary_item(items, "prot_eclss_dcs", "PROTOCOLS", "CONTRAMEDIDA 04 · AIRE Y CAMINATAS",
        "Limpieza de Aire y Respirar Oxígeno Antes de una Caminata Espacial", "CRITICAL",
        "HOSTILE_ENV", "RESPIRATORY",
        "Aire de cabina: aviso si CO2 pasa de 0.50 kPa y alerta roja sobre 0.80 kPa, o si falta oxígeno (< 18 kPa) · Antes de salir en traje: mínimo 120 min respirando oxígeno puro con ejercicio suave (ISLE), o 240 min en reposo.",
        "El traje espacial funciona a una presión mucho más baja que la cabina (~29.6 kPa vs 101.3 kPa), parecido a subir de golpe a una montaña altísima. Si el astronauta sale sin limpiar antes el nitrógeno disuelto en su sangre respirando oxígeno puro, ese gas forma burbujas en las articulaciones o nervios (enfermedad por descompresión o DCS, igual que les pasa a los buzos).",
        "1) Si falla el aire en cabina, revisa CDRA/CAMRAS y usa cartuchos LiOH de reserva. 2) Si alguien sale con dolor articular o poca pre-respiración, dale oxígeno al 100 % inmediato, inmoviliza la zona y prepara recompresión.",
        "NASA HRP — 5 Hazards of Human Spaceflight",
        "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/",
        "EVA_DCS_RISK SYM_EVA_JOINT_PAIN");

    /* 3. SÍNTOMAS Y CONDICIONES CLÍNICAS */
    add_glossary_item(items, "sym_immune_flags", "SYMPTOMS", "CLÍNICA 01 · DEFENSAS",
        "Alergias en la Piel, Virus Dormidos, Fiebre y Azúcar en Sangre", "CAUTION",
        "HOSTILE_ENV", "IMMUNE",
        "Aviso amarillo si aparece sarpullido, virus latente o glucosa fuera de 70–140 mg/dL · Alerta roja si hay fiebre >= 39.0 °C, azúcar muy alta/baja o fiebre >= 37.8 °C combinada con infección.",
        "El estrés del viaje y vivir en aire reciclado bajan un poco las defensas naturales. Por eso es común que salgan irritaciones en la piel por contacto, se reactiven virus que el cuerpo tenía dormidos (como herpes simple) o cambie el azúcar en sangre. Además, dentro de una nave cerrada cualquier infección se contagia rápido si no se aísla a tiempo.",
        "1) Toma muestras simples de saliva para análisis y evita acercarte mucho al resto si tienes fiebre >= 37.8 °C. 2) Regula el azúcar comiendo o hidratándote bien. 3) Revisa limpieza y filtros HEPA.",
        "NASA HRP — Human Research Program Research",
        "https://www.nasa.gov/hrp/research/",
        "SYM_RASH_ALLERGY SYM_VIRAL_REACTIVATION SYM_INFECTION_SIGNS IMMUNE_CRITICAL");

    add_glossary_item(items, "sym_eva_ortho", "SYMPTOMS", "CLÍNICA 02 · ARTICULACIONES Y MAREO",
        "Dolor Articular Después de Caminata EVA y Mareo al Incorporarse", "CRITICAL",
        "GRAVITY", "CARDIOVASCULAR",
        "Dolor articular después de salir al espacio = alerta roja inmediata · Mareo al pararse o cambiar de postura = aviso cardiovascular (o agravante en sangrado abundante).",
        "Si te duele un hombro, codo o rodilla justo después de una caminata espacial, puede haber burbujas de nitrógeno atrapadas (DCS) y hay que actuar rápido. En cambio, el mareo ortostático pasa porque en 0g el cuerpo achica su volumen de sangre (~15 %); al volver a sentir gravedad o perder líquido, cuesta unos segundos que llegue suficiente sangre a la cabeza.",
        "1) Dolor post-EVA: oxígeno puro al 100 % y recompresión sin esperar. 2) Mareo al levantarse: recuéstate boca arriba, bebe 350–500 ml de agua con sales y controla pulso y presión con Tempus Pro.",
        "CSA — Living in Space: Effects of Space on the Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp",
        "SYM_EVA_JOINT_PAIN SYM_ORTHOSTATIC_DIZZINESS GRAV_CARDIO_VENOUS");

    add_glossary_item(items, "sym_countermeasures_bits", "SYMPTOMS", "CLÍNICA 03 · HÁBITOS DIARIOS",
        "Las 4 Ayudas Diarias: Bandas en Muslos, Complejo B, Vitamina D3 y Cultivo Veggie", "OK",
        "GRAVITY", "MUSCULOSKELETAL",
        "Controles simples de todos los días que protegen ojos, huesos, energía y defensas sin necesidad de medicamentos complejos.",
        "Son cuatro hábitos preventivos fáciles de seguir a bordo: las bandas en los muslos evitan presión excesiva en la cabeza; el Complejo B refuerza nervios, sangre y vista; la Vitamina D3 reemplaza la falta de luz solar para cuidar los huesos; y sumar hojas frescas del huerto Veggie aporta vitaminas vivas y mejora el ánimo frente a la comida envasada.",
        "Mantén todos los días la Vitamina D3 y una porción fresca de Veggie cuando haya cosecha; agrega las Bandas en Muslos y el Complejo B si aparece riesgo visual (SANS) o cansancio.",
        "NASA — Ejercitación y Nutrición Espacial",
        "https://www.nasa.gov/AtoZ/exercising-in-space/",
        "SYM_THIGH_CUFF_ACTIVE SYM_B_COMPLEX_TAKEN SYM_VIT_D_TAKEN SYM_VEGGIE_FRESH_FOOD");

    /* 4. FISIOLOGÍA FEMENINA Y CICLO HORMONAL */
    add_glossary_item(items, "fem_menstrual_microg", "HORMONAL", "SALUD FEMENINA 01 · CICLO",
        "Cómo Funciona el Ciclo Menstrual en el Espacio", "OK",
        "GRAVITY", "CARDIOVASCULAR",
        "Etapas: No Aplica · Menstrual (días 1–5) · Folicular (días 6–13) · Ovulatoria (días 14–16) · Lútea (días 17–28) · Suprimido (si usa anticonceptivo continuo).",
        "Aunque mucha gente duda si la menstruación cambia sin gravedad, el útero empuja el flujo normalmente gracias a sus propias contracciones musculares, igual que en la Tierra. Registrar en qué día y fase estás ayuda al sistema a no confundir cambios normales de temperatura, cansancio o cólicos con una enfermedad o infección.",
        "Anota cada día en qué fase está la tripulante, si hubo sangrado (ninguno, leve, moderado o abundante) y si tuvo dolor cólico (0 a 10), asegurando buena hidratación e insumos.",
        "NASA — The Human Body in Space",
        "https://www.nasa.gov/humans-in-space/the-human-body-in-space/",
        "HORMONAL_FEMALE CYCLE_MENSTRUAL CYCLE_FOLLICULAR");

    add_glossary_item(items, "fem_iron_anemia", "HORMONAL", "SALUD FEMENINA 02 · HIERRO Y ANEMIA",
        "Cuidado del Hierro, Menstruación Abundante y 'Anemia Espacial'", "CRITICAL",
        "GRAVITY", "CARDIOVASCULAR",
        "Aviso si hay menstruación activa o cólico >= 4 sin tomar hierro o con poca agua · Alerta roja (No apto para EVA) si hay flujo abundante con pulso acelerado, presión baja (< 95 mmHg), mareo o cólico >= 8/10.",
        "En el espacio el cuerpo destruye glóbulos rojos un poco más rápido de lo normal ('anemia espacial') y maneja menos volumen de sangre. Si a eso se suma un sangrado menstrual abundante sin reponer hierro, puede bajar rápido la energía, acelerarse el corazón y aparecer mareos peligrosos dentro de un traje espacial.",
        "1) Tomar suplemento de Hierro + Complejo B junto con comida (sin café ni té por 1 hora) y sumar 350–500 ml de agua con sales. 2) Si el sangrado es muy abundante o el dolor llega a 8/10 o más, suspender caminatas EVA y descansar.",
        "CSA — Effects of Space on the Body (Cardiovascular & Blood Volume)",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp",
        "HORMONAL_MENSTRUAL_CARE HORMONAL_MENORRHAGIA_CRIT");

    add_glossary_item(items, "fem_luteal_thermo", "HORMONAL", "SALUD FEMENINA 03 · FASE LÚTEA",
        "Subida Normal de Temperatura en Fase Lútea (Días 17–28)", "CAUTION",
        "ISOLATION", "BEHAVIORAL",
        "Aviso informativo en días 17–28 si la temperatura sube un poco (+0.35 °C sobre lo normal, pero por debajo de 37.8 °C) o si baja la calidad del sueño y el ánimo.",
        "Durante la segunda mitad del ciclo (fase lútea), la progesterona sube naturalmente la temperatura del cuerpo entre +0.3 y +0.5 °C y acelera apenas la respiración al dormir. Si no se tiene en cuenta este dato hormonal, cualquiera podría asustarse pensando que tiene principio de fiebre cuando en realidad es algo totalmente fisiológico.",
        "1) Confirmar que esté por debajo de 37.8 °C y sin signos de infección antes de pensar en fiebre. 2) Enfriar un poco la cabina para dormir mejor y cuidar la iluminación nocturna. 3) Revisar CO2 si aparece dolor de cabeza.",
        "NASA HRP — Human Research Program Research",
        "https://www.nasa.gov/hrp/research/",
        "HORMONAL_LUTEAL_EFFECT");

    add_glossary_item(items, "fem_suppression_vte", "HORMONAL", "SALUD FEMENINA 04 · SUPRESIÓN Y CIRCULACIÓN",
        "Supresión del Ciclo (LARC / Pastillas), Circulación Venosa y Huesos", "CAUTION",
        "GRAVITY", "CARDIOVASCULAR",
        "Aviso cuando se suprime el ciclo con anticonceptivos y además hay poca hidratación (< 85 %), menos de 65 min/día de ejercicio semanal o falta de Vitamina D3.",
        "Muchas astronautas eligen pausar su menstruación durante misiones largas usando DIU hormonal / implantes (LARC) o pastillas combinadas (ACO/COC). Como en gravedad cero la sangre venosa del cuello circula más lento, los métodos con estrógeno piden cuidar todavía más la hidratación para evitar coágulos venosos (VTE) y mantener fuerte la masa ósea.",
        "1) Cubrir el 100 % del agua diaria recomendada para mantener fluida la circulación. 2) Cumplir el ejercicio de fuerza ARED/Zero-T2 y Vitamina D3. 3) Chequear las venas del cuello con el ecógrafo Tempus Pro.",
        "NASA HRP — Cardiovascular & Bone Countermeasures",
        "https://www.nasa.gov/hrp/research/",
        "HORMONAL_VENOUS_BONE");

    /* 5. REGLAS Y UMBRALES */
    add_glossary_item(items, "thresh_score_aptitude", "THRESHOLDS", "UMBRAL 01 · PUNTAJE Y SALIDAS",
        "Cómo se Calcula el Puntaje (0–100%) y el Permiso para Caminatas EVA", "OK",
        "DISTANCE", "CARDIOVASCULAR",
        "Todos arrancan con 100 puntos: cada alerta roja resta 25 pts, cada aviso amarillo resta 10 pts y los desvíos menores hasta 10 pts · Si hay alerta roja, tormenta solar, mareo >= 5, cólico >= 8 o sueño < 5 h queda 'No apto' para salir.",
        "Para que nadie tenga que interpretar veinte números por separado bajo presión, P.U.L.S.O. resume el estado general en una nota del 0 al 100 % y en un semáforo claro: 'Apto' (puede salir a caminata espacial), 'Restringido' (trabaja adentro con precaución) o 'No apto' (reposo o atención inmediata).",
        "Cuando un astronauta cae a 50 % o menos en estado crítico —o acumula 2 o más alertas rojas— se marca automáticamente como 'URGENCIA' y aparece un botón flotante rojo para ir directo a su ficha.",
        "NASA HRP — 5 Hazards of Human Spaceflight",
        "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/",
        "URGENCIA APTO RESTRINGIDO NO APTO");

    /* 6. DEFINICIONES DE MEDIDAS Y UNIDADES */
    add_glossary_item(items, "meas_vitals_cardio", "MEASURES", "MEDIDAS 01 · SIGNOS VITALES",
        "Latidos (lpm), Presión (mmHg), Oxígeno (%), Temperatura (°C) y Azúcar (mg/dL)", "OK",
        "GRAVITY", "CARDIOVASCULAR",
        "Valores habituales en reposo: pulso 50–80 lpm · presión ~110–130 / 70–85 mmHg · oxígeno SpO2 >= 95 % · temperatura 36.2–37.2 °C · glucosa 70–140 mg/dL.",
        "lpm son los latidos del corazón por minuto. mmHg mide con qué fuerza empuja la sangre en las arterias (el primer número es la máxima al latir y el segundo la mínima en reposo). SpO2 (%) indica qué porcentaje de tu sangre lleva oxígeno. °C es la temperatura corporal y mg/dL mide la cantidad de azúcar (glucosa) circulando en sangre.",
        "No mires solo si un número 'suena alto o bajo': compáralo siempre con el valor normal que tenía ese mismo astronauta antes de despegar.",
        "NASA — The Human Body in Space",
        "https://www.nasa.gov/humans-in-space/the-human-body-in-space/",
        "FC PA PAS PAD SPO2 TEMP GLUCOSA");

    add_glossary_item(items, "meas_vision_rad", "MEASURES", "MEDIDAS 02 · VISTA Y RADIACIÓN",
        "Índice Visual (1.00 = 20/20) y Unidades de Radiación (mSv y mSv/h)", "OK",
        "RADIATION", "OCULAR",
        "Vista normal base: 1.00 (bajar 0.15 o más avisa riesgo SANS) · Radiación normal en estación: ~0.4–1.0 mSv/día · Tope máximo de por vida en NASA: 600 mSv acumulados.",
        "El índice visual traduce qué tan nítido ves en escala decimal (1.00 equivale a una vista estándar 20/20; si baja a 0.75 es señal de revisar los ojos por SANS). En radiación, el milisievert (mSv) mide cuánto daño biológico potencial absorbió el cuerpo en el día o en toda la carrera, mientras que mSv/h indica qué tan intensa está la radiación en cada hora (clave durante tormentas solares).",
        "Vigila si el número visual baja respecto al inicio del viaje y chequea diariamente que la radiación por hora no dé saltos bruscos.",
        "CSA — Space Radiation and Its Effects on the Human Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp",
        "AGUDEZA VISUAL SANS OCT ONSD MSV MSV_H");

    add_glossary_item(items, "meas_cabin_kpa", "MEASURES", "MEDIDAS 03 · AIRE Y GRAVEDAD",
        "Presión del Aire en Cabina (kPa) y Fuerza de Gravedad (g)", "OK",
        "HOSTILE_ENV", "RESPIRATORY",
        "Presión total como al nivel del mar: 101.3 kPa · Oxígeno normal (pO2): ~21.0 kPa · CO2 ideal: <= 0.40 kPa (aviso si pasa 0.50) · Gravedad: 0g flotando, 0.16g Luna, 0.38g Marte, 1g Tierra.",
        "El kilopascal (kPa) mide cuánto empuja el aire o cada gas por separado: respirar a 101.3 kPa total con ~21 kPa de oxígeno es igual que estar en una ciudad al nivel del mar. El CO2 debe quedar bajito (<= 0.40 kPa) porque si sube marea y da dolor de cabeza. En cuanto a gravedad, '1g' es el peso normal en la Tierra y '0g' es flotar en ingravidez.",
        "Mantén el CO2 por debajo de 0.40–0.50 kPa con los limpiadores CDRA/CAMRAS y el oxígeno arriba de 18 kPa para respirar sin ahogo ni fatiga.",
        "NASA HRP — 5 Hazards of Human Spaceflight",
        "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/",
        "KPA PO2 PCO2 GRAVEDAD");

    add_glossary_item(items, "meas_wellbeing_scales", "MEASURES", "MEDIDAS 04 · SUEÑO Y REFLEJOS",
        "Horas de Sueño (h), Escalas de Síntomas y Test de Reflejos (ms)", "OK",
        "ISOLATION", "BEHAVIORAL",
        "Sueño sano: 7.5–8.5 h/día · Dolor, mareo o cólico: 0 (nada) a 10 (máximo) · Ánimo y estrés: escala 1–10 · Calidad de sueño, apetito y trabajo en equipo: escala 1–5 · Reflejos PVT: ~220–300 ms.",
        "ms (milisegundos) mide qué tan rápido reacciona el cerebro cuando aparece una luz verde en el test de reflejos (1000 ms = 1 segundo): si alguien cansado sube de 250 ms a más de 300 ms, sus reflejos ya están más lentos. Las escalas del 0 al 10 miden molestias, mientras que las del 1 al 5 evalúan qué tan bien dormiste, comiste o te llevaste con el equipo (5 = excelente).",
        "Si el tiempo en ms sube más de un 15–20 % frente a tu marca normal, descansa antes de operar controles delicados y revisa también el nivel de CO2.",
        "CSA — Living in Space: Effects of Space on the Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp",
        "PVT MS SUENO ESTRES ANIMO");

    add_glossary_item(items, "meas_metabolic_eva", "MEASURES", "MEDIDAS 05 · AGUA, COMIDA Y ENTRENO",
        "Agua (ml), Calorías (kcal), Peso (kg), Ejercicio y Pre-Respiración (min)", "OK",
        "GRAVITY", "MUSCULOSKELETAL",
        "Agua diaria: ~2400–2800 ml (unos 2.5 litros) · Energía: ~2300–3100 kcal · Ejercicio real: 90 min/día · Oxígeno previo a EVA: >= 120 min (protocolo rápido ISLE).",
        "ml (mililitros) cuenta el agua bebida en el día (1000 ml = 1 litro) para evitar mareos y cálculos renales. kcal mide la energía de las comidas para no bajar de peso sin querer. min (minutos) mide tanto los 90 minutos de ejercicio real diario como los minutos respirando oxígeno puro antes de salir en traje espacial (120 min con ejercicio suave o 240 min quieto).",
        "Recuerda que puedes tomar hasta 1 o 2 días de descanso muscular por semana sin afectar tu promedio si cargas bien tu hidratación y comidas.",
        "NASA — Exercising in Space",
        "https://www.nasa.gov/AtoZ/exercising-in-space/",
        "ML KCAL KG MIN PRE_RESP");

    add_glossary_item(items, "meas_ops_units", "MEASURES", "MEDIDAS 06 · DÍAS, SEÑAL Y SEMÁFORO",
        "Día de Misión, Retraso de Señal (s), Puntaje (%) y Niveles de Alerta", "OK",
        "DISTANCE", "BEHAVIORAL",
        "Día = jornada de misión · Retraso (s) = segundos que tarda la radio en llegar a Tierra · Puntaje: 0 a 100 % · Estados: Normal, Precaución, Crítico y Urgencia.",
        "El día de registro ordena los chequeos cronológicamente. La latencia en segundos muestra cuánto tarda en viajar un mensaje hasta el control en Tierra (desde menos de 1 segundo en órbita hasta más de 20 minutos en Marte). El semáforo traduce todo a cuatro colores simples: Verde (Normal), Amarillo (Precaución), Rojo (Crítico) y Urgencia (atención ya mismo).",
        "Si un tripulante entra en Urgencia, pulsa el botón rojo flotante de abajo a la derecha para ir directo a ver qué necesita.",
        "NASA HRP — Human Research Program Architecture",
        "https://www.nasa.gov/hrp/research/",
        "SOL DIA LATENCIA PUNTAJE URGENCIA");

    /* 7. SIGLAS Y ACRÓNIMOS */
    add_glossary_item(items, "acro_agencies_ops", "ACRONYMS", "SIGLAS 01 · AGENCIAS Y SALIDAS",
        "CSA / ASC · EVA · Apto / Restringido / No apto · LEO", "OK",
        "DISTANCE", "CARDIOVASCULAR",
        "Nombres cortos que aparecen en documentos espaciales y permisos de caminata.",
        "CSA / ASC: Agencia Espacial Canadiense (colabora junto a la NASA en nutrición, radiación y medicina). EVA: Actividad Extravehicular, es decir, salir afuera de la nave con traje espacial. Apto / Restringido / No apto: el semáforo de permiso médico para hacer esa salida. LEO: Órbita Terrestre Baja (donde vuela la Estación Espacial Internacional, a ~400 km de altura).",
        "Revisa siempre la etiqueta de aptitud EVA en la ficha del astronauta antes de planificar trabajos afuera de la nave.",
        "NASA HRP — 5 Hazards of Human Spaceflight",
        "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/",
        "NASA HRP CSA ASC EVA LEO");

    add_glossary_item(items, "acro_env_rad", "ACRONYMS", "SIGLAS 02 · AIRE DE CABINA Y RADIACIÓN",
        "ECLSS · CDRA · CAMRAS · LiOH · HEPA · SPE · GCR", "OK",
        "HOSTILE_ENV", "RESPIRATORY",
        "Equipos que mantienen respirable la nave y tipos de radiación del espacio.",
        "ECLSS: conjunto de máquinas que reciclan agua y aire en la nave. CDRA (en la estación ISS) y CAMRAS (en la cápsula Orion): equipos principales que limpian el CO2 del aire automáticamente. LiOH: cartuchos de hidróxido de litio de emergencia para absorber CO2 si falla lo principal. HEPA: filtros de partículas y microbios. SPE: tormenta rápida de partículas del Sol. GCR: radiación cósmica constante de fondo.",
        "Si falla CDRA o CAMRAS, pasa a cartuchos LiOH; si hay aviso SPE, entra al refugio blindado con bolsas de agua.",
        "CSA — Space Radiation and Its Effects on the Human Body",
        "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp",
        "ECLSS CDRA CAMRAS LIOH HEPA SPE GCR");

    add_glossary_item(items, "acro_syndromes", "ACRONYMS", "SIGLAS 03 · PROBLEMAS MÉDICOS TÍPICOS",
        "SANS · DCS · SMS · VTE / TEV", "OK",
        "GRAVITY", "OCULAR",
        "Siglas médicas frecuentes en astronautas dentro de esta consola.",
        "SANS: problema en los ojos y la vista causado por el líquido que sube a la cabeza en ingravidez. DCS: enfermedad por descompresión (burbujas dolorosas de nitrógeno si faltó oxígeno antes de salir al vacío). SMS: mareo espacial típico de los primeros días flotando sin gravedad. VTE / TEV: coágulo en una vena por sangre más lenta o espesa.",
        "Si ves alguna de estas siglas en una alerta, la consola te indicará los pasos concretos para atenderla sin adivinar.",
        "NASA — The Human Body in Space",
        "https://www.nasa.gov/humans-in-space/the-human-body-in-space/",
        "SANS DCS SMS VTE TEV");

    add_glossary_item(items, "acro_hardware_ex", "ACRONYMS", "SIGLAS 04 · MÁQUINAS DE EJERCICIO Y HUERTO",
        "ARED · T2/COLBERT · Zero-T2 · CEVIS · Thigh Cuff · Veggie", "OK",
        "GRAVITY", "MUSCULOSKELETAL",
        "Nombres de los equipos de entrenamiento, compresión y comida fresca a bordo.",
        "ARED: máquina avanzada de fuerza con resistencia por vacío (reemplaza a las pesas en 0g). T2 / COLBERT: cinta de correr sujeta con arnés en la ISS. Zero-T2: rutina para misiones profundas sin cinta de correr que combina ejercicios resistivos (como volante de inercia en Orion) y bici. CEVIS: bicicleta fija especial. Thigh Cuff: bandas elásticas para los muslos. Veggie: mini invernadero LED para cultivar lechuga y vegetales frescos.",
        "Combina 90 min de ejercicio en estas máquinas con hojas frescas de Veggie y bandas en muslos si hace falta.",
        "NASA — Exercising in Space",
        "https://www.nasa.gov/AtoZ/exercising-in-space/",
        "ARED T2 COLBERT ZERO-T2 CEVIS THIGH CUFF VEGGIE");

    add_glossary_item(items, "acro_diag_psych", "ACRONYMS", "SIGLAS 05 · INSTRUMENTOS Y PRUEBAS",
        "Tempus Pro · Radi-N2 · CANS · ARCHeR · HFBP · PVT · Standard Measures", "OK",
        "ISOLATION", "BEHAVIORAL",
        "Aparatos portátiles médicos, sensores de radiación y tests psicológicos.",
        "Tempus Pro: maletín médico compacto con ecógrafo y monitor de signos vitales para misiones lejos de la Tierra. Radi-N2 (tubitos de burbujas pasivos) y CANS (sensor electrónico en vivo): medidores canadienses de radiación neutrónica y cósmica. ARCHeR y HFBP: guías psicológicas y de sueño para viajes largos. PVT: test corto de velocidad de reflejos. Standard Measures: chequeo médico estándar de referencia.",
        "Usa PVT en cada Check-In y Tempus Pro cuando una alerta pida revisar corazón, venas u ojos.",
        "NASA HRP — Human Research Program Research",
        "https://www.nasa.gov/hrp/research/",
        "TEMPUS RADI-N2 CANS ARCHER HFBP PVT STANDARD MEASURES");

    add_glossary_item(items, "acro_hormonal_sup", "ACRONYMS", "SIGLAS 06 · SUPLEMENTOS Y SALUD FEMENINA",
        "LARC · ACO / COC · Fe · Vitamina D3 · Complejo B", "OK",
        "GRAVITY", "CARDIOVASCULAR",
        "Siglas de métodos hormonales y vitaminas que aparecen en los controles.",
        "LARC: método anticonceptivo de larga duración sin estrógeno (como implante o DIU hormonal) muy usado para pausar la regla en misiones largas. ACO / COC: pastillas anticonceptivas combinadas con estrógeno. Fe: símbolo químico del hierro (clave para prevenir anemia). Vitamina D3: vitamina para los huesos ante falta de sol. Complejo B: grupo de vitaminas que protegen nervios, sangre y vista.",
        "Ajusta hierro, vitamina D3 y complejo B según sexo, etapa hormonal y rutina de ejercicio.",
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

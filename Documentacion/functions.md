# 02 - Especificación del BackEnd en C (Lógica, Datos y API REST)

Este documento define el contrato técnico único del **backend en C**: estructuras de datos, persistencia atómica en `.json`, motor de lógica médica basado en NASA/CSA ([fuentes.md](./fuentes.md)) y endpoints REST consumidos por el frontend ([frontend_ui.md](./frontend_ui.md)).

---

## 1. Tipos de datos para Astronautas, Misiones y Entorno (`src/health_types.h`)

Todos los tipos base del dominio se centralizan en un único header (`health_types.h`) para que el motor de lógica, la persistencia y el servidor HTTP compartan exactamente el mismo contrato sin dependencias circulares.

### 1.1 Enums y Constantes de Dominio
* **`AlertSeverity`:** `ALERT_OK` (Normal), `ALERT_CAUTION` (Precaución), `ALERT_CRITICAL` (Crítico).
* **`OperationalAptitude` (Aptitud para EVA / Pilotaje manual):** `APT_GO` (Apto), `APT_RESTRICTED` (Precaución / restringido), `APT_NO_GO` (No apto para EVA ni maniobras críticas).
* **`NasaHazard` (Los 5 Peligros de NASA HRP):**
  * `HAZARD_RADIATION` (Radiación espacial)
  * `HAZARD_ISOLATION` (Aislamiento y confinamiento)
  * `HAZARD_DISTANCE` (Distancia de la Tierra / autonomía)
  * `HAZARD_GRAVITY` (Campos gravitatorios)
  * `HAZARD_CLOSED_ENV` (Entornos cerrados y hostiles)
* **`BodySystem` (Para semaforización y mapa corporal en UI):**
  * `SYS_CARDIOVASCULAR` (`0`)
  * `SYS_NEURO_OCULAR` (`1`)
  * `SYS_RESPIRATORY_EVA` (`2`)
  * `SYS_MUSCULOSKELETAL` (`3`)
  * `SYS_IMMUNE` (`4`)
  * `SYS_BEHAVIORAL` (`5`)
  * `SYS_RADIATION` (`6`)
* **`GravityMode`:** `GRAV_MICRO` (0g tránsito/ISS), `GRAV_MOON` (1/6g), `GRAV_MARS` (3/8g), `GRAV_EARTH` (1g), `GRAV_DYNAMIC` (lanzamiento/reentrada).
* **`HormonalCyclePhase` (Fisiología hormonal femenina):**
  * `CYCLE_NA` (`0`) — No aplica / Masculino
  * `CYCLE_MENSTRUAL` (`1`) — Menstrual (Días 1–5)
  * `CYCLE_FOLLICULAR` (`2`) — Folicular (Días 6–12)
  * `CYCLE_OVULATORY` (`3`) — Ovulatoria (Días 13–15)
  * `CYCLE_LUTEAL` (`4`) — Lútea (Días 16–28, termogénesis por progesterona)
  * `CYCLE_SUPPRESSED` (`5`) — Suprimido (LARC / Anticonceptivo)
* **Bitmask de síntomas y contramedidas (`SymptomFlags`):**
  * `SYM_NONE` (`0`)
  * `SYM_RASH_ALLERGY` (`1 << 0`) — hipersensibilidad / sarpullido
  * `SYM_VIRAL_REACTIVATION` (`1 << 1`) — signos inmunes / reactivación latente
  * `SYM_INFECTION_SIGNS` (`1 << 2`) — sospecha de infección
  * `SYM_EVA_JOINT_PAIN` (`1 << 3`) — dolor articular/trauma post-EVA (DCS / lesión)
  * `SYM_ORTHOSTATIC_DIZZINESS` (`1 << 4`) — mareo al incorporarse en gravedad
  * `SYM_THIGH_CUFF_ACTIVE` (`1 << 5`) — contramedida SANS activa
  * `SYM_B_COMPLEX_TAKEN` (`1 << 6`) — suplementación B-Complex
  * `SYM_VIT_D_TAKEN` (`1 << 7`) — suplementación Vitamina D
  * `SYM_VEGGIE_FRESH_FOOD` (`1 << 8`) — consumo de vegetales frescos / soporte Veggie
* **Constantes NASA de referencia:**
  * `NASA_CAREER_RAD_LIMIT_MSV = 600.0f` (límite universal de carrera NASA).
  * `MAX_HISTORY_SAMPLES = 90` (ventana histórica por astronauta en memoria).
  * `MAX_CREW_PER_MISSION = 8`, `MAX_MISSIONS = 8`, `MAX_ASTRONAUTS = 32`, `MAX_ALERTS = 16`.
  * `BODY_SYSTEM_COUNT = 7`.

### 1.2 Structs Principales
1. **`AstronautBaseline` (Línea base individual — alineado 1:1 con `data/defaults.json`):**
   * *Signos vitales base:* `fc_base` (lpm), `pa_sis_base` (mmHg), `pa_dia_base` (mmHg), `spo2_base` (%), `temp_base` (°C), `glucosa_base` (mg/dL), `peso_base` (kg).
   * *Sueño, cognición y conducta base:* `sueno_base_h` (h), `animo_base` (1–10), `estres_base` (1–10), `cognitivo_base_ms` (ms).
   * *Metas de nutrición y contramedidas:* `calorias_objetivo_kcal` (kcal), `hidratacion_objetivo_ml` (ml), `apetito_base` (1–5), `ejercicio_objetivo_min` (min/día), `pre_resp_o2_objetivo_min` (min).
   * *Salud espacial base:* `vision_base` (índice relativo), `densidad_osea_base_pct` (100.0%), `dosis_diaria_nominal_msv` (mSv/día).
2. **`DailyHealthSample` (Registro periódico / Check-in ingresado desde la web):**
   * `dia_mision` (`int`), `timestamp_unix` (`long`).
   * *Vitales:* `fc` (lpm), `pa_sis` (mmHg), `pa_dia` (mmHg), `spo2` (%), `temp_c` (°C), `glucosa_mg_dl` (mg/dL), `peso_kg` (kg).
   * *Radiación:* `dosis_dia_msv` (mSv del día, incluyendo componente neutrónico).
   * *Riesgos espaciales específicos:* `vision_actual` (índice relativo), `dolor_cabeza` (0–10), `mareo_vestibular` (0–10), `ejercicio_min` (min/día), `tuvo_eva` (`int` 0/1), `pre_resp_o2_min` (min de pre-respiración O2), `sintomas_flags` (`unsigned int`).
   * *Conductual y nutrición:* `sueno_horas` (h), `sueno_calidad` (1–5), `animo` (1–10), `estres` (1–10), `cognitivo_ms` (ms), `cohesion_equipo` (1–5), `apetito_comida` (1–5), `calorias_kcal` (kcal), `hidratacion_ml` (ml).
   * *Fisiología hormonal femenina (Regla 9):* `fase_ciclo` (`HormonalCyclePhase` 0–5), `dia_ciclo` (`int` 0–60), `flujo_menstrual` (`int` 0–3), `colico_menstrual` (`int` 0–10), `suplemento_hierro` (`int` 0/1).
3. **`Astronaut`:**
   * Identidad: `id` (`char[16]`), `nombre` (`char[64]`), `sexo` (`char[16]`, `"F"` o `"M"`, normalizado mediante `is_valid_sex` / `normalize_sex_str`), `rol` (`char[32]`), `mission_id` (`char[16]`), `avatar` (`char[256]`, ruta local en `web/assets/avatars/`, incluyendo imágenes personalizadas `custom_<id>.<ext>`).
   * Acumuladores: `radiacion_previa_msv` (`float`), `radiacion_mision_msv` (`float` — acumulador histórico independiente del tamaño del ring buffer).
   * Datos fisiológicos: `AstronautBaseline baseline`, `SampleRingBuffer history`.
   * Estado calculado: `EvaluationReport report` (alertas activas, modo autónomo y semáforo por sistema).
4. **`CabinEnv` (Entorno de la nave / misión):**
   * `presion_cabina_kpa` (normal ~101.3 kPa; exploración hipobárica ~56–70 kPa), `po2_kpa` (~21.3 kPa normal), `pco2_kpa` (< 0.4–0.5 kPa nominal), `tasa_rad_msv_h`, `evento_spe_activo` (tormenta solar 0/1), `gravedad_actual` (`GravityMode`), `latencia_com_seg` (0s LEO, ~1.3s Luna, ~300–1320s Marte).
5. **`Mission`:**
   * `id` (`char[16]`), `nombre` (`char[64]`), `destino` (`char[32]`), `dia_actual` (`int`), `CabinEnv env`, lista de IDs de astronautas asignados (`crew_ids[MAX_CREW_PER_MISSION][16]`, `crew_count`).
6. **`HealthAlert` y `EvaluationReport`:**
   * Cada alerta contiene: `id_regla` (`char[32]`), `NasaHazard hazard`, `BodySystem system`, `AlertSeverity severity`, `referencia_nasa` (`char[64]`, ej. `"Risk of Hypoxia / Tempus Pro"`), `titulo` (`char[64]`), `explicacion` (`char[192]`), `contramedida` (`char[256]`).
   * `EvaluationReport` agrupa:
     * `AlertSeverity global_status` (semáforo general del astronauta).
     * `int health_score` (`0..100`, puntaje numérico de salud para el cálculo de **Promedio de Salud** de la misión).
     * `int critical_alert_count` (conteo de alertas activas con severidad `ALERT_CRITICAL`).
     * `int is_very_critical` (`1` cuando `global_status == ALERT_CRITICAL && (health_score <= 50 || critical_alert_count >= 2)`, disparando **Prioridad Total** en UI).
     * `OperationalAptitude eva_aptitude` (`APT_GO` / `APT_RESTRICTED` / `APT_NO_GO` para EVA y pilotaje manual).
     * `resumen_estado` (`char[96]`, descripción breve para la tarjeta principal del astronauta, ej. `"Nominal"` o `"Dolor de cabeza · Revisar CO2"`).
     * `AlertSeverity system_status[BODY_SYSTEM_COUNT]`, `HealthAlert alerts[MAX_ALERTS]`, `alert_count`, `autonomous_mode` (`1` cuando `latencia_com_seg > 5.0f`).
7. **`MissionHealthSummary` (Para la vista `PROMEDIO DE SALUD` y Prioridad Total):**
   * Calculado dinámicamente sobre la misión activa: `mission_health_avg` (promedio `0..100` de la tripulación), `crew_ok_count`, `crew_caution_count`, `crew_critical_count`, `crew_very_critical_count`, `priority_astronaut_id` (ID del tripulante con menor puntaje entre quienes presentan `is_very_critical == 1`), promedios grupales (`fc_avg`, `spo2_avg`, `sueno_avg_h`, `estres_avg`, `rad_mision_avg_msv`) y estado semafórico de los 5 Peligros HRP (`hazard_status[5]`).

### 1.3 Operaciones, Validación y Seguridad (`astronaut.h/.c`, `mission.h/.c`)
* **Constructores / Inicializadores:** `astronaut_init_from_defaults()`, `mission_init()`.
* **Sanitización de IDs:** `is_valid_id(const char *id)` exige exclusivamente `[a-zA-Z0-9_-]` (1..15 chars) para prevenir *path traversal* al abrir `data/astronauts/<id>.json` o `data/missions/<id>.json`.
* **Muestras parciales (Check-in rápido):** si un `POST /api/astronauts/:id/samples` envía solo un subconjunto de campos (por ej. `sueno_horas`, `animo`, `estres` y `sintomas_flags`), el backend completa los campos omitidos usando la última muestra del astronauta (o su `baseline` si es la primera muestra) antes de validar.
* **Validadores de entrada (`validate_sample()` / `validate_cabin()`):** verifican rangos fisiológicos y físicos posibles (`temp_c` 30.0–43.0, `spo2` 40.0–100.0, `fc` 25–230, `pa_sis` 50–250, `pa_dia` 30–150, `sueno_horas` 0.0–24.0, `presion_cabina_kpa` 20.0–120.0, `po2_kpa` 5.0–105.0, `pco2_kpa` 0.0–10.0) devolviendo códigos claros (`OK`, `ERR_INVALID_RANGE`, `ERR_INVALID_ID`, `ERR_NOT_FOUND`, `ERR_LIMIT_REACHED`).

---

## 2. Estructura de datos para el Historial (`SampleRingBuffer`)

Dado que el motor médico necesita **tanto la última medición como promedios y tendencias recientes (últimos 3 y 7 días)**, se usa un **Buffer Circular (`SampleRingBuffer`) por astronauta** junto a arreglos indexados en memoria para misiones y tripulantes.

```c
typedef struct {
    DailyHealthSample items[MAX_HISTORY_SAMPLES]; // 90 muestras
    int head;   // Índice donde se escribirá la próxima muestra
    int count;  // Cantidad actual de muestras válidas (0..MAX_HISTORY_SAMPLES)
} SampleRingBuffer;
```

### Reglas de consistencia del Buffer Circular
1. **Inserción en `O(1)`:** sin fragmentación de memoria ni `memmove`. Al llenarse (`count == MAX_HISTORY_SAMPLES`), sobrescribe la muestra más antigua.
2. **Acumulador de radiación separado:** `radiacion_mision_msv` suma `dosis_dia_msv` al registrar cada nueva muestra y **nunca** se recalcula recorriendo el buffer, evitando perder dosis histórica tras superar las 90 muestras.
3. **Promedios por días reales (`dia_mision`):** al calcular el promedio de los últimos $K$ días (3 o 7), las funciones auxiliares filtran muestras con `dia_mision > dia_actual - K` (o actualizan el registro si ya existe una muestra para el mismo `dia_mision`, evitando que múltiples check-ins en un mismo día distorsionen la ventana temporal).

---

## 3. Persistencia en archivos `.json` (`src/storage.h/.c`)

```text
data/
├── defaults.json             <- Valores promedio / referencia inicial NASA
├── astronauts/
│   ├── astro_01.json         <- Perfil + baseline + acumuladores + historial
│   └── astro_02.json
└── missions/
    └── artemis_sim.json      <- Configuración de misión, crew_ids y CabinEnv
```

1. **Guardado automático:** cada operación mutante (`POST`, `PUT`, `DELETE`) actualiza la memoria y ejecuta `storage_save_astronaut()` o `storage_save_mission()`.
2. **Escritura atómica:** se escribe primero en `<ruta>.json.tmp`, se verifica `fflush()` + `fclose()` sin error y se ejecuta `rename()` sobre `<ruta>.json`.
3. **Carga al arranque:** `storage_load_all()` carga `defaults.json`, misiones y astronautas, valida los IDs, reconstruye los `SampleRingBuffer` y ejecuta `evaluate_astronaut()` sobre toda la tripulación.

---

## 4. Archivo `data/defaults.json` (Referencia Basal NASA)

| Campo | Valor Default | Unidad / Escala | Referencia |
| :--- | :--- | :--- | :--- |
| `fc_base` | `70.0` | lpm | *Standard Measures / Tempus Pro* |
| `pa_sis_base` | `120.0` | mmHg | *Standard Measures* |
| `pa_dia_base` | `80.0` | mmHg | *Standard Measures* |
| `spo2_base` | `98.0` | % | *Risk of Hypoxia* |
| `temp_base` | `36.6` | °C | Basal oral/timpánica |
| `glucosa_base` | `90.0` | mg/dL | Glucemia basal |
| `peso_base` | `72.0` | kg | Masa corporal pre-vuelo |
| `sueno_base_h` | `7.5` | h/día | *ARCHeR / Inadequate Sleep* |
| `animo_base` | `8` | 1–10 | *Behavioral Performance* |
| `estres_base` | `3` | 1–10 | *ARCHeR* |
| `cognitivo_base_ms` | `260.0` | ms | Test de reacción psicomotor |
| `calorias_objetivo_kcal` | `2500` | kcal/día | *Enhanced Diet / Food Appeal* |
| `hidratacion_objetivo_ml` | `2500` | ml/día | Meta hídrica diaria |
| `apetito_base` | `4` | 1–5 | Aceptabilidad de menú |
| `vision_base` | `1.0` | índice rel. | *SANS / Brain Fluid Pressure* |
| `densidad_osea_base_pct` | `100.0` | % | *Bone Changes / CIPHER* |
| `ejercicio_objetivo_min` | `90` | min/día | *Zero-T2 / ARED* |
| `pre_resp_o2_objetivo_min` | `120` | min | Protocolo pre-EVA estándar |
| `dosis_diaria_nominal_msv` | `0.5` | mSv/día | *Radi-N2 / CANS* |

---

## 5. Motor de Lógica Médica y Reglas Espaciales (`src/health_engine.h/.c`)

El núcleo evalúa al astronauta en **3 pasos** tras cada ingreso de muestra o cambio ambiental:

### Paso 1: Análisis vs. Línea Base Personal y Tendencias
* Desviación respecto a baseline (`delta = actual - baseline`, y `%` de variación).
* Promedios móviles por `dia_mision`: ventana de **3 días** (sueño, fatiga aguda, hidratación) y **7 días** (ejercicio semanal, ingesta calórica, tendencia de peso).

### Paso 2: Las 9 Reglas Combinadas Trazables a NASA HRP
1. **Radiación Espacial (`HAZARD_RADIATION` -> `SYS_RADIATION`):**
   * *SPE / Pico Solar:* `env.evento_spe_activo == 1` o `env.tasa_rad_msv_h >= 0.5` o `dosis_dia_msv > 2.0` -> `ALERT_CRITICAL`. Ref: `"CSA Radi-N2 / CANS / Risk of Radiation"`. Contramedida: ingresar de inmediato a zona blindada (*storm shelter*), suspender EVA y registrar dosimetría continua.
   * *Acumulado de Carrera:* `(radiacion_previa_msv + radiacion_mision_msv) >= 480.0` (80% de 600 mSv) -> `ALERT_CAUTION`; `>= 600.0` -> `ALERT_CRITICAL`. Contramedida: restringir exposiciones EVA y replanificar rotaciones de misión.
2. **SANS — Síndrome Neuro-Ocular (`HAZARD_GRAVITY` -> `SYS_NEURO_OCULAR`):**
   * `vision_actual <= baseline.vision_base - 0.15` **Y** (`dolor_cabeza >= 4` o `pa_sis >= pa_sis_base + 20`) -> `ALERT_CAUTION` (o `ALERT_CRITICAL` si `vision_actual <= baseline.vision_base - 0.25`). Ref: `"Risk of SANS / Thigh Cuff / Brain Fluid Pressure"`. Contramedida: iniciar protocolo **Thigh Cuff** (`SYM_THIGH_CUFF_ACTIVE`), verificar suplementación **B-Complex** (`SYM_B_COMPLEX_TAKEN`) y realizar examen de fondo de ojo / ecografía óptica (*Tempus Pro*).
3. **Hipoxia e Hipercapnia (`HAZARD_CLOSED_ENV` -> `SYS_RESPIRATORY_EVA`):**
   * *Hipoxia:* `spo2 < 94.0` o `env.po2_kpa < 18.0` -> `ALERT_CAUTION`; `spo2 < 90.0` o `env.po2_kpa < 15.0` -> `ALERT_CRITICAL`. Ref: `"Risk of Hypoxia / Tempus Pro"`. Contramedida: administrar O2 suplementario, inspeccionar sistema ECLSS y reducir esfuerzo físico.
   * *Hipercapnia (CO2 alto):* `env.pco2_kpa > 0.5` **Y** (`dolor_cabeza >= 4` o `cognitivo_ms > baseline.cognitivo_base_ms * 1.20`) -> `ALERT_CAUTION` (`ALERT_CRITICAL` si `pco2_kpa > 0.8`). Ref: `"Hostile Closed Environments / ECLSS"`. Contramedida: revisar depurador (*scrubber*) de CO2 y ventilar módulo antes de atribuir síntomas a fatiga.
4. **Enfermedad por Descompresión — DCS (`HAZARD_CLOSED_ENV` -> `SYS_RESPIRATORY_EVA`):**
   * `tuvo_eva == 1` **Y** (`pre_resp_o2_min < baseline.pre_resp_o2_objetivo_min` o `sintomas_flags & SYM_EVA_JOINT_PAIN`) -> `ALERT_CRITICAL`. Ref: `"Risk of Decompression Sickness / EVA"`. Contramedida: protocolo inmediato de O2 al 100%, evaluación neurológica y preparación de recompresión en esclusa/traje.
5. **Sistema Cardiovascular, Hemostasia Venosa, Óseo, Vestibular y Cargas Dinámicas (`HAZARD_GRAVITY` -> `SYS_CARDIOVASCULAR` / `SYS_MUSCULOSKELETAL` / `SYS_NEURO_OCULAR`):**
   * *Cardio/Orto y Hemostasia Venosa:* `fc > fc_base + 25` o `fc > 105` o `fc < 45` o `pa_sis < 90` o `pa_sis > 140` o `SYM_ORTHOSTATIC_DIZZINESS` o deshidratación sostenida en microgravedad (`hidratacion_ml < 0.75 * hidratacion_objetivo_ml`) -> `ALERT_CAUTION` (`ALERT_CRITICAL` si `pa_sis < 85` o `fc > 130`). Ref: `"Cardiovascular Adaptations / Venous Haemostasis / Tempus Pro"`. Contramedida: carga de fluidos/electrolitos, prendas de compresión inferior, monitoreo vascular/ECG.
   * *Óseo/Muscular:* promedio 7 días de `ejercicio_min < 60` -> `ALERT_CAUTION` (`< 45` -> `ALERT_CRITICAL` o refuerzo prioritario). Ref: `"Zero-T2 / Risk of Bone Changes"`. Contramedida: reforzar rutina resistiva/aeróbica (*ARED* / *Zero-T2*) y verificar `SYM_VIT_D_TAKEN`.
   * *Vestibular y Cargas Dinámicas:* `mareo_vestibular >= 5` en `GRAV_MOON` / `GRAV_MARS` / `GRAV_EARTH`, o `env.gravedad_actual == GRAV_DYNAMIC` con desacondicionamiento/mareo (`mareo_vestibular >= 4` o prom. 7d `ejercicio_min < 60`) -> `ALERT_CAUTION` (`eva_aptitude = APT_NO_GO`). Ref: `"Sensorimotor & Vestibular / Dynamic Loads / Manual Piloting"`. Contramedida: diferir pilotaje manual y tareas EVA por 24–48 h; asegurar soporte de protección de ocupante.
6. **Inmunidad, Microbioma y Metabolismo (`HAZARD_CLOSED_ENV` -> `SYS_IMMUNE`):**
   * `temp_c >= 37.8` o `(sintomas_flags & (SYM_VIRAL_REACTIVATION | SYM_INFECTION_SIGNS | SYM_RASH_ALLERGY))` o alteración glucémica (`glucosa_mg_dl < 70.0` o `> 140.0`) -> `ALERT_CAUTION` (`ALERT_CRITICAL` si `temp_c >= 39.0` o signos combinados). Ref: `"Immune Biomarkers / Host-Microorganism / Standard Measures"`. Contramedida: aislar/reducir contacto cercano, tomar muestra de saliva seca, estabilizar glucemia, revisar filtros de aire y cultivos (*Veggie*).
7. **Aislamiento, Sueño, Cohesión de Equipo y Nutrición (`HAZARD_ISOLATION` -> `SYS_BEHAVIORAL`):**
   * *Deuda de sueño / Estrés:* promedio 3 días de `sueno_horas < 6.0` **Y** (`cognitivo_ms > cognitivo_base_ms * 1.15` o `estres >= 7` o `animo <= 4`) -> `ALERT_CAUTION` (`ALERT_CRITICAL` si promedio < 5.0 h y `estres >= 8`). Ref: `"ARCHeR / Inadequate Sleep"`. Contramedida: ajustar iluminación LED circadiana, descargar tareas no críticas y aplicar protocolo de soporte conductual / cuidado de plantas (*Veggie*).
   * *Cohesión de Equipo:* `cohesion_equipo <= 2` -> `ALERT_CAUTION`. Ref: `"Risk of Inadequate Teamwork / HFBP"`. Contramedida: programar briefing de coordinación, redistribuir carga de trabajo y actividad grupal de descompresión.
   * *Nutrición / Fatiga de menú:* promedio 7 días de `calorias_kcal < 0.80 * calorias_objetivo_kcal` o `apetito_comida <= 2` sostenido o `peso_kg < peso_base * 0.95` -> `ALERT_CAUTION`. Ref: `"Enhanced Diet / Food Appeal"`. Contramedida: rotar menú con dieta enriquecida (omega-3/flavonoides) y reforzar hidratación.
8. **Autonomía por Distancia (`HAZARD_DISTANCE`):**
   * Si `env.latencia_com_seg > 5.0` -> `autonomous_mode = 1`. Toda alerta emitida opera en modo de triaje autosuficiente a bordo sin esperar confirmación en tiempo real desde Control de Misión.
9. **Fisiología Hormonal Femenina, Menstruación y Anemia/Hemostasia (`HAZARD_GRAVITY` / `HAZARD_ISOLATION` -> `SYS_CARDIOVASCULAR` / `SYS_IMMUNE` / `SYS_BEHAVIORAL`):**
   * *Menorragia / Dismenorrea Severa (`HORMONAL_MENORRHAGIA_CRIT`):* `(flujo_menstrual == 3 && (fc > fc_base + 18 || pa_sis < 95 || SYM_ORTHOSTATIC_DIZZINESS || colico_menstrual >= 7)) || colico_menstrual >= 8` -> `ALERT_CRITICAL` y `eva_aptitude = APT_NO_GO`. Contramedida: suspender EVA, hierro terapéutico + B-Complex, reposición de fluidos y control de hemoglobina/ferritina.
   * *Cuidado Menstrual y Prevención de Ferropenia (`HORMONAL_MENSTRUAL_CARE`):* `(fase_ciclo == CYCLE_MENSTRUAL || flujo_menstrual >= 1 || colico_menstrual >= 4)` con falta de hierro (`suplemento_hierro == 0`), `flujo >= 2`, `colico >= 4` o baja hidratación -> `ALERT_CAUTION` (`eva_aptitude = APT_RESTRICTED` si `colico >= 4`).
   * *Fase Lútea (`HORMONAL_LUTEAL_EFFECT`):* `fase_ciclo == CYCLE_LUTEAL` con alza térmica fisiológica por progesterona (`temp_c >= temp_base + 0.35 && temp_c < 37.8`), `colico >= 3`, cefalea con déficit de sueño o `animo <= 5` -> `ALERT_CAUTION` (termorregulación + higiene circadiana *ARCHeR*).
   * *Supresión Hormonal (`HORMONAL_VENOUS_BONE`):* `fase_ciclo == CYCLE_SUPPRESSED` con hidratación `< 85%`, ejercicio 7d `< 65 min/día` o falta de Vitamina D (`SYM_VIT_D_TAKEN == 0`) -> `ALERT_CAUTION` (*Venous Haemostasis* / *Zero-T2* + Vitamina D).

### Paso 3: Consolidación del Reporte, Puntaje de Salud (`health_score`) y Aptitud EVA
* Se determina `global_status` como la máxima severidad activa, se actualiza `system_status[0..6]` y se ordenan las alertas de `ALERT_CRITICAL` a `ALERT_CAUTION`.
* Se calcula `health_score` (`0..100`): parte de `100`, resta `25` por cada alerta `ALERT_CRITICAL`, `10` por cada `ALERT_CAUTION` y hasta `10` puntos adicionales por desviaciones leves respecto a `AstronautBaseline` (acotado al rango `[0, 100]`), junto a `critical_alert_count` y `is_very_critical` (`1` cuando `global_status == ALERT_CRITICAL && (health_score <= 50 || critical_alert_count >= 2)`).
* Se determina `eva_aptitude`: `APT_NO_GO` si hay cualquier `ALERT_CRITICAL`, `evento_spe_activo == 1`, `mareo_vestibular >= 5`, `colico_menstrual >= 8` o deuda aguda de sueño (`sueno_3d < 5.0`); `APT_RESTRICTED` si hay alertas `ALERT_CAUTION` o `colico_menstrual >= 4`; `APT_GO` en estado nominal.
* Las explicaciones y contramedidas generadas por `health_engine.c` usan lenguaje clínico legible en español (por ejemplo, `gravity_mode_to_es()` convierte `GRAV_MOON` → `"Gravedad Lunar (1/6 g)"`) sin exponer identificadores de código al operador.

---

## 6. Servidor HTTP y Contrato de Endpoints REST (`src/server.c`)

* **Puerto local:** `http://localhost:8080` (100% offline).
* **Archivos estáticos:** toda ruta fuera de `/api/` sirve desde `web/` (`index.html`, `styles.css`, `app.js`).
* **Compilación:** `gcc -Wall -Wextra -std=c11 ... -lm`.

### Tabla de Endpoints Oficiales

| Método y Ruta | Propósito | Cuerpo Request (JSON) | Respuesta (JSON) |
| :--- | :--- | :--- | :--- |
| `GET /api/defaults` | Obtener `AstronautBaseline` de referencia (`defaults.json`) | — | Objeto `AstronautBaseline` + `NASA_CAREER_RAD_LIMIT_MSV` |
| `GET /api/glossary` | Obtener glosario clínico y operativo detallado NASA HRP / CSA (5 peligros, protocolos, síntomas, salud femenina, umbrales, `MEASURES` y `ACRONYMS`, con `keywords` separadas de los textos visibles y `source_title`) | — | Objeto JSON `{version, title, sources[], items[]}` |
| `POST /api/avatars` | Decodificar y guardar una imagen personalizada (`data:image/...;base64,...`, máx. 2 MB en PNG/JPG/WEBP/GIF/SVG) en `web/assets/avatars/` | `{"id": "...", "data_url": "data:image/...;base64,..."}` | `201 Created` + ruta relativa `avatar` (y actualiza astronauta si `id` existe) |
| `GET /api/missions` | Listar misiones disponibles | — | Array de misiones (`id`, `nombre`, `destino`, `dia_actual`, `crew_count`) |
| `POST /api/missions` | Crear misión nueva | `id`, `nombre`, `destino`, `env` opcional | `201 Created` + objeto `Mission` |
| `GET /api/missions/:id` | Estado completo de misión, `CabinEnv`, `MissionHealthSummary` y tarjetas de tripulación | — | `Mission`, `CabinEnv`, `autonomous_mode`, `MissionHealthSummary` (incl. `crew_very_critical_count`, `priority_astronaut_id`), array resumen de `crew` (`id`, `nombre`, `sexo`, `rol`, `avatar`, `global_status`, `health_score`, `critical_alert_count`, `is_very_critical`, `eva_aptitude`, `fase_ciclo`, `resumen_estado`) |
| `PUT /api/missions/:id/env` | Actualizar `CabinEnv`, `gravedad_actual` o `latencia_com_seg` | Campos parciales o totales de `CabinEnv` | `200 OK` + `CabinEnv` actualizado y tripulación re-evaluada |
| `POST /api/missions/:id/simulate` | Inyectar escenario de prueba o resetear a nominal | `{"scenario": "NOMINAL" \| "SPE_STORM" \| "HYPOXIA" \| "HYPERCAPNIA" \| "SANS_SLEEP" \| "FATIGUE_ISOLATION" \| "DCS_EVA" \| "HORMONAL_FEMALE"}` | `200 OK` + estado actualizado de misión y alertas disparadas |
| `GET /api/astronauts` | Listar todos los astronautas registrados | — | Array resumido (`id`, `nombre`, `sexo`, `rol`, `mission_id`, `avatar`, `global_status`, `health_score`, `critical_alert_count`, `is_very_critical`, `eva_aptitude`, `fase_ciclo`, `resumen_estado`) |
| `POST /api/astronauts` | Crear o importar astronauta nuevo (completa faltantes con `defaults.json`, admite `history[]` opcional y avatar `data:image/...;base64,...`) | `id`, `nombre`, `sexo` (`F`/`M`), `rol`, `mission_id`, `avatar` opcional (ruta o Base64), `baseline` opcional, `history` opcional | `201 Created` + objeto `Astronaut` persistido |
| `GET /api/astronauts/:id` | Detalle completo de un astronauta | — | Perfil, `baseline`, `radiacion_*`, `history[]`, `EvaluationReport` (incl. `critical_alert_count` e `is_very_critical`) |
| `PUT /api/astronauts/:id` | Actualizar perfil, `mission_id` o `baseline` | Campos a actualizar | `200 OK` + `Astronaut` re-evaluado y persistido |
| `DELETE /api/astronauts/:id` | Eliminar astronauta de prueba y desvincular de misión | — | `200 OK` + confirmación |
| `POST /api/astronauts/:id/samples` | Registrar muestra / *Crew Check-in* (acepta campos parciales) | Objeto `DailyHealthSample` | `201 Created` + muestra consolidada y nuevo `EvaluationReport` |
| `GET /api/astronauts/:id/export` | Descargar bitácora clínica completa para sincronización diferida | — | JSON descargable (`Content-Disposition: attachment`) |

---

## 7. Notas de Revisión y Mejoras Implementadas en el Backend
* **Validación hemodinámica, temporal y de línea base (`validate_sample()` y `validate_baseline()` en `src/astronaut.c`):** además de los rangos fisiológicos por variable, se exige `dia_mision >= 1` y consistencia de presión arterial (`pa_dia < pa_sis` y `pa_dia_base < pa_sis_base`), devolviendo `ERR_INVALID_RANGE` (`400 Bad Request`) tanto en muestras diarias como al crear/importar o editar la línea base (`AstronautBaseline`) de un astronauta.
* **Actualización idempotente de cualquier Sol y orden cronológico garantizado (`astronaut_add_sample()` y `sample_prepare_partial_base()`):** el `SampleRingBuffer` verifica todas las muestras almacenadas al recibir un `dia_mision`; si el Sol ya existe (incluso si corresponde a un día previo al último), se actualiza in-place ajustando exactamente el diferencial en `radiacion_mision_msv` sin duplicar entradas ni alterar el Sol más reciente. Si se inserta un Sol intermedio omitido previamente (*backfill*), el buffer mantiene orden cronológico ascendente por `dia_mision`.
* **Evaluación combinada de deshidratación aguda y sostenida (`evaluate_astronaut()` en `src/health_engine.c`):** la Regla 5 (`GRAV_CARDIO_VENOUS`) evalúa tanto la ingesta hídrica del último día (`hidratacion_ml < 0.75 * hidratacion_objetivo_ml`) como el promedio móvil de 3 días (`hid_3d < 0.75 * hidratacion_objetivo_ml`).
* **Validación estricta de tipos JSON, bitmask `SymptomFlags` y enrutamiento URI (`src/storage.c`, `src/astronaut.c`, `src/server.c`, `third_party/mongoose.c`):** se rechazan con `400 Bad Request` tipos JSON inválidos (ej. strings en campos numéricos o en `sintomas_flags`), valores negativos o fuera del bitmask válido de 9 bits (`0..511` / `0x1FF`) en `sintomas_flags`, historiales corruptos en `CARGAR ARCHIVO`, parámetros ambientales fuera de rango en `POST /api/missions` y desbordamientos numéricos (`NaN`/`Inf`). Asimismo, el comodín `*` en rutas REST coincide con un único segmento de URI para que subrutas inexistentes bajo `/api/` retornen `404 Not Found`.
* **Campos descriptivos legibles en respuestas JSON (`src/storage.c`):** junto a los códigos numéricos de los enums (`global_status`, `eva_aptitude`, `hazard`, `system`, `gravedad_actual`, `fase_ciclo`), el serializador incluye sus equivalentes en texto (`global_status_str`, `eva_aptitude_str`, `hazard_str`, `system_str`, `gravedad_nombre`, `fase_ciclo_str`), además de los indicadores `critical_alert_count`, `is_very_critical`, `crew_very_critical_count` y `priority_astronaut_id`.
* **Subida atómica de imágenes personalizadas y glosario NASA (`src/storage.c`, `src/server.c`):** `storage_save_custom_avatar()` decodifica cargas `data:image/<png|jpeg|webp|gif|svg+xml>;base64,...` de hasta 2 MB y las persiste de forma atómica en `web/assets/avatars/custom_<id>.<ext>`. `build_glossary_json()` (`GET /api/glossary`) expone el glosario clínico NASA HRP / CSA en lenguaje comprensible (incluyendo `MEASURES` y `ACRONYMS`) separando palabras clave técnicas en `keywords` y manteniendo coherencia 1:1 con los umbrales de `health_engine.c`.



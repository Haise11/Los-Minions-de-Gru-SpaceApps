# Especificación del BackEnd en C (Lógica, Datos y Conexión Web)

Este documento define exclusivamente la arquitectura del **backend en C**, las estructuras de datos, la persistencia en archivos `.json`, el motor de lógica médica/espacial basado en la documentación de NASA/CSA ([fuentes.md](./fuentes.md)) y el protocolo de conexión con el frontend web.

---

## 1. Tipos de datos para Astronautas, Misiones y Entorno (`health_types.h`)

Todos los tipos base del dominio se centralizan en un único header (`health_types.h`) para que el motor de lógica, la persistencia y el servidor HTTP compartan exactamente el mismo contrato sin dependencias circulares.

### 1.1 Enums y Constantes de Dominio
* **`AlertSeverity`:** `ALERT_OK` (Normal), `ALERT_CAUTION` (Precaución), `ALERT_CRITICAL` (Crítico).
* **`NasaHazard` (Los 5 Peligros de NASA HRP):**
  * `HAZARD_RADIATION` (Radiación espacial)
  * `HAZARD_ISOLATION` (Aislamiento y confinamiento)
  * `HAZARD_DISTANCE` (Distancia de la Tierra / autonomía)
  * `HAZARD_GRAVITY` (Campos gravitatorios)
  * `HAZARD_CLOSED_ENV` (Entornos cerrados y hostiles)
* **`BodySystem` (Para semaforización y mapa corporal):**
  * `SYS_CARDIOVASCULAR`, `SYS_NEURO_OCULAR`, `SYS_RESPIRATORY_EVA`, `SYS_MUSCULOSKELETAL`, `SYS_IMMUNE`, `SYS_BEHAVIORAL`, `SYS_RADIATION`.
* **`GravityMode`:** `GRAV_MICRO` (0g tránsito/ISS), `GRAV_MOON` (1/6g), `GRAV_MARS` (3/8g), `GRAV_EARTH` (1g), `GRAV_DYNAMIC` (lanzamiento/reentrada).
* **Bitmask de síntomas (`SymptomFlags`):**
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
  * `MAX_CREW_PER_MISSION = 8`, `MAX_ALERTS = 16`.

### 1.2 Structs Principales
1. **`AstronautBaseline` (Línea base individual — inspirado en *Artemis II Standard Measures*):**
   * Signos vitales base: `fc_base`, `pa_sis_base`, `pa_dia_base`, `spo2_base`, `temp_base`, `glucosa_base`, `peso_base`.
   * Salud espacial base: `sueno_base_h`, `vision_base`, `cognitivo_base_ms`, `densidad_osea_base_pct` (100.0%), `calorias_objetivo_kcal`, `hidratacion_objetivo_ml`, `ejercicio_objetivo_min`.
2. **`DailyHealthSample` (Registro periódico / Check-in ingresado desde la web):**
   * `dia_mision` (`int`), `timestamp_unix` (`long`).
   * **Vitales:** `fc` (lpm), `pa_sis` (mmHg), `pa_dia` (mmHg), `spo2` (%), `temp_c` (°C), `glucosa_mg_dl` (mg/dL), `peso_kg` (kg).
   * **Radiación:** `dosis_dia_msv` (mSv del día, incluyendo componente neutrónico).
   * **Riesgos espaciales específicos:** `vision_actual` (índice relativo), `dolor_cabeza` (0–10), `mareo_vestibular` (0–10), `ejercicio_min` (min/día), `tuvo_eva` (`int` 0/1), `pre_resp_o2_min` (min de pre-respiración O2), `sintomas_flags` (`unsigned int`).
   * **Conductual y nutrición:** `sueno_horas` (h), `sueno_calidad` (1–5), `animo` (1–10), `estres` (1–10), `cognitivo_ms` (ms), `cohesion_equipo` (1–5), `apetito_comida` (1–5), `calorias_kcal` (kcal), `hidratacion_ml` (ml).
3. **`Astronaut`:**
   * Identidad: `id` (`char[16]`), `nombre` (`char[64]`), `rol` (`char[32]`).
   * Acumuladores: `radiacion_previa_msv`, `radiacion_mision_msv`.
   * Datos fisiológicos: `AstronautBaseline baseline`, `SampleRingBuffer history` (ver sección 2).
   * Estado calculado: último `EvaluationReport` (alertas activas y semáforo por sistema).
4. **`CabinEnv` (Entorno de la nave / misión):**
   * `presion_cabina_kpa` (normal ~101.3 kPa; exploración hipobárica ~56–70 kPa), `po2_kpa` (~21.3 kPa normal), `pco2_kpa` (< 0.4–0.5 kPa nominal), `tasa_rad_msv_h`, `evento_spe_activo` (tormenta solar 0/1), `gravedad_actual` (`GravityMode`), `latencia_com_seg` (0s LEO, ~1.3s Luna, ~300–1320s Marte).
5. **`Mission`:**
   * `id` (`char[16]`), `nombre` (`char[64]`), `destino` (`char[32]`), `dia_actual` (`int`), `CabinEnv env`, lista de IDs de astronautas asignados (`crew_ids[MAX_CREW_PER_MISSION]`, `crew_count`).
6. **`HealthAlert` y `EvaluationReport`:**
   * Cada alerta contiene: `id_regla`, `NasaHazard hazard`, `BodySystem system`, `AlertSeverity severity`, `titulo[64]`, `explicacion[192]`, `contramedida[256]` (pasos concretos para el astronauta).
   * `EvaluationReport` agrupa: `AlertSeverity global_status`, `AlertSeverity system_status[7]`, `HealthAlert alerts[MAX_ALERTS]`, `alert_count`, `autonomous_mode` (activo cuando `latencia_com_seg > 5.0f`).

### 1.3 Operaciones (Setters, Getters y Validación)
Se implementan en `astronaut.h/.c` y `mission.h/.c`:
* **Constructores / Inicializadores:** `astronaut_init_from_defaults()`, `mission_init()`.
* **Getters / Setters encapsulados** para modificar baseline, entorno de cabina y estado de misión sin exponer índices inválidos.
* **Validadores de entrada (`validate_sample()` / `validate_cabin()`):** verifican rangos fisiológicos posibles antes de aceptar datos desde la web (ej. `temp_c` entre 30.0 y 43.0, `spo2` entre 40.0 y 100.0, `fc` entre 25 y 230) devolviendo códigos de error claros (`ERR_INVALID_RANGE`, `ERR_NOT_FOUND`, `OK`).

---

## 2. Estructura de datos óptima para los ingresos desde la Web

Dado que la página web enviará registros diarios/periódicos de forma continua y el motor médico necesita **tanto la última medición como promedios y tendencias recientes (últimos 3, 7 y 14 días)**, la estructura óptima en C es un **Buffer Circular (`SampleRingBuffer`) por astronauta** complementado con un **arreglo indexado en memoria para el catálogo de astronautas y misiones**.

```c
typedef struct {
    DailyHealthSample items[MAX_HISTORY_SAMPLES]; // Ej. 90 muestras
    int head;   // Índice donde se escribirá la próxima muestra
    int count;  // Cantidad actual de muestras válidas (0..MAX_HISTORY_SAMPLES)
} SampleRingBuffer;
```

### Por qué es la estructura óptima en C
* **Inserción en `O(1)`:** cada actualización que llega desde la web sobrescribe la posición más vieja si se llena el buffer, sin mover memoria (`memmove`) ni sufrir fragmentación de `malloc/free`.
* **Lectura rápida del estado actual y tendencias:** obtener la última muestra es `O(1)`, y calcular promedios móviles de los últimos $K$ días (ej. déficit de sueño de 3 días o promedio de ejercicio de 7 días) recorre solo $K$ elementos contiguos en memoria.
* **Mapeo directo a JSON:** se serializa fácilmente en orden cronológico (de la muestra más antigua a la más reciente).
* **Para tripulación y misiones:** como el número de astronautas por misión es pequeño ($N \le 8$) y el total de misiones en una instancia es acotado, un arreglo estático/dinámico con búsqueda por `id` (o tabla hash simple si creciera) da acceso prácticamente inmediato sin complejidad innecesaria.

---

## 3. Persistencia en archivos `.json`

Para cumplir con la persistencia continua sin usar bases de datos pesadas (y mantener el backend 100% portable y offline), se organizan los datos en una carpeta `data/`:

```text
data/
├── defaults.json             <- Valores promedio / referencia inicial por defecto
├── astronauts/
│   ├── astro_01.json         <- Perfil + baseline + historial completo de muestras
│   └── astro_02.json
└── missions/
    └── artemis_sim.json      <- Configuración de misión, tripulación asignada y CabinEnv
```

### Reglas de persistencia (`storage.h/.c`)
1. **Guardado automático por actualización:** cada `POST` o `PUT` desde la web (nueva muestra médica, cambio de entorno de cabina, creación de astronauta) actualiza la estructura en memoria y dispara inmediatamente `storage_save_astronaut()` o `storage_save_mission()`.
2. **Escritura atómica (anti-corrupción):** el backend primero escribe el JSON en `archivo.json.tmp`, verifica que `fflush/fclose` no falle y luego ejecuta `rename("archivo.json.tmp", "archivo.json")`. Así nunca queda un JSON cortado a la mitad.
3. **Carga al arranque:** al iniciar el servidor en C, `storage_load_all()` lee `defaults.json`, todos los `.json` en `data/missions/` y `data/astronauts/`, reconstruye los `SampleRingBuffer` y ejecuta una evaluación inicial.

---

## 4. Archivo `data/defaults.json` (Valores promedio de referencia del astronauta)

Este archivo contiene los **valores por defecto / línea base inicial** que el backend carga al crear un astronauta nuevo (luego ajustables según la fisiología individual del tripulante) y las referencias de meta diaria basadas en estándares de la NASA:

* **Signos vitales promedio:**
  * `fc_base`: `70.0` lpm (frecuencia cardíaca en reposo)
  * `pa_sis_base`: `120.0` mmHg (presión arterial sistólica)
  * `pa_dia_base`: `80.0` mmHg (presión arterial diastólica)
  * `spo2_base`: `98.0` % (saturación arterial de oxígeno)
  * `temp_base`: `36.6` °C (temperatura corporal)
  * `glucosa_base`: `90.0` mg/dL (glucemia en ayunas/basal)
  * `peso_base`: `72.0` kg (masa corporal de referencia)
* **Sueño, cognición y bienestar conductual (HFBP / *ARCHeR*):**
  * `sueno_base_h`: `7.5` horas/día (umbral saludable mínimo en vuelo: >= 6.5–7.0 h)
  * `animo_base`: `8` (escala 1–10)
  * `estres_base`: `3` (escala 1–10)
  * `cognitivo_base_ms`: `260.0` ms (tiempo de reacción basal en test psicomotor)
* **Nutrición e hidratación (*Food Appeal* / *Enhanced Diet*):**
  * `calorias_objetivo_kcal`: `2500` kcal/día
  * `hidratacion_objetivo_ml`: `2500` ml/día
  * `apetito_base`: `4` (aceptabilidad de menú, escala 1–5)
* **Salud espacial específica (*SANS*, *Zero-T2*, EVA, Radiación):**
  * `vision_base`: `1.0` (índice de agudeza visual basal; < 0.85 indica deterioro)
  * `densidad_osea_base_pct`: `100.0` % (referencia pre-vuelo)
  * `ejercicio_objetivo_min`: `90` min/día (referencia ISS: ~1.5 h/día entre resistivo y aeróbico)
  * `pre_resp_o2_objetivo_min`: `120` min (mínimo de pre-respiración O2 para EVA desde cabina estándar; ajustable según presión)
  * `radiacion_limite_carrera_msv`: `600.0` mSv (límite oficial de carrera NASA)
  * `dosis_diaria_nominal_msv`: `0.5` mSv/día (referencia aproximada de exposición base en órbita/espacio profundo sin tormenta solar)

---

## 5. Motor de Lógica Médica y Reglas Espaciales (`health_engine.h/.c`)

El núcleo del backend evalúa al astronauta en **3 pasos** cada vez que ingresa un dato nuevo:

### Paso 1: Análisis vs. Línea Base Personal y Tendencias
Para cada métrica numérica $x$, se calcula:
* **Desviación respecto a su propia línea base:** `delta = actual - baseline` (y porcentaje de cambio).
* **Promedios móviles:** promedio de últimos **3 días** (para fatiga aguda, hidratación, sueño) y **7 días** (para ejercicio semanal, pérdida de peso, radiación acumulada).

### Paso 2: Reglas Combinadas (Correlación Multi-Indicador)
El motor evalúa reglas trazables directamente a los 5 Peligros de la NASA:

1. **Radiación Espacial (`HAZARD_RADIATION`):**
   * *Evento Solar Agudo (SPE):* si `env.evento_spe_activo == 1` o `env.tasa_rad_msv_h >= 0.5` -> `CRITICAL` -> **Contramedida:** ingresar de inmediato a zona blindada (*storm shelter*), registrar dosimetría continua (estilo *CANS*) y suspender EVA.
   * *Acumulado de Carrera:* si `(radiacion_previa_msv + radiacion_mision_msv)` alcanza el 80% de `600 mSv` (`>= 480 mSv`) -> `CAUTION`; si `>= 600 mSv` -> `CRITICAL` -> **Contramedida:** limitar exposición extra-vehicular y replanificar rotaciones.
2. **SANS — Síndrome Neuro-Ocular (`HAZARD_GRAVITY`):**
   * Si `vision_actual <= baseline.vision_base - 0.15` **Y** (`dolor_cabeza >= 4` o presión arterial elevada respecto a base) -> `CAUTION`/`CRITICAL` -> **Contramedida:** iniciar protocolo **Thigh Cuff** (compresión en muslos), verificar suplementación **B-Complex**, realizar examen de fondo de ojo / ecografía de nervio óptico (*Tempus Pro*).
3. **Hipoxia e Hipercapnia (`HAZARD_CLOSED_ENV`):**
   * *Hipoxia:* si `spo2 < 94.0` (o `env.po2_kpa < 18.0`) -> `CAUTION`; si `spo2 < 90.0` -> `CRITICAL` -> **Contramedida:** administrar O2 suplementario, verificar presión parcial de O2 en cabina y reducir esfuerzo físico.
   * *Hipercapnia (CO2 alto):* si `env.pco2_kpa > 0.5` **Y** (`dolor_cabeza >= 4` o `cognitivo_ms > baseline.cognitivo_base_ms * 1.20`) -> `CAUTION`/`CRITICAL` -> **Contramedida:** inspeccionar depurador de CO2 (*scrubber*) y ventilar módulo antes de atribuir el cuadro a fatiga.
4. **Enfermedad por Descompresión — DCS (`HAZARD_CLOSED_ENV`):**
   * Si `tuvo_eva == 1` **Y** (`pre_resp_o2_min < pre_resp_objetivo_min` o `sintomas_flags & SYM_EVA_JOINT_PAIN`) -> `CRITICAL` -> **Contramedida:** protocolo inmediato de O2 al 100%, evaluación neurológica y preparación de recompresión en esclusa/traje.
5. **Sistema Cardiovascular, Óseo y Vestibular (`HAZARD_GRAVITY`):**
   * *Cardio/Orto:* si `fc > fc_base + 25` en reposo, `pa_sis < 90` o `SYM_ORTHOSTATIC_DIZZINESS` tras transición de gravedad -> `CAUTION`/`CRITICAL` -> **Contramedida:** carga de fluidos/electrolitos, prendas de compresión inferior y monitoreo ECG.
   * *Óseo/Muscular:* si el promedio de `ejercicio_min` de los últimos 7 días `< 60 min/día` -> `CAUTION` -> **Contramedida:** reforzar rutina resistiva/aeróbica (*ARED* / *Zero-T2*) y verificar `SYM_VIT_D_TAKEN`.
   * *Vestibular:* si `mareo_vestibular >= 5` en `GRAV_MOON`/`GRAV_MARS`/`GRAV_EARTH` -> `CAUTION` -> **Contramedida:** diferir pilotaje manual (*Simulating Manual Piloting*) y tareas de EVA por 24–48 h.
6. **Inmunidad y Microbioma (`HAZARD_CLOSED_ENV`):**
   * Si `temp_c >= 37.8` **O** (`sintomas_flags & (SYM_VIRAL_REACTIVATION | SYM_INFECTION_SIGNS | SYM_RASH_ALLERGY)`) -> `CAUTION`/`CRITICAL` -> **Contramedida:** aislar/reducir contacto cercano, tomar muestra de saliva seca (*Immune Biomarkers*), revisar higiene de superficies y filtros/cultivos (*Veggie*).
7. **Aislamiento, Sueño y Nutrición (`HAZARD_ISOLATION`):**
   * *Deuda de sueño / Fatiga:* si promedio de `sueno_horas` en 3 días `< 6.0 h` **Y** (`cognitivo_ms > cognitivo_base_ms * 1.15` o `estres >= 7` o `animo <= 4`) -> `CAUTION`/`CRITICAL` -> **Contramedida:** reajustar ciclo de iluminación LED circadiana, descargar tareas no críticas, sesión de soporte conductual / cuidado de plantas (*Veggie*).
   * *Nutrición / Fatiga de menú:* si promedio de `calorias_kcal < 0.80 * calorias_objetivo_kcal` o `apetito_comida <= 2` sostenido o pérdida de `peso_kg > 5%` respecto a `peso_base` -> `CAUTION` -> **Contramedida:** rotar menú hacia dieta enriquecida (*Enhanced Diet*, omega-3/flavonoides) y reforzar hidratación (`hidratacion_ml`).
8. **Autonomía por Distancia (`HAZARD_DISTANCE`):**
   * Si `env.latencia_com_seg > 5.0` -> `autonomous_mode = 1`: toda alerta incluye instrucciones autosuficientes de triaje a bordo sin esperar confirmación en tiempo real desde Control de Misión.

### Paso 3: Consolidación del Reporte
Se calcula `global_status` como la peor severidad activa, se actualiza el semáforo de los 7 `BodySystem` y se ordenan las alertas de `CRITICAL` a `CAUTION`.

---

## 6. Conexión entre BackEnd (C) y FrontEnd (HTML, CSS, JS)

### Arquitectura de conexión recomendada
El ejecutable principal en C (`./space_health_server`) embebe un servidor HTTP ligero (`mongoose.c/.h`) y un serializador/parser JSON (`cJSON.c/.h`), compilados directamente junto al código del proyecto sin dependencias externas ni servidores en la nube:

* **Puerto local:** `http://localhost:8080` (100% offline).
* **Archivos estáticos:** toda petición que no empiece por `/api/` sirve directamente desde `web/` (`index.html`, `app.js`, `styles.css`).
* **Comunicación:** el frontend en JavaScript usa `fetch('/api/...')` enviando y recibiendo `application/json`.

*(Plan B si deciden no usar librería HTTP en C: compilar la lógica como binario CLI `health_cli` que recibe y devuelve JSON por `stdin`/`stdout`, y levantarlo detrás de un script mínimo en `python3` con `http.server` de ~40 líneas).*

### Contrato de Endpoints REST (API JSON)

| Método y Ruta | Propósito | Cuerpo Request (JSON) | Respuesta (JSON) |
| :--- | :--- | :--- | :--- |
| `GET /api/defaults` | Obtener valores promedio de `defaults.json` | — | Objeto `AstronautBaseline` + referencias NASA |
| `GET /api/missions` | Listar misiones disponibles | — | Array resumido de misiones (`id`, `nombre`, `destino`, `dia_actual`) |
| `GET /api/missions/:id` | Estado completo de la misión y resumen de su tripulación | — | `Mission`, `CabinEnv`, `autonomous_mode`, lista de astronautas con `global_status` |
| `PUT /api/missions/:id/env` | Actualizar entorno de cabina o latencia desde la web | Campos de `CabinEnv` (`po2_kpa`, `pco2_kpa`, `latencia_com_seg`, etc.) | `CabinEnv` actualizado + tripulación re-evaluada |
| `POST /api/missions/:id/simulate` | Inyectar escenario de prueba para demo | `{"scenario": "SPE_STORM" \| "HYPOXIA" \| "HYPERCAPNIA" \| "SANS_SLEEP"}` | Estado actualizado de misión y alertas disparadas |
| `GET /api/astronauts/:id` | Detalle completo de un astronauta | — | Perfil, `baseline`, array de `history`, `EvaluationReport` (alertas + contramedidas) |
| `POST /api/astronauts` | Crear astronauta nuevo (usa `defaults.json` si faltan campos de baseline) | `id`, `nombre`, `rol`, `mission_id`, `baseline` opcional | Objeto `Astronaut` creado y persistido en `<id>.json` |
| `POST /api/astronauts/:id/samples` | Registrar nueva toma de datos / check-in desde la web | Objeto `DailyHealthSample` | `201 Created` + nuevo `EvaluationReport` y estado actualizado |

### Flujo de ejecución por cada actualización en la página web
1. El usuario (o el simulador de sensores en JS) envía un `POST /api/astronauts/astro_01/samples` con JSON.
2. **`server.c`** parsea el JSON con `cJSON` hacia un `DailyHealthSample`.
3. **`astronaut.c`** ejecuta `validate_sample()`. Si algún dato es inválido, devuelve `400 Bad Request` con el motivo.
4. **`astronaut.c`** inserta la muestra en el `SampleRingBuffer` de `astro_01` y actualiza `radiacion_mision_msv`.
5. **`health_engine.c`** corre `evaluate_astronaut(astro, mission->env)` generando el nuevo `EvaluationReport` (severidades, riesgos NASA y contramedidas).
6. **`storage.c`** guarda atómicamente `data/astronauts/astro_01.json`.
7. **`server.c`** serializa el `EvaluationReport` + resumen actualizado a JSON y responde `200 OK` al navegador, que actualiza gráficos, alertas y semáforos al instante.

#ifndef HEALTH_TYPES_H
#define HEALTH_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Constantes NASA y limites de memoria */
#define NASA_CAREER_RAD_LIMIT_MSV 600.0f
#define MAX_HISTORY_SAMPLES       90
#define MAX_CREW_PER_MISSION      8
#define MAX_MISSIONS              8
#define MAX_ASTRONAUTS            32
#define MAX_ALERTS                16
#define BODY_SYSTEM_COUNT         7
#define NASA_HAZARD_COUNT         5

/* Codigos de retorno / validacion */
typedef enum {
    OK                = 0,
    ERR_INVALID_RANGE = -1,
    ERR_INVALID_ID    = -2,
    ERR_NOT_FOUND     = -3,
    ERR_LIMIT_REACHED = -4,
    ERR_IO            = -5,
    ERR_PARSE         = -6
} StatusCode;

/* Severidad de alerta (semaforo) */
typedef enum {
    ALERT_OK       = 0,
    ALERT_CAUTION  = 1,
    ALERT_CRITICAL = 2
} AlertSeverity;

/* Aptitud operativa para EVA y pilotaje manual */
typedef enum {
    APT_GO         = 0,
    APT_RESTRICTED = 1,
    APT_NO_GO      = 2
} OperationalAptitude;

/* Los 5 Peligros de NASA HRP */
typedef enum {
    HAZARD_RADIATION  = 0,
    HAZARD_ISOLATION  = 1,
    HAZARD_DISTANCE   = 2,
    HAZARD_GRAVITY    = 3,
    HAZARD_CLOSED_ENV = 4
} NasaHazard;

/* Los 7 Sistemas Corporales para el mapa anatomico en UI */
typedef enum {
    SYS_CARDIOVASCULAR  = 0,
    SYS_NEURO_OCULAR    = 1,
    SYS_RESPIRATORY_EVA = 2,
    SYS_MUSCULOSKELETAL = 3,
    SYS_IMMUNE          = 4,
    SYS_BEHAVIORAL      = 5,
    SYS_RADIATION       = 6
} BodySystem;

/* Regimen gravitatorio actual */
typedef enum {
    GRAV_MICRO   = 0, /* 0g transito / ISS */
    GRAV_MOON    = 1, /* 1/6g superficie lunar */
    GRAV_MARS    = 2, /* 3/8g superficie marciana */
    GRAV_EARTH   = 3, /* 1g terrestre */
    GRAV_DYNAMIC = 4  /* Cargas dinamicas: lanzamiento / reentrada / descenso */
} GravityMode;

/* Fase del ciclo hormonal / menstrual (para tripulacion femenina) */
typedef enum {
    CYCLE_NA         = 0, /* No aplica (Masculino) o no registrado */
    CYCLE_MENSTRUAL  = 1, /* Fase menstrual (sangrado activo, dias 1-5) */
    CYCLE_FOLLICULAR = 2, /* Fase folicular (estrogenica, dias 6-13) */
    CYCLE_OVULATORY  = 3, /* Fase ovulatoria (pico LH/estrogeno, dias 14-16) */
    CYCLE_LUTEAL     = 4, /* Fase lutea / premenstrual (progesterona alta, dias 17-28) */
    CYCLE_SUPPRESSED = 5  /* Supresion menstrual farmacologica continua (ACO / DIU-LNG) */
} HormonalCyclePhase;

/* Bitmask de sintomas clinicos y contramedidas activas */
typedef enum {
    SYM_NONE                  = 0,
    SYM_RASH_ALLERGY          = (1 << 0), /* Hipersensibilidad / sarpullido */
    SYM_VIRAL_REACTIVATION    = (1 << 1), /* Reactivacion viral latente / inmunidad */
    SYM_INFECTION_SIGNS       = (1 << 2), /* Sospecha de infeccion */
    SYM_EVA_JOINT_PAIN        = (1 << 3), /* Dolor articular post-EVA (DCS / lesion) */
    SYM_ORTHOSTATIC_DIZZINESS = (1 << 4), /* Mareo ortostatico al incorporarse */
    SYM_THIGH_CUFF_ACTIVE     = (1 << 5), /* Contramedida SANS Thigh Cuff activa */
    SYM_B_COMPLEX_TAKEN       = (1 << 6), /* Suplementacion B-Complex tomada */
    SYM_VIT_D_TAKEN           = (1 << 7), /* Suplementacion Vitamina D tomada */
    SYM_VEGGIE_FRESH_FOOD     = (1 << 8)  /* Consumo de vegetales frescos / soporte Veggie */
} SymptomFlags;

/* 1. Linea base individual del astronauta (alineado 1:1 con data/defaults.json) */
typedef struct {
    /* Signos vitales base */
    float fc_base;                  /* lpm (default 70.0) */
    float pa_sis_base;              /* mmHg (default 120.0) */
    float pa_dia_base;              /* mmHg (default 80.0) */
    float spo2_base;                /* % (default 98.0) */
    float temp_base;                /* C (default 36.6) */
    float glucosa_base;             /* mg/dL (default 90.0) */
    float peso_base;                /* kg (default 72.0) */

    /* Sueno, cognicion y conducta base */
    float sueno_base_h;             /* h/dia (default 7.5) */
    int   animo_base;               /* 1-10 (default 8) */
    int   estres_base;              /* 1-10 (default 3) */
    float cognitivo_base_ms;        /* ms (default 260.0) */

    /* Metas de nutricion y contramedidas */
    int   calorias_objetivo_kcal;   /* kcal/dia (default 2500) */
    int   hidratacion_objetivo_ml;  /* ml/dia (default 2500) */
    int   apetito_base;             /* 1-5 (default 4) */
    int   ejercicio_objetivo_min;   /* min/dia (default 90) */
    int   pre_resp_o2_objetivo_min; /* min (default 120) */

    /* Salud espacial base */
    float vision_base;              /* indice rel. (default 1.0) */
    float densidad_osea_base_pct;   /* % (default 100.0) */
    float dosis_diaria_nominal_msv; /* mSv/dia (default 0.5) */
} AstronautBaseline;

/* 2. Muestra diaria / Check-in de salud */
typedef struct {
    int   dia_mision;
    long  timestamp_unix;

    /* Signos vitales */
    float fc;
    float pa_sis;
    float pa_dia;
    float spo2;
    float temp_c;
    float glucosa_mg_dl;
    float peso_kg;

    /* Radiacion */
    float dosis_dia_msv;

    /* Riesgos espaciales especificos */
    float        vision_actual;
    int          dolor_cabeza;     /* 0-10 */
    int          mareo_vestibular; /* 0-10 */
    int          ejercicio_min;    /* min/dia */
    int          dia_descanso;     /* 0/1 Dia de descanso programado (descarga muscular) */
    int          tuvo_eva;         /* 0/1 */
    int          pre_resp_o2_min;  /* min pre-respiracion O2 */
    unsigned int sintomas_flags;   /* Bitmask SymptomFlags */

    /* Conductual y nutricion */
    float sueno_horas;
    int   sueno_calidad;           /* 1-5 */
    int   animo;                   /* 1-10 */
    int   estres;                  /* 1-10 */
    float cognitivo_ms;            /* ms (test reflejos) */
    int   cohesion_equipo;         /* 1-5 */
    int   apetito_comida;          /* 1-5 */
    int   calorias_kcal;           /* kcal */
    int   hidratacion_ml;          /* ml */

    /* Fisiologia hormonal y menstrual (tripulacion femenina) */
    int   fase_ciclo;              /* HormonalCyclePhase (0..5) */
    int   dia_ciclo;               /* Dia del ciclo menstrual (0..60) */
    int   colico_menstrual;        /* Dismenorrea / dolor pelvico (0..10) */
    int   flujo_menstrual;         /* 0=Ninguno, 1=Leve, 2=Moderado, 3=Abundante */
    int   suplemento_hierro;       /* 0/1 Suplementacion de Hierro / Ferritina */
} DailyHealthSample;

/* Buffer circular de muestras historicas (O(1)) */
typedef struct {
    DailyHealthSample items[MAX_HISTORY_SAMPLES];
    int head;  /* Indice donde se escribira la proxima muestra */
    int count; /* Cantidad actual de muestras validas (0..MAX_HISTORY_SAMPLES) */
} SampleRingBuffer;

/* 6. Alerta medica individual y Reporte de Evaluacion */
typedef struct {
    char          id_regla[32];
    NasaHazard    hazard;
    BodySystem    system;
    AlertSeverity severity;
    char          referencia_nasa[64];
    char          titulo[64];
    char          explicacion[256];
    char          contramedida[384];
} HealthAlert;

typedef struct {
    AlertSeverity       global_status;
    int                 health_score; /* 0..100 */
    OperationalAptitude eva_aptitude;
    char                resumen_estado[96];
    AlertSeverity       system_status[BODY_SYSTEM_COUNT];
    HealthAlert         alerts[MAX_ALERTS];
    int                 alert_count;
    int                 critical_alert_count;
    int                 is_very_critical; /* 1 si global_status==CRITICAL y (health_score<=50 o >=2 alertas criticas) */
    int                 autonomous_mode; /* 1 si latencia_com_seg > 5.0f */
} EvaluationReport;

/* 3. Perfil completo de Astronauta */
typedef struct {
    char              id[16];
    char              nombre[64];
    char              rol[32];
    char              sexo[16];     /* "F" (Femenino) o "M" (Masculino) */
    char              mission_id[16];
    char              avatar[256];
    float             radiacion_previa_msv;
    float             radiacion_mision_msv;
    AstronautBaseline baseline;
    SampleRingBuffer  history;
    EvaluationReport  report;
} Astronaut;

/* 4. Entorno de cabina de la nave / mision */
typedef struct {
    float       presion_cabina_kpa;
    float       po2_kpa;
    float       pco2_kpa;
    float       tasa_rad_msv_h;
    int         evento_spe_activo;
    GravityMode gravedad_actual;
    float       latencia_com_seg;
} CabinEnv;

/* 5. Mision espacial */
typedef struct {
    char     id[16];
    char     nombre[64];
    char     destino[32];
    int      dia_actual;
    CabinEnv env;
    char     crew_ids[MAX_CREW_PER_MISSION][16];
    int      crew_count;
} Mission;

/* 7. Resumen de Salud de la Mision (para vista PROMEDIO DE SALUD) */
typedef struct {
    int           mission_health_avg; /* 0..100 */
    int           crew_ok_count;
    int           crew_caution_count;
    int           crew_critical_count;
    int           crew_very_critical_count;
    char          priority_astronaut_id[16];
    float         fc_avg;
    float         spo2_avg;
    float         sueno_avg_h;
    float         estres_avg;
    float         rad_mision_avg_msv;
    AlertSeverity hazard_status[NASA_HAZARD_COUNT];
} MissionHealthSummary;

/* Estado global en memoria del servidor */
typedef struct {
    AstronautBaseline defaults;
    Mission           missions[MAX_MISSIONS];
    int               mission_count;
    Astronaut         astronauts[MAX_ASTRONAUTS];
    int               astronaut_count;
    char              data_dir[256];
} AppState;

#ifdef __cplusplus
}
#endif

#endif /* HEALTH_TYPES_H */

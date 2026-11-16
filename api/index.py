"""
Vercel Serverless Backend for P.U.L.S.O. (Programa Unificado para La Salud Optima).
1:1 Python standard-library port of the C11 NASA HRP / CSA clinical engine and REST API.
"""

from http.server import BaseHTTPRequestHandler
import base64
import json
import math
import os
import re
import shutil
import time
import urllib.parse

# =============================================================================
# Constants & Enums (aligned with src/health_types.h)
# =============================================================================
NASA_CAREER_RAD_LIMIT_MSV = 600.0
MAX_HISTORY_SAMPLES = 90
MAX_CREW_PER_MISSION = 8
MAX_MISSIONS = 8
MAX_ASTRONAUTS = 32
MAX_ALERTS = 16
BODY_SYSTEM_COUNT = 7
NASA_HAZARD_COUNT = 5

OK = 0
ERR_INVALID_RANGE = -1
ERR_INVALID_ID = -2
ERR_NOT_FOUND = -3
ERR_LIMIT_REACHED = -4
ERR_IO = -5
ERR_PARSE = -6

ALERT_OK = 0
ALERT_CAUTION = 1
ALERT_CRITICAL = 2

APT_GO = 0
APT_RESTRICTED = 1
APT_NO_GO = 2

HAZARD_RADIATION = 0
HAZARD_ISOLATION = 1
HAZARD_DISTANCE = 2
HAZARD_GRAVITY = 3
HAZARD_CLOSED_ENV = 4

SYS_CARDIOVASCULAR = 0
SYS_NEURO_OCULAR = 1
SYS_RESPIRATORY_EVA = 2
SYS_MUSCULOSKELETAL = 3
SYS_IMMUNE = 4
SYS_BEHAVIORAL = 5
SYS_RADIATION = 6

GRAV_MICRO = 0
GRAV_MOON = 1
GRAV_MARS = 2
GRAV_EARTH = 3
GRAV_DYNAMIC = 4

CYCLE_NA = 0
CYCLE_MENSTRUAL = 1
CYCLE_FOLLICULAR = 2
CYCLE_OVULATORY = 3
CYCLE_LUTEAL = 4
CYCLE_SUPPRESSED = 5

SYM_NONE = 0
SYM_RASH_ALLERGY = 1 << 0
SYM_VIRAL_REACTIVATION = 1 << 1
SYM_INFECTION_SIGNS = 1 << 2
SYM_EVA_JOINT_PAIN = 1 << 3
SYM_ORTHOSTATIC_DIZZINESS = 1 << 4
SYM_THIGH_CUFF_ACTIVE = 1 << 5
SYM_B_COMPLEX_TAKEN = 1 << 6
SYM_VIT_D_TAKEN = 1 << 7
SYM_VEGGIE_FRESH_FOOD = 1 << 8

ID_RE = re.compile(r"^[a-zA-Z0-9_-]{1,15}$")

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEED_DATA_DIR = os.path.join(BASE_DIR, "data")
RUNTIME_DATA_DIR = os.environ.get("PULSO_DATA_DIR", "/tmp/pulso_data")


def alert_severity_to_str(sev):
    return {ALERT_OK: "OK", ALERT_CAUTION: "CAUTION", ALERT_CRITICAL: "CRITICAL"}.get(sev, "OK")


def aptitude_to_str(apt):
    return {APT_GO: "Apto", APT_RESTRICTED: "Restringido", APT_NO_GO: "No apto"}.get(apt, "Apto")


def hazard_to_str(haz):
    return {
        HAZARD_RADIATION: "HAZARD_RADIATION",
        HAZARD_ISOLATION: "HAZARD_ISOLATION",
        HAZARD_DISTANCE: "HAZARD_DISTANCE",
        HAZARD_GRAVITY: "HAZARD_GRAVITY",
        HAZARD_CLOSED_ENV: "HAZARD_CLOSED_ENV",
    }.get(haz, "HAZARD_GRAVITY")


def body_system_to_str(sys_id):
    return {
        SYS_CARDIOVASCULAR: "SYS_CARDIOVASCULAR",
        SYS_NEURO_OCULAR: "SYS_NEURO_OCULAR",
        SYS_RESPIRATORY_EVA: "SYS_RESPIRATORY_EVA",
        SYS_MUSCULOSKELETAL: "SYS_MUSCULOSKELETAL",
        SYS_IMMUNE: "SYS_IMMUNE",
        SYS_BEHAVIORAL: "SYS_BEHAVIORAL",
        SYS_RADIATION: "SYS_RADIATION",
    }.get(sys_id, "SYS_CARDIOVASCULAR")


def gravity_mode_to_str(g):
    return {
        GRAV_MICRO: "GRAV_MICRO",
        GRAV_MOON: "GRAV_MOON",
        GRAV_MARS: "GRAV_MARS",
        GRAV_EARTH: "GRAV_EARTH",
        GRAV_DYNAMIC: "GRAV_DYNAMIC",
    }.get(g, "GRAV_MICRO")


def gravity_mode_to_es(g):
    return {
        GRAV_MICRO: "Microgravedad (0g)",
        GRAV_MOON: "Gravedad Lunar (1/6g)",
        GRAV_MARS: "Gravedad Marciana (3/8g)",
        GRAV_EARTH: "Gravedad Terrestre (1g)",
        GRAV_DYNAMIC: "Cargas Dinamicas",
    }.get(g, "Microgravedad (0g)")


def hormonal_phase_to_str(phase):
    return {
        CYCLE_NA: "CYCLE_NA",
        CYCLE_MENSTRUAL: "CYCLE_MENSTRUAL",
        CYCLE_FOLLICULAR: "CYCLE_FOLLICULAR",
        CYCLE_OVULATORY: "CYCLE_OVULATORY",
        CYCLE_LUTEAL: "CYCLE_LUTEAL",
        CYCLE_SUPPRESSED: "CYCLE_SUPPRESSED",
    }.get(phase, "CYCLE_NA")


def is_valid_id(val):
    return isinstance(val, str) and bool(ID_RE.match(val))


def is_valid_sex(sexo):
    return isinstance(sexo, str) and sexo in {
        "F", "f", "Femenino", "femenino", "Mujer", "mujer",
        "M", "m", "Masculino", "masculino", "Hombre", "hombre",
    }


def normalize_sex_str(src):
    if not isinstance(src, str) or not src:
        return "M"
    if src[0] in ("F", "f") or src in ("Mujer", "mujer"):
        return "F"
    return "M"


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def is_int_num(v):
    return is_num(v) and math.floor(v) == v


def is_valid_float(v, min_v, max_v):
    return is_num(v) and (min_v <= float(v) <= max_v)


def get_float(obj, key, current):
    if key not in obj:
        return current
    v = obj[key]
    return float(v) if is_num(v) else float("nan")


def get_int(obj, key, current):
    if key not in obj:
        return current
    v = obj[key]
    if is_int_num(v) and -100000 <= v <= 10000000:
        return int(v)
    return -999999


def get_bool_int(obj, key, current):
    if key not in obj:
        return current
    v = obj[key]
    if isinstance(v, bool):
        return 1 if v else 0
    return get_int(obj, key, current)


def get_uint(obj, key, current):
    if key not in obj:
        return current
    v = obj[key]
    if is_int_num(v) and 0 <= v <= 511:
        return int(v)
    return 0xFFFFFFFF


def get_long(obj, key, current):
    if key not in obj:
        return current
    v = obj[key]
    if is_num(v) and v >= 0:
        return int(v)
    return current


def get_str(obj, key, current):
    v = obj.get(key)
    if isinstance(v, str) and len(v) > 0:
        return v
    return current


# =============================================================================
# Defaults, Cabin, Sample, Astronaut, Mission Initializers & Validation
# =============================================================================
def baseline_init_nasa_defaults():
    return {
        "fc_base": 70.0,
        "pa_sis_base": 120.0,
        "pa_dia_base": 80.0,
        "spo2_base": 98.0,
        "temp_base": 36.6,
        "glucosa_base": 90.0,
        "peso_base": 72.0,
        "sueno_base_h": 7.5,
        "animo_base": 8,
        "estres_base": 3,
        "cognitivo_base_ms": 260.0,
        "calorias_objetivo_kcal": 2500,
        "hidratacion_objetivo_ml": 2500,
        "apetito_base": 4,
        "vision_base": 1.0,
        "densidad_osea_base_pct": 100.0,
        "ejercicio_objetivo_min": 90,
        "pre_resp_o2_objetivo_min": 120,
        "dosis_diaria_nominal_msv": 0.5,
    }


def baseline_from_json(b, obj):
    if not isinstance(obj, dict):
        return
    b["fc_base"] = get_float(obj, "fc_base", b["fc_base"])
    b["pa_sis_base"] = get_float(obj, "pa_sis_base", b["pa_sis_base"])
    b["pa_dia_base"] = get_float(obj, "pa_dia_base", b["pa_dia_base"])
    b["spo2_base"] = get_float(obj, "spo2_base", b["spo2_base"])
    b["temp_base"] = get_float(obj, "temp_base", b["temp_base"])
    b["glucosa_base"] = get_float(obj, "glucosa_base", b["glucosa_base"])
    b["peso_base"] = get_float(obj, "peso_base", b["peso_base"])
    b["sueno_base_h"] = get_float(obj, "sueno_base_h", b["sueno_base_h"])
    b["animo_base"] = get_int(obj, "animo_base", b["animo_base"])
    b["estres_base"] = get_int(obj, "estres_base", b["estres_base"])
    b["cognitivo_base_ms"] = get_float(obj, "cognitivo_base_ms", b["cognitivo_base_ms"])
    b["calorias_objetivo_kcal"] = get_int(obj, "calorias_objetivo_kcal", b["calorias_objetivo_kcal"])
    b["hidratacion_objetivo_ml"] = get_int(obj, "hidratacion_objetivo_ml", b["hidratacion_objetivo_ml"])
    b["apetito_base"] = get_int(obj, "apetito_base", b["apetito_base"])
    b["vision_base"] = get_float(obj, "vision_base", b["vision_base"])
    b["densidad_osea_base_pct"] = get_float(obj, "densidad_osea_base_pct", b["densidad_osea_base_pct"])
    b["ejercicio_objetivo_min"] = get_int(obj, "ejercicio_objetivo_min", b["ejercicio_objetivo_min"])
    b["pre_resp_o2_objetivo_min"] = get_int(obj, "pre_resp_o2_objetivo_min", b["pre_resp_o2_objetivo_min"])
    b["dosis_diaria_nominal_msv"] = get_float(obj, "dosis_diaria_nominal_msv", b["dosis_diaria_nominal_msv"])


def baseline_to_json(b, include_rad_limit=False):
    out = dict(b)
    if include_rad_limit:
        out["NASA_CAREER_RAD_LIMIT_MSV"] = NASA_CAREER_RAD_LIMIT_MSV
    return out


def validate_baseline(b):
    if not is_valid_float(b["fc_base"], 30.0, 200.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(b["pa_sis_base"], 60.0, 220.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(b["pa_dia_base"], 35.0, 140.0):
        return ERR_INVALID_RANGE
    if b["pa_dia_base"] >= b["pa_sis_base"]:
        return ERR_INVALID_RANGE
    if not is_valid_float(b["spo2_base"], 70.0, 100.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(b["temp_base"], 34.0, 40.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(b["glucosa_base"], 40.0, 300.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(b["peso_base"], 25.0, 250.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(b["sueno_base_h"], 1.0, 16.0):
        return ERR_INVALID_RANGE
    if not (1 <= b["animo_base"] <= 10):
        return ERR_INVALID_RANGE
    if not (1 <= b["estres_base"] <= 10):
        return ERR_INVALID_RANGE
    if not is_valid_float(b["cognitivo_base_ms"], 80.0, 2000.0):
        return ERR_INVALID_RANGE
    if not (500 <= b["calorias_objetivo_kcal"] <= 8000):
        return ERR_INVALID_RANGE
    if not (500 <= b["hidratacion_objetivo_ml"] <= 8000):
        return ERR_INVALID_RANGE
    if not (1 <= b["apetito_base"] <= 5):
        return ERR_INVALID_RANGE
    if not (0 <= b["ejercicio_objetivo_min"] <= 600):
        return ERR_INVALID_RANGE
    if not (0 <= b["pre_resp_o2_objetivo_min"] <= 600):
        return ERR_INVALID_RANGE
    if not is_valid_float(b["vision_base"], 0.1, 2.5):
        return ERR_INVALID_RANGE
    if not is_valid_float(b["densidad_osea_base_pct"], 20.0, 150.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(b["dosis_diaria_nominal_msv"], 0.0, 100.0):
        return ERR_INVALID_RANGE
    return OK


def cabin_init_nominal():
    return {
        "presion_cabina_kpa": 101.3,
        "po2_kpa": 21.3,
        "pco2_kpa": 0.35,
        "tasa_rad_msv_h": 0.02,
        "evento_spe_activo": 0,
        "gravedad_actual": GRAV_MICRO,
        "latencia_com_seg": 1.3,
    }


def cabin_from_json_partial(env, obj):
    if not isinstance(obj, dict):
        return
    env["presion_cabina_kpa"] = get_float(obj, "presion_cabina_kpa", env["presion_cabina_kpa"])
    env["po2_kpa"] = get_float(obj, "po2_kpa", env["po2_kpa"])
    env["pco2_kpa"] = get_float(obj, "pco2_kpa", env["pco2_kpa"])
    env["tasa_rad_msv_h"] = get_float(obj, "tasa_rad_msv_h", env["tasa_rad_msv_h"])
    env["evento_spe_activo"] = get_bool_int(obj, "evento_spe_activo", env["evento_spe_activo"])
    env["latencia_com_seg"] = get_float(obj, "latencia_com_seg", env["latencia_com_seg"])
    if "gravedad_actual" in obj:
        g = obj["gravedad_actual"]
        if is_int_num(g):
            env["gravedad_actual"] = int(g)
        elif isinstance(g, str):
            env["gravedad_actual"] = {
                "GRAV_MICRO": GRAV_MICRO,
                "GRAV_MOON": GRAV_MOON,
                "GRAV_MARS": GRAV_MARS,
                "GRAV_EARTH": GRAV_EARTH,
                "GRAV_DYNAMIC": GRAV_DYNAMIC,
            }.get(g, -1)
        else:
            env["gravedad_actual"] = -1


def cabin_to_json(env):
    return {
        "presion_cabina_kpa": env["presion_cabina_kpa"],
        "po2_kpa": env["po2_kpa"],
        "pco2_kpa": env["pco2_kpa"],
        "tasa_rad_msv_h": env["tasa_rad_msv_h"],
        "evento_spe_activo": env["evento_spe_activo"],
        "gravedad_actual": int(env["gravedad_actual"]),
        "gravedad_nombre": gravity_mode_to_str(env["gravedad_actual"]),
        "latencia_com_seg": env["latencia_com_seg"],
    }


def validate_cabin(env):
    if not is_valid_float(env["presion_cabina_kpa"], 20.0, 120.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(env["po2_kpa"], 5.0, 105.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(env["pco2_kpa"], 0.0, 10.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(env["tasa_rad_msv_h"], 0.0, 500.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(env["latencia_com_seg"], 0.0, 10000.0):
        return ERR_INVALID_RANGE
    if env["evento_spe_activo"] not in (0, 1):
        return ERR_INVALID_RANGE
    if not (GRAV_MICRO <= env["gravedad_actual"] <= GRAV_DYNAMIC):
        return ERR_INVALID_RANGE
    return OK


def sample_from_baseline(b, dia_mision):
    return {
        "dia_mision": dia_mision if dia_mision > 0 else 1,
        "timestamp_unix": int(time.time()),
        "fc": b["fc_base"],
        "pa_sis": b["pa_sis_base"],
        "pa_dia": b["pa_dia_base"],
        "spo2": b["spo2_base"],
        "temp_c": b["temp_base"],
        "glucosa_mg_dl": b["glucosa_base"],
        "peso_kg": b["peso_base"],
        "dosis_dia_msv": b["dosis_diaria_nominal_msv"],
        "vision_actual": b["vision_base"],
        "dolor_cabeza": 0,
        "mareo_vestibular": 0,
        "ejercicio_min": b["ejercicio_objetivo_min"],
        "dia_descanso": 0,
        "tuvo_eva": 0,
        "pre_resp_o2_min": b["pre_resp_o2_objetivo_min"],
        "sintomas_flags": SYM_VIT_D_TAKEN,
        "sueno_horas": b["sueno_base_h"],
        "sueno_calidad": 4,
        "animo": b["animo_base"],
        "estres": b["estres_base"],
        "cognitivo_ms": b["cognitivo_base_ms"],
        "cohesion_equipo": 4,
        "apetito_comida": b["apetito_base"],
        "calorias_kcal": b["calorias_objetivo_kcal"],
        "hidratacion_ml": b["hidratacion_objetivo_ml"],
        "fase_ciclo": CYCLE_NA,
        "dia_ciclo": 0,
        "colico_menstrual": 0,
        "flujo_menstrual": 0,
        "suplemento_hierro": 0,
    }


def sample_prepare_partial_base(a, dia_mision):
    hist = a["history"]
    if dia_mision > 0 and hist:
        for item in reversed(hist):
            if item["dia_mision"] == dia_mision:
                out = dict(item)
                out["timestamp_unix"] = int(time.time())
                return out
    if hist:
        latest = hist[-1]
        out = dict(latest)
        if dia_mision > 0:
            if dia_mision != latest["dia_mision"]:
                out["tuvo_eva"] = 0
                out["dia_descanso"] = 0
            out["dia_mision"] = dia_mision
        else:
            out["dia_mision"] = latest["dia_mision"] + 1
            out["tuvo_eva"] = 0
            out["dia_descanso"] = 0
        out["timestamp_unix"] = int(time.time())
        return out
    return sample_from_baseline(a["baseline"], dia_mision if dia_mision > 0 else 1)


def sample_from_json_partial(s, obj):
    if not isinstance(obj, dict):
        return
    s["dia_mision"] = get_int(obj, "dia_mision", s["dia_mision"])
    s["timestamp_unix"] = get_long(obj, "timestamp_unix", s["timestamp_unix"])
    s["fc"] = get_float(obj, "fc", s["fc"])
    s["pa_sis"] = get_float(obj, "pa_sis", s["pa_sis"])
    s["pa_dia"] = get_float(obj, "pa_dia", s["pa_dia"])
    s["spo2"] = get_float(obj, "spo2", s["spo2"])
    s["temp_c"] = get_float(obj, "temp_c", s["temp_c"])
    s["glucosa_mg_dl"] = get_float(obj, "glucosa_mg_dl", s["glucosa_mg_dl"])
    s["peso_kg"] = get_float(obj, "peso_kg", s["peso_kg"])
    s["dosis_dia_msv"] = get_float(obj, "dosis_dia_msv", s["dosis_dia_msv"])
    s["vision_actual"] = get_float(obj, "vision_actual", s["vision_actual"])
    s["dolor_cabeza"] = get_int(obj, "dolor_cabeza", s["dolor_cabeza"])
    s["mareo_vestibular"] = get_int(obj, "mareo_vestibular", s["mareo_vestibular"])
    s["ejercicio_min"] = get_int(obj, "ejercicio_min", s["ejercicio_min"])
    s["dia_descanso"] = get_bool_int(obj, "dia_descanso", s["dia_descanso"])
    s["tuvo_eva"] = get_bool_int(obj, "tuvo_eva", s["tuvo_eva"])
    s["pre_resp_o2_min"] = get_int(obj, "pre_resp_o2_min", s["pre_resp_o2_min"])
    s["sintomas_flags"] = get_uint(obj, "sintomas_flags", s["sintomas_flags"])
    s["sueno_horas"] = get_float(obj, "sueno_horas", s["sueno_horas"])
    s["sueno_calidad"] = get_int(obj, "sueno_calidad", s["sueno_calidad"])
    s["animo"] = get_int(obj, "animo", s["animo"])
    s["estres"] = get_int(obj, "estres", s["estres"])
    s["cognitivo_ms"] = get_float(obj, "cognitivo_ms", s["cognitivo_ms"])
    s["cohesion_equipo"] = get_int(obj, "cohesion_equipo", s["cohesion_equipo"])
    s["apetito_comida"] = get_int(obj, "apetito_comida", s["apetito_comida"])
    s["calorias_kcal"] = get_int(obj, "calorias_kcal", s["calorias_kcal"])
    s["hidratacion_ml"] = get_int(obj, "hidratacion_ml", s["hidratacion_ml"])

    if "fase_ciclo" in obj:
        fc_val = obj["fase_ciclo"]
        if isinstance(fc_val, str):
            s["fase_ciclo"] = {
                "CYCLE_NA": CYCLE_NA,
                "CYCLE_MENSTRUAL": CYCLE_MENSTRUAL,
                "CYCLE_FOLLICULAR": CYCLE_FOLLICULAR,
                "CYCLE_OVULATORY": CYCLE_OVULATORY,
                "CYCLE_LUTEAL": CYCLE_LUTEAL,
                "CYCLE_SUPPRESSED": CYCLE_SUPPRESSED,
            }.get(fc_val, -1)
        else:
            s["fase_ciclo"] = get_int(obj, "fase_ciclo", s["fase_ciclo"])

    s["dia_ciclo"] = get_int(obj, "dia_ciclo", s["dia_ciclo"])
    s["colico_menstrual"] = get_int(obj, "colico_menstrual", s["colico_menstrual"])
    s["flujo_menstrual"] = get_int(obj, "flujo_menstrual", s["flujo_menstrual"])
    s["suplemento_hierro"] = get_bool_int(obj, "suplemento_hierro", s["suplemento_hierro"])


def sample_to_json(s):
    out = dict(s)
    out["fase_ciclo_str"] = hormonal_phase_to_str(s["fase_ciclo"])
    return out


def validate_sample(s):
    if not (1 <= s["dia_mision"] <= 10000):
        return ERR_INVALID_RANGE
    if not is_valid_float(s["temp_c"], 30.0, 43.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(s["spo2"], 40.0, 100.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(s["fc"], 25.0, 230.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(s["pa_sis"], 50.0, 250.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(s["pa_dia"], 30.0, 150.0):
        return ERR_INVALID_RANGE
    if s["pa_dia"] >= s["pa_sis"]:
        return ERR_INVALID_RANGE
    if not is_valid_float(s["sueno_horas"], 0.0, 24.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(s["glucosa_mg_dl"], 20.0, 600.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(s["peso_kg"], 20.0, 250.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(s["dosis_dia_msv"], 0.0, 5000.0):
        return ERR_INVALID_RANGE
    if not is_valid_float(s["vision_actual"], 0.05, 2.5):
        return ERR_INVALID_RANGE
    if not is_valid_float(s["cognitivo_ms"], 50.0, 5000.0):
        return ERR_INVALID_RANGE
    if not (0 <= s["dolor_cabeza"] <= 10):
        return ERR_INVALID_RANGE
    if not (0 <= s["mareo_vestibular"] <= 10):
        return ERR_INVALID_RANGE
    if not (0 <= s["ejercicio_min"] <= 600):
        return ERR_INVALID_RANGE
    if s["dia_descanso"] not in (0, 1):
        return ERR_INVALID_RANGE
    if s["tuvo_eva"] not in (0, 1):
        return ERR_INVALID_RANGE
    if not (0 <= s["pre_resp_o2_min"] <= 600):
        return ERR_INVALID_RANGE
    if not (1 <= s["sueno_calidad"] <= 5):
        return ERR_INVALID_RANGE
    if not (1 <= s["animo"] <= 10):
        return ERR_INVALID_RANGE
    if not (1 <= s["estres"] <= 10):
        return ERR_INVALID_RANGE
    if not (1 <= s["cohesion_equipo"] <= 5):
        return ERR_INVALID_RANGE
    if not (1 <= s["apetito_comida"] <= 5):
        return ERR_INVALID_RANGE
    if not (0 <= s["calorias_kcal"] <= 10000):
        return ERR_INVALID_RANGE
    if not (0 <= s["hidratacion_ml"] <= 10000):
        return ERR_INVALID_RANGE
    if not (0 <= s["sintomas_flags"] <= 0x1FF):
        return ERR_INVALID_RANGE
    if not (0 <= s["fase_ciclo"] <= 5):
        return ERR_INVALID_RANGE
    if not (0 <= s["dia_ciclo"] <= 60):
        return ERR_INVALID_RANGE
    if not (0 <= s["colico_menstrual"] <= 10):
        return ERR_INVALID_RANGE
    if not (0 <= s["flujo_menstrual"] <= 3):
        return ERR_INVALID_RANGE
    if s["suplemento_hierro"] not in (0, 1):
        return ERR_INVALID_RANGE
    return OK


def astronaut_add_sample(a, s):
    rc = validate_sample(s)
    if rc != OK:
        return rc
    hist = a["history"]
    if hist and s["dia_mision"] > 0:
        for i in range(len(hist) - 1, -1, -1):
            if hist[i]["dia_mision"] == s["dia_mision"]:
                delta_rad = s["dosis_dia_msv"] - hist[i]["dosis_dia_msv"]
                hist[i] = dict(s)
                a["radiacion_mision_msv"] = max(0.0, a["radiacion_mision_msv"] + delta_rad)
                return OK
        if len(hist) == MAX_HISTORY_SAMPLES and s["dia_mision"] < hist[0]["dia_mision"]:
            a["radiacion_mision_msv"] += s["dosis_dia_msv"]
            return OK

    hist.append(dict(s))
    if len(hist) > MAX_HISTORY_SAMPLES:
        hist.pop(0)
    a["radiacion_mision_msv"] += s["dosis_dia_msv"]
    hist.sort(key=lambda item: item["dia_mision"])
    return OK


def astronaut_compute_window_averages(a, window_days):
    hist = a["history"]
    b = a["baseline"]
    if not hist or window_days <= 0:
        return (
            0,
            b["sueno_base_h"],
            float(b["ejercicio_objetivo_min"]),
            float(b["calorias_objetivo_kcal"]),
            float(b["hidratacion_objetivo_ml"]),
        )
    current_day = hist[-1]["dia_mision"]
    items = []
    if current_day > 0:
        items = [s for s in hist if current_day - window_days < s["dia_mision"] <= current_day]
    if not items:
        items = hist[-window_days:]

    sum_sueno = sum(s["sueno_horas"] for s in items)
    sum_ej_all = sum(float(s["ejercicio_min"]) for s in items)
    rest_days = sum(1 for s in items if s["dia_descanso"])
    active_ej = [float(s["ejercicio_min"]) for s in items if not s["dia_descanso"]]
    sum_cal = sum(float(s["calorias_kcal"]) for s in items)
    sum_hid = sum(float(s["hidratacion_ml"]) for s in items)
    matched = len(items)

    if rest_days <= 2 and active_ej:
        ej_avg = sum(active_ej) / len(active_ej)
    elif rest_days <= 2 and not active_ej:
        ej_avg = float(b["ejercicio_objetivo_min"])
    else:
        ej_avg = sum_ej_all / matched

    return (matched, sum_sueno / matched, ej_avg, sum_cal / matched, sum_hid / matched)


def validate_custom_avatar_data_url(data_url):
    if not isinstance(data_url, str) or not data_url.startswith("data:image/"):
        return ERR_PARSE, None
    rest = data_url[11:]
    semi = rest.find(";")
    comma = rest.find(",")
    if semi < 0 or comma < 0 or semi > comma:
        return ERR_PARSE, None
    if not rest[semi:].startswith(";base64,"):
        return ERR_PARSE, None
    mime = rest[:semi]
    if mime not in ("png", "jpeg", "jpg", "webp", "gif", "svg+xml"):
        return ERR_INVALID_RANGE, None
    b64 = rest[comma + 1:]
    if len(b64) < 4 or len(b64) > 3 * 1024 * 1024:
        return ERR_INVALID_RANGE, None
    try:
        cleaned = re.sub(r"\s+", "", b64)
        decoded = base64.b64decode(cleaned, validate=True)
    except Exception:
        return ERR_PARSE, None
    if len(decoded) == 0 or len(decoded) > 2 * 1024 * 1024:
        return ERR_INVALID_RANGE, None
    return OK, data_url


# =============================================================================
# Health Evaluation Engine (9 NASA HRP / CSA Rules)
# =============================================================================
def push_alert(rep, id_regla, hazard, system, severity, referencia_nasa, titulo, explicacion, contramedida):
    if len(rep["alerts"]) >= MAX_ALERTS:
        return
    rep["alerts"].append({
        "id_regla": id_regla,
        "hazard": hazard,
        "hazard_str": hazard_to_str(hazard),
        "system": system,
        "system_str": body_system_to_str(system),
        "severity": severity,
        "severity_str": alert_severity_to_str(severity),
        "referencia_nasa": referencia_nasa,
        "titulo": titulo,
        "explicacion": explicacion,
        "contramedida": contramedida,
    })
    if 0 <= system < BODY_SYSTEM_COUNT and severity > rep["system_status"][system]:
        rep["system_status"][system] = severity
    if severity > rep["global_status"]:
        rep["global_status"] = severity


def evaluate_astronaut(a, env_in=None):
    env = env_in if env_in is not None else cabin_init_nominal()
    rep = {
        "global_status": ALERT_OK,
        "health_score": 100,
        "eva_aptitude": APT_GO,
        "resumen_estado": "Normal",
        "system_status": [ALERT_OK] * BODY_SYSTEM_COUNT,
        "alerts": [],
        "alert_count": 0,
        "critical_alert_count": 0,
        "is_very_critical": 0,
        "autonomous_mode": 1 if env["latencia_com_seg"] > 5.0 else 0,
    }
    b = a["baseline"]
    s = a["history"][-1] if a["history"] else sample_from_baseline(b, 1)

    _, sueno_3d, _, _, hid_3d = astronaut_compute_window_averages(a, 3)
    _, _, ej_7d, cal_7d, _ = astronaut_compute_window_averages(a, 7)

    vestibular_no_go = 0

    # REGLA 1: Radiacion Espacial
    if env["evento_spe_activo"] == 1 or env["tasa_rad_msv_h"] >= 0.5 or s["dosis_dia_msv"] > 2.0:
        estado_spe = "activa" if env["evento_spe_activo"] else "inactiva"
        push_alert(
            rep, "RAD_SPE_SPIKE", HAZARD_RADIATION, SYS_RADIATION, ALERT_CRITICAL,
            "Radi-N2 / CANS / Riesgo por Radiación",
            "Tormenta Solar SPE / Pico Dosimetrico",
            f"Pico de radiacion detectado: tormenta solar {estado_spe}, tasa={env['tasa_rad_msv_h']:.2f} mSv/h, dosis diaria={s['dosis_dia_msv']:.2f} mSv.",
            "1) Entra cuanto antes al refugio más protegido de la nave y cancela cualquier salida al exterior (EVA). 2) Deja encendidos todo el tiempo los detectores Radi-N2 y CANS para vigilar cuánto baja la radiación. 3) Refuerza las paredes del refugio con bolsas de agua o víveres, porque frenan muy bien las partículas solares.",
        )
    elif s["dosis_dia_msv"] >= 1.0:
        push_alert(
            rep, "RAD_DAILY_ELEVATED", HAZARD_RADIATION, SYS_RADIATION, ALERT_CAUTION,
            "Radi-N2 / CANS / Riesgo por Radiación",
            "Exposicion Diaria a Radiacion Elevada",
            f"Dosis diaria elevada ({s['dosis_dia_msv']:.2f} mSv/dia vs nominal {b['dosis_diaria_nominal_msv']:.2f} mSv/dia).",
            "1) Pasa menos tiempo en los módulos de pared más fina o cerca de ventanas. 2) Revisa los lectores Radi-N2 y CANS para confirmar que no siga subiendo. 3) Cambia tus tareas hacia la zona central de la nave donde hay más protección.",
        )

    total_career_rad = a["radiacion_previa_msv"] + a["radiacion_mision_msv"]
    if total_career_rad >= NASA_CAREER_RAD_LIMIT_MSV:
        push_alert(
            rep, "RAD_CAREER_LIMIT", HAZARD_RADIATION, SYS_RADIATION, ALERT_CRITICAL,
            "Radi-N2 / CANS / Riesgo por Radiación",
            "Limite de Radiacion de Carrera Alcanzado",
            f"Dosis acumulada de carrera ({total_career_rad:.1f} mSv) alcanza/supera el limite de seguridad ({NASA_CAREER_RAD_LIMIT_MSV:.0f} mSv).",
            "1) No hagas ninguna caminata espacial nueva para no sumar más dosis. 2) Trabaja y duerme siempre en el sector mejor blindado de la nave. 3) Coordina rotación prioritaria para reducir tu exposición acumulada.",
        )
    elif total_career_rad >= 480.0:
        push_alert(
            rep, "RAD_CAREER_CAUTION", HAZARD_RADIATION, SYS_RADIATION, ALERT_CAUTION,
            "Radi-N2 / CANS / Riesgo por Radiación",
            "Acercamiento a Limite de Radiacion (80%)",
            f"Dosis acumulada de carrera ({total_career_rad:.1f} mSv) supera el 80% del limite de seguridad (480/{NASA_CAREER_RAD_LIMIT_MSV:.0f} mSv).",
            "1) Evita caminatas espaciales que no sean urgentes para guardar margen seguro. 2) Reorganiza tus turnos en áreas internas con menor nivel de radiación.",
        )

    # REGLA 2: SANS
    vision_drop = b["vision_base"] - s["vision_actual"]
    if vision_drop >= 0.25 - 1e-4:
        push_alert(
            rep, "SANS_CRITICAL", HAZARD_GRAVITY, SYS_NEURO_OCULAR, ALERT_CRITICAL,
            "Riesgo SANS / Bandas en Muslos / Presión Intracraneal",
            "Sindrome Neuro-Ocular Crítico (SANS)",
            f"Caida severa de agudeza visual ({s['vision_actual']:.2f} vs base {b['vision_base']:.2f}), cefalea {s['dolor_cabeza']}/10, PA {s['pa_sis']:.0f}/{s['pa_dia']:.0f} mmHg.",
            "1) Ponte bandas elásticas en los muslos de 2 a 4 horas durante el día para frenar que los líquidos suban a la cabeza. 2) Toma Complejo B hoy mismo con comida. 3) Hazte un chequeo de fondo de ojo y ecografía ocular con Tempus Pro.",
        )
    elif vision_drop >= 0.15 - 1e-4 and (s["dolor_cabeza"] >= 4 or s["pa_sis"] >= b["pa_sis_base"] + 20.0 - 1e-4):
        cuff_tag = " [Banda en Muslos Activa]" if (s["sintomas_flags"] & SYM_THIGH_CUFF_ACTIVE) else ""
        push_alert(
            rep, "SANS_CAUTION", HAZARD_GRAVITY, SYS_NEURO_OCULAR, ALERT_CAUTION,
            "Riesgo SANS / Bandas en Muslos / Presión Intracraneal",
            "Alerta Neuro-Ocular (SANS)",
            f"Indicios de SANS: vision {s['vision_actual']:.2f} (base {b['vision_base']:.2f}), dolor de cabeza {s['dolor_cabeza']}/10, PA sistolica {s['pa_sis']:.0f} mmHg.{cuff_tag}",
            "1) Ponte bandas en los muslos 2 a 4 horas al día (y quítalas si duermes o sientes hormigueo). 2) Toma Complejo B con una comida para apoyar los vasos sanguíneos. 3) Revisa tus ojos con el ecógrafo Tempus Pro.",
        )

    # REGLA 3: Hipoxia e Hipercapnia
    if s["spo2"] < 90.0 or env["po2_kpa"] < 15.0:
        push_alert(
            rep, "ENV_HYPOXIA_CRIT", HAZARD_CLOSED_ENV, SYS_RESPIRATORY_EVA, ALERT_CRITICAL,
            "Riesgo de Hipoxia / Tempus Pro",
            "Hipoxia Critica / Caida de O2 en Cabina",
            f"Hipoxia critica detectada: SpO2={s['spo2']:.1f}% (base {b['spo2_base']:.1f}%), pO2 cabina={env['po2_kpa']:.1f} kPa.",
            "1) Ponte mascarilla de oxígeno al 100% de inmediato para recuperar aire en sangre. 2) Quédate en reposo absoluto sin esfuerzo físico. 3) Revisa urgente las válvulas de oxígeno del sistema de soporte vital (ECLSS).",
        )
    elif s["spo2"] < 94.0 or env["po2_kpa"] < 18.0:
        push_alert(
            rep, "ENV_HYPOXIA_CAUT", HAZARD_CLOSED_ENV, SYS_RESPIRATORY_EVA, ALERT_CAUTION,
            "Riesgo de Hipoxia / Tempus Pro",
            "Precaucion por Hipoxia / Baja pO2",
            f"Saturacion u oxigeno ambiental reducido: SpO2={s['spo2']:.1f}%, pO2 cabina={env['po2_kpa']:.1f} kPa.",
            "1) Usa oxígeno extra y baja el ritmo de trabajo físico mientras se estabiliza la cabina. 2) Verifica que el flujo de oxígeno del ECLSS esté normal. 3) Vigila tu saturación continua con el monitor Tempus Pro.",
        )

    if env["pco2_kpa"] > 0.8:
        push_alert(
            rep, "ENV_HYPERCAPNIA_CRIT", HAZARD_CLOSED_ENV, SYS_RESPIRATORY_EVA, ALERT_CRITICAL,
            "Entornos Cerrados Hostiles / ECLSS",
            "Hipercapnia Critica por Acumulacion de CO2",
            f"Hipercapnia critica: pCO2={env['pco2_kpa']:.2f} kPa (>0.8 kPa), cefalea {s['dolor_cabeza']}/10, reflejos {s['cognitivo_ms']:.0f} ms.",
            "1) Ventila el módulo de inmediato y busca de dónde sale tanto CO2 estancado. 2) Si los filtros principales CDRA o CAMRAS fallan, coloca cartuchos de emergencia LiOH. 3) Usa mascarilla de oxígeno si te duele fuerte la cabeza.",
        )
    elif env["pco2_kpa"] > 0.5 and (s["dolor_cabeza"] >= 4 or s["cognitivo_ms"] > b["cognitivo_base_ms"] * 1.20):
        push_alert(
            rep, "ENV_HYPERCAPNIA_CAUT", HAZARD_CLOSED_ENV, SYS_RESPIRATORY_EVA, ALERT_CAUTION,
            "Entornos Cerrados Hostiles / ECLSS (CDRA / CAMRAS)",
            "Hipercapnia / Revisar Depurador de CO2",
            f"CO2 sobre nominal NASA (~0.40 kPa): {env['pco2_kpa']:.2f} kPa con cefalea ({s['dolor_cabeza']}/10) y reflejos {s['cognitivo_ms']:.0f} ms (base {b['cognitivo_base_ms']:.0f} ms).",
            "1) Revisa los purificadores de aire CDRA/CAMRAS y asegúrate de que los ventiladores circulen el aire (en ingravidez el CO2 se estanca). 2) Toma agua y vuelve a medir el CO2 antes de pensar que solo estás cansado.",
        )

    # REGLA 4: DCS
    if s["tuvo_eva"] == 1 and (s["pre_resp_o2_min"] < b["pre_resp_o2_objetivo_min"] or (s["sintomas_flags"] & SYM_EVA_JOINT_PAIN)):
        pain_txt = ", dolor articular reportado" if (s["sintomas_flags"] & SYM_EVA_JOINT_PAIN) else ""
        push_alert(
            rep, "EVA_DCS_RISK", HAZARD_CLOSED_ENV, SYS_RESPIRATORY_EVA, ALERT_CRITICAL,
            "Riesgo de Enfermedad Descompresiva / EVA (ISLE)",
            "Riesgo Critico de Descompresion (DCS Post-EVA)",
            f"Alerta post-EVA: pre-respiracion O2={s['pre_resp_o2_min']} min (protocolo ISLE >={b['pre_resp_o2_objetivo_min']} min; reposo 240 min){pain_txt}.",
            "1) Respira oxígeno puro al 100% ya mismo para ayudar a disolver burbujas de nitrógeno en las articulaciones. 2) Mantén quieta la zona con dolor. 3) Revisa reflejos y fuerza, y prepara recompresión en la esclusa o traje.",
        )

    # REGLA 5: Cardio, Hemostasia Venosa, Oseo, Vestibular y Cargas Dinamicas
    low_hydration = b["hidratacion_objetivo_ml"] > 0 and (
        s["hidratacion_ml"] < int(0.75 * b["hidratacion_objetivo_ml"])
        or hid_3d < 0.75 * b["hidratacion_objetivo_ml"]
    )
    ortho_dizzy = 1 if (s["sintomas_flags"] & SYM_ORTHOSTATIC_DIZZINESS) else 0

    if s["pa_sis"] < 85.0 or s["fc"] > 130.0:
        push_alert(
            rep, "GRAV_CARDIO_CRIT", HAZARD_GRAVITY, SYS_CARDIOVASCULAR, ALERT_CRITICAL,
            "Adaptaciones Cardiovasculares / Hemostasia Venosa / Tempus Pro",
            "Inestabilidad Cardiovascular Critica",
            f"Inestabilidad hemodinamica critica: FC={s['fc']:.0f} lpm (base {b['fc_base']:.0f}), PA={s['pa_sis']:.0f}/{s['pa_dia']:.0f} mmHg.",
            "1) Acuéstate boca arriba o levanta un poco las piernas para que llegue bien sangre al cerebro. 2) Toma agua con sales/electrolitos despacio pero sin demora. 3) Revisa corazón y venas con el monitor portátil Tempus Pro.",
        )
    elif (
        s["fc"] > b["fc_base"] + 25.0
        or s["fc"] > 105.0
        or s["fc"] < 45.0
        or s["pa_sis"] < 90.0
        or s["pa_sis"] > 140.0
        or ortho_dizzy
        or low_hydration
    ):
        dizzy_txt = ", mareo ortostatico" if ortho_dizzy else ""
        push_alert(
            rep, "GRAV_CARDIO_VENOUS", HAZARD_GRAVITY, SYS_CARDIOVASCULAR, ALERT_CAUTION,
            "Adaptaciones Cardiovasculares / Hemostasia Venosa / Tempus Pro",
            "Adaptacion Cardiovascular / Hemostasia Venosa",
            f"Desviacion cardio/hemostasia: FC={s['fc']:.0f} lpm, PA={s['pa_sis']:.0f}/{s['pa_dia']:.0f} mmHg, hidratacion={s['hidratacion_ml']}/{b['hidratacion_objetivo_ml']} ml (3d {hid_3d:.0f} ml){dizzy_txt}.",
            "1) Bebe entre 350 y 500 ml de agua con electrolitos para recuperar volumen de sangre. 2) Usa prenda de compresión en piernas si sientes mareo al levantarte. 3) Controla tu flujo venoso con Tempus Pro.",
        )

    if ej_7d < 45.0:
        push_alert(
            rep, "GRAV_BONE_CRIT", HAZARD_GRAVITY, SYS_MUSCULOSKELETAL, ALERT_CRITICAL,
            "ARED / CEVIS / Protocolo Zero-T2 (Sin Cinta)",
            "Riesgo Severo de Perdida Osea y Muscular",
            f"Deficit severo de acondicionamiento fisico: promedio 7d = {ej_7d:.0f} min/dia activos (meta {b['ejercicio_objetivo_min']} min).",
            "1) Retoma ya los 90 min diarios de ejercicio activo (pesas ARED, bici CEVIS o protocolo sin cinta Zero-T2) para que huesos y músculos no se debiliten. 2) Toma tu Vitamina D3 diaria con la comida principal.",
        )
    elif ej_7d < 60.0:
        push_alert(
            rep, "GRAV_BONE_CAUT", HAZARD_GRAVITY, SYS_MUSCULOSKELETAL, ALERT_CAUTION,
            "ARED / CEVIS / Protocolo Zero-T2 (Sin Cinta)",
            "Desacondicionamiento Musculoesqueletico",
            f"Promedio de ejercicio activo en 7 dias bajo el umbral protector: {ej_7d:.0f} min/dia (meta {b['ejercicio_objetivo_min']} min).",
            "1) Sube de a poco hasta completar 90 min activos/día en ARED, bici CEVIS o protocolo Zero-T2. 2) Asegúrate de tomar tu Vitamina D3 hoy para ayudar a fijar calcio en los huesos.",
        )

    if s["mareo_vestibular"] >= 5 or (
        env["gravedad_actual"] == GRAV_DYNAMIC and (s["mareo_vestibular"] >= 4 or ej_7d < 60.0)
    ):
        vestibular_no_go = 1
        push_alert(
            rep, "GRAV_VESTIBULAR_DYNAMIC", HAZARD_GRAVITY, SYS_NEURO_OCULAR, ALERT_CAUTION,
            "Sensoriomotor, Mareo Espacial (SMS) y Cargas Dinámicas",
            "Alteracion Vestibular / Mareo Espacial (No apto)",
            f"Riesgo sensoriomotor/SMS en {gravity_mode_to_es(env['gravedad_actual'])}: mareo vestibular {s['mareo_vestibular']}/10, ejercicio 7d {ej_7d:.0f} min/dia.",
            "1) Pospón pilotaje manual y caminatas EVA durante 24 a 48 horas hasta que pase el mareo. 2) Fija la vista en un punto estable, evita girar rápido la cabeza e hidrátate bien.",
        )

    # REGLA 6: Inmunidad, Microbioma y Metabolismo
    immune_flags = s["sintomas_flags"] & (SYM_RASH_ALLERGY | SYM_VIRAL_REACTIVATION | SYM_INFECTION_SIGNS)
    glucose_out = s["glucosa_mg_dl"] < 70.0 or s["glucosa_mg_dl"] > 140.0
    fever_caut = s["temp_c"] >= 37.8
    fever_crit = s["temp_c"] >= 39.0
    imm_txt = "activos" if immune_flags else "ninguno"

    if fever_crit or (fever_caut and (immune_flags != 0 or glucose_out)) or s["glucosa_mg_dl"] < 55.0 or s["glucosa_mg_dl"] > 200.0:
        push_alert(
            rep, "IMMUNE_CRITICAL", HAZARD_CLOSED_ENV, SYS_IMMUNE, ALERT_CRITICAL,
            "Biomarcadores Inmunes / Microbioma / Medidas Estándar",
            "Compromiso Inmunologico / Metabolico Critico",
            f"Respuesta inmune/metabolica critica: Temp={s['temp_c']:.1f} C, Glucosa={s['glucosa_mg_dl']:.0f} mg/dL, signos inmunes={imm_txt}.",
            "1) Quédate en cabina aparte para no contagiar al resto y guarda muestras de saliva. 2) Estabiliza el azúcar en sangre (comida o hidratación según corresponda). 3) Revisa filtros de aire HEPA y plantas del cultivo Veggie.",
        )
    elif fever_caut or immune_flags != 0 or glucose_out:
        push_alert(
            rep, "IMMUNE_CAUTION", HAZARD_CLOSED_ENV, SYS_IMMUNE, ALERT_CAUTION,
            "Biomarcadores Inmunes / Microbioma / Medidas Estándar",
            "Alerta de Inmunidad / Microbioma / Glucemia",
            f"Alteracion inmune o metabolica: Temp={s['temp_c']:.1f} C, Glucosa={s['glucosa_mg_dl']:.0f} mg/dL, signos inmunes={imm_txt}.",
            "1) Mantén distancia preventiva con el equipo y toma una muestra de saliva para control. 2) Ajusta comida o hidratación para normalizar la glucosa. 3) Revisa limpieza de superficies y ventilación.",
        )

    # REGLA 7: Aislamiento, Sueno, Cohesion y Nutricion
    if sueno_3d < 5.0 and s["estres"] >= 8:
        push_alert(
            rep, "ISO_SLEEP_CRIT", HAZARD_ISOLATION, SYS_BEHAVIORAL, ALERT_CRITICAL,
            "ARCHeR / Privación de Sueño",
            "Fatiga Cognitiva Critica y Deuda de Sueno",
            f"Deuda aguda de sueno (prom 3d {sueno_3d:.1f} h) con estres severo ({s['estres']}/10) y reflejos {s['cognitivo_ms']:.0f} ms.",
            "1) No hagas tareas delicadas ni caminatas EVA (descanso obligatorio). 2) Asegura un bloque continuo de 8 horas de sueño usando luces cálidas antes de dormir. 3) Pide apoyo del equipo para bajar tu carga.",
        )
    elif sueno_3d < 6.0 and (s["cognitivo_ms"] > b["cognitivo_base_ms"] * 1.15 or s["estres"] >= 7 or s["animo"] <= 4):
        push_alert(
            rep, "ISO_SLEEP_CAUT", HAZARD_ISOLATION, SYS_BEHAVIORAL, ALERT_CAUTION,
            "ARCHeR / Privación de Sueño",
            "Deuda de Sueno y Fatiga Conductual (ARCHeR)",
            f"Deficit de sueno 3d ({sueno_3d:.1f} h vs base {b['sueno_base_h']:.1f} h), estres {s['estres']}/10, animo {s['animo']}/10, reflejos {s['cognitivo_ms']:.0f} ms.",
            "1) Regula las luces LED del módulo para ayudar a tu reloj biológico. 2) Pasa tareas secundarias a otro día y descansa más hoy. 3) Haz una pausa breve o cuida las plantas del módulo Veggie para despejar la mente.",
        )

    if s["cohesion_equipo"] <= 2:
        push_alert(
            rep, "ISO_TEAMWORK", HAZARD_ISOLATION, SYS_BEHAVIORAL, ALERT_CAUTION,
            "Riesgo de Cohesión de Equipo / HFBP",
            "Friccion o Baja Cohesion de Equipo",
            f"Indice de cohesion de equipo bajo ({s['cohesion_equipo']}/5) reportado en el control diario.",
            "1) Hagan una charla corta y tranquila para repartir mejor las tareas del día. 2) Bajen la presión en tareas no urgentes. 3) Compartan una comida o actividad grupal distendida para aliviar tensiones.",
        )

    if (
        (b["calorias_objetivo_kcal"] > 0 and cal_7d < 0.80 * b["calorias_objetivo_kcal"])
        or s["apetito_comida"] <= 2
        or s["peso_kg"] < b["peso_base"] * 0.95
    ):
        push_alert(
            rep, "ISO_NUTRITION", HAZARD_ISOLATION, SYS_BEHAVIORAL, ALERT_CAUTION,
            "Dieta Enriquecida / Apetito Nutricional",
            "Deficit Calórico / Fatiga de Menu Espacial",
            f"Deficit nutricional: ingesta 7d {cal_7d:.0f}/{b['calorias_objetivo_kcal']} kcal, apetito {s['apetito_comida']}/5, peso {s['peso_kg']:.1f}/{b['peso_base']:.1f} kg.",
            "1) Cambia a comidas del menú enriquecido (más sabor, omega-3 y antioxidantes) para recuperar el apetito. 2) Agrega vegetales frescos del cultivo Veggie. 3) Toma agua en sorbos repartidos durante el día.",
        )

    # REGLA 9: Fisiologia Hormonal Femenina
    is_female = len(a["sexo"]) > 0 and a["sexo"][0] in ("F", "f")
    if is_female or s["fase_ciclo"] != CYCLE_NA or s["flujo_menstrual"] > 0 or s["colico_menstrual"] > 0:
        if (
            s["flujo_menstrual"] == 3
            and (s["fc"] > b["fc_base"] + 18.0 or s["pa_sis"] < 95.0 or ortho_dizzy or s["colico_menstrual"] >= 7)
        ) or s["colico_menstrual"] >= 8:
            push_alert(
                rep, "HORMONAL_MENORRHAGIA_CRIT", HAZARD_GRAVITY, SYS_CARDIOVASCULAR, ALERT_CRITICAL,
                "Medidas Estándar / Adaptaciones Cardiovasculares / EVA",
                "Menorragia / Dismenorrea Severa y Riesgo de Anemia",
                f"Menorragia/dismenorrea severa (dia {s['dia_ciclo']}, flujo {s['flujo_menstrual']}/3, colico {s['colico_menstrual']}/10, FC {s['fc']:.0f} lpm, PA {s['pa_sis']:.0f}/{s['pa_dia']:.0f} mmHg).",
                "1) Suspende cualquier caminata EVA hoy por riesgo de mareo o bajada de presión. 2) Toma suplemento de Hierro + Complejo B para reponer glóbulos rojos. 3) Bebe líquido con electrolitos y descansa.",
            )
        elif (
            s["fase_ciclo"] == CYCLE_MENSTRUAL or s["flujo_menstrual"] >= 1 or s["colico_menstrual"] >= 4
        ) and (
            s["suplemento_hierro"] == 0 or s["flujo_menstrual"] >= 2 or s["colico_menstrual"] >= 4 or low_hydration
        ):
            hierro_txt = "Activo" if s["suplemento_hierro"] else "Pendiente"
            push_alert(
                rep, "HORMONAL_MENSTRUAL_CARE", HAZARD_GRAVITY, SYS_IMMUNE, ALERT_CAUTION,
                "Medidas Estándar / Complejo B / Dieta Enriquecida",
                "Monitoreo Menstrual: Prevencion de Ferropenia y Dismenorrea",
                f"Fase menstrual activa (dia {s['dia_ciclo']}, flujo {s['flujo_menstrual']}/3, colico {s['colico_menstrual']}/10, hierro={hierro_txt}).",
                "1) Toma Hierro + Complejo B junto a una comida y evita café o té durante 1 hora para absorberlo mejor. 2) Suma unos 350 ml extra de agua. 3) Revisa cómo sigue el flujo en tu próximo control.",
            )

        if s["fase_ciclo"] == CYCLE_LUTEAL and (
            (s["temp_c"] >= b["temp_base"] + 0.35 and s["temp_c"] < 37.8)
            or s["colico_menstrual"] >= 3
            or (s["dolor_cabeza"] >= 3 and s["sueno_horas"] < b["sueno_base_h"] - 0.8)
            or s["animo"] <= 5
        ):
            push_alert(
                rep, "HORMONAL_LUTEAL_EFFECT", HAZARD_ISOLATION, SYS_BEHAVIORAL, ALERT_CAUTION,
                "Medidas Estándar / ARCHeR / Complejo B",
                "Ajuste Hormonal Fase Lutea (Termorregulacion y Sueno)",
                f"Fase lutea (dia {s['dia_ciclo']}): Temp {s['temp_c']:.1f} C (base {b['temp_base']:.1f} C), sueno {s['sueno_horas']:.1f} h, animo {s['animo']}/10, cefalea {s['dolor_cabeza']}/10.",
                "1) Baja un poco la temperatura de tu litera (en fase lútea el cuerpo sube ~0.3–0.5 °C de forma normal, no es fiebre). 2) Refuerza Complejo B y luz suave al acostarte. 3) Chequea el CO2 si persiste dolor de cabeza.",
            )

        if s["fase_ciclo"] == CYCLE_SUPPRESSED and (
            low_hydration
            or (b["hidratacion_objetivo_ml"] > 0 and s["hidratacion_ml"] < int(0.85 * b["hidratacion_objetivo_ml"]))
            or ej_7d < 65.0
            or (s["sintomas_flags"] & SYM_VIT_D_TAKEN) == 0
        ):
            push_alert(
                rep, "HORMONAL_VENOUS_BONE", HAZARD_GRAVITY, SYS_CARDIOVASCULAR, ALERT_CAUTION,
                "Hemostasia Venosa / ARED / Protocolo Zero-T2",
                "Supresion Hormonal: Vigilancia de Hemostasia Venosa y Osea",
                f"Supresion hormonal activa con hidratacion {s['hidratacion_ml']}/{b['hidratacion_objetivo_ml']} ml o ejercicio 7d {ej_7d:.0f} min/dia.",
                "1) Toma agua hasta cubrir el 100% de tu meta diaria para mantener la sangre fluida y evitar coágulos venosos. 2) Controla el flujo del cuello con Tempus Pro. 3) Cumple tu rutina ARED/Zero-T2 y toma Vitamina D3.",
            )

    # Ordenar alertas: ALERT_CRITICAL primero, luego ALERT_CAUTION
    rep["alerts"].sort(key=lambda al: -al["severity"])
    rep["alert_count"] = len(rep["alerts"])

    # Score
    score = 100
    for al in rep["alerts"]:
        if al["severity"] == ALERT_CRITICAL:
            score -= 25
        elif al["severity"] == ALERT_CAUTION:
            score -= 10

    minor_penalty = 0
    if abs(s["fc"] - b["fc_base"]) > 10.0:
        minor_penalty += 2
    if s["sueno_horas"] < b["sueno_base_h"] - 0.8:
        minor_penalty += 2
    if s["estres"] > b["estres_base"] + 2:
        minor_penalty += 2
    if s["dolor_cabeza"] >= 2:
        minor_penalty += 2
    if s["colico_menstrual"] >= 2:
        minor_penalty += 2
    if b["hidratacion_objetivo_ml"] > 0 and s["hidratacion_ml"] < int(0.90 * b["hidratacion_objetivo_ml"]):
        minor_penalty += 2
    score -= min(minor_penalty, 10)
    score = max(0, min(100, score))
    rep["health_score"] = score

    crit_cnt = sum(1 for al in rep["alerts"] if al["severity"] == ALERT_CRITICAL)
    rep["critical_alert_count"] = crit_cnt
    rep["is_very_critical"] = 1 if (rep["global_status"] == ALERT_CRITICAL and (score <= 50 or crit_cnt >= 2)) else 0

    if (
        rep["global_status"] == ALERT_CRITICAL
        or env["evento_spe_activo"] == 1
        or s["mareo_vestibular"] >= 5
        or s["colico_menstrual"] >= 8
        or sueno_3d < 5.0
        or vestibular_no_go
    ):
        rep["eva_aptitude"] = APT_NO_GO
    elif rep["global_status"] == ALERT_CAUTION or s["colico_menstrual"] >= 4:
        rep["eva_aptitude"] = APT_RESTRICTED
    else:
        rep["eva_aptitude"] = APT_GO

    if not rep["alerts"]:
        resumen = "Normal"
    else:
        top = rep["alerts"][0]
        rid = top["id_regla"]
        if rid in ("ENV_HYPERCAPNIA_CAUT", "ENV_HYPERCAPNIA_CRIT"):
            resumen = "Dolor de cabeza · Revisar CO2"
        elif rid in ("SANS_CAUTION", "SANS_CRITICAL"):
            resumen = "Dolor de cabeza · Alerta SANS" if s["dolor_cabeza"] > 0 else "Alerta Neuro-Ocular SANS"
        elif rid == "RAD_SPE_SPIKE":
            resumen = "Alerta Radiacion SPE · Refugio"
        elif rid in ("ENV_HYPOXIA_CRIT", "ENV_HYPOXIA_CAUT"):
            resumen = "Alerta Hipoxia · Revisar O2/ECLSS"
        elif rid == "EVA_DCS_RISK":
            resumen = "Riesgo DCS Post-EVA · O2 100%"
        elif rid == "HORMONAL_MENORRHAGIA_CRIT":
            resumen = "Menorragia / Dismenorrea · No apto"
        elif rid == "HORMONAL_MENSTRUAL_CARE":
            resumen = "Ciclo Menstrual · Suplementar Hierro"
        elif rid == "HORMONAL_LUTEAL_EFFECT":
            resumen = "Fase Lútea · Ajuste Térmico/Sueño"
        elif rid == "HORMONAL_VENOUS_BONE":
            resumen = "Supresión Hormonal · Vigilar Flujo"
        else:
            resumen = top["titulo"]
    rep["resumen_estado"] = resumen

    rep["global_status_str"] = alert_severity_to_str(rep["global_status"])
    rep["eva_aptitude_str"] = aptitude_to_str(rep["eva_aptitude"])
    a["report"] = rep


def evaluate_mission_crew(state, m):
    for a in state["astronauts"]:
        if a["mission_id"] == m["id"] or a["id"] in m["crew_ids"]:
            evaluate_astronaut(a, m["env"])


def evaluate_all(state):
    missions_by_id = {m["id"]: m for m in state["missions"]}
    for a in state["astronauts"]:
        m = missions_by_id.get(a["mission_id"])
        evaluate_astronaut(a, m["env"] if m else None)


def evaluate_mission_summary(state, m):
    crew = [a for a in state["astronauts"] if a["mission_id"] == m["id"] or a["id"] in m["crew_ids"]]
    summary = {
        "mission_health_avg": 100,
        "crew_ok_count": 0,
        "crew_caution_count": 0,
        "crew_critical_count": 0,
        "crew_very_critical_count": 0,
        "priority_astronaut_id": "",
        "fc_avg": 0.0,
        "spo2_avg": 0.0,
        "sueno_avg_h": 0.0,
        "estres_avg": 0.0,
        "rad_mision_avg_msv": 0.0,
        "hazard_status": [ALERT_OK] * NASA_HAZARD_COUNT,
    }
    if not crew:
        if m["env"]["latencia_com_seg"] > 5.0:
            summary["hazard_status"][HAZARD_DISTANCE] = ALERT_CAUTION
        return summary

    sum_score = 0
    sum_fc = 0.0
    sum_spo2 = 0.0
    sum_sueno = 0.0
    sum_estres = 0.0
    sum_rad_mis = 0.0
    best_vc_score = 101
    best_vc_crits = -1

    for a in crew:
        rep = a["report"]
        sum_score += rep["health_score"]
        if rep["global_status"] == ALERT_CRITICAL:
            summary["crew_critical_count"] += 1
        elif rep["global_status"] == ALERT_CAUTION:
            summary["crew_caution_count"] += 1
        else:
            summary["crew_ok_count"] += 1

        if rep["is_very_critical"]:
            summary["crew_very_critical_count"] += 1
            if rep["health_score"] < best_vc_score or (
                rep["health_score"] == best_vc_score and rep["critical_alert_count"] > best_vc_crits
            ):
                best_vc_score = rep["health_score"]
                best_vc_crits = rep["critical_alert_count"]
                summary["priority_astronaut_id"] = a["id"]

        s = a["history"][-1] if a["history"] else None
        if s:
            sum_fc += s["fc"]
            sum_spo2 += s["spo2"]
            sum_sueno += s["sueno_horas"]
            sum_estres += float(s["estres"])
        else:
            sum_fc += a["baseline"]["fc_base"]
            sum_spo2 += a["baseline"]["spo2_base"]
            sum_sueno += a["baseline"]["sueno_base_h"]
            sum_estres += float(a["baseline"]["estres_base"])
        sum_rad_mis += a["radiacion_mision_msv"]

        for al in rep["alerts"]:
            hz = al["hazard"]
            sv = al["severity"]
            if 0 <= hz < NASA_HAZARD_COUNT and sv > summary["hazard_status"][hz]:
                summary["hazard_status"][hz] = sv

    cnt = len(crew)
    summary["mission_health_avg"] = int(round(sum_score / cnt))
    summary["fc_avg"] = sum_fc / cnt
    summary["spo2_avg"] = sum_spo2 / cnt
    summary["sueno_avg_h"] = sum_sueno / cnt
    summary["estres_avg"] = sum_estres / cnt
    summary["rad_mision_avg_msv"] = sum_rad_mis / cnt

    if m["env"]["latencia_com_seg"] > 5.0 and summary["hazard_status"][HAZARD_DISTANCE] < ALERT_CAUTION:
        summary["hazard_status"][HAZARD_DISTANCE] = ALERT_CAUTION

    return summary


# =============================================================================
# Scenario Simulator
# =============================================================================
def reset_astronaut_recent_to_nominal(a, current_day):
    if not a["history"]:
        s = sample_from_baseline(a["baseline"], current_day if current_day > 0 else 14)
        astronaut_add_sample(a, s)
        return

    fc_offsets = (-1.0, 0.0, 1.0, -1.0, 2.0)
    spo2_offsets = (0.0, 0.1, -0.1, 0.1, -0.2)
    sueno_offsets = (0.1, -0.1, 0.2, 0.0, -0.2)
    rad_offsets = (-0.01, 0.01, 0.0, -0.02, 0.02)
    estres_offsets = (-1, 0, 1, 0, -1)
    cog_offsets = (-3.0, 1.0, -2.0, 2.0, 0.0)
    ej_offsets = (2, 0, 4, -2, 3)
    cal_offsets = (20, -10, 30, 0, -20)
    hid_offsets = (30, 0, 50, -20, 20)

    seed_offset = ord(a["id"][-1]) if a["id"] else 0
    hist = a["history"]
    start_idx = max(0, len(hist) - 7)
    for i in range(start_idx, len(hist)):
        item = hist[i]
        day = item["dia_mision"]
        ts = item["timestamp_unix"]
        old_rad = item["dosis_dia_msv"]

        fresh = sample_from_baseline(a["baseline"], day)
        fresh["timestamp_unix"] = ts
        k = (day + seed_offset) % 5
        fresh["fc"] += fc_offsets[k]
        fresh["spo2"] = min(100.0, fresh["spo2"] + spo2_offsets[k])
        fresh["sueno_horas"] += sueno_offsets[k]
        fresh["dosis_dia_msv"] = max(0.0, fresh["dosis_dia_msv"] + rad_offsets[k])
        fresh["estres"] = max(1, min(10, fresh["estres"] + estres_offsets[k]))
        fresh["cognitivo_ms"] += cog_offsets[k]
        fresh["ejercicio_min"] += ej_offsets[k]
        fresh["calorias_kcal"] += cal_offsets[k]
        fresh["hidratacion_ml"] += hid_offsets[k]
        if a["sexo"] and a["sexo"][0] in ("F", "f"):
            fresh["fase_ciclo"] = CYCLE_FOLLICULAR
            fresh["dia_ciclo"] = 6 + (day % 7)
            fresh["flujo_menstrual"] = 0
            fresh["colico_menstrual"] = 0
            fresh["suplemento_hierro"] = 1

        hist[i] = fresh
        a["radiacion_mision_msv"] = max(0.0, a["radiacion_mision_msv"] + (fresh["dosis_dia_msv"] - old_rad))


def simulate_mission_scenario(state, m, scenario_name):
    valid_scenarios = {
        "NOMINAL", "SPE_STORM", "HYPOXIA", "HYPERCAPNIA",
        "SANS_SLEEP", "FATIGUE_ISOLATION", "DCS_EVA", "HORMONAL_FEMALE",
    }
    if scenario_name not in valid_scenarios:
        return ERR_INVALID_RANGE

    saved_latency = m["env"]["latencia_com_seg"]
    m["env"] = cabin_init_nominal()
    m["env"]["latencia_com_seg"] = saved_latency

    crew = [a for a in state["astronauts"] if a["mission_id"] == m["id"] or a["id"] in m["crew_ids"]]
    for a in crew:
        reset_astronaut_recent_to_nominal(a, m["dia_actual"])

    if scenario_name == "NOMINAL":
        evaluate_mission_crew(state, m)
        return OK

    if scenario_name == "SPE_STORM":
        m["env"]["evento_spe_activo"] = 1
        m["env"]["tasa_rad_msv_h"] = 1.85
        for a in crew:
            s = sample_prepare_partial_base(a, m["dia_actual"])
            s["dosis_dia_msv"] = 3.20
            s["estres"] = 6
            astronaut_add_sample(a, s)
    elif scenario_name == "HYPOXIA":
        m["env"]["po2_kpa"] = 14.4
        m["env"]["presion_cabina_kpa"] = 74.0
        for idx, a in enumerate(crew):
            s = sample_prepare_partial_base(a, m["dia_actual"])
            s["spo2"] = 88.5 if idx % 2 == 0 else 91.5
            s["fc"] = a["baseline"]["fc_base"] + 22.0
            s["cognitivo_ms"] = a["baseline"]["cognitivo_base_ms"] * 1.18
            s["dolor_cabeza"] = 5
            astronaut_add_sample(a, s)
    elif scenario_name == "HYPERCAPNIA":
        m["env"]["pco2_kpa"] = 0.72
        for a in crew:
            s = sample_prepare_partial_base(a, m["dia_actual"])
            s["dolor_cabeza"] = 6
            s["cognitivo_ms"] = a["baseline"]["cognitivo_base_ms"] * 1.28
            s["sueno_calidad"] = 2
            astronaut_add_sample(a, s)
    elif scenario_name == "SANS_SLEEP":
        for idx, a in enumerate(crew):
            hist = a["history"]
            for k in range(max(0, len(hist) - 3), len(hist)):
                hist[k]["sueno_horas"] = 4.7
            s = sample_prepare_partial_base(a, m["dia_actual"])
            s["vision_actual"] = a["baseline"]["vision_base"] - (0.26 if idx == 0 else 0.18)
            s["dolor_cabeza"] = 6
            s["pa_sis"] = a["baseline"]["pa_sis_base"] + 22.0
            s["sueno_horas"] = 4.6
            s["estres"] = 8
            s["cognitivo_ms"] = a["baseline"]["cognitivo_base_ms"] * 1.22
            astronaut_add_sample(a, s)
    elif scenario_name == "FATIGUE_ISOLATION":
        for a in crew:
            hist = a["history"]
            start = max(0, len(hist) - 7)
            for k in range(start, len(hist)):
                hist[k]["calorias_kcal"] = int(0.72 * a["baseline"]["calorias_objetivo_kcal"])
                if k >= len(hist) - 3:
                    hist[k]["sueno_horas"] = 5.3
            s = sample_prepare_partial_base(a, m["dia_actual"])
            s["sueno_horas"] = 5.1
            s["estres"] = 8
            s["animo"] = 3
            s["cohesion_equipo"] = 2
            s["apetito_comida"] = 2
            s["calorias_kcal"] = int(0.70 * a["baseline"]["calorias_objetivo_kcal"])
            s["cognitivo_ms"] = a["baseline"]["cognitivo_base_ms"] * 1.20
            astronaut_add_sample(a, s)
    elif scenario_name == "DCS_EVA":
        for idx, a in enumerate(crew):
            s = sample_prepare_partial_base(a, m["dia_actual"])
            if idx < 2:
                s["tuvo_eva"] = 1
                s["pre_resp_o2_min"] = 75
                s["sintomas_flags"] |= SYM_EVA_JOINT_PAIN
                s["fc"] = a["baseline"]["fc_base"] + 18.0
            astronaut_add_sample(a, s)
    elif scenario_name == "HORMONAL_FEMALE":
        fem_idx = 0
        for a in crew:
            if a["sexo"] and a["sexo"][0] in ("F", "f"):
                s = sample_prepare_partial_base(a, m["dia_actual"])
                if fem_idx == 0:
                    s["fase_ciclo"] = CYCLE_MENSTRUAL
                    s["dia_ciclo"] = 2
                    s["flujo_menstrual"] = 2
                    s["colico_menstrual"] = 5
                    s["suplemento_hierro"] = 0
                    s["fc"] = a["baseline"]["fc_base"] + 12.0
                    s["hidratacion_ml"] = int(0.82 * a["baseline"]["hidratacion_objetivo_ml"])
                else:
                    s["fase_ciclo"] = CYCLE_LUTEAL
                    s["dia_ciclo"] = 24
                    s["flujo_menstrual"] = 0
                    s["colico_menstrual"] = 4
                    s["suplemento_hierro"] = 1
                    s["temp_c"] = a["baseline"]["temp_base"] + 0.5
                    s["sueno_horas"] = a["baseline"]["sueno_base_h"] - 1.2
                    s["dolor_cabeza"] = 4
                    s["animo"] = 5
                astronaut_add_sample(a, s)
                fem_idx += 1
        if fem_idx == 0 and crew:
            a = crew[0]
            s = sample_prepare_partial_base(a, m["dia_actual"])
            s["fase_ciclo"] = CYCLE_MENSTRUAL
            s["dia_ciclo"] = 2
            s["flujo_menstrual"] = 2
            s["colico_menstrual"] = 5
            s["suplemento_hierro"] = 0
            astronaut_add_sample(a, s)

    evaluate_mission_crew(state, m)
    return OK


# =============================================================================
# JSON Parsers / Serializers & Disk Storage (/tmp/pulso_data seeded from data/)
# =============================================================================
def mission_init(mid="artemis_sim", nombre="Artemis / Transito a Marte", destino="Transito a Marte"):
    return {
        "id": mid or "artemis_sim",
        "nombre": nombre or "Artemis / Transito a Marte",
        "destino": destino or "Transito a Marte",
        "dia_actual": 14,
        "env": cabin_init_nominal(),
        "crew_ids": [],
    }


def mission_from_json(obj):
    if not isinstance(obj, dict):
        return None, ERR_PARSE
    mid = obj.get("id")
    if not is_valid_id(mid):
        return None, ERR_INVALID_ID
    if "nombre" in obj and (not isinstance(obj["nombre"], str) or not obj["nombre"]):
        return None, ERR_PARSE
    if "destino" in obj and (not isinstance(obj["destino"], str) or not obj["destino"]):
        return None, ERR_PARSE

    m = mission_init(mid, obj.get("nombre"), obj.get("destino"))
    m["dia_actual"] = get_int(obj, "dia_actual", m["dia_actual"])
    if not (1 <= m["dia_actual"] <= 10000):
        return None, ERR_INVALID_RANGE

    if "env" in obj:
        if not isinstance(obj["env"], dict):
            return None, ERR_PARSE
        cabin_from_json_partial(m["env"], obj["env"])
        vrc = validate_cabin(m["env"])
        if vrc != OK:
            return None, vrc

    if "crew_ids" in obj:
        if not isinstance(obj["crew_ids"], list):
            return None, ERR_PARSE
        for cid in obj["crew_ids"]:
            if not is_valid_id(cid):
                return None, ERR_INVALID_ID
            if cid not in m["crew_ids"]:
                if len(m["crew_ids"]) >= MAX_CREW_PER_MISSION:
                    return None, ERR_LIMIT_REACHED
                m["crew_ids"].append(cid)

    return m, OK


def mission_to_json(m):
    return {
        "id": m["id"],
        "nombre": m["nombre"],
        "destino": m["destino"],
        "dia_actual": m["dia_actual"],
        "crew_count": len(m["crew_ids"]),
        "env": cabin_to_json(m["env"]),
        "crew_ids": list(m["crew_ids"]),
    }


def astronaut_from_json(obj, defaults):
    if not isinstance(obj, dict):
        return None, ERR_PARSE
    aid = obj.get("id")
    if not is_valid_id(aid):
        return None, ERR_INVALID_ID
    if "nombre" in obj and (not isinstance(obj["nombre"], str) or not obj["nombre"]):
        return None, ERR_PARSE
    if "rol" in obj and (not isinstance(obj["rol"], str) or not obj["rol"]):
        return None, ERR_PARSE
    if "sexo" in obj and not is_valid_sex(obj["sexo"]):
        return None, ERR_INVALID_RANGE
    if "mission_id" in obj and not is_valid_id(obj["mission_id"]):
        return None, ERR_INVALID_ID
    if "avatar" in obj and (not isinstance(obj["avatar"], str) or not obj["avatar"]):
        return None, ERR_PARSE

    a = {
        "id": aid,
        "nombre": get_str(obj, "nombre", "Tripulante"),
        "rol": get_str(obj, "rol", "Especialista de Mision"),
        "sexo": normalize_sex_str(obj.get("sexo", "M")),
        "mission_id": get_str(obj, "mission_id", "artemis_sim"),
        "avatar": "assets/avatars/astro_01.svg",
        "radiacion_previa_msv": 12.0,
        "radiacion_mision_msv": 0.0,
        "baseline": dict(defaults),
        "history": [],
        "report": {},
    }
    if not is_valid_id(a["mission_id"]):
        return None, ERR_INVALID_ID

    ava = obj.get("avatar")
    if isinstance(ava, str) and ava:
        if ava.startswith("data:image/"):
            arc, validated = validate_custom_avatar_data_url(ava)
            if arc != OK:
                return None, arc
            a["avatar"] = validated
        else:
            a["avatar"] = ava

    a["radiacion_previa_msv"] = get_float(obj, "radiacion_previa_msv", a["radiacion_previa_msv"])
    if not is_valid_float(a["radiacion_previa_msv"], 0.0, 10000.0):
        return None, ERR_INVALID_RANGE

    has_rad_mision = "radiacion_mision_msv" in obj
    explicit_rad_mision = 0.0
    if has_rad_mision:
        explicit_rad_mision = get_float(obj, "radiacion_mision_msv", 0.0)
        if not is_valid_float(explicit_rad_mision, 0.0, 10000.0):
            return None, ERR_INVALID_RANGE

    if "baseline" in obj:
        if not isinstance(obj["baseline"], dict):
            return None, ERR_PARSE
        baseline_from_json(a["baseline"], obj["baseline"])
        brc = validate_baseline(a["baseline"])
        if brc != OK:
            return None, brc

    if "history" in obj:
        if not isinstance(obj["history"], list):
            return None, ERR_PARSE
        for idx, elem in enumerate(obj["history"], start=1):
            if not isinstance(elem, dict):
                return None, ERR_PARSE
            s = sample_from_baseline(a["baseline"], idx)
            sample_from_json_partial(s, elem)
            src = astronaut_add_sample(a, s)
            if src != OK:
                return None, src

    if has_rad_mision and explicit_rad_mision > a["radiacion_mision_msv"]:
        a["radiacion_mision_msv"] = explicit_rad_mision

    if not a["history"]:
        s0 = sample_from_baseline(a["baseline"], 1)
        src0 = astronaut_add_sample(a, s0)
        if src0 != OK:
            return None, src0

    return a, OK


def astronaut_summary_to_json(a):
    rep = a["report"]
    latest = a["history"][-1] if a["history"] else None
    return {
        "id": a["id"],
        "nombre": a["nombre"],
        "rol": a["rol"],
        "sexo": a["sexo"] or "M",
        "mission_id": a["mission_id"],
        "avatar": a["avatar"],
        "global_status": rep["global_status"],
        "global_status_str": rep["global_status_str"],
        "health_score": rep["health_score"],
        "eva_aptitude": rep["eva_aptitude"],
        "eva_aptitude_str": rep["eva_aptitude_str"],
        "resumen_estado": rep["resumen_estado"],
        "alert_count": rep["alert_count"],
        "critical_alert_count": rep["critical_alert_count"],
        "is_very_critical": rep["is_very_critical"],
        "fase_ciclo": int(latest["fase_ciclo"]) if latest else 0,
    }


def astronaut_to_json(a, include_report=True):
    out = {
        "id": a["id"],
        "nombre": a["nombre"],
        "rol": a["rol"],
        "sexo": a["sexo"] or "M",
        "mission_id": a["mission_id"],
        "avatar": a["avatar"],
        "radiacion_previa_msv": a["radiacion_previa_msv"],
        "radiacion_mision_msv": a["radiacion_mision_msv"],
        "radiacion_total_msv": a["radiacion_previa_msv"] + a["radiacion_mision_msv"],
        "baseline": baseline_to_json(a["baseline"], False),
        "history": [sample_to_json(s) for s in a["history"]],
    }
    if include_report:
        out["report"] = a["report"]
    return out


def build_mission_detail_json(state, m):
    out = mission_to_json(m)
    out["autonomous_mode"] = 1 if m["env"]["latencia_com_seg"] > 5.0 else 0
    summary = evaluate_mission_summary(state, m)
    out["summary"] = summary
    out["mission_health_avg"] = summary["mission_health_avg"]
    out["crew"] = [
        astronaut_summary_to_json(a)
        for a in state["astronauts"]
        if a["mission_id"] == m["id"] or a["id"] in m["crew_ids"]
    ]
    return out


def write_json_atomic(filepath, obj):
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    tmp_path = f"{filepath}.tmp"
    with open(tmp_path, "w", encoding="utf-8") as fp:
        json.dump(obj, fp, ensure_ascii=False, indent=2)
        fp.write("\n")
    os.replace(tmp_path, filepath)


def read_json_file(filepath):
    try:
        with open(filepath, "r", encoding="utf-8") as fp:
            return json.load(fp)
    except Exception:
        return None


def storage_save_mission(data_dir, m):
    write_json_atomic(os.path.join(data_dir, "missions", f"{m['id']}.json"), mission_to_json(m))


def storage_save_astronaut(data_dir, a):
    write_json_atomic(os.path.join(data_dir, "astronauts", f"{a['id']}.json"), astronaut_to_json(a, False))


def storage_delete_astronaut_file(data_dir, aid):
    p = os.path.join(data_dir, "astronauts", f"{aid}.json")
    if os.path.exists(p):
        os.remove(p)


def ensure_runtime_data_dir():
    if not os.path.exists(RUNTIME_DATA_DIR):
        if os.path.isdir(SEED_DATA_DIR):
            shutil.copytree(SEED_DATA_DIR, RUNTIME_DATA_DIR, dirs_exist_ok=True)
        else:
            os.makedirs(RUNTIME_DATA_DIR, exist_ok=True)
    return RUNTIME_DATA_DIR


def load_state():
    data_dir = ensure_runtime_data_dir()
    state = {
        "data_dir": data_dir,
        "defaults": baseline_init_nasa_defaults(),
        "missions": [],
        "astronauts": [],
    }

    def_path = os.path.join(data_dir, "defaults.json")
    def_json = read_json_file(def_path)
    if isinstance(def_json, dict):
        baseline_from_json(state["defaults"], def_json)
    else:
        write_json_atomic(def_path, baseline_to_json(state["defaults"], False))

    mis_dir = os.path.join(data_dir, "missions")
    if os.path.isdir(mis_dir):
        for name in sorted(os.listdir(mis_dir)):
            if name.endswith(".json") and len(state["missions"]) < MAX_MISSIONS:
                mj = read_json_file(os.path.join(mis_dir, name))
                m, rc = mission_from_json(mj)
                if rc == OK:
                    state["missions"].append(m)

    if not state["missions"]:
        m0 = mission_init("artemis_sim", "Artemis IV / Transito a Marte — Sol 14", "Transito a Marte")
        state["missions"].append(m0)
        storage_save_mission(data_dir, m0)

    ast_dir = os.path.join(data_dir, "astronauts")
    if os.path.isdir(ast_dir):
        for name in sorted(os.listdir(ast_dir)):
            if name.endswith(".json") and len(state["astronauts"]) < MAX_ASTRONAUTS:
                aj = read_json_file(os.path.join(ast_dir, name))
                a, rc = astronaut_from_json(aj, state["defaults"])
                if rc == OK:
                    state["astronauts"].append(a)
                    for m in state["missions"]:
                        if m["id"] == a["mission_id"] and a["id"] not in m["crew_ids"] and len(m["crew_ids"]) < MAX_CREW_PER_MISSION:
                            m["crew_ids"].append(a["id"])

    evaluate_all(state)
    return state


_CACHED_STATE = None


def get_state():
    global _CACHED_STATE
    if _CACHED_STATE is None or not os.path.exists(RUNTIME_DATA_DIR):
        _CACHED_STATE = load_state()
    return _CACHED_STATE


def find_mission(state, mid):
    return next((m for m in state["missions"] if m["id"] == mid), None)


def find_astronaut(state, aid):
    return next((a for a in state["astronauts"] if a["id"] == aid), None)


# =============================================================================
# Clinical Glossary (GET /api/glossary)
# =============================================================================
def build_glossary_json():
    def item(gid, category, code, title, severity, hazard, system_body, thresholds, summary, protocol, source_title, source_url, keywords=""):
        return {
            "id": gid,
            "category": category,
            "code": code,
            "title": title,
            "severity": severity,
            "hazard": hazard,
            "system": system_body,
            "thresholds": thresholds,
            "summary": summary,
            "protocol": protocol,
            "source_title": source_title,
            "source_url": source_url,
            "keywords": keywords,
        }

    return {
        "version": "NASA-HRP-STD-3001-2026",
        "title": "Glosario Clínico, Medidas, Siglas y Protocolos NASA HRP / CSA",
        "sources": [
            {"id": "HRP_5_HAZARDS", "agency": "NASA HRP", "title": "5 Hazards of Human Spaceflight", "url": "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/"},
            {"id": "NASA_BODY_SPACE", "agency": "NASA", "title": "The Human Body in Space", "url": "https://www.nasa.gov/humans-in-space/the-human-body-in-space/"},
            {"id": "CSA_EFFECTS", "agency": "CSA / ASC", "title": "Living in Space: Effects of Space on the Body", "url": "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp"},
            {"id": "CSA_RADIATION", "agency": "CSA / ASC", "title": "Space Radiation and Its Effects on the Human Body", "url": "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp"},
            {"id": "NASA_COUNTERMEASURES", "agency": "NASA", "title": "Exercising in Space (Countermeasures: ARED, T2, CEVIS)", "url": "https://www.nasa.gov/AtoZ/exercising-in-space/"},
            {"id": "NASA_RESEARCH", "agency": "NASA HRP", "title": "Human Research Program Architecture & Standards", "url": "https://www.nasa.gov/hrp/research/"},
        ],
        "items": [
            item("haz_radiation", "HAZARDS", "PELIGRO 1 · RADIACIÓN", "Radiación Espacial (Rayos Cósmicos y Tormentas Solares)", "CRITICAL", "RADIATION", "RADIATION",
                 "En órbita baja (ISS): ~0.4–1.0 mSv por día · Lejos de la Tierra: ~1.3 mSv por día · Aviso amarillo desde 1.0 mSv/día o 480 mSv acumulados · Alerta roja si supera 2.0 mSv/día, hay tormenta solar o llega al tope de 600 mSv en toda su carrera.",
                 "Aquí en la Tierra el campo magnético nos protege como un escudo, pero en el espacio estamos mucho más expuestos a partículas que vienen del Sol (tormentas solares SPE) y de fuera del sistema solar (rayos cósmicos GCR). Con el tiempo esta radiación puede dañar células y defensas, irritar los ojos o aumentar riesgos médicos a largo plazo.",
                 "1) Si salta alerta de tormenta solar, entra enseguida al sector más protegido de la nave y cancela cualquier caminata afuera. 2) Usa bolsas de agua o víveres alrededor del refugio porque frenan muy bien la radiación. 3) Mantén encendidos los detectores Radi-N2 y CANS para seguir la dosis en vivo.",
                 "CSA — Space Radiation and Its Effects on the Human Body", "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp", "RAD_SPE_SPIKE RAD_CAREER_LIMIT"),
            item("haz_gravity", "HAZARDS", "PELIGRO 2 · GRAVEDAD", "Cambios de Gravedad (Ingravidez 0g, Luna y Marte)", "CAUTION", "GRAVITY", "CARDIOVASCULAR",
                 "Vista (SANS): baja >= 0.15 (aviso) o >= 0.25 (alerta roja) · Ejercicio semanal: aviso si promedias menos de 60 min/día o alerta roja bajo 45 min/día · Mareo espacial: alerta si marca 5/10 o más.",
                 "Cuando no hay gravedad (0g), la sangre y los líquidos del cuerpo dejan de bajar a las piernas y se acumulan hacia la cara y la cabeza. Eso puede apretar un poco los ojos y cambiar la vista (SANS), hacer que los huesos pierdan calcio (1–1.5 % al mes si no se hace fuerza) y provocar mareos parecidos al mareo de viaje al llegar al espacio o volver a pisar suelo.",
                 "1) Haz por lo menos 90 minutos de ejercicio real al día (pesas en ARED, bicicleta CEVIS o rutina Zero-T2) para que huesos y corazón sigan fuertes. 2) Toma Vitamina D3 todos los días. 3) Si notas presión en la cabeza o vista rara, usa las bandas en los muslos y Complejo B.",
                 "NASA — The Human Body in Space", "https://www.nasa.gov/humans-in-space/the-human-body-in-space/", "GRAV_BONE_CAUT GRAV_BONE_CRIT SYM_VIT_D_TAKEN"),
            item("haz_hostile_env", "HAZARDS", "PELIGRO 3 · AIRE Y CABINA", "Vivir en una Nave Cerrada (Oxígeno, CO2 y Soporte Vital)", "CRITICAL", "HOSTILE_ENV", "RESPIRATORY",
                 "CO2 normal: <= 0.40 kPa · Aviso si pasa de 0.50 kPa con dolor de cabeza o reflejos lentos · Alerta roja si supera 0.80 kPa | Oxígeno normal (SpO2): 95–100 %, aviso bajo 94 % y alerta roja bajo 90 %.",
                 "En una nave espacial no se puede abrir la ventana, así que el sistema de aire (ECLSS) tiene que limpiar el aire todo el tiempo. Como en gravedad cero el aire caliente no sube solo, el CO2 que exhalamos puede quedar flotando en bolsas invisibles cerca de la cara, causando dolor de cabeza y pesadez mental. Para limpiarlo se usan los equipos CDRA o CAMRAS, y los cartuchos LiOH quedan guardados por si hay emergencia.",
                 "1) Si sube el CO2, revisa primero los limpiadores de aire CDRA/CAMRAS y que los ventiladores estén moviendo el aire; usa cartuchos LiOH si el sistema principal falla. 2) Si falta oxígeno o baja la saturación, ponte mascarilla de oxígeno al 100 % y descansa.",
                 "NASA HRP — 5 Hazards of Human Spaceflight", "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/", "ENV_HYPERCAPNIA ENV_HYPOXIA CDRA CAMRAS LIOH"),
            item("haz_isolation", "HAZARDS", "PELIGRO 4 · AISLAMIENTO", "Encierro, Poco Sueño y Ánimo en Misiones Largas", "CAUTION", "ISOLATION", "BEHAVIORAL",
                 "Sueño promedio de 3 días menor a 6 horas (+ reflejos lentos, estrés >= 7 o ánimo <= 4) activa aviso amarillo · Menos de 5 horas de sueño con estrés >= 8 pasa a alerta roja.",
                 "Pasar meses encerrado con el mismo equipo, lejos de casa y viendo amanecer cada 90 minutos (o sin día y noche naturales) desordena el reloj interno del cuerpo. Si duermes mal varios días seguidos baja tu concentración, te irritas más fácil, puedes perder el apetito por la comida espacial de siempre y aparecen roces en la convivencia del grupo.",
                 "1) Cuida un horario fijo de 7.5 a 8.5 horas de sueño usando luces suaves antes de dormir. 2) Si estás agotado, deja las tareas secundarias para otro día. 3) Agrega vegetales frescos del cultivo Veggie y busca momentos tranquilos para conversar con el equipo.",
                 "CSA — Effects of Space on the Body", "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp", "ISO_SLEEP_COGNITIVE ARCHER"),
            item("haz_distance", "HAZARDS", "PELIGRO 5 · DISTANCIA", "Estar Lejos de la Tierra y Atender Urgencias Sin Ayuda en Vivo", "CAUTION", "DISTANCE", "BEHAVIORAL",
                 "Órbita cercana: menos de 1 segundo de retraso · Luna: ~1.3 segundos · Viaje a Marte: de 3 a 22 minutos solo de ida · Si la señal tarda más de 5 segundos, la nave activa el Modo Autónomo.",
                 "Cuando viajas a la Luna o a Marte ya no puedes llamar al médico de la Tierra y esperar respuesta al instante, ni volver rápido en una cápsula de rescate. Por eso la tripulación y el sistema de a bordo tienen que detectar problemas solos, guiarse paso a paso con esta consola y usar equipos portátiles como Tempus Pro.",
                 "1) Cuando la señal tarde más de 5 segundos, apóyate en las alertas y pasos guiados locales de P.U.L.S.O. sin esperar confirmación terrestre. 2) Usa el monitor portátil Tempus Pro para chequeos y guarda las bitácoras para enviarlas después.",
                 "NASA HRP — Human Research Program Research", "https://www.nasa.gov/hrp/research/", "AUTONOMOUS_MODE"),
            item("prot_ared_zerot2", "PROTOCOLS", "CONTRAMEDIDA 01 · EJERCICIO", "Rutina Diaria de Ejercicio (ARED, Bici CEVIS, Zero-T2) + Vitamina D3", "OK", "GRAVITY", "MUSCULOSKELETAL",
                 "Objetivo ideal: 90 min diarios de ejercicio real (dentro del bloque de 2 a 2.5 horas en estación) · Aviso si promedias menos de 60 min/día en la semana · Alerta roja bajo 45 min/día (puedes marcar 1 o 2 días de descanso por semana sin que te baje el promedio).",
                 "Sin gravedad, los músculos y huesos creen que ya no hacen falta y empiezan a perder fuerza rápido. Para evitarlo se combinan pesas sin peso real (máquina ARED con cilindros de vacío), bicicleta fija (CEVIS) y el protocolo Zero-T2 ('sin cinta de correr'), pensado para cápsulas chicas de espacio profundo donde no entra una cinta como la T2/COLBERT de la ISS. La Vitamina D3 ayuda a que el calcio siga entrando al hueso ya que allí no tomas sol.",
                 "1) Completa tus 90 minutos activos diarios combinando fuerza (ARED/Zero-T2) y cardio (CEVIS). 2) Toma 1 dosis de Vitamina D3 con la comida principal. 3) Usa tu día de descanso programado (1 o 2 por semana) cuando sientas fatiga muscular.",
                 "NASA — Exercising in Space", "https://www.nasa.gov/AtoZ/exercising-in-space/", "GRAV_BONE_CAUT GRAV_BONE_CRIT SYM_VIT_D_TAKEN ZERO-T2"),
            item("prot_sans_cuff", "PROTOCOLS", "CONTRAMEDIDA 02 · VISTA Y CABEZA", "Cuidar la Vista en Gravedad Cero (SANS): Bandas en Muslos + Complejo B", "CAUTION", "GRAVITY", "OCULAR",
                 "Aviso amarillo si la vista baja >= 0.15 puntos junto con dolor de cabeza (>= 4/10) o presión alta · Alerta roja si la vista baja >= 0.25 puntos respecto a tu medida normal.",
                 "En el espacio, al subir más líquido hacia la cabeza, aumenta la presión detrás de los ojos y puede aplanar un poco el globo ocular o hinchar el nervio óptico (esto se llama SANS y suele volver más difícil enfocar de cerca). Las bandas elásticas en los muslos retienen parte del líquido abajo en las piernas, y el Complejo B cuida los vasos sanguíneos y nervios.",
                 "1) Ponte las bandas en la parte alta de ambos muslos durante 2 a 4 horas de día (quítatelas al dormir o si sientes hormigueo). 2) Toma Complejo B con comida. 3) Hazte un chequeo ocular con el ecógrafo Tempus Pro.",
                 "NASA — The Human Body in Space (SANS)", "https://www.nasa.gov/humans-in-space/the-human-body-in-space/", "SANS_CAUTION SANS_CRITICAL SYM_THIGH_CUFF_ACTIVE SYM_B_COMPLEX_TAKEN"),
            item("prot_storm_shelter", "PROTOCOLS", "CONTRAMEDIDA 03 · REFUGIO SOLAR", "Cómo Protegerse en el Refugio Durante una Tormenta Solar", "CRITICAL", "RADIATION", "RADIATION",
                 "Se activa apenas suena alarma de tormenta solar (SPE), cuando la radiación por hora sube de 0.5 mSv/h o la dosis del día pasa de 2.0 mSv.",
                 "Cuando el Sol lanza una llamarada fuerte (SPE), llegan partículas cargadas con mucha energía en poco tiempo. Como el metal por sí solo no frena todo, los materiales con mucho hidrógeno —como las bolsas de agua potable, la comida y los depósitos de plástico grueso— forman el mejor escudo improvisado dentro de la parte central de la nave.",
                 "1) Cancela al instante cualquier caminata espacial afuera. 2) Junta a toda la tripulación en el centro mejor blindado rodeado de bolsas de agua. 3) Sigue las lecturas de Radi-N2 y CANS hasta recibir confirmación de que bajó el pico.",
                 "CSA — Space Radiation and Its Effects on the Human Body", "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp", "RAD_SPE_SPIKE"),
            item("prot_eclss_dcs", "PROTOCOLS", "CONTRAMEDIDA 04 · AIRE Y CAMINATAS", "Limpieza de Aire y Respirar Oxígeno Antes de una Caminata Espacial", "CRITICAL", "HOSTILE_ENV", "RESPIRATORY",
                 "Aire de cabina: aviso si CO2 pasa de 0.50 kPa y alerta roja sobre 0.80 kPa, o si falta oxígeno (< 18 kPa) · Antes de salir en traje: mínimo 120 min respirando oxígeno puro con ejercicio suave (ISLE), o 240 min en reposo.",
                 "El traje espacial funciona a una presión mucho más baja que la cabina (~29.6 kPa vs 101.3 kPa), parecido a subir de golpe a una montaña altísima. Si el astronauta sale sin limpiar antes el nitrógeno disuelto en su sangre respirando oxígeno puro, ese gas forma burbujas en las articulaciones o nervios (enfermedad por descompresión o DCS, igual que les pasa a los buzos).",
                 "1) Si falla el aire en cabina, revisa CDRA/CAMRAS y usa cartuchos LiOH de reserva. 2) Si alguien sale con dolor articular o poca pre-respiración, dale oxígeno al 100 % inmediato, inmoviliza la zona y prepara recompresión.",
                 "NASA HRP — 5 Hazards of Human Spaceflight", "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/", "EVA_DCS_RISK SYM_EVA_JOINT_PAIN"),
            item("sym_immune_flags", "SYMPTOMS", "CLÍNICA 01 · DEFENSAS", "Alergias en la Piel, Virus Dormidos, Fiebre y Azúcar en Sangre", "CAUTION", "HOSTILE_ENV", "IMMUNE",
                 "Aviso amarillo si aparece sarpullido, virus latente o glucosa fuera de 70–140 mg/dL · Alerta roja si hay fiebre >= 39.0 °C, azúcar muy alta/baja o fiebre >= 37.8 °C combinada con infección.",
                 "El estrés del viaje y vivir en aire reciclado bajan un poco las defensas naturales. Por eso es común que salgan irritaciones en la piel por contacto, se reactiven virus que el cuerpo tenía dormidos (como herpes simple) o cambie el azúcar en sangre. Además, dentro de una nave cerrada cualquier infección se contagia rápido si no se aísla a tiempo.",
                 "1) Toma muestras simples de saliva para análisis y evita acercarte mucho al resto si tienes fiebre >= 37.8 °C. 2) Regula el azúcar comiendo o hidratándote bien. 3) Revisa limpieza y filtros HEPA.",
                 "NASA HRP — Human Research Program Research", "https://www.nasa.gov/hrp/research/", "SYM_RASH_ALLERGY SYM_VIRAL_REACTIVATION SYM_INFECTION_SIGNS IMMUNE_CRITICAL"),
            item("sym_eva_ortho", "SYMPTOMS", "CLÍNICA 02 · ARTICULACIONES Y MAREO", "Dolor Articular Después de Caminata EVA y Mareo al Incorporarse", "CRITICAL", "GRAVITY", "CARDIOVASCULAR",
                 "Dolor articular después de salir al espacio = alerta roja inmediata · Mareo al pararse o cambiar de postura = aviso cardiovascular (o agravante en sangrado abundante).",
                 "Si te duele un hombro, codo o rodilla justo después de una caminata espacial, puede haber burbujas de nitrógeno atrapadas (DCS) y hay que actuar rápido. En cambio, el mareo ortostático pasa porque en 0g el cuerpo achica su volumen de sangre (~15 %); al volver a sentir gravedad o perder líquido, cuesta unos segundos que llegue suficiente sangre a la cabeza.",
                 "1) Dolor post-EVA: oxígeno puro al 100 % y recompresión sin esperar. 2) Mareo al levantarse: recuéstate boca arriba, bebe 350–500 ml de agua con sales y controla pulso y presión con Tempus Pro.",
                 "CSA — Living in Space: Effects of Space on the Body", "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp", "SYM_EVA_JOINT_PAIN SYM_ORTHOSTATIC_DIZZINESS GRAV_CARDIO_VENOUS"),
            item("sym_countermeasures_bits", "SYMPTOMS", "CLÍNICA 03 · HÁBITOS DIARIOS", "Las 4 Ayudas Diarias: Bandas en Muslos, Complejo B, Vitamina D3 y Cultivo Veggie", "OK", "GRAVITY", "MUSCULOSKELETAL",
                 "Controles simples de todos los días que protegen ojos, huesos, energía y defensas sin necesidad de medicamentos complejos.",
                 "Son cuatro hábitos preventivos fáciles de seguir a bordo: las bandas en los muslos evitan presión excesiva en la cabeza; el Complejo B refuerza nervios, sangre y vista; la Vitamina D3 reemplaza la falta de luz solar para cuidar los huesos; y sumar hojas frescas del huerto Veggie aporta vitaminas vivas y mejora el ánimo frente a la comida envasada.",
                 "Mantén todos los días la Vitamina D3 y una porción fresca de Veggie cuando haya cosecha; agrega las Bandas en Muslos y el Complejo B si aparece riesgo visual (SANS) o cansancio.",
                 "NASA — Ejercitación y Nutrición Espacial", "https://www.nasa.gov/AtoZ/exercising-in-space/", "SYM_THIGH_CUFF_ACTIVE SYM_B_COMPLEX_TAKEN SYM_VIT_D_TAKEN SYM_VEGGIE_FRESH_FOOD"),
            item("fem_menstrual_microg", "HORMONAL", "SALUD FEMENINA 01 · CICLO", "Cómo Funciona el Ciclo Menstrual en el Espacio", "OK", "GRAVITY", "CARDIOVASCULAR",
                 "Etapas: No Aplica · Menstrual (días 1–5) · Folicular (días 6–13) · Ovulatoria (días 14–16) · Lútea (días 17–28) · Suprimido (si usa anticonceptivo continuo).",
                 "Aunque mucha gente duda si la menstruación cambia sin gravedad, el útero empuja el flujo normalmente gracias a sus propias contracciones musculares, igual que en la Tierra. Registrar en qué día y fase estás ayuda al sistema a no confundir cambios normales de temperatura, cansancio o cólicos con una enfermedad o infección.",
                 "Anota cada día en qué fase está la tripulante, si hubo sangrado (ninguno, leve, moderado o abundante) y si tuvo dolor cólico (0 a 10), asegurando buena hidratación e insumos.",
                 "NASA — The Human Body in Space", "https://www.nasa.gov/humans-in-space/the-human-body-in-space/", "HORMONAL_FEMALE CYCLE_MENSTRUAL CYCLE_FOLLICULAR"),
            item("fem_iron_anemia", "HORMONAL", "SALUD FEMENINA 02 · HIERRO Y ANEMIA", "Cuidado del Hierro, Menstruación Abundante y 'Anemia Espacial'", "CRITICAL", "GRAVITY", "CARDIOVASCULAR",
                 "Aviso si hay menstruación activa o cólico >= 4 sin tomar hierro o con poca agua · Alerta roja (No apto para EVA) si hay flujo abundante con pulso acelerado, presión baja (< 95 mmHg), mareo o cólico >= 8/10.",
                 "En el espacio el cuerpo destruye glóbulos rojos un poco más rápido de lo normal ('anemia espacial') y maneja menos volumen de sangre. Si a eso se suma un sangrado menstrual abundante sin reponer hierro, puede bajar rápido la energía, acelerarse el corazón y aparecer mareos peligrosos dentro de un traje espacial.",
                 "1) Tomar suplemento de Hierro + Complejo B junto con comida (sin café ni té por 1 hora) y sumar 350–500 ml de agua con sales. 2) Si el sangrado es muy abundante o el dolor llega a 8/10 o más, suspender caminatas EVA y descansar.",
                 "CSA — Effects of Space on the Body (Cardiovascular & Blood Volume)", "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp", "HORMONAL_MENSTRUAL_CARE HORMONAL_MENORRHAGIA_CRIT"),
            item("fem_luteal_thermo", "HORMONAL", "SALUD FEMENINA 03 · FASE LÚTEA", "Subida Normal de Temperatura en Fase Lútea (Días 17–28)", "CAUTION", "ISOLATION", "BEHAVIORAL",
                 "Aviso informativo en días 17–28 si la temperatura sube un poco (+0.35 °C sobre lo normal, pero por debajo de 37.8 °C) o si baja la calidad del sueño y el ánimo.",
                 "Durante la segunda mitad del ciclo (fase lútea), la progesterona sube naturalmente la temperatura del cuerpo entre +0.3 y +0.5 °C y acelera apenas la respiración al dormir. Si no se tiene en cuenta este dato hormonal, cualquiera podría asustarse pensando que tiene principio de fiebre cuando en realidad es algo totalmente fisiológico.",
                 "1) Confirmar que esté por debajo de 37.8 °C y sin signos de infección antes de pensar en fiebre. 2) Enfriar un poco la cabina para dormir mejor y cuidar la iluminación nocturna. 3) Revisar CO2 si aparece dolor de cabeza.",
                 "NASA HRP — Human Research Program Research", "https://www.nasa.gov/hrp/research/", "HORMONAL_LUTEAL_EFFECT"),
            item("fem_suppression_vte", "HORMONAL", "SALUD FEMENINA 04 · SUPRESIÓN Y CIRCULACIÓN", "Supresión del Ciclo (LARC / Pastillas), Circulación Venosa y Huesos", "CAUTION", "GRAVITY", "CARDIOVASCULAR",
                 "Aviso cuando se suprime el ciclo con anticonceptivos y además hay poca hidratación (< 85 %), menos de 65 min/día de ejercicio semanal o falta de Vitamina D3.",
                 "Muchas astronautas eligen pausar su menstruación durante misiones largas usando DIU hormonal / implantes (LARC) o pastillas combinadas (ACO/COC). Como en gravedad cero la sangre venosa del cuello circula más lento, los métodos con estrógeno piden cuidar todavía más la hidratación para evitar coágulos venosos (VTE) y mantener fuerte la masa ósea.",
                 "1) Cubrir el 100 % del agua diaria recomendada para mantener fluida la circulación. 2) Cumplir el ejercicio de fuerza ARED/Zero-T2 y Vitamina D3. 3) Chequear las venas del cuello con el ecógrafo Tempus Pro.",
                 "NASA HRP — Cardiovascular & Bone Countermeasures", "https://www.nasa.gov/hrp/research/", "HORMONAL_VENOUS_BONE"),
            item("thresh_score_aptitude", "THRESHOLDS", "UMBRAL 01 · PUNTAJE Y SALIDAS", "Cómo se Calcula el Puntaje (0–100%) y el Permiso para Caminatas EVA", "OK", "DISTANCE", "CARDIOVASCULAR",
                 "Todos arrancan con 100 puntos: cada alerta roja resta 25 pts, cada aviso amarillo resta 10 pts y los desvíos menores hasta 10 pts · Si hay alerta roja, tormenta solar, mareo >= 5, cólico >= 8 o sueño < 5 h queda 'No apto' para salir.",
                 "Para que nadie tenga que interpretar veinte números por separado bajo presión, P.U.L.S.O. resume el estado general en una nota del 0 al 100 % y en un semáforo claro: 'Apto' (puede salir a caminata espacial), 'Restringido' (trabaja adentro con precaución) o 'No apto' (reposo o atención inmediata).",
                 "Cuando un astronauta cae a 50 % o menos en estado crítico —o acumula 2 o más alertas rojas— se marca automáticamente como 'URGENCIA' y aparece un botón flotante rojo para ir directo a su ficha.",
                 "NASA HRP — 5 Hazards of Human Spaceflight", "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/", "URGENCIA APTO RESTRINGIDO NO APTO"),
            item("meas_vitals_cardio", "MEASURES", "MEDIDAS 01 · SIGNOS VITALES", "Latidos (lpm), Presión (mmHg), Oxígeno (%), Temperatura (°C) y Azúcar (mg/dL)", "OK", "GRAVITY", "CARDIOVASCULAR",
                 "Valores habituales en reposo: pulso 50–80 lpm · presión ~110–130 / 70–85 mmHg · oxígeno SpO2 >= 95 % · temperatura 36.2–37.2 °C · glucosa 70–140 mg/dL.",
                 "lpm son los latidos del corazón por minuto. mmHg mide con qué fuerza empuja la sangre en las arterias (el primer número es la máxima al latir y el segundo la mínima en reposo). SpO2 (%) indica qué porcentaje de tu sangre lleva oxígeno. °C es la temperatura corporal y mg/dL mide la cantidad de azúcar (glucosa) circulando en sangre.",
                 "No mires solo si un número 'suena alto o bajo': compáralo siempre con el valor normal que tenía ese mismo astronauta antes de despegar.",
                 "NASA — The Human Body in Space", "https://www.nasa.gov/humans-in-space/the-human-body-in-space/", "FC PA PAS PAD SPO2 TEMP GLUCOSA"),
            item("meas_vision_rad", "MEASURES", "MEDIDAS 02 · VISTA Y RADIACIÓN", "Índice Visual (1.00 = 20/20) y Unidades de Radiación (mSv y mSv/h)", "OK", "RADIATION", "OCULAR",
                 "Vista normal base: 1.00 (bajar 0.15 o más avisa riesgo SANS) · Radiación normal en estación: ~0.4–1.0 mSv/día · Tope máximo de por vida en NASA: 600 mSv acumulados.",
                 "El índice visual traduce qué tan nítido ves en escala decimal (1.00 equivale a una vista estándar 20/20; si baja a 0.75 es señal de revisar los ojos por SANS). En radiación, el milisievert (mSv) mide cuánto daño biológico potencial absorbió el cuerpo en el día o en toda la carrera, mientras que mSv/h indica qué tan intensa está la radiación en cada hora (clave durante tormentas solares).",
                 "Vigila si el número visual baja respecto al inicio del viaje y chequea diariamente que la radiación por hora no dé saltos bruscos.",
                 "CSA — Space Radiation and Its Effects on the Human Body", "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp", "AGUDEZA VISUAL SANS OCT ONSD MSV MSV_H"),
            item("meas_cabin_kpa", "MEASURES", "MEDIDAS 03 · AIRE Y GRAVEDAD", "Presión del Aire en Cabina (kPa) y Fuerza de Gravedad (g)", "OK", "HOSTILE_ENV", "RESPIRATORY",
                 "Presión total como al nivel del mar: 101.3 kPa · Oxígeno normal (pO2): ~21.0 kPa · CO2 ideal: <= 0.40 kPa (aviso si pasa 0.50) · Gravedad: 0g flotando, 0.16g Luna, 0.38g Marte, 1g Tierra.",
                 "El kilopascal (kPa) mide cuánto empuja el aire o cada gas por separado: respirar a 101.3 kPa total con ~21 kPa de oxígeno es igual que estar en una ciudad al nivel del mar. El CO2 debe quedar bajito (<= 0.40 kPa) porque si sube marea y da dolor de cabeza. En cuanto a gravedad, '1g' es el peso normal en la Tierra y '0g' es flotar en ingravidez.",
                 "Mantén el CO2 por debajo de 0.40–0.50 kPa con los limpiadores CDRA/CAMRAS y el oxígeno arriba de 18 kPa para respirar sin ahogo ni fatiga.",
                 "NASA HRP — 5 Hazards of Human Spaceflight", "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/", "KPA PO2 PCO2 GRAVEDAD"),
            item("meas_wellbeing_scales", "MEASURES", "MEDIDAS 04 · SUEÑO Y REFLEJOS", "Horas de Sueño (h), Escalas de Síntomas y Test de Reflejos (ms)", "OK", "ISOLATION", "BEHAVIORAL",
                 "Sueño sano: 7.5–8.5 h/día · Dolor, mareo o cólico: 0 (nada) a 10 (máximo) · Ánimo y estrés: escala 1–10 · Calidad de sueño, apetito y trabajo en equipo: escala 1–5 · Reflejos PVT: ~220–300 ms.",
                 "ms (milisegundos) mide qué tan rápido reacciona el cerebro cuando aparece una luz verde en el test de reflejos (1000 ms = 1 segundo): si alguien cansado sube de 250 ms a más de 300 ms, sus reflejos ya están más lentos. Las escalas del 0 al 10 miden molestias, mientras que las del 1 al 5 evalúan qué tan bien dormiste, comiste o te llevaste con el equipo (5 = excelente).",
                 "Si el tiempo en ms sube más de un 15–20 % frente a tu marca normal, descansa antes de operar controles delicados y revisa también el nivel de CO2.",
                 "CSA — Living in Space: Effects of Space on the Body", "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp", "PVT MS SUENO ESTRES ANIMO"),
            item("meas_metabolic_eva", "MEASURES", "MEDIDAS 05 · AGUA, COMIDA Y ENTRENO", "Agua (ml), Calorías (kcal), Peso (kg), Ejercicio y Pre-Respiración (min)", "OK", "GRAVITY", "MUSCULOSKELETAL",
                 "Agua diaria: ~2400–2800 ml (unos 2.5 litros) · Energía: ~2300–3100 kcal · Ejercicio real: 90 min/día · Oxígeno previo a EVA: >= 120 min (protocolo rápido ISLE).",
                 "ml (mililitros) cuenta el agua bebida en el día (1000 ml = 1 litro) para evitar mareos y cálculos renales. kcal mide la energía de las comidas para no bajar de peso sin querer. min (minutos) mide tanto los 90 minutos de ejercicio real diario como los minutos respirando oxígeno puro antes de salir en traje espacial (120 min con ejercicio suave o 240 min quieto).",
                 "Recuerda que puedes tomar hasta 1 o 2 días de descanso muscular por semana sin afectar tu promedio si cargas bien tu hidratación y comidas.",
                 "NASA — Exercising in Space", "https://www.nasa.gov/AtoZ/exercising-in-space/", "ML KCAL KG MIN PRE_RESP"),
            item("meas_ops_units", "MEASURES", "MEDIDAS 06 · DÍAS, SEÑAL Y SEMÁFORO", "Día de Misión, Retraso de Señal (s), Puntaje (%) y Niveles de Alerta", "OK", "DISTANCE", "BEHAVIORAL",
                 "Día = jornada de misión · Retraso (s) = segundos que tarda la radio en llegar a Tierra · Puntaje: 0 a 100 % · Estados: Normal, Precaución, Crítico y Urgencia.",
                 "El día de registro ordena los chequeos cronológicamente. La latencia en segundos muestra cuánto tarda en viajar un mensaje hasta el control en Tierra (desde menos de 1 segundo en órbita hasta más de 20 minutos en Marte). El semáforo traduce todo a cuatro colores simples: Verde (Normal), Amarillo (Precaución), Rojo (Crítico) y Urgencia (atención ya mismo).",
                 "Si un tripulante entra en Urgencia, pulsa el botón rojo flotante de abajo a la derecha para ir directo a ver qué necesita.",
                 "NASA HRP — Human Research Program Architecture", "https://www.nasa.gov/hrp/research/", "SOL DIA LATENCIA PUNTAJE URGENCIA"),
            item("acro_agencies_ops", "ACRONYMS", "SIGLAS 01 · AGENCIAS Y SALIDAS", "CSA / ASC · EVA · Apto / Restringido / No apto · LEO", "OK", "DISTANCE", "CARDIOVASCULAR",
                 "Nombres cortos que aparecen en documentos espaciales y permisos de caminata.",
                 "CSA / ASC: Agencia Espacial Canadiense (colabora junto a la NASA en nutrición, radiación y medicina). EVA: Actividad Extravehicular, es decir, salir afuera de la nave con traje espacial. Apto / Restringido / No apto: el semáforo de permiso médico para hacer esa salida. LEO: Órbita Terrestre Baja (donde vuela la Estación Espacial Internacional, a ~400 km de altura).",
                 "Revisa siempre la etiqueta de aptitud EVA en la ficha del astronauta antes de planificar trabajos afuera de la nave.",
                 "NASA HRP — 5 Hazards of Human Spaceflight", "https://www.nasa.gov/hrp/5-hazards-of-human-spaceflight/", "NASA HRP CSA ASC EVA LEO"),
            item("acro_env_rad", "ACRONYMS", "SIGLAS 02 · AIRE DE CABINA Y RADIACIÓN", "ECLSS · CDRA · CAMRAS · LiOH · HEPA · SPE · GCR", "OK", "HOSTILE_ENV", "RESPIRATORY",
                 "Equipos que mantienen respirable la nave y tipos de radiación del espacio.",
                 "ECLSS: conjunto de máquinas que reciclan agua y aire en la nave. CDRA (en la estación ISS) y CAMRAS (en la cápsula Orion): equipos principales que limpian el CO2 del aire automáticamente. LiOH: cartuchos de hidróxido de litio de emergencia para absorber CO2 si falla lo principal. HEPA: filtros de partículas y microbios. SPE: tormenta rápida de partículas del Sol. GCR: radiación cósmica constante de fondo.",
                 "Si falla CDRA o CAMRAS, pasa a cartuchos LiOH; si hay aviso SPE, entra al refugio blindado con bolsas de agua.",
                 "CSA — Space Radiation and Its Effects on the Human Body", "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/space-radiation-and-its-effects-on-the-human-body.asp", "ECLSS CDRA CAMRAS LIOH HEPA SPE GCR"),
            item("acro_syndromes", "ACRONYMS", "SIGLAS 03 · PROBLEMAS MÉDICOS TÍPICOS", "SANS · DCS · SMS · VTE / TEV", "OK", "GRAVITY", "OCULAR",
                 "Siglas médicas frecuentes en astronautas dentro de esta consola.",
                 "SANS: problema en los ojos y la vista causado por el líquido que sube a la cabeza en ingravidez. DCS: enfermedad por descompresión (burbujas dolorosas de nitrógeno si faltó oxígeno antes de salir al vacío). SMS: mareo espacial típico de los primeros días flotando sin gravedad. VTE / TEV: coágulo en una vena por sangre más lenta o espesa.",
                 "Si ves alguna de estas siglas en una alerta, la consola te indicará los pasos concretos para atenderla sin adivinar.",
                 "NASA — The Human Body in Space", "https://www.nasa.gov/humans-in-space/the-human-body-in-space/", "SANS DCS SMS VTE TEV"),
            item("acro_hardware_ex", "ACRONYMS", "SIGLAS 04 · MÁQUINAS DE EJERCICIO Y HUERTO", "ARED · T2/COLBERT · Zero-T2 · CEVIS · Thigh Cuff · Veggie", "OK", "GRAVITY", "MUSCULOSKELETAL",
                 "Nombres de los equipos de entrenamiento, compresión y comida fresca a bordo.",
                 "ARED: máquina avanzada de fuerza con resistencia por vacío (reemplaza a las pesas en 0g). T2 / COLBERT: cinta de correr sujeta con arnés en la ISS. Zero-T2: rutina para misiones profundas sin cinta de correr que combina ejercicios resistivos (como volante de inercia en Orion) y bici. CEVIS: bicicleta fija especial. Thigh Cuff: bandas elásticas para los muslos. Veggie: mini invernadero LED para cultivar lechuga y vegetales frescos.",
                 "Combina 90 min de ejercicio en estas máquinas con hojas frescas de Veggie y bandas en muslos si hace falta.",
                 "NASA — Exercising in Space", "https://www.nasa.gov/AtoZ/exercising-in-space/", "ARED T2 COLBERT ZERO-T2 CEVIS THIGH CUFF VEGGIE"),
            item("acro_diag_psych", "ACRONYMS", "SIGLAS 05 · INSTRUMENTOS Y PRUEBAS", "Tempus Pro · Radi-N2 · CANS · ARCHeR · HFBP · PVT · Standard Measures", "OK", "ISOLATION", "BEHAVIORAL",
                 "Aparatos portátiles médicos, sensores de radiación y tests psicológicos.",
                 "Tempus Pro: maletín médico compacto con ecógrafo y monitor de signos vitales para misiones lejos de la Tierra. Radi-N2 (tubitos de burbujas pasivos) y CANS (sensor electrónico en vivo): medidores canadienses de radiación neutrónica y cósmica. ARCHeR y HFBP: guías psicológicas y de sueño para viajes largos. PVT: test corto de velocidad de reflejos. Standard Measures: chequeo médico estándar de referencia.",
                 "Usa PVT en cada Check-In y Tempus Pro cuando una alerta pida revisar corazón, venas u ojos.",
                 "NASA HRP — Human Research Program Research", "https://www.nasa.gov/hrp/research/", "TEMPUS RADI-N2 CANS ARCHER HFBP PVT STANDARD MEASURES"),
            item("acro_hormonal_sup", "ACRONYMS", "SIGLAS 06 · SUPLEMENTOS Y SALUD FEMENINA", "LARC · ACO / COC · Fe · Vitamina D3 · Complejo B", "OK", "GRAVITY", "CARDIOVASCULAR",
                 "Siglas de métodos hormonales y vitaminas que aparecen en los controles.",
                 "LARC: método anticonceptivo de larga duración sin estrógeno (como implante o DIU hormonal) muy usado para pausar la regla en misiones largas. ACO / COC: pastillas anticonceptivas combinadas con estrógeno. Fe: símbolo químico del hierro (clave para prevenir anemia). Vitamina D3: vitamina para los huesos ante falta de sol. Complejo B: grupo de vitaminas que protegen nervios, sangre y vista.",
                 "Ajusta hierro, vitamina D3 y complejo B según sexo, etapa hormonal y rutina de ejercicio.",
                 "CSA — Effects of Space on the Body", "https://www.asc-csa.gc.ca/eng/astronauts/living-in-space/effects-of-space-on-the-body.asp", "LARC ACO COC FE VITAMINA D COMPLEJO B"),
        ],
    }


# =============================================================================
# Vercel HTTP Request Handler
# =============================================================================
class handler(BaseHTTPRequestHandler):
    def _resolve_path(self):
        parsed = urllib.parse.urlparse(self.path)
        qs = urllib.parse.parse_qs(parsed.query)
        if "__path" in qs and qs["__path"]:
            p = qs["__path"][0]
        else:
            p = parsed.path
        if not p.startswith("/"):
            p = "/" + p
        return p.rstrip("/") or "/"

    def _send_json(self, status_code, payload, extra_headers=None):
        body = (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        if extra_headers:
            for k, v in extra_headers.items():
                self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_error(self, status_code, err_code, msg):
        self._send_json(status_code, {"error": msg, "code": err_code})

    def _read_json_body(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return None
        if length <= 0:
            return None
        raw = self.rfile.read(length)
        try:
            parsed = json.loads(raw.decode("utf-8"))
        except Exception:
            return None
        return parsed if isinstance(parsed, dict) else None

    def _dispatch(self, method):
        if method == "OPTIONS":
            self.send_response(204)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.end_headers()
            return

        path = self._resolve_path()
        state = get_state()

        if path == "/api/defaults":
            if method != "GET":
                return self._send_error(405, ERR_INVALID_RANGE, "Método no permitido")
            return self._send_json(200, baseline_to_json(state["defaults"], True))

        if path == "/api/glossary":
            if method != "GET":
                return self._send_error(405, ERR_INVALID_RANGE, "Método no permitido")
            return self._send_json(200, build_glossary_json())

        if path == "/api/avatars":
            if method != "POST":
                return self._send_error(405, ERR_INVALID_RANGE, "Método no permitido")
            body = self._read_json_body()
            if body is None:
                return self._send_error(400, ERR_PARSE, "JSON invalido en el cuerpo de la peticion")
            du = body.get("data_url")
            if not isinstance(du, str):
                return self._send_error(400, ERR_PARSE, "Campo data_url requerido (data:image/...;base64,...)")
            rc, validated = validate_custom_avatar_data_url(du)
            if rc != OK:
                return self._send_error(400, rc, "Formato de imagen no soportado o base64 invalido (use PNG, JPG, WEBP, GIF o SVG <= 2MB)")
            id_hint = body.get("id") if isinstance(body.get("id"), str) else "custom"
            if is_valid_id(id_hint):
                existing_a = find_astronaut(state, id_hint)
                if existing_a:
                    existing_a["avatar"] = validated
                    storage_save_astronaut(state["data_dir"], existing_a)
            return self._send_json(201, {"avatar": validated, "status": "saved"})

        if path == "/api/missions":
            if method == "GET":
                arr = []
                for m in state["missions"]:
                    s = evaluate_mission_summary(state, m)
                    mo = mission_to_json(m)
                    mo["autonomous_mode"] = 1 if m["env"]["latencia_com_seg"] > 5.0 else 0
                    mo["mission_health_avg"] = s["mission_health_avg"]
                    arr.append(mo)
                return self._send_json(200, arr)
            if method == "POST":
                body = self._read_json_body()
                if body is None:
                    return self._send_error(400, ERR_PARSE, "JSON invalido en el cuerpo de la peticion")
                new_m, rc = mission_from_json(body)
                if rc != OK:
                    return self._send_error(400, rc, "ID de mision o parametros invalidos")
                existing = find_mission(state, new_m["id"])
                if existing:
                    if not new_m["crew_ids"] and existing["crew_ids"]:
                        new_m["crew_ids"] = list(existing["crew_ids"])
                    existing.update(new_m)
                    evaluate_mission_crew(state, existing)
                    storage_save_mission(state["data_dir"], existing)
                    return self._send_json(201, build_mission_detail_json(state, existing))
                if len(state["missions"]) >= MAX_MISSIONS:
                    return self._send_error(422, ERR_LIMIT_REACHED, "Limite maximo de misiones alcanzado")
                state["missions"].append(new_m)
                evaluate_mission_crew(state, new_m)
                storage_save_mission(state["data_dir"], new_m)
                return self._send_json(201, build_mission_detail_json(state, new_m))
            return self._send_error(405, ERR_INVALID_RANGE, "Método no permitido")

        m_env = re.match(r"^/api/missions/([^/]+)/env$", path)
        if m_env:
            if method not in ("PUT", "POST"):
                return self._send_error(405, ERR_INVALID_RANGE, "Método no permitido")
            mid = urllib.parse.unquote(m_env.group(1))
            if not is_valid_id(mid):
                return self._send_error(400, ERR_INVALID_ID, "ID de mision invalido")
            m = find_mission(state, mid)
            if not m:
                return self._send_error(404, ERR_NOT_FOUND, "Mision no encontrada")
            body = self._read_json_body()
            if body is None:
                return self._send_error(400, ERR_PARSE, "JSON invalido")
            candidate = dict(m["env"])
            cabin_from_json_partial(candidate, body)
            new_dia = m["dia_actual"]
            if "dia_actual" in body:
                d_val = body["dia_actual"]
                if not is_int_num(d_val) or not (1 <= int(d_val) <= 10000):
                    return self._send_error(400, ERR_INVALID_RANGE, "dia_actual fuera de rango (1..10000)")
                new_dia = int(d_val)
            vrc = validate_cabin(candidate)
            if vrc != OK:
                return self._send_error(400, vrc, "Parametros de entorno de cabina fuera de rango fisico")
            m["env"] = candidate
            m["dia_actual"] = new_dia
            evaluate_mission_crew(state, m)
            storage_save_mission(state["data_dir"], m)
            return self._send_json(200, build_mission_detail_json(state, m))

        m_sim = re.match(r"^/api/missions/([^/]+)/simulate$", path)
        if m_sim:
            if method != "POST":
                return self._send_error(405, ERR_INVALID_RANGE, "Método no permitido")
            mid = urllib.parse.unquote(m_sim.group(1))
            if not is_valid_id(mid):
                return self._send_error(400, ERR_INVALID_ID, "ID de mision invalido")
            m = find_mission(state, mid)
            if not m:
                return self._send_error(404, ERR_NOT_FOUND, "Mision no encontrada")
            body = self._read_json_body()
            if body is None:
                return self._send_error(400, ERR_PARSE, "JSON invalido")
            scenario = body.get("scenario", "")
            if not isinstance(scenario, str):
                scenario = ""
            src = simulate_mission_scenario(state, m, scenario)
            if src != OK:
                return self._send_error(400, src, "Escenario de simulacion desconocido")
            storage_save_mission(state["data_dir"], m)
            for a in state["astronauts"]:
                if a["mission_id"] == m["id"] or a["id"] in m["crew_ids"]:
                    storage_save_astronaut(state["data_dir"], a)
            resp = build_mission_detail_json(state, m)
            resp["scenario_applied"] = scenario
            return self._send_json(200, resp)

        m_id = re.match(r"^/api/missions/([^/]+)$", path)
        if m_id:
            if method != "GET":
                return self._send_error(405, ERR_INVALID_RANGE, "Método no permitido")
            mid = urllib.parse.unquote(m_id.group(1))
            if not is_valid_id(mid):
                return self._send_error(400, ERR_INVALID_ID, "ID de mision invalido")
            m = find_mission(state, mid)
            if not m:
                return self._send_error(404, ERR_NOT_FOUND, "Mision no encontrada")
            return self._send_json(200, build_mission_detail_json(state, m))

        if path == "/api/astronauts":
            if method == "GET":
                return self._send_json(200, [astronaut_summary_to_json(a) for a in state["astronauts"]])
            if method == "POST":
                body = self._read_json_body()
                if body is None:
                    return self._send_error(400, ERR_PARSE, "JSON invalido en el cuerpo de la peticion")
                new_a, rc = astronaut_from_json(body, state["defaults"])
                if rc != OK:
                    return self._send_error(400, rc, "ID de astronauta invalido (solo [a-zA-Z0-9_-], 1..15 caracteres) o parametros fuera de rango")
                dest_m = find_mission(state, new_a["mission_id"])
                if not dest_m and state["missions"]:
                    dest_m = state["missions"][0]
                    new_a["mission_id"] = dest_m["id"]
                if dest_m and new_a["id"] not in dest_m["crew_ids"] and len(dest_m["crew_ids"]) >= MAX_CREW_PER_MISSION:
                    return self._send_error(422, ERR_LIMIT_REACHED, "Limite maximo de tripulantes por mision alcanzado")

                target = find_astronaut(state, new_a["id"])
                if target:
                    if target["mission_id"] != new_a["mission_id"]:
                        old_m = find_mission(state, target["mission_id"])
                        if old_m and target["id"] in old_m["crew_ids"]:
                            old_m["crew_ids"].remove(target["id"])
                            storage_save_mission(state["data_dir"], old_m)
                    target.clear()
                    target.update(new_a)
                else:
                    if len(state["astronauts"]) >= MAX_ASTRONAUTS:
                        return self._send_error(422, ERR_LIMIT_REACHED, "Limite maximo de astronautas alcanzado")
                    state["astronauts"].append(new_a)
                    target = new_a

                if dest_m:
                    if target["id"] not in dest_m["crew_ids"] and len(dest_m["crew_ids"]) < MAX_CREW_PER_MISSION:
                        dest_m["crew_ids"].append(target["id"])
                    storage_save_mission(state["data_dir"], dest_m)
                evaluate_astronaut(target, dest_m["env"] if dest_m else None)
                storage_save_astronaut(state["data_dir"], target)
                return self._send_json(201, astronaut_to_json(target, True))
            return self._send_error(405, ERR_INVALID_RANGE, "Método no permitido")

        a_samp = re.match(r"^/api/astronauts/([^/]+)/samples$", path)
        if a_samp:
            if method != "POST":
                return self._send_error(405, ERR_INVALID_RANGE, "Método no permitido")
            aid = urllib.parse.unquote(a_samp.group(1))
            if not is_valid_id(aid):
                return self._send_error(400, ERR_INVALID_ID, "ID de astronauta invalido")
            a = find_astronaut(state, aid)
            if not a:
                return self._send_error(404, ERR_NOT_FOUND, "Astronauta no encontrado")
            body = self._read_json_body()
            if body is None:
                return self._send_error(400, ERR_PARSE, "JSON invalido")

            m = find_mission(state, a["mission_id"])
            latest = a["history"][-1] if a["history"] else None
            default_day = m["dia_actual"] if m else (latest["dia_mision"] if latest else 1)
            d_val = body.get("dia_mision")
            target_day = int(d_val) if (is_int_num(d_val) and int(d_val) > 0) else default_day

            sample = sample_prepare_partial_base(a, target_day)
            sample_from_json_partial(sample, body)
            rc = astronaut_add_sample(a, sample)
            if rc != OK:
                return self._send_error(400, rc, "Valores fisiologicos de la muestra fuera de rango")

            if m and sample["dia_mision"] > m["dia_actual"]:
                m["dia_actual"] = sample["dia_mision"]
                storage_save_mission(state["data_dir"], m)

            evaluate_astronaut(a, m["env"] if m else None)
            storage_save_astronaut(state["data_dir"], a)
            return self._send_json(201, {
                "sample": sample_to_json(sample),
                "report": a["report"],
                "astronaut": astronaut_to_json(a, True),
            })

        a_exp = re.match(r"^/api/astronauts/([^/]+)/export$", path)
        if a_exp:
            if method != "GET":
                return self._send_error(405, ERR_INVALID_RANGE, "Método no permitido")
            aid = urllib.parse.unquote(a_exp.group(1))
            if not is_valid_id(aid):
                return self._send_error(400, ERR_INVALID_ID, "ID de astronauta invalido")
            a = find_astronaut(state, aid)
            if not a:
                return self._send_error(404, ERR_NOT_FOUND, "Astronauta no encontrado")
            pretty = (json.dumps(astronaut_to_json(a, True), ensure_ascii=False, indent=2) + "\n").encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Disposition", f'attachment; filename="{a["id"]}_bitacora_hrp.json"')
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(pretty)))
            self.end_headers()
            self.wfile.write(pretty)
            return

        a_id = re.match(r"^/api/astronauts/([^/]+)$", path)
        if a_id:
            aid = urllib.parse.unquote(a_id.group(1))
            if not is_valid_id(aid):
                return self._send_error(400, ERR_INVALID_ID, "ID de astronauta invalido")
            a = find_astronaut(state, aid)
            if not a:
                return self._send_error(404, ERR_NOT_FOUND, "Astronauta no encontrado")

            if method == "GET":
                return self._send_json(200, astronaut_to_json(a, True))

            if method == "PUT":
                body = self._read_json_body()
                if body is None:
                    return self._send_error(400, ERR_PARSE, "JSON invalido")

                if "radiacion_previa_msv" in body:
                    if not is_valid_float(body["radiacion_previa_msv"], 0.0, 10000.0):
                        return self._send_error(400, ERR_INVALID_RANGE, "radiacion_previa_msv fuera de rango")
                if "radiacion_mision_msv" in body:
                    if not is_valid_float(body["radiacion_mision_msv"], 0.0, 10000.0):
                        return self._send_error(400, ERR_INVALID_RANGE, "radiacion_mision_msv fuera de rango")
                if "mission_id" in body:
                    mid_val = body["mission_id"]
                    if not is_valid_id(mid_val):
                        return self._send_error(400, ERR_INVALID_ID, "mission_id invalido")
                    chk_m = find_mission(state, mid_val)
                    if chk_m and a["id"] not in chk_m["crew_ids"] and len(chk_m["crew_ids"]) >= MAX_CREW_PER_MISSION:
                        return self._send_error(422, ERR_LIMIT_REACHED, "Limite maximo de tripulantes por mision alcanzado")
                if "nombre" in body and (not isinstance(body["nombre"], str) or not body["nombre"]):
                    return self._send_error(400, ERR_PARSE, "nombre invalido")
                if "rol" in body and (not isinstance(body["rol"], str) or not body["rol"]):
                    return self._send_error(400, ERR_PARSE, "rol invalido")

                resolved_avatar = None
                if "avatar" in body:
                    ava_val = body["avatar"]
                    if not isinstance(ava_val, str) or not ava_val:
                        return self._send_error(400, ERR_PARSE, "avatar invalido")
                    if ava_val.startswith("data:image/"):
                        arc, resolved_avatar = validate_custom_avatar_data_url(ava_val)
                        if arc != OK:
                            return self._send_error(400, arc, "Formato de imagen de avatar invalido")
                    else:
                        resolved_avatar = ava_val

                if "sexo" in body and not is_valid_sex(body["sexo"]):
                    return self._send_error(400, ERR_INVALID_RANGE, "sexo invalido (use F o M)")

                candidate_base = dict(a["baseline"])
                if "baseline" in body:
                    if not isinstance(body["baseline"], dict):
                        return self._send_error(400, ERR_PARSE, "baseline debe ser un objeto JSON")
                    baseline_from_json(candidate_base, body["baseline"])
                    if validate_baseline(candidate_base) != OK:
                        return self._send_error(400, ERR_INVALID_RANGE, "Parametros de baseline fuera de rango fisiologico")

                if "nombre" in body:
                    a["nombre"] = body["nombre"]
                if "rol" in body:
                    a["rol"] = body["rol"]
                if resolved_avatar is not None:
                    a["avatar"] = resolved_avatar
                if "sexo" in body:
                    a["sexo"] = normalize_sex_str(body["sexo"])
                if "radiacion_previa_msv" in body:
                    a["radiacion_previa_msv"] = float(body["radiacion_previa_msv"])
                if "radiacion_mision_msv" in body:
                    a["radiacion_mision_msv"] = float(body["radiacion_mision_msv"])
                if "mission_id" in body and body["mission_id"] != a["mission_id"]:
                    old_m = find_mission(state, a["mission_id"])
                    if old_m and a["id"] in old_m["crew_ids"]:
                        old_m["crew_ids"].remove(a["id"])
                        storage_save_mission(state["data_dir"], old_m)
                    a["mission_id"] = body["mission_id"]
                    new_m = find_mission(state, a["mission_id"])
                    if new_m and a["id"] not in new_m["crew_ids"] and len(new_m["crew_ids"]) < MAX_CREW_PER_MISSION:
                        new_m["crew_ids"].append(a["id"])
                        storage_save_mission(state["data_dir"], new_m)

                a["baseline"] = candidate_base
                m = find_mission(state, a["mission_id"])
                evaluate_astronaut(a, m["env"] if m else None)
                storage_save_astronaut(state["data_dir"], a)
                return self._send_json(200, astronaut_to_json(a, True))

            if method == "DELETE":
                deleted_id = a["id"]
                for m in state["missions"]:
                    if deleted_id in m["crew_ids"]:
                        m["crew_ids"].remove(deleted_id)
                        storage_save_mission(state["data_dir"], m)
                storage_delete_astronaut_file(state["data_dir"], deleted_id)
                state["astronauts"] = [x for x in state["astronauts"] if x["id"] != deleted_id]
                return self._send_json(200, {"status": "deleted", "id": deleted_id})

            return self._send_error(405, ERR_INVALID_RANGE, "Método no permitido")

        if path.startswith("/api/"):
            return self._send_error(404, ERR_NOT_FOUND, "Endpoint API no encontrado")
        return self._send_error(404, ERR_NOT_FOUND, "Ruta no encontrada")

    def do_OPTIONS(self):
        self._dispatch("OPTIONS")

    def do_GET(self):
        self._dispatch("GET")

    def do_POST(self):
        self._dispatch("POST")

    def do_PUT(self):
        self._dispatch("PUT")

    def do_DELETE(self):
        self._dispatch("DELETE")

    def log_message(self, format, *args):
        return

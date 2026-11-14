# 05 - Mapeo de Datos NASA HRP y Parámetros Médicos

## 1. Propósito
Traducir los recursos de investigación recopilados en `fuentes.md` en **variables medibles, umbrales de referencia y contramedidas concretas** que alimenten tanto al motor de evaluación en C como a los datos de prueba (`data/sample_telemetry.csv`).

---

## 2. Matriz de Variables y Referencias NASA / CSA

| Categoría HRP | Variable en el Software | Fuente de Referencia (`fuentes.md`) | Rango Nominal / Umbral de Alerta | Contramedida Sugerida por el Sistema |
| :--- | :--- | :--- | :--- | :--- |
| **Entorno Cerrado / Hipoxia** | `spo2_percent` (%) | Risk of Hypoxia / Tempus Pro | Nominal: 95–100%<br>Alerta: < 93% | Verificar presión parcial de O2, revisar sistema ECLSS y administrar O2 suplementario. |
| **Cardiovascular** | `heart_rate_bpm` (lpm) | Risk of Cardiovascular Adaptations / Tempus Pro | Nominal: 55–90 lpm (reposo)<br>Alerta: > 105 o < 45 lpm | Evaluar hidratación, carga de ejercicio y realizar ECG de control. |
| **Cardiovascular** | `systolic_bp` / `diastolic_bp` | Tempus Pro / Standard Measures | Nominal: ~120/80 mmHg<br>Alerta: > 140/90 o < 90/60 | Monitoreo de redistribución de fluidos; evaluar uso de *Thigh Cuff* si aplica. |
| **Radiación Espacial** | `radiation_dose_msv` (mSv/día) | CSA Radi-N2 / CANS / Risk of Radiation | Nominal: < 1.0 mSv/día<br>Alerta: > 2.0 mSv/día (pico solar) | Refugiarse en zona blindada de la nave; registrar exposición acumulada. |
| **Aislamiento y Sueño** | `sleep_hours` (hs/día) | ARCHeR / Risk from Inadequate Sleep | Nominal: 7–8.5 hs<br>Alerta: < 6 hs sostenido | Ajustar ciclo de iluminación circadiana; reducir carga cognitiva no crítica. |
| **Salud Conductual** | `stress_level` / `mood_score` | ARCHeR / Behavioral Performance | Estrés > 7 o Ánimo < 4 | Activar protocolo de bienestar, pausas activas o sesión de soporte autónomo. |
| **Microgravedad (Óseo/Muscular)** | `exercise_minutes` (min/día) | Zero-T2 / Bone Changes / Fitness | Meta: >= 90 min/día<br>Alerta: < 45 min/día | Programar rutina de ejercicio resistivo (ARED) para prevenir pérdida ósea y muscular. |
| **Neuro-ocular (SANS)** | `ocular_pressure_flag` (0/1) | Risk of SANS / Brain Fluid Pressure | Nominal: 0 (sin síntomas)<br>Alerta: 1 (visión borrosa/cefalea) | Realizar examen de fondo de ojo/ultrasonido ocular guiado y reportar en bitácora. |

---

## 3. Escenarios de Prueba para el Archivo `data/sample_telemetry.csv`
Para validar el motor en C y lucir la herramienta en la presentación, se prepararán los siguientes escenarios:
- [ ] **Escenario 1 — Estado Nominal (Día de misión típico):** Todos los parámetros dentro de rango; contramedidas preventivas cumplidas.
- [ ] **Escenario 2 — Evento de Partículas Solares (Radiación):** Aumento súbito de `radiation_dose_msv`; activación de alerta roja de radiación y protocolo de refugio.
- [ ] **Escenario 3 — Fatiga y Aislamiento (ARCHeR):** 3 días consecutivos con `sleep_hours < 5.5` y `stress_level >= 8`; alerta de riesgo conductual y degradación de desempeño.
- [ ] **Escenario 4 — Anomalía Ambiental (Entorno Cerrado):** Descenso progresivo de `spo2_percent` simulando falla en calidad de aire de cabina.

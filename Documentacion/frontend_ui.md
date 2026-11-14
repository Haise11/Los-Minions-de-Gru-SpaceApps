# 04 - Interfaz Web y Experiencia de Usuario (HTML, CSS y JavaScript)

## 1. Objetivo de Diseño
La interfaz web será servida por el backend en C y debe permitir que un astronauta evalúe su estado de salud y el de su tripulación **en cuestión de segundos**, bajo condiciones de estrés o fatiga, con total claridad visual y funcionamiento 100% offline.

---

## 2. Estructura Visual (`web/index.html`)
1. **Barra Superior de Misión:**
   - Estado general de la tripulación (Normal / Precaución / Crítico).
   - Indicador de autonomía / latencia de comunicación con Tierra (ej. *Modo Autónomo — Latencia Marte: 14 min*).
   - Botón rápido para simular eventos críticos (útil para la presentación al jurado).
2. **Panel de los 5 Peligros del HRP:**
   - Tarjetas visuales con semáforo de estado para: *Radiación*, *Aislamiento*, *Distancia*, *Gravedad* y *Entorno Cerrado*.
3. **Signos Vitales y Telemetría en Tiempo Real:**
   - Métricas clave inspiradas en *Tempus Pro* y *Standard Measures*: Frecuencia Cardíaca, Presión Arterial, SpO2, Dosis de Radiación, Horas de Sueño.
   - Gráficos de evolución temporal (renderizados en `<canvas>` con JavaScript puro o librería local sin depender de internet).
4. **Asistente de Contramedidas y Guías:**
   - Lista dinámica de acciones recomendadas según las alertas devueltas por el backend en C (ej. *"SpO2 en descenso: verificar filtros de CO2/O2 del hábitat"* o *"Déficit de sueño: aplicar protocolo de higiene circadiana"*).
5. **Formulario de Chequeo Diario (*Crew Check-in*):**
   - Registro rápido de estado anímico, estrés, fatiga y síntomas físicos (ej. molestias visuales asociadas a SANS) que envía un `POST /api/checkin` al backend en C.

---

## 3. Lineamientos de Estilo (`web/styles.css`)
- [ ] Diseñar tema oscuro de alto contraste ("modo cabina espacial") para reducir fatiga visual.
- [ ] Definir código de colores inequívoco:
  - Verde (`#22c55e`): Parámetro nominal.
  - Amarillo/Ámbar (`#f59e0b`): Precaución / requiere seguimiento.
  - Rojo (`#ef4444`): Alerta crítica / acción inmediata.
- [ ] Diseño responsivo adaptado tanto a pantallas de tablet (como usan en cabina/ISS) como a monitores de escritorio.

---

## 4. Lógica de Cliente (`web/app.js`)
- [ ] Implementar función de *polling* periódico (`fetch('/api/status')` cada 2–5 segundos) para actualizar tarjetas y alertas sin recargar la página.
- [ ] Consultar `GET /api/history` para dibujar gráficos de tendencias en `<canvas>` usando JavaScript nativo (cero dependencias externas para garantizar modo offline).
- [ ] Conectar el formulario de *Crew Check-in* con `POST /api/checkin` y actualizar inmediatamente la interfaz con la nueva evaluación del motor en C.
- [ ] Conectar botones de simulación (`POST /api/simulate`) para demostrar en vivo cómo el sistema detecta anomalías y sugiere contramedidas.

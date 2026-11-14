# 04 - Interfaz Web y Experiencia de Usuario (HTML, CSS y JavaScript)

## 1. Objetivo de Diseño y Referencias Visuales
La interfaz web será servida directamente por el backend en C (`http://localhost:8080`) y sigue el principio de **simplicidad radical** para que un astronauta bajo estrés, fatiga o desconexión con Tierra nunca se pierda entre menús complejos.

El diseño se basa directamente en las referencias gráficas del equipo:
* **[DibujodePagina.jpeg](./Imagenes%20de%20referencia/DibujodePagina.jpeg):** estructura de dos columnas (barra lateral `OPTIONS` a la izquierda + vista principal por **Misión** a la derecha con grilla de tarjetas de astronautas y vista ampliada `EJ` al seleccionar uno).
* **[Credenciales.jpeg](./Imagenes%20de%20referencia/Credenciales.jpeg):** estética visual de tarjetas tipo credencial sobre consola oscura azulada/pizarra, foto del astronauta a la izquierda y 3 líneas de información clara a la derecha.

Todos los endpoints y campos están unificados con el contrato REST de [functions.md](./functions.md).

---

## 2. Arquitectura Visual en 2 Niveles (`web/index.html`)

Para evitar saturar la pantalla, la interfaz separa **la vista general rápida de la misión** (Nivel 1) de **las herramientas detalladas** (Nivel 2, visibles solo al abrir una tarjeta o elegir una opción lateral).

### Nivel 1: Pantalla Principal (Vista por Misión)

1. **Columna Izquierda — Menú `||| OPTIONS` (Plegable/Desplegable y Scrolleable):**
   Cabecera interactiva (`#sidebar-toggle-btn` con ícono `|||` + `OPTIONS` + flecha) que permite **plegar y desplegar** la barra lateral izquierda (persistiendo estado en `localStorage`) con navegación directa (tal como en `DibujodePagina.jpeg`):
   * **`CARGAR / CARGAR ARCHIVO`:** acceso rápido para:
     1. Registrar un *Crew Check-in* diario (incluyendo bloque dedicado de **Salud Hormonal y Ciclo Menstrual**).
     2. Crear un nuevo astronauta (`POST /api/astronauts` prellenado con `GET /api/defaults`, selector de `sexo` `F`/`M` y **subida de imagen personalizada** desde el dispositivo) o nueva misión (`POST /api/missions`).
     3. **Subir un archivo `.json`** desde el dispositivo para importar un perfil o historial completo de astronauta, con opción de adjuntar fotografía personalizada.
   * **`MOSTRAR`:** selector de misiones (`GET /api/missions`) y filtro rápido por estado (`Todos`, `Precaución`, `Crítico`, `Muy Crítico`).
   * **`PROMEDIO DE SALUD`:** vista resumen de la misión alimentada por `MissionHealthSummary` (`mission_health_avg` `0..100%`, conteo `OK`/`CAUTION`/`CRITICAL`/`VERY_CRITICAL`, **barra de estadísticas colapsable** `#summary-stats-toggle-btn` / `#summary-stats-panel` oculta por defecto, semáforo de los 5 Peligros del HRP, entorno de cabina `CabinEnv` y promedios grupales).
   * **`GLOSARIO HRP`:** apartado de referencia médica detallada (`GET /api/glossary`) con tarjetas **100% colapsables** (contraídas por defecto, botones *Expandir todo* / *Colapsar todo*, auto-expansión al buscar con resultados acotados y fuentes oficiales en `<details>` desplegable) y filtros por categoría (*5 Peligros HRP*, *Protocolos y Contramedidas*, *Síntomas y Condiciones*, *Salud Hormonal Femenina*, *Umbrales Clínicos*, *Medidas y Unidades*, *Siglas y Acrónimos*).
   * **`SIMULADOR (DEMO)`:** sección secundaria colapsable en lenguaje clínico español sin tokens internos (`POST /api/missions/:id/simulate` con `data-sim`: `NOMINAL`, `SPE_STORM`, `HYPOXIA`, `HYPERCAPNIA`, `SANS_SLEEP`, `FATIGUE_ISOLATION`, `DCS_EVA`, `HORMONAL_FEMALE`) y selector de latencia (`Órbita 0.2s` / `Luna 1.3s` / `Marte 14m`).

2. **Columna Derecha — Banner de Prioridad Total + Barra de Misión + Filtros + Grilla de Tripulación:**
   * **Banner de Prioridad Total (`#priority-emergency-banner`):** cuando un astronauta entra en estado **MUY CRÍTICO** (`is_very_critical == 1`), se activa un banner rojo de emergencia en la parte superior, se enfoca automáticamente la ficha del paciente prioritario y el dashboard bloquea cambios de misión, selección de otros tripulantes, cambios de latencia/contramedidas ajenas, importación/creación de perfiles y nuevas simulaciones hasta resolver o restablecer a estado nominal.
   * **Barra de Misión:** título `MISIÓN: <nombre>` (ej. *Artemis / Tránsito a Marte — Día 14*), puntaje general `Promedio de Salud: XX%` y badge de **Modo Autónomo** (`autonomous_mode == 1` cuando `latencia_com_seg > 5.0`).
   * **Barra de Filtros Combinables (`.crew-filter-bar`):** permite filtrar simultáneamente por *Nombre*, *Apellido* (ignorando rangos como `Cmdr.`, `Dr.`, `Ing.`, `Esp.` y tildes), *Gravedad* (`Todos`, `Nominal`, `Precaución`, `Crítico`, `Muy Crítico`), *Aptitud EVA* (`GO (Apto)` / `RESTRICTED (Restringido)` / `NO-GO (No Apto)`), *Sexo* (`F`/`M`) y criterio de *Ordenación*, más botón *Limpiar Filtros*.
   * **Grilla de Tarjetas de Astronautas (3 columnas por fila, estilo `Credenciales.jpeg`):**
     Cada tarjeta muestra únicamente lo esencial para evaluar al tripulante en 2 segundos:
     * **Izquierda:** fotografía local del astronauta (`avatar` en `web/assets/avatars/`, 100% offline).
     * **Derecha:**
       1. **Nombre** (`nombre`) y rol breve.
       2. **Estado** (`ESTADO` + ícono de corazón `♥` en verde `#00ff66`, ámbar `#ffd000` o rojo `#ff2a4b` + puntaje `health_score`%, badge `🚨 MUY CRÍTICO` si `is_very_critical == 1` y badge `EVA: GO / NO-GO`).
       3. **Descripción breve** (`resumen_estado`: ej. *"Nominal"*, *"Dolor de cabeza · Revisar CO2"* o *"Alerta Radiación SPE"*).

---

### Nivel 2: Vistas Secundarias y Detalle de Astronauta (al hacer clic)

1. **Modal / Vista Detallada del Astronauta (recuadro `EJ` de `DibujodePagina.jpeg`):**
   Al hacer clic en cualquier tarjeta de astronauta se abre su ficha clínica (`GET /api/astronauts/:id`) con botón de cierre (`×`) en la esquina superior derecha:
   * **Cabecera de Credencial:** foto ampliada con botón **"📷 Cambiar Foto"** (`POST /api/avatars` en PNG/JPG/WEBP/GIF/SVG), nombre, rol, badge de sexo biológico (`♀ FEMENINO` / `♂ MASCULINO`), botón **"Exportar Bitácora"** (`GET /api/astronauts/:id/export`) y botón **"Check-in Rápido"**.
   * **Mapa Corporal Interactivo de los 7 Sistemas (`BodySystem` en 2 columnas):** diagrama SVG ampliado (`440×410`) con etiquetas en español mediante líneas guía sin superposición y sincronización interactiva (*hover* y *click*) entre órganos del SVG y la leyenda lateral (`#system-legend-list`).
   * **Estadísticas Plegables (`#detail-stats-toggle-btn` / `#detail-stats-panel`):** el bloque de *Signos Vitales vs. Línea Base*, barra de radiación acumulada y gráficos `<canvas>` permanece **colapsado por defecto** para priorizar alertas y mapa corporal (manteniendo su estado durante el polling de 4s y volviendo a colapsar al cambiar de astronauta).
   * **Sub-panel Hormonal Estricto para Tripulación Femenina (`#detail-hormonal-panel`):** se renderiza **exclusivamente** cuando `sexo === "F"` (oculto por completo para tripulación masculina); muestra fase del ciclo (`CYCLE_PHASE_NAMES`), flujo, cólico y botón 1-clic de **Suplemento Hierro (Fe) + Hidratación**.
   * **Asistente de Contramedidas HRP:** lista de alertas sin recorte de altura (scroll natural de página), ordenadas (`CRITICAL` → `CAUTION`), redactadas en lenguaje clínico claro sin exponer identificadores internos de código, con pasos accionables y botones rápidos de contramedidas.
   * **Gráficos de Tendencias (`<canvas>` puro):** evolución de los últimos 14–30 días (`fc`, `spo2`, `sueno_horas`, `dosis_dia_msv`, `estres`, `vision_actual`) trazados dinámicamente al desplegar las estadísticas.

2. **Formulario de Chequeo Diario (*Crew Check-in* desde `CARGAR` o desde la ficha):**
   * **Campos rápidos:** sueño (horas y calidad 1–5), ánimo (1–10), estrés (1–10), cohesión del equipo (1–5), dolor de cabeza (0–10), mareo vestibular (0–10), apetito (1–5) y ejercicio diario (min).
   * **Mini-Test de Reflejos Integrado:** botón **"Medir Reflejos (3 toques)"** que mide con `performance.now()` en JS el tiempo de reacción visual del astronauta en milisegundos.
   * **Desplegable opcional de vitales / EVA** y **Bloque Hormonal Femenino (`#ci-hormonal-box`)** visible únicamente cuando el astronauta seleccionado tiene `sexo === "F"` (enviando valores neutros `0` en tripulación masculina).
   * **Toggles de síntomas y contramedidas:** checkboxes clínicos en español codificados al bitmask del backend (`POST /api/astronauts/:id/samples`) sin exponer nombres de constantes internas.

3. **Panel `PROMEDIO DE SALUD` y Entorno de Cabina (`CabinEnv`):**
   * Muestra los 5 Peligros del HRP a nivel de misión, estadísticas grupales colapsables por defecto y controles ambientales (`presion_cabina_kpa`, `po2_kpa`, `pco2_kpa`, `tasa_rad_msv_h`, `gravedad_actual` y `latencia_com_seg`) vía `PUT /api/missions/:id/env`.

---

## 3. Lineamientos de Estilo (`web/styles.css`)
- [x] Paleta de cabina espacial de **tonos más oscuros y alto contraste**: fondo abisal `#010307`, panel lateral `#030711`, tarjetas `#060d1b` (`hover` `#0c1830`), bordes reforzados `#284c7a` / `#3b6ca8`, texto principal `#ffffff` y realce cian neón `#00e5ff` (más acento rosa `#f472b6` exclusivo para indicadores femeninos).
- [x] Tipografía base cómoda (`17px` en raíz), espaciado generoso en formularios y tarjetas de alerta sin `max-height` restrictivo para lectura fluida en misiones de larga duración.
- [x] Semáforo de estado de alto contraste (para el corazón `♥` de cada tarjeta, mapa corporal y alertas):
  - Verde (`#00ff66`): `OK` — Nominal.
  - Amarillo/Ámbar (`#ffd000`): `CAUTION` — Precaución.
  - Rojo (`#ff2a4b`): `CRITICAL` — Crítico / acción inmediata (más badge `🚨 MUY CRÍTICO` para estado **Muy Crítico**).
- [x] Grid responsivo de 3 columnas en escritorio, 2 en tablet/portátil (`<= 1380px`) y 1 en pantallas angostas (`<= 860px`).

---

## 4. Lógica de Cliente (`web/app.js`)
- [x] **Estado local de aplicación:** `currentMissionId`, `currentAstronautId`, `activeOptionView` (`mostrar` | `promedio` | `cargar` | `glosario`), `detailStatsExpanded` (`false` por defecto), `summaryStatsExpanded` (`false` por defecto), `crewFilter`, `glossaryExpandedIds` (`Set`), `isPriorityMode` / `priorityAstronautIds` / `topPriorityAstronautId`, `defaults`, `glossaryData`.
- [x] **Barra lateral `||| OPTIONS` plegable:** `toggleSidebar()` / `setSidebarCollapsed()` alternan la clase `.sidebar-collapsed` en `#app-shell` y guardan preferencia en `localStorage`.
- [x] **Polling inteligente cada 4 segundos:** consulta `GET /api/missions/:id` (y `GET /api/astronauts/:id` si hay un detalle abierto) manteniendo el estado de los paneles colapsables y las tarjetas del glosario sin sobrescribir formularios en edición.
- [x] **Modo Prioridad Total (`applyVeryCriticalPriorityMode` / `focusPriorityAstronaut`):** al detectar `crew_very_critical_count > 0` o `is_very_critical === 1`, limpia filtros residuales de tripulación al activarse, bloquea controles no prioritarios (incluidos selectores de latencia y contramedidas ajenas), auto-enfoca al astronauta crítico y permite únicamente su atención clínica, exportación o restablecimiento a simulación `NOMINAL`.
- [x] **Subida de imágenes personalizadas (`FileReader` + `POST /api/avatars`):** `readImageFileAsDataUrl()` valida formato (`PNG`, `JPG`, `WEBP`, `GIF`, `SVG`) y límite de 2 MB al crear astronauta, importar `.json` o actualizar avatar.
- [x] **Glosario clínico colapsable (`loadGlossaryData` / `syncGlossaryAutoExpand` / `renderGlossary`):** todas las tarjetas inician contraídas (`glossary-collapsed`), se expanden/colapsan por clic individual o global sin quedar bloqueadas por la búsqueda, se auto-expanden al buscar (`>= 2` caracteres con `<= 5` coincidencias) e incluyen categorías `MEASURES` (Medidas y Unidades) y `ACRONYMS` (Siglas y Acrónimos).
- [x] **Gráficos en `<canvas>` nativo:** `drawTrendChart()` 100% offline para 6 series fisiológicas (`fc`, `spo2`, `sueno_horas`, `dosis_dia_msv`, `estres`, `vision_actual`), ejecutándose automáticamente al expandir `#detail-stats-panel`.
- [x] **Interfaz limpia sin exposición de símbolos técnicos:** ni el HTML, ni los textos visibles de JS, ni las tarjetas del glosario muestran nombres de constantes en C, bitmasks, expresiones regulares ni rutas `/api/...` al operador médico o astronauta.



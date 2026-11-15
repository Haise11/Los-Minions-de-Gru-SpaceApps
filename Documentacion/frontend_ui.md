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

### Nivel 1: Pantalla Principal (Monitor de Tripulación)

1. **Columna Izquierda — Menú `OPCIONES` (Plegable/Desplegable y Scrolleable, sin emojis):**
   Cabecera interactiva (`#sidebar-toggle-btn` sin flecha direccional lateral y con ícono de 3 barras `.bars-icon` **horizontales** al estar plegado y **verticales** al estar desplegado) que permite **plegar y desplegar** la barra lateral izquierda (persistiendo estado en `localStorage`). Al plegarse, cada opción muestra un **ícono SVG monocromático** significativo en lugar de abreviaturas de 2 letras (`TR`, `CK`, etc.). Orden estricto:
   1. **`TRIPULACIÓN` (`#nav-mostrar`):** vista principal de credenciales asignadas (`GET /api/astronauts`) encabezada por la bandeja de **Recordatorios Proactivos de Rutina (`#crew-routine-nudges`)** para recordar ejercicios, vegetales frescos (`Veggie`), hidratación, suplementos y días de descanso. Al volver a pulsar `TRIPULACIÓN` mientras se inspecciona la ficha de un astronauta, cierra el detalle y vuelve a mostrar a toda la tripulación.
   2. **`CHECK IN` (`#nav-checkin` / `#view-checkin`):** apartado independiente diseñado como flujo vertical cómodo y espaciado (`.form-scroll-flow`, `.form-card-spacious`, 5 bloques temáticos breves más el bloque hormonal condicional) con recordatorios contextuales del tripulante (`#ci-routine-nudges`), selector de **`Día de Descanso Programado` (`#ci-dia-descanso`)**, bloque dedicado de **Salud Hormonal y Ciclo Menstrual** visible solo en tripulantes femeninas y el mini-test psicomotor (PVT, sin emojis).
   3. **`PROMEDIO DE SALUD` (`#nav-promedio`):** vista resumen de la tripulación alimentada por `MissionHealthSummary` (`mission_health_avg` `0..100%`, conteo `Normal`/`Precaución`/`Crítico`/`Urgencia`, **barra de estadísticas colapsable** `#summary-stats-toggle-btn` / `#summary-stats-panel` oculta por defecto, semáforo de factores de riesgo fisiológico, entorno de cabina `CabinEnv` y promedios grupales).
   4. **`GLOSARIO` (`#nav-glosario`):** apartado de referencia médica detallada (`GET /api/glossary`) con tarjetas **100% colapsables de tamaño uniforme** (sin botones globales de *Expandir todo* / *Colapsar todo*, ordenadas de las entradas **más peligrosas a las más inofensivas**: `CRITICAL` → `CAUTION` → `OK`, auto-expansión al buscar con resultados acotados y fuentes científicas en `<details>` desplegable) y filtros por categoría (*Factores de Riesgo*, *Protocolos y Contramedidas*, *Síntomas y Condiciones*, *Salud Hormonal Femenina*, *Umbrales Clínicos*, *Medidas y Unidades*, *Siglas y Acrónimos*).
   5. **`CARGAR ASTRONAUTA` (`#nav-cargar`):** centro de alta e importación con 2 subpestañas:
      - **Nuevo Astronauta** (`POST /api/astronauts` prellenado con `GET /api/defaults`, selector de `sexo` `F`/`M` y **subida de imagen personalizada** desde el dispositivo).
      - **Cargar con JSON** (subir un archivo `.json` local con perfil/bitácora e imagen personalizada opcional).
   6. **`MODIFICAR ASTRONAUTA` (`#nav-modificar` / `#view-modificar`):** apartado dedicado en flujo vertical cómodo (`.form-scroll-flow`, `.form-card-spacious`, dividido en selector con banda de estado completa y subgrupos `2A/2B` y `3A/3B`) donde se puede editar cada dato del astronauta seleccionado (nombre, sexo, rol, **Cambiar Foto de Perfil**, dosis previas/misión, todos los parámetros basales y el último registro clínico/hormonal incluyendo `dia_descanso`), junto con **"⬇ Exportar Bitácora (.json)"** (`GET /api/astronauts/:id/export`) y **"🗑 Eliminar Astronauta"** (`DELETE /api/astronauts/:id`).

   *(Nota: se omiten del pie lateral las etiquetas de conexión offline y de "Último pulso local" / hora).*

2. **Columna Derecha — Cabecera Combinada + Banner/Atajo Flotante de Urgencia + Grilla Única:**
   * **Cabecera Superior Combinada (`#main-topbar`):** muestra el título institucional **`P.U.L.S.O.`** (con tipografía sutil *Space Grotesk* peso `700`, integrada al estilo *Grafito Neutro* sin estridencias) junto a su subtítulo (*Programa Unificado para La Salud Óptima*), combinado con el contador de tripulación y el botón **`Filtros ▾`** ubicado a la derecha (cerrado por defecto).
   * **Banner Informativo + Atajo Flotante de Urgencia (`#priority-emergency-banner` y `#floating-urgent-btn`):** cuando al menos un astronauta entra en estado **Urgencia** (`is_very_critical == 1`), se muestra el banner superior informativo y un **botón flotante en la esquina inferior derecha (`🚨 Ir a Urgencia`)** que permite saltar directamente (o rotar) hacia los tripulantes urgentes desde cualquier vista, **sin bloquear** la navegación hacia el resto de la tripulación ni el cierre de la ficha.
   * **Bandeja Proactiva de Rutina (`#crew-routine-nudges`):** muestra recordatorios accionables por tripulante (ejercicio pendiente o descanso recomendado, suplementos Vitamina D3 / Hierro, cultivo fresco `Veggie`, meta hídrica) con accesos directos a `Check-In`, `Aplicar ya` o `Marcar descanso`.
   * **Panel Desplegable de Filtros (`.crew-filter-bar`, oculto por defecto):** al abrir `Filtros ▾` se despliega una grilla en 2 filas acorde al boceto del equipo:
     - Fila 1 (5 columnas): `NOMBRE`, `APELLIDO` (ignorando rangos y tildes), `SEXO`, `ESTADO` (`Todas`, `Normal`, `Precaución`, `Crítico`, `Urgencia`) y `APTITUD EVA` (`Todas`, `Apto`, `Restringido`, `No apto`).
     - Fila 2: botón `✕ LIMPIAR` (columna 1) + selector ancho `ORDENAR` (`Prioridad Clínica`, `Menor Puntaje`, `Mayor Puntaje`, `Nombre (A–Z)`, `Apellido (A–Z)`).
   * **Grilla de Tarjetas de Astronautas (3 columnas por fila, estilo `Credenciales.jpeg`):**
     Cada tarjeta muestra únicamente lo esencial:
     * **Izquierda:** fotografía local del astronauta (`avatar` en `web/assets/avatars/`).
     * **Derecha:**
       1. **Nombre** (`nombre`), rol y badge `🚨 URGENCIA` si aplica (sin badge de estado EVA en las tarjetas principales).
       2. **Estado** (`ESTADO` + ícono de corazón `♥` + puntaje `health_score`%).
       3. **Diagnóstico / Resumen** (`resumen_estado`: ej. *"Normal"*, *"Dolor de cabeza · Revisar CO2"* o *"Alerta Radiación SPE"*).

---

### Nivel 2: Vista Detallada Única del Astronauta (al hacer clic)

1. **Vista Exclusiva del Astronauta Seleccionado (`#astronaut-detail-panel`):**
   Al hacer clic en cualquier tarjeta de astronauta, **se oculta el resto de la tripulación y la barra de filtros**, dejando visible únicamente la ficha clínica del astronauta elegido (`GET /api/astronauts/:id`). En la esquina **superior derecha del panel** (`position: absolute; top: 16px; right: 16px`) se encuentra el botón **`×`** (`#detail-close-btn`), que cierra la ficha y regresa a la grilla principal:
   * **Cabecera de Credencial:** fotografía ampliada (sin botón de edición en esta ficha; la subida/cambio de foto vive exclusivamente en `OPCIONES → MODIFICAR ASTRONAUTA`), nombre, rol, badge de sexo biológico (`♀ FEMENINO` / `♂ MASCULINO`), aptitud operativa (`Aptitud EVA: Apto / Restringido / No apto`) y semáforo/puntaje (sin botones de *Check-in Rápido*, *Exportar Bitácora* ni *Eliminar* en esta vista).
   * **Mapa Corporal Interactivo Segmentado (Masculino / Femenino), Inspección por Zona y Síntomas:** esquema SVG anatómico ampliado (`460×440`) con **proporciones reales (abdomen corto y piernas largas)** que adapta dinámicamente su silueta entre **figura masculina** y **figura femenina** (`BODY_SILHOUETTES` según `sexo`), dividido en segmentos corporales con órganos internos y el **corazón encapsulado dentro de los pulmones con borde verde más oscuro**. Al tocar cualquier órgano o parte del cuerpo (`inspectBodyPart`), el panel se desplaza suavemente hacia abajo (`#body-part-detail-box`) mostrando el estado clínico, métricas comparadas, síntomas localizados y protocolo de esa zona. Además, expresa dinámicamente síntomas activos sincronizados 1:1 con `health_engine.c` y lista en `#body-symptom-list` y `#system-legend-list` los síntomas/sistemas en orden clínico con **iconos minimalistas monocromáticos** de línea fina (`getMinimalistOrganIconSvg`).
   * **Estadísticas Plegables (`#detail-stats-toggle-btn` / `#detail-stats-panel`):** el bloque de *Signos Vitales vs. Línea Base*, barra de radiación acumulada y gráficos `<canvas>` permanece **colapsado por defecto** para priorizar alertas y mapa corporal (manteniendo su estado durante el polling de 4s y volviendo a colapsar al cambiar de astronauta).
   * **Sub-panel Hormonal Estricto para Tripulación Femenina (`#detail-hormonal-panel`):** se renderiza **exclusivamente** cuando `sexo === "F"` (oculto por completo para tripulación masculina); muestra fase del ciclo (`CYCLE_PHASE_NAMES`: Menstrual 1–5, Folicular 6–13, Ovulatoria 14–16, Lútea 17–28, Suprimido), flujo, cólico, tarjeta ampliada sin emojis de **Suplemento de Hierro + Folato** y protocolo paso a paso cuando está activa (`#cm-fem-action-summary`).
   * **Asistente de Contramedidas, Recordatorios y Alertas Clínicas:** lista de alertas ordenadas (`CRITICAL` → `CAUTION`), bloque `#detail-routine-nudges` con pendientes de rutina, botones de acciones de 1 clic en tarjeta sin emojis y con tipografía diferenciada y ampliada (`--font-cm`: *Outfit* / *Montserrat* / *Space Grotesk*, preservando la paleta *Grafito Neutro*) que indican pasos numerados accionables (`Banda en Muslos`, `Complejo B`, `Vitamina D3`, `Dieta Vegetal Fresca`), junto al bloque **`#cm-action-summary-list`** con pasos numerados de ejecución y `.alert-cm` ampliado.
   * **Gráficos de Tendencias (`<canvas>` puro):** evolución de los últimos 14–30 días (`fc`, `spo2`, `sueno_horas`, `dosis_dia_msv`, `estres`, `vision_actual`) trazados dinámicamente al expandir las estadísticas.

2. **Formulario de Chequeo Diario (*Crew Check-in* en flujo vertical cómodo):**
   * **Campos rápidos:** sueño (horas y calidad 1–5), ánimo (1–10), estrés (1–10), cohesión del equipo (1–5), dolor de cabeza (0–10), mareo vestibular (0–10), apetito (1–5), ejercicio diario (min) y casilla de **Día de Descanso Programado (`#ci-dia-descanso`)**.
   * **Mini-Test de Reflejos Integrado (sin emojis):** botón **"Medir Reflejos (3 toques)"** que mide con `performance.now()` en JS el tiempo de reacción visual del astronauta en milisegundos.
   * **Bloque Hormonal Femenino (`#ci-hormonal-box`)** visible únicamente cuando el astronauta seleccionado tiene `sexo === "F"` (enviando valores neutros `0` en tripulación masculina).
   * **Toggles de síntomas y contramedidas:** checkboxes clínicos en español codificados al bitmask del backend (`POST /api/astronauts/:id/samples`) sin exponer nombres de constantes internas.

3. **Panel `PROMEDIO DE SALUD` y Entorno de Cabina (`CabinEnv`):**
   * Muestra los factores de riesgo fisiológico, estadísticas grupales colapsables por defecto y controles ambientales (`presion_cabina_kpa`, `po2_kpa`, `pco2_kpa`, `tasa_rad_msv_h`, `gravedad_actual` y `latencia_com_seg`) vía `PUT /api/missions/:id/env`.

---

## 3. Lineamientos de Estilo (`web/styles.css` · Paleta *Grafito Neutro*)
- [x] Paleta de diseño **Grafito Neutro** (según [Grafito neutro@2x.png](./Imagenes%20de%20referencia/Grafito%20neutro@2x.png)):
  - **Fondo:** `#101010` (superficie lateral `#141414`, paneles `#161616`, inputs `#121212`).
  - **Tarjeta:** `#1A1A1A` (`hover` `#242424`, bordes neutros `#2A2A2A` / `#404040`).
  - **Acento:** `#D4D4D4` (texto principal `#F5F5F5`, texto secundario `#A3A3A3`).
- [x] Tipografía jerárquica equilibrada: **Space Grotesk** (peso `700`) para el título **P.U.L.S.O.**, **Inter** para texto general, nombres, roles y descripciones; **Outfit / Montserrat** (`--font-cm`) ampliada para los protocolos y selección de contramedidas; y **JetBrains Mono** (`tabular-nums`) para **etiquetas, porcentajes, estado y siglas**.
- [x] Semáforo de estado de alto contraste (para el borde superior, corazón `♥` de cada tarjeta, mapa corporal y alertas):
  - Verde (`#4ADE80`): `OK` — Normal.
  - Ámbar (`#FBBF24`): `CAUTION` — Atención / Precaución.
  - Rojo (`#F87171`): `CRITICAL` — Crítico (más badge `🚨 URGENCIA` y botón flotante `#floating-urgent-btn` para estado **Urgencia**).
- [x] Grid responsivo de 3 columnas en escritorio, 2 en tablet/portátil (`<= 1380px`) y 1 en pantallas angostas (`<= 860px`).

---

## 4. Lógica de Cliente (`web/app.js`)
- [x] **Estado local de aplicación:** `currentMissionId`, `currentAstronautId`, `allCrewList`, `activeOptionView` (`mostrar` | `checkin` | `promedio` | `glosario` | `cargar` | `modificar`), `crewFiltersExpanded` (`false` por defecto), `detailStatsExpanded` (`false` por defecto), `summaryStatsExpanded` (`false` por defecto), `crewFilter`, `glossaryExpandedIds` (`Set`), `isPriorityMode` / `urgentAstronautIds` / `topPriorityAstronautId`, `defaults`, `glossaryData`.
- [x] **Barra lateral `OPCIONES` plegable:** `toggleSidebar()` / `setSidebarCollapsed()` alternan la clase `.sidebar-collapsed` en `#app-shell` (rotando las 3 líneas de `.bars-icon` entre horizontal al cerrar y vertical al abrir) y guardan preferencia en `localStorage`.
- [x] **Polling inteligente cada 4 segundos:** consulta `GET /api/missions/:id` (para `CabinEnv` y semáforo) y `GET /api/astronauts` (para mostrar todos los tripulantes registrados independientemente de la misión), manteniendo el estado de los paneles colapsables, la vista exclusiva del detalle y las tarjetas del glosario sin sobrescribir formularios en edición.
- [x] **Atajo Flotante de Urgencia (`updateUrgentStatus` / `focusUrgentAstronaut`):** al detectar `is_very_critical === 1`, actualiza el banner informativo y el botón flotante inferior derecho (`🚨 Ir a Urgencia`), permitiendo navegar o rotar hacia los tripulantes urgentes sin bloquear la interacción normal ni el botón `×`.
- [x] **Subida de imágenes personalizadas (`FileReader` + `POST /api/avatars`):** `readImageFileAsDataUrl()` valida formato (`PNG`, `JPG`, `WEBP`, `GIF`, `SVG`) y límite de 2 MB al crear astronauta, importar `.json` o actualizar avatar.
- [x] **Glosario clínico colapsable (`loadGlossaryData` / `syncGlossaryAutoExpand` / `renderGlossary`):** todas las tarjetas inician contraídas (`glossary-collapsed`), se expanden/colapsan por clic individual o global sin quedar bloqueadas por la búsqueda, se auto-expanden al buscar (`>= 2` caracteres con `<= 5` coincidencias) e incluyen categorías `MEASURES` (Medidas y Unidades) y `ACRONYMS` (Siglas y Acrónimos).
- [x] **Gráficos en `<canvas>` nativo:** `drawTrendChart()` 100% offline para 6 series fisiológicas (`fc`, `spo2`, `sueno_horas`, `dosis_dia_msv`, `estres`, `vision_actual`), ejecutándose automáticamente al expandir `#detail-stats-panel`.
- [x] **Interfaz limpia sin exposición de símbolos técnicos:** el HTML, los textos visibles de JS y las tarjetas del glosario usan español clínico claro (`Normal`, `Urgencia`, `Apto / Restringido / No apto`) sin exponer nombres de constantes en C, bitmasks ni rutas `/api/...`.



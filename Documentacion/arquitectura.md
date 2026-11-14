# 01 - Arquitectura General del Sistema

## 1. Propósito del Proyecto
Desarrollar un software autónomo de monitoreo de salud para astronautas en misiones espaciales de larga duración (desafío **Health Monitoring Software** — NASA Space Apps Challenge 2026). El sistema centraliza variables fisiológicas, dosimetría de radiación, parámetros ambientales de cabina y métricas conductuales para evaluar riesgos según los lineamientos del *Human Research Program (HRP)* y ofrecer contramedidas accionables **100% offline**, sin depender de comunicación en tiempo real con la Tierra.

---

## 2. Arquitectura Técnica (Backend en C + Interfaz Web Integrada)
Toda la lógica de negocio, evaluación médica, persistencia y servidor HTTP estará desarrollada íntegramente en **lenguaje C (C11)**. El binario ejecutable en C (`./space_health_server`) servirá tanto los archivos estáticos del frontend (**HTML, CSS y JavaScript puro**) como la API REST JSON consumida por el navegador.

```text
+-------------------------------------------------------------------+
|                     NAVEGADOR DEL ASTRONAUTA                      |
|   HTML (Estructura) + CSS (Cabina/Alertas) + JS (Canvas/Fetch)    |
+---------------------------------+---------------------------------+
                                  | HTTP / JSON (localhost:8080)
+---------------------------------v---------------------------------+
|                      BACKEND EN LENGUAJE C                        |
|  1. Servidor HTTP single-thread (mongoose) + JSON (cJSON)         |
|  2. Motor de Evaluación de Riesgos HRP (9 Reglas / 5 Peligros)    |
|  3. Gestión de Tripulación, Misiones y Ring Buffers (O(1))        |
|  4. Persistencia Atómica Local en archivos JSON (data/*.json)     |
+-------------------------------------------------------------------+
```

### Decisiones Clave de Arquitectura
* **Cero dependencias externas en tiempo de ejecución:** las librerías `mongoose.c/.h` (servidor HTTP) y `cJSON.c/.h` (parser/serializador JSON) se incluyen vendoreadas en `third_party/` y se compilan estáticamente junto al proyecto.
* **Servidor single-threaded event-driven:** el bucle principal de `mongoose` procesa las peticiones secuencialmente, eliminando condiciones de carrera en memoria y en disco sin necesidad de hilos ni mutex complejos.
* **Funcionamiento 100% offline:** el frontend usa `<canvas>` nativo y `fetch()` contra `localhost:8080`, sin CDNs, fuentes externas ni servicios en la nube.

---

## 3. Estructura de Directorios del Proyecto

```text
Los-Minions-de-Gru-SpaceApps/
├── Documentacion/
│   ├── Imagenes de referencia/
│   │   ├── DibujodePagina.jpeg # Boceto de estructura principal y tarjetas
│   │   └── Credenciales.jpeg   # Referencia estética de tarjetas tipo credencial
│   ├── fuentes.md              # Enlaces oficiales NASA HRP y CSA
│   ├── arquitectura.md         # Este documento: visión general y estructura
│   ├── functions.md            # Contrato técnico del backend en C y API REST
│   ├── frontend_ui.md          # Especificación de interfaz web y experiencia UX
│   └── datos_nasa.md           # Matriz médica, umbrales HRP y escenarios seed
├── src/
│   ├── main.c                  # Punto de entrada, carga inicial y loop HTTP
│   ├── health_types.h          # Enums, constantes NASA y structs compartidos
│   ├── astronaut.c / .h        # Operaciones de astronauta, validación y ring buffer
│   ├── mission.c / .h          # Gestión de misiones, tripulación y CabinEnv
│   ├── health_engine.c / .h    # Motor de reglas HRP, promedios y contramedidas
│   ├── storage.c / .h          # Carga y escritura atómica en data/*.json
│   └── server.c / .h           # Enrutamiento HTTP, API REST y archivos estáticos
├── third_party/
│   ├── mongoose.c / .h         # Servidor HTTP embebido
│   └── cJSON.c / .h            # Parser y generador JSON en C
├── tests/
│   └── test_engine.c           # Pruebas unitarias en C (reglas HRP y validación)
├── web/
│   ├── index.html              # Panel principal de salud de tripulación
│   ├── styles.css              # Tema oscuro de cabina espacial y semáforos
│   ├── app.js                  # Lógica cliente, gráficos Canvas y polling
│   └── assets/avatars/         # Fotos locales de astronautas (100% offline)
├── data/
│   ├── defaults.json           # Línea base promedio de referencia NASA
│   ├── astronauts/             # Un archivo .json por astronauta (con historial)
│   │   ├── astro_01.json
│   │   └── astro_02.json
│   └── missions/               # Un archivo .json por misión y su CabinEnv
│       └── artemis_sim.json
└── Makefile                    # Compilación (make, make run, make test, make clean)
```

---

## 4. Flujo de Compilación y Ejecución (`Makefile`)

* **`make`**: compila el proyecto con `gcc`/`clang` (`-Wall -Wextra -std=c11`) generando `./space_health_server`.
* **`make run`**: compila si hace falta y levanta el servidor local en `http://localhost:8080`, cargando `data/defaults.json`, `data/missions/*.json` y `data/astronauts/*.json`.
* **`make test`**: compila y ejecuta `tests/test_engine.c` para verificar las 9 reglas médicas HRP (incluyendo fisiología hormonal femenina), el buffer circular, la subida de avatares, el glosario `/api/glossary` y la validación de rangos.
* **`make clean`**: limpia binarios y archivos objeto temporales.

---

## 5. Checklist de Coordinación e Integración
- [x] Unificar el contrato de datos (structs en C, campos JSON y endpoints REST) entre backend y frontend.
- [x] Incorporar librerías vendoreadas (`mongoose` y `cJSON`) en `third_party/` y configurar el `Makefile`.
- [x] Implementar datos semilla (`data/defaults.json`, misión inicial y tripulantes con ~14 días de historial).
- [x] Implementar y probar con `make test` el motor de evaluación `health_engine.c`.
- [x] Verificar el flujo completo en navegador `100% offline`, incluyendo cambio de latencia a modo Marte (`autonomous_mode`).

---

## 6. Cambios y Mejoras de Revisión Final
1. **Soporte de banderas CLI y variables de entorno (`src/main.c`):** se añadió soporte opcional para `--port <puerto>` (o variable `PORT`) y `--data-dir <directorio>` manteniendo por defecto el puerto `8080` y el directorio `data/`, lo que facilita ejecutar pruebas aisladas de integración sin alterar los datos semilla.
2. **Latencia de respuesta del bucle de eventos (`src/main.c`):** se optimizó el intervalo de `mg_mgr_poll()` a `50 ms` para respuestas HTTP instantáneas en local.
3. **Pruebas integrales HTTP en `tests/test_engine.c`:** además de las pruebas unitarias médicas y de buffer circular, `make test` levanta una instancia real del servidor HTTP en un puerto efímero local y verifica las rutas REST (incluidos `GET /api/glossary`, `POST /api/avatars`, métricas hormonales y criterios de **Prioridad Total**) y la entrega de archivos estáticos.
4. **Actualizaciones de usabilidad, estética y fisiología femenina:** se añadió menú lateral `||| OPTIONS` plegable/desplegable con persistencia local, tema espacial de tonos más oscuros y alto contraste (`#010307` / `#00e5ff`), vista dedicada de **Glosario Clínico NASA HRP / CSA** con tarjetas colapsables y categorías de **Medidas/Unidades** y **Siglas/Acrónimos**, carga de fotografías personalizadas en PNG/JPG/WEBP/GIF/SVG (`storage_save_custom_avatar`) y soporte estricto para sexo biológico y ciclo hormonal/menstrual femenino (**Regla 9**).
5. **Experiencia clínica depurada y Modo Prioridad Total:** los paneles estadísticos (resumen de misión y vitales/gráficos del astronauta) inician colapsados por defecto; la grilla incorpora filtros combinables por nombre, apellido, gravedad, aptitud EVA y sexo; el mapa corporal SVG opera en formato ampliado de 2 columnas con sincronización interactiva; y los estados **MUY CRÍTICO** (`is_very_critical == 1`) activan bloqueo de Prioridad Total centrado en el tripulante afectado sin exponer identificadores internos de C o JS en pantalla.



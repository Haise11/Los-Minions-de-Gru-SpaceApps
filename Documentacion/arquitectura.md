# 01 - Arquitectura General del Sistema

## 1. Propósito del Proyecto
Desarrollar un software autónomo de monitoreo de salud para astronautas en misiones espaciales de larga duración (desafío **Health Monitoring Software** — NASA Space Apps Challenge 2026). El sistema centraliza variables fisiológicas, dosimetría de radiación y métricas conductuales para evaluar riesgos según los lineamientos del *Human Research Program (HRP)* y ofrecer contramedidas accionables sin depender de comunicación en tiempo real con la Tierra.

---

## 2. Arquitectura Técnica (Backend en C + Interfaz Web Integrada)
Toda la estructura central del programa y el ámbito del **backend** estarán desarrollados íntegramente en **lenguaje C**. A su vez, el backend en C actuará como servidor e integrador que **mezcla, sirve y articula** la capa visual en **HTML, CSS y JavaScript**.

```text
+-------------------------------------------------------------------+
|                     NAVEGADOR DEL ASTRONAUTA                      |
|   HTML (Estructura) + CSS (Estilos/Alertas) + JS (Gráficos/API)   |
+---------------------------------+---------------------------------+
                                  | HTTP / JSON (localhost:PUERTO)
+---------------------------------v---------------------------------+
|                      BACKEND EN LENGUAJE C                        |
|  1. Servidor HTTP / Sockets TCP (sirve HTML/CSS/JS y endpoints)   |
|  2. Motor de Evaluación de Riesgos HRP (5 Peligros + Umbrales)    |
|  3. Gestor de Telemetría y Persistencia Local (CSV / JSON / Bin)  |
+-------------------------------------------------------------------+
```

---

## 3. Estructura Propuesta de Directorios
```text
Los-Minions-de-Gru-SpaceApps/
├── Documentacion/
│   ├── fuentes.md
│   ├── 01_arquitectura.md
│   ├── 02_backend_c.md
│   ├── 03_servidor_http.md
│   ├── 04_frontend_ui.md
│   └── 05_datos_nasa.md
├── src/
│   ├── main.c              # Punto de entrada e inicialización del servidor y motor
│   ├── server.c / .h       # Servidor HTTP embebido en C y enrutamiento
│   ├── health_engine.c/.h  # Lógica de evaluación de riesgos y contramedidas HRP
│   ├── telemetry.c / .h    # Carga, simulación y almacenamiento de datos biométricos
│   └── json_utils.c / .h   # Serialización de respuestas JSON desde C
├── web/
│   ├── index.html          # Interfaz principal del panel de salud
│   ├── styles.css          # Diseño de cabina, alto contraste y semáforo de alertas
│   └── app.js              # Consumo de API en C, gráficos dinámicos e interactividad
├── data/
│   └── sample_telemetry.csv # Datos simulados basados en Standard Measures / HRP
└── Makefile                # Script de compilación estándar (gcc / clang)
```

---

## 4. Tareas de Coordinación e Integración
- [ ] Definir el contrato de datos (campos de telemetría y formato JSON) entre el backend en C y el frontend en JS.
- [ ] Configurar el `Makefile` para compilar todo el proyecto en C con un único comando (`make`).
- [ ] Verificar que al ejecutar el binario compilado (`./health_monitor`), se inicie el servidor local y se pueda acceder desde el navegador sin dependencias externas.
- [ ] Probar el flujo completo en modo 100% offline (simulando latencia o desconexión con la Tierra).

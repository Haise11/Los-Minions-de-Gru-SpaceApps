/* =========================================================================
 * Monitor Medico de Tripulacion (Frontend 100% Offline JS)
 * ========================================================================= */

let currentMissionId = "artemis_sim";
let currentAstronautId = null;
let activeOptionView = "mostrar";
let crewFilter = "ALL";
let crewFiltersExpanded = false;
let defaults = null;
let currentMissionData = null;
let allCrewList = [];
let currentAstronautDetail = null;
let pollTimerId = null;
let prefillRequestSeq = 0;

/* Estado de barras estadisticas colapsables (ocultas por defecto) */
let detailStatsExpanded = false;
let summaryStatsExpanded = false;

/* Estado de casos en Urgencia (informativo + atajo flotante, sin bloqueos) */
let isPriorityMode = false;
let urgentAstronautIds = [];
let priorityAstronautIds = new Set();
let topPriorityAstronautId = null;

/* Estado de carga de imagenes personalizadas (Base64 Data URL) */
let newAstronautCustomAvatarDataUrl = null;
let jsonCustomAvatarDataUrl = null;
let modCustomAvatarDataUrl = null;
let modOriginalAvatarPath = null;
let avatarCacheBust = 0;

/* Parte o segmento corporal seleccionado en el Mapa Corporal */
let selectedBodyPartKey = null;
let crewDetailsMap = {};

/* Guias breves e inmediatamente accionables para contramedidas aplicadas */
const COUNTERMEASURE_GUIDANCE = [
  {
    mask: 32,
    title: "Banda de Compresión en Muslos",
    howToAct: "1) Ajustar en tercio superior de ambos muslos por 2–4 h diurnas. 2) Vigilar temperatura y pulso distal. 3) Retirar al dormir o ante hormigueo."
  },
  {
    mask: 64,
    title: "Suplementación Complejo B",
    howToAct: "1) Tomar 1 comprimido diario con comida sólida y 250 ml de agua. 2) Priorizar ante baja visual SANS, cefalea o uso de hierro."
  },
  {
    mask: 128,
    title: "Suplemento de Vitamina D3",
    howToAct: "1) Tomar 1 dosis diaria junto a la comida principal. 2) Combinar hoy con carga ósea en ARED / CEVIS o protocolo Zero-T2."
  },
  {
    mask: 256,
    title: "Dieta Vegetal Fresca (Veggie)",
    howToAct: "1) Sumar 1 porción fresca del cultivo Veggie en el menú de hoy. 2) Acompañar con hidratación para optimizar antioxidantes y fibra."
  }
];

function resolveAvatarUrl(path) {
  const base = path && path.length > 0 ? path : "assets/avatars/astro_01.svg";
  if (avatarCacheBust > 0 && base.startsWith("assets/avatars/custom_")) {
    return `${base}?v=${avatarCacheBust}`;
  }
  return base;
}

/* Estado del Glosario NASA HRP / CSA (tarjetas colapsadas por defecto) */
let glossaryData = null;
let activeGlossaryCategory = "ALL";
let glossaryExpandedIds = new Set();

/* Estado del Mini-Test de Reflejos (3 toques) */
let reflexState = {
  active: false,
  phase: "idle", /* idle | waiting | ready */
  timeoutId: null,
  startTimeMs: 0,
  samples: []
};

const CYCLE_PHASE_NAMES = [
  "No Aplica",
  "Menstrual",
  "Folicular",
  "Ovulatoria",
  "Lútea",
  "Suprimido (LARC/ACO)"
];

const SYSTEM_NAMES = [
  "0 · Cardiovascular / Hemostasia",
  "1 · Neuro-Ocular (SANS / Vestibular)",
  "2 · Respiratorio / EVA (O₂ / CO₂ / DCS)",
  "3 · Musculoesquelético (Zero-T2 / Óseo)",
  "4 · Inmunológico y Metabólico",
  "5 · Conductual, Sueño y Nutrición",
  "6 · Radiación Espacial (Dosimetría)"
];

const CATEGORY_LABELS_ES = {
  HAZARDS: "Factor de Riesgo",
  PROTOCOLS: "Protocolo",
  SYMPTOMS: "Síntoma / Clínica",
  HORMONAL: "Salud Femenina",
  THRESHOLDS: "Umbrales",
  MEASURES: "Medidas y Unidades",
  ACRONYMS: "Siglas y Acrónimos"
};

const SEVERITY_LABELS_ES = {
  OK: "Normal",
  CAUTION: "Precaución",
  CRITICAL: "Crítico"
};

const RANK_TITLES = new Set([
  "cmdr.", "cmdr", "dr.", "dr", "dra.", "dra", "ing.", "ing",
  "esp.", "esp", "capt.", "capt", "cap.", "cap", "lt.", "lt",
  "col.", "col", "prof.", "prof"
]);

const HAZARD_META = [
  {
    id: 0,
    name: "1. Radiación Espacial (GCR / Tormentas Solares SPE)",
    desc: "Dosimetría acumulada de carrera (límite 600 mSv), picos solares y monitoreo neutrónico Radi-N2 / CANS."
  },
  {
    id: 1,
    name: "2. Aislamiento y Confinamiento (ARCHeR / HFBP)",
    desc: "Deuda de sueño (3d), fatiga cognitiva (PVT), estrés, cohesión de equipo y aceptabilidad nutricional."
  },
  {
    id: 2,
    name: "3. Distancia de la Tierra (Autonomía Médica)",
    desc: "Latencia de comunicación (>5.0 s activa Modo Autónomo a bordo y exportación diferida de bitácora)."
  },
  {
    id: 3,
    name: "4. Campos Gravitatorios (SANS / Cardio / Óseo / Vestibular)",
    desc: "Desplazamiento cefálico de fluidos, estasis venosa, pérdida ósea/muscular y transición a gravedad."
  },
  {
    id: 4,
    name: "5. Entornos Cerrados y Hostiles (ECLSS / EVA / Microbioma)",
    desc: "Hipoxia (pO₂), hipercapnia por falla de depurador (pCO₂), enfermedad descompresiva (DCS) e inmunidad."
  }
];

function normalizeText(str) {
  return String(str || "")
    .normalize("NFD")
    .replace(/[\u0300-\u036f]/g, "")
    .toLowerCase()
    .trim();
}

function parseAstronautNameParts(fullName) {
  const tokens = String(fullName || "").trim().split(/\s+/).filter(Boolean);
  while (tokens.length > 1 && RANK_TITLES.has(normalizeText(tokens[0]))) {
    tokens.shift();
  }
  if (tokens.length === 0) return { firstName: "", lastName: "" };
  if (tokens.length === 1) return { firstName: tokens[0], lastName: "" };
  return {
    firstName: tokens.slice(0, -1).join(" "),
    lastName: tokens[tokens.length - 1]
  };
}

function isCrewVeryCritical(c) {
  if (!c) return false;
  if (c.is_very_critical === 1) return true;
  return c.global_status === 2 && ((c.health_score ?? 100) <= 50 || (c.critical_alert_count || 0) >= 2);
}

/* =========================================================================
 * Inicializacion y Polling Inteligente
 * ========================================================================= */
document.addEventListener("DOMContentLoaded", async () => {
  try {
    if (localStorage.getItem("hrp_sidebar_collapsed") === "1") {
      setSidebarCollapsed(true);
    }
  } catch (_) {}

  setupBodyMapInteractions();
  setCrewFiltersExpanded(false);
  setDetailStatsExpanded(false);
  setSummaryStatsExpanded(false);

  await loadDefaults();
  await refreshMissionState(true);
  loadGlossaryData();

  /* Polling cada 4 segundos sin pisar formularios activos */
  pollTimerId = setInterval(() => {
    refreshMissionState(false);
  }, 4000);
});

function setSidebarCollapsed(collapsed) {
  const shell = document.getElementById("app-shell");
  const toggleBtn = document.getElementById("sidebar-toggle-btn");
  if (!shell) return;
  shell.classList.toggle("sidebar-collapsed", collapsed);
  if (toggleBtn) {
    toggleBtn.setAttribute("aria-expanded", collapsed ? "false" : "true");
  }
  try {
    localStorage.setItem("hrp_sidebar_collapsed", collapsed ? "1" : "0");
  } catch (_) {}
}

function toggleSidebar() {
  const shell = document.getElementById("app-shell");
  if (!shell) return;
  const isCollapsed = shell.classList.contains("sidebar-collapsed");
  setSidebarCollapsed(!isCollapsed);
}

function setDetailStatsExpanded(expanded) {
  detailStatsExpanded = !!expanded;
  const panel = document.getElementById("detail-stats-panel");
  const btn = document.getElementById("detail-stats-toggle-btn");
  const icon = document.getElementById("detail-stats-toggle-icon");
  if (panel) panel.classList.toggle("hidden", !detailStatsExpanded);
  if (btn) {
    btn.setAttribute("aria-expanded", detailStatsExpanded ? "true" : "false");
    const labelSpan = btn.querySelectorAll("span")[1];
    if (labelSpan) {
      labelSpan.textContent = detailStatsExpanded
        ? "📊 Ocultar Estadísticas Clínicas y Gráficos Históricos"
        : "📊 Mostrar Estadísticas Clínicas y Gráficos Históricos";
    }
  }
  if (icon) icon.textContent = detailStatsExpanded ? "▾" : "▸";
  if (detailStatsExpanded && currentAstronautDetail) {
    renderAstronautCharts(currentAstronautDetail);
  }
}

function toggleDetailStatsPanel() {
  setDetailStatsExpanded(!detailStatsExpanded);
}

function setSummaryStatsExpanded(expanded) {
  summaryStatsExpanded = !!expanded;
  const panel = document.getElementById("summary-stats-panel");
  const btn = document.getElementById("summary-stats-toggle-btn");
  const icon = document.getElementById("summary-stats-toggle-icon");
  if (panel) panel.classList.toggle("hidden", !summaryStatsExpanded);
  if (btn) {
    btn.setAttribute("aria-expanded", summaryStatsExpanded ? "true" : "false");
    const labelSpan = btn.querySelectorAll("span")[1];
    if (labelSpan) {
      labelSpan.textContent = summaryStatsExpanded
        ? "📊 Ocultar Estadísticas Generales de la Tripulación"
        : "📊 Mostrar Estadísticas Generales de la Tripulación";
    }
  }
  if (icon) icon.textContent = summaryStatsExpanded ? "▾" : "▸";
}

function toggleSummaryStatsPanel() {
  setSummaryStatsExpanded(!summaryStatsExpanded);
}

function showToast(message, isError = false) {
  const banner = document.getElementById("toast-banner");
  if (!banner) return;
  banner.textContent = message;
  banner.style.borderColor = isError ? "#F87171" : "#D4D4D4";
  banner.style.background = isError
    ? "rgba(248, 113, 113, 0.2)"
    : "rgba(212, 212, 212, 0.14)";
  banner.classList.remove("hidden");
  clearTimeout(banner._hideTimer);
  banner._hideTimer = setTimeout(() => {
    banner.classList.add("hidden");
  }, 4200);
}

async function loadDefaults() {
  try {
    const res = await fetch("/api/defaults");
    if (res.ok) {
      defaults = await res.json();
      const fcInput = document.getElementById("na-fc-base");
      const suenoInput = document.getElementById("na-sueno-base");
      if (fcInput && defaults.fc_base) fcInput.value = defaults.fc_base;
      if (suenoInput && defaults.sueno_base_h) suenoInput.value = defaults.sueno_base_h;
    }
  } catch (err) {
    console.error("Error cargando valores iniciales:", err);
  }
}

async function refreshMissionState(initialLoad = false) {
  try {
    const [missionRes, crewRes] = await Promise.all([
      fetch(`/api/missions/${encodeURIComponent(currentMissionId)}`),
      fetch("/api/astronauts")
    ]);
    if (!missionRes.ok && !crewRes.ok) return;

    if (missionRes.ok) {
      currentMissionData = await missionRes.json();
    } else {
      currentMissionData = { id: currentMissionId, env: {}, summary: {}, crew: [] };
    }

    if (crewRes.ok) {
      const crewArr = await crewRes.json();
      allCrewList = Array.isArray(crewArr) ? crewArr : (currentMissionData.crew || []);
    } else {
      allCrewList = currentMissionData.crew || [];
    }

    const detailResults = await Promise.allSettled(
      allCrewList.map(async (c) => {
        const r = await fetch(`/api/astronauts/${encodeURIComponent(c.id)}`);
        if (!r.ok) return null;
        return await r.json();
      })
    );
    const nextMap = {};
    detailResults.forEach((res) => {
      if (res.status === "fulfilled" && res.value && res.value.id) {
        nextMap[res.value.id] = res.value;
      }
    });
    crewDetailsMap = nextMap;

    updateUrgentStatus(allCrewList);
    renderCrewRoutineNudges();
    updateMostrarLayoutVisibility();
    renderCrewGrid(allCrewList);
    renderPromedioView(currentMissionData, allCrewList, initialLoad);
    populateCheckinAstronautSelector(allCrewList, initialLoad);
    populateModifyAstronautSelector(allCrewList, initialLoad);

    if (currentAstronautId) {
      await loadAstronautDetail(currentAstronautId, false);
    }
  } catch (err) {
    console.error("Error refrescando estado clínico:", err);
  }
}

function updateUrgentStatus(crewList) {
  const crew = crewList || [];
  const vcList = crew.filter(isCrewVeryCritical);
  isPriorityMode = vcList.length > 0;
  urgentAstronautIds = vcList.map((c) => c.id);
  priorityAstronautIds = new Set(urgentAstronautIds);
  topPriorityAstronautId = urgentAstronautIds[0] || null;

  document.body.classList.toggle("priority-mode-active", isPriorityMode);

  const banner = document.getElementById("priority-emergency-banner");
  const labelEl = document.getElementById("priority-patient-label");
  const reasonEl = document.getElementById("priority-patient-reason");
  if (banner) {
    banner.classList.toggle("hidden", !isPriorityMode);
  }
  if (isPriorityMode && topPriorityAstronautId) {
    const topCrew = vcList[0];
    if (labelEl && topCrew) {
      labelEl.textContent = `${topCrew.nombre} — Salud: ${topCrew.health_score}% (${topCrew.resumen_estado || "Urgencia"})`;
    }
    if (reasonEl) {
      const extra = vcList.length > 1 ? ` · ${vcList.length} tripulantes en estado de urgencia` : "";
      reasonEl.textContent = `Atención clínica prioritaria recomendada${extra}. Usa el atajo flotante para ir directo en cualquier momento.`;
    }
  }

  const floatBtn = document.getElementById("floating-urgent-btn");
  const floatLbl = document.getElementById("floating-urgent-label");
  if (floatBtn) {
    floatBtn.classList.toggle("hidden", !isPriorityMode);
  }
  if (floatLbl && isPriorityMode) {
    floatLbl.textContent = vcList.length > 1
      ? `🚨 Urgencia (${vcList.length})`
      : "🚨 Ir a Urgencia";
  }
}

function focusUrgentAstronaut() {
  if (!urgentAstronautIds || urgentAstronautIds.length === 0) return;
  let targetId = urgentAstronautIds[0];
  if (activeOptionView === "mostrar" && currentAstronautId) {
    const idx = urgentAstronautIds.indexOf(currentAstronautId);
    if (idx !== -1 && urgentAstronautIds.length > 1) {
      targetId = urgentAstronautIds[(idx + 1) % urgentAstronautIds.length];
    } else if (idx !== -1) {
      const panel = document.getElementById("astronaut-detail-panel");
      if (panel) panel.scrollIntoView({ behavior: "smooth", block: "start" });
      return;
    }
  }
  switchOptionView("mostrar");
  if (currentAstronautId !== targetId) {
    setDetailStatsExpanded(false);
  }
  currentAstronautId = targetId;
  updateMostrarLayoutVisibility();
  loadAstronautDetail(targetId, true);
}

function focusPriorityAstronaut() {
  focusUrgentAstronaut();
}

function setCrewFiltersExpanded(expanded) {
  crewFiltersExpanded = !!expanded;
  const bar = document.getElementById("crew-filter-bar");
  const btn = document.getElementById("filter-toggle-btn");
  const chev = document.getElementById("filter-toggle-chevron");
  if (bar) {
    const showBar = crewFiltersExpanded && activeOptionView === "mostrar" && !currentAstronautId;
    bar.classList.toggle("hidden", !showBar);
  }
  if (btn) {
    btn.setAttribute("aria-expanded", crewFiltersExpanded ? "true" : "false");
    btn.classList.toggle("active", crewFiltersExpanded);
  }
  if (chev) {
    chev.textContent = crewFiltersExpanded ? "▴" : "▾";
  }
}

function toggleCrewFilterDrawer() {
  setCrewFiltersExpanded(!crewFiltersExpanded);
}

function updateMostrarLayoutVisibility() {
  const isMostrar = activeOptionView === "mostrar";
  const hasDetail = !!currentAstronautId;

  const filterArea = document.getElementById("topbar-filter-area");
  if (filterArea) {
    filterArea.classList.toggle("hidden", !isMostrar || hasDetail);
  }

  const filterBar = document.getElementById("crew-filter-bar");
  if (filterBar) {
    filterBar.classList.toggle("hidden", !isMostrar || hasDetail || !crewFiltersExpanded);
  }

  const crewGrid = document.getElementById("crew-grid");
  if (crewGrid) {
    crewGrid.classList.toggle("hidden", hasDetail);
  }

  const nudgesPanel = document.getElementById("crew-routine-nudges");
  if (nudgesPanel) {
    const hasContent = nudgesPanel.dataset.hasNudges === "1";
    nudgesPanel.classList.toggle("hidden", !isMostrar || hasDetail || !hasContent);
  }
}

/* =========================================================================
 * Recordatorios Proactivos de Rutina de Mision (Ejercicio, Dieta, Agua, Descanso)
 * ========================================================================= */
function computeAstronautRoutineNudges(astro) {
  if (!astro) return [];
  const hist = astro.history || [];
  const latest = hist.length > 0 ? hist[hist.length - 1] : null;
  if (!latest) return [];

  const b = astro.baseline || {};
  const isFem = (astro.sexo || "M").toUpperCase() === "F";
  const flags = latest.sintomas_flags || 0;
  const nudges = [];

  /* 1. Racha sin dia de descanso programado (>= 6 dias activos seguidos) */
  const recent7 = hist.slice(-7);
  const restIn7 = recent7.filter((s) => s.dia_descanso === 1).length;
  let consecutiveActive = 0;
  for (let i = hist.length - 1; i >= 0; i--) {
    if (hist[i].dia_descanso === 1) break;
    consecutiveActive++;
  }
  if (latest.dia_descanso !== 1 && consecutiveActive >= 6 && restIn7 === 0) {
    nudges.push({
      id: "rest_day",
      astroId: astro.id,
      astroName: astro.nombre,
      kind: "rest",
      title: `Programar día de descanso (${consecutiveActive} días activos seguidos)`,
      desc: "Puedes registrar un día de descanso programado para descarga muscular (hasta 1–2/sem sin penalizar el promedio de 7 días).",
      actionType: "mark_rest",
      actionLabel: "Marcar Descanso Hoy"
    });
  }

  /* 2. Ejercicio pendiente del dia (si no es dia de descanso) */
  const ejTarget = b.ejercicio_objetivo_min || 90;
  if (latest.dia_descanso !== 1 && (latest.ejercicio_min || 0) < ejTarget) {
    const missingEj = Math.max(5, ejTarget - (latest.ejercicio_min || 0));
    nudges.push({
      id: "exercise",
      astroId: astro.id,
      astroName: astro.nombre,
      kind: "routine",
      title: `Completar ${missingEj} min de ejercicio diario (${latest.ejercicio_min || 0}/${ejTarget} min)`,
      desc: "Protocolo diario ARED / CEVIS / Zero-T2 para proteger densidad ósea y masa muscular.",
      actionType: "goto_checkin",
      actionLabel: "Actualizar en Check-In"
    });
  }

  /* 3. Hidratacion diaria pendiente */
  const hidTarget = b.hidratacion_objetivo_ml || 2500;
  if ((latest.hidratacion_ml || 0) < Math.round(hidTarget * 0.9)) {
    const missingMl = Math.max(100, hidTarget - (latest.hidratacion_ml || 0));
    nudges.push({
      id: "hydration",
      astroId: astro.id,
      astroName: astro.nombre,
      kind: "routine",
      title: `Beber +${missingMl} ml de agua hoy (${latest.hidratacion_ml || 0}/${hidTarget} ml)`,
      desc: "Sostener volemia plasmática y prevenir estasis venosa o fatiga por deshidratación.",
      actionType: "add_water",
      actionLabel: "+350 ml Ahora"
    });
  }

  /* 4. Dieta Vegetal Fresca (Veggie) */
  if ((flags & 256) === 0) {
    nudges.push({
      id: "veggie",
      astroId: astro.id,
      astroName: astro.nombre,
      kind: "routine",
      title: "Incluir porción fresca de cultivo Veggie en la dieta de hoy",
      desc: "Aporta antioxidantes, folato y variedad nutricional frente a fatiga de menú en órbita.",
      actionType: "mark_flag_256",
      actionLabel: "Registrar Veggie"
    });
  }

  /* 5. Micronutrientes: Vitamina D3 y Hierro femenino en menstruacion */
  if ((flags & 128) === 0) {
    nudges.push({
      id: "vitd",
      astroId: astro.id,
      astroName: astro.nombre,
      kind: "routine",
      title: "Registrar toma diaria de Vitamina D3",
      desc: "Cofactor indispensable para absorción de calcio y salud ósea sin exposición solar.",
      actionType: "mark_flag_128",
      actionLabel: "Registrar Vit. D3"
    });
  }

  if (isFem && (latest.fase_ciclo === 1 || (latest.flujo_menstrual || 0) >= 1) && latest.suplemento_hierro !== 1) {
    nudges.push({
      id: "iron",
      astroId: astro.id,
      astroName: astro.nombre,
      kind: "routine",
      title: "Tomar suplemento de Hierro (Fe) + Complejo B",
      desc: "Compensa pérdida menstrual y hemólisis espacial para prevenir ferropenia.",
      actionType: "mark_iron",
      actionLabel: "Registrar Hierro + Agua"
    });
  }

  return nudges;
}

function buildNudgeHtml(n, showAstroName = true) {
  const namePrefix = showAstroName ? `<span class="mono-tag" style="margin-right:6px;">${escapeHtml(n.astroName)}</span>` : "";
  return `<div class="routine-nudge-item ${n.kind === "rest" ? "nudge-rest" : ""}">
    <div class="routine-nudge-copy">
      <strong>${namePrefix}${escapeHtml(n.title)}</strong>
      <small>${escapeHtml(n.desc)}</small>
    </div>
    <div class="routine-nudge-actions">
      <button type="button" class="action-btn btn-secondary" onclick="applyRoutineQuickAction('${escapeAttr(n.astroId)}', '${escapeAttr(n.actionType)}')">
        ${escapeHtml(n.actionLabel)}
      </button>
    </div>
  </div>`;
}

function renderCrewRoutineNudges() {
  const container = document.getElementById("crew-routine-nudges");
  if (!container) return;

  const nudges = [];
  allCrewList.forEach((c) => {
    const full = crewDetailsMap[c.id];
    if (!full) return;
    const list = computeAstronautRoutineNudges(full);
    /* Mostrar hasta 2 prioridades por tripulante para mantener claridad operativa */
    list.slice(0, 2).forEach((n) => nudges.push(n));
  });

  if (nudges.length === 0) {
    container.dataset.hasNudges = "0";
    container.innerHTML = "";
    container.classList.add("hidden");
    return;
  }

  const shown = nudges.slice(0, 5);
  container.dataset.hasNudges = "1";
  container.innerHTML = `
    <div class="routine-nudges-header">
      <span class="routine-nudges-title">RUTINA OPERATIVA DE MISIÓN · RECORDATORIOS ACTIVOS (${nudges.length})</span>
      <span class="routine-nudges-sub">Acciones preventivas pendientes antes del cierre de jornada</span>
    </div>
    <div class="routine-nudges-list">
      ${shown.map((n) => buildNudgeHtml(n, true)).join("")}
    </div>
  `;
}

function renderDetailRoutineNudges(astro) {
  const container = document.getElementById("detail-routine-nudges");
  if (!container) return;
  const nudges = computeAstronautRoutineNudges(astro);
  if (nudges.length === 0) {
    container.innerHTML = "";
    container.classList.add("hidden");
    return;
  }
  container.classList.remove("hidden");
  container.innerHTML = `
    <div class="routine-nudges-panel routine-nudges-compact">
      <div class="routine-nudges-header">
        <span class="routine-nudges-title">RUTINA PENDIENTE DEL TRIPULANTE (${nudges.length})</span>
      </div>
      <div class="routine-nudges-list">
        ${nudges.slice(0, 3).map((n) => buildNudgeHtml(n, false)).join("")}
      </div>
    </div>
  `;
}

function renderCheckinRoutineNudges(astroId) {
  const container = document.getElementById("ci-routine-nudges");
  if (!container) return;
  const astro = crewDetailsMap[astroId];
  const nudges = computeAstronautRoutineNudges(astro);
  if (nudges.length === 0) {
    container.innerHTML = "";
    container.classList.add("hidden");
    return;
  }
  container.classList.remove("hidden");
  container.innerHTML = `
    <div class="routine-nudges-header">
      <span class="routine-nudges-title">RECORDATORIOS DE RUTINA PARA ESTE CHECK-IN (${nudges.length})</span>
      <span class="routine-nudges-sub">Sugerencias preventivas según historial reciente</span>
    </div>
    <div class="routine-nudges-list">
      ${nudges.map((n) => buildNudgeHtml(n, false)).join("")}
    </div>
  `;
}

function openCheckinForAstronaut(astroId) {
  switchOptionView("checkin");
  const sel = document.getElementById("ci-astronaut");
  if (sel && astroId) {
    sel.value = astroId;
    prefillCheckinFromAstronaut(astroId);
  }
}

async function applyRoutineQuickAction(astroId, actionType) {
  if (!astroId) return;
  if (actionType === "goto_checkin") {
    openCheckinForAstronaut(astroId);
    return;
  }

  let astro = crewDetailsMap[astroId];
  if (!astro) {
    try {
      const r = await fetch(`/api/astronauts/${encodeURIComponent(astroId)}`);
      if (r.ok) astro = await r.json();
    } catch (_) {}
  }
  if (!astro) return;

  const hist = astro.history || [];
  const latest = hist.length > 0 ? hist[hist.length - 1] : null;
  const targetDay = latest ? latest.dia_mision : currentMissionData?.dia_actual || 14;
  const currentFlags = latest ? latest.sintomas_flags || 0 : 0;
  let patch = { dia_mision: targetDay };
  let msg = "Rutina actualizada.";

  if (actionType === "mark_rest") {
    patch.dia_descanso = 1;
    patch.ejercicio_min = 0;
    msg = `Día de descanso programado registrado para ${astro.nombre}.`;
  } else if (actionType === "add_water") {
    patch.hidratacion_ml = Math.min(10000, (latest?.hidratacion_ml || 0) + 350);
    msg = `+350 ml de hidratación registrados para ${astro.nombre}.`;
  } else if (actionType === "mark_flag_256") {
    patch.sintomas_flags = currentFlags | 256;
    msg = `Dieta Vegetal Fresca (Veggie) registrada para ${astro.nombre}.`;
  } else if (actionType === "mark_flag_128") {
    patch.sintomas_flags = currentFlags | 128;
    msg = `Vitamina D3 registrada para ${astro.nombre}.`;
  } else if (actionType === "mark_iron") {
    patch.suplemento_hierro = 1;
    patch.sintomas_flags = currentFlags | 64;
    patch.hidratacion_ml = Math.max(latest?.hidratacion_ml || 0, astro.baseline?.hidratacion_objetivo_ml || 2500);
    msg = `Hierro + Complejo B e hidratación registrados para ${astro.nombre}.`;
  }

  try {
    const res = await fetch(`/api/astronauts/${encodeURIComponent(astroId)}/samples`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(patch)
    });
    if (res.ok) {
      showToast(msg);
      await refreshMissionState(true);
      if (activeOptionView === "checkin") {
        prefillCheckinFromAstronaut(astroId);
      }
    } else {
      const err = await res.json().catch(() => ({}));
      showToast(err.error || "No se pudo actualizar la rutina", true);
    }
  } catch (err) {
    showToast("Error de red al actualizar rutina", true);
  }
}

/* =========================================================================
 * Navegacion entre Vistas (OPCIONES: MOSTRAR | CHECK IN | PROMEDIO | GLOSARIO | CARGAR | MODIFICAR)
 * ========================================================================= */
function switchOptionView(viewName, resetDetailOnMostrar = false) {
  activeOptionView = viewName;
  const views = ["mostrar", "checkin", "promedio", "glosario", "cargar", "modificar"];
  views.forEach((v) => {
    const sec = document.getElementById(`view-${v}`);
    const btn = document.getElementById(`nav-${v}`);
    if (sec) sec.classList.toggle("hidden", v !== viewName);
    if (btn) btn.classList.toggle("active", v === viewName);
  });
  if (viewName === "mostrar" && resetDetailOnMostrar && currentAstronautId) {
    closeAstronautDetail();
  } else {
    updateMostrarLayoutVisibility();
  }
  if (viewName === "modificar") {
    const sel = document.getElementById("mod-astronaut-select");
    const preferred = currentAstronautId || (sel && sel.value) || (allCrewList[0]?.id || "");
    if (sel && preferred) {
      sel.value = preferred;
      loadAstronautForModify(preferred);
    }
  }
  if (viewName === "glosario" && !glossaryData) {
    loadGlossaryData();
  }
}

function switchCargarTab(tabName) {
  const activeTab = tabName === "file" ? "file" : "create";
  const tabs = ["create", "file"];
  tabs.forEach((t) => {
    const panel = document.getElementById(`cargar-tab-${t}`);
    const btn = document.getElementById(`subtab-btn-${t}`);
    if (panel) panel.classList.toggle("hidden", t !== activeTab);
    if (btn) btn.classList.toggle("active", t === activeTab);
  });
}

function setCrewFilter(filterVal) {
  crewFilter = filterVal || "ALL";
  const sevSelect = document.getElementById("filter-severity");
  if (sevSelect && sevSelect.value !== crewFilter) {
    sevSelect.value = crewFilter;
  }
  renderCrewGrid(allCrewList);
  if (activeOptionView !== "mostrar") {
    switchOptionView("mostrar");
  }
}

function applyCrewFilters() {
  renderCrewGrid(allCrewList);
}

function clearCrewFilters() {
  const firstEl = document.getElementById("filter-first-name");
  const lastEl = document.getElementById("filter-last-name");
  const evaEl = document.getElementById("filter-eva");
  const sexEl = document.getElementById("filter-sex");
  const sortEl = document.getElementById("filter-sort");
  if (firstEl) firstEl.value = "";
  if (lastEl) lastEl.value = "";
  if (evaEl) evaEl.value = "ALL";
  if (sexEl) sexEl.value = "ALL";
  if (sortEl) sortEl.value = "PRIORITY";
  setCrewFilter("ALL");
}

/* =========================================================================
 * Renderizado de Grilla de Tarjetas
 * ========================================================================= */

function severityClass(sev) {
  if (sev === 2) return "critical";
  if (sev === 1) return "caution";
  return "ok";
}

function evaBadgeClass(apt) {
  if (apt === 2) return "eva-nogo";
  if (apt === 1) return "eva-restricted";
  return "eva-go";
}

function formatEvaAptitudeEs(apt, strFallback) {
  if (apt === 2) return "No apto";
  if (apt === 1) return "Restringido";
  if (apt === 0) return "Apto";
  if (strFallback === "NO-GO" || strFallback === "No apto") return "No apto";
  if (strFallback === "RESTRICTED" || strFallback === "Restringido") return "Restringido";
  return "Apto";
}

function renderCrewGrid(crewList) {
  const grid = document.getElementById("crew-grid");
  const counter = document.getElementById("crew-counter-label");
  if (!grid) return;

  const firstQuery = normalizeText(document.getElementById("filter-first-name")?.value || "");
  const lastQuery = normalizeText(document.getElementById("filter-last-name")?.value || "");
  const evaFilter = document.getElementById("filter-eva")?.value || "ALL";
  const sexFilter = document.getElementById("filter-sex")?.value || "ALL";
  const sortMode = document.getElementById("filter-sort")?.value || "PRIORITY";

  const filtered = crewList.filter((c) => {
    const vc = isCrewVeryCritical(c);
    if (crewFilter === "VERY_CRITICAL" && !vc) return false;
    if (crewFilter === "CRITICAL" && c.global_status !== 2) return false;
    if (crewFilter === "CAUTION" && c.global_status !== 1) return false;
    if (crewFilter === "OK" && c.global_status !== 0) return false;

    if (evaFilter === "GO" && c.eva_aptitude !== 0) return false;
    if (evaFilter === "RESTRICTED" && c.eva_aptitude !== 1) return false;
    if (evaFilter === "NO_GO" && c.eva_aptitude !== 2) return false;

    const cSex = (c.sexo || "M").toUpperCase();
    if (sexFilter !== "ALL" && cSex !== sexFilter) return false;

    if (firstQuery || lastQuery) {
      const parts = parseAstronautNameParts(c.nombre);
      if (firstQuery && !normalizeText(parts.firstName).includes(firstQuery)) return false;
      if (lastQuery && !normalizeText(parts.lastName).includes(lastQuery)) return false;
    }
    return true;
  });

  filtered.sort((a, b) => {
    if (sortMode === "SCORE_ASC") return (a.health_score ?? 100) - (b.health_score ?? 100);
    if (sortMode === "SCORE_DESC") return (b.health_score ?? 100) - (a.health_score ?? 100);
    const pa = parseAstronautNameParts(a.nombre);
    const pb = parseAstronautNameParts(b.nombre);
    if (sortMode === "FIRST_NAME") return pa.firstName.localeCompare(pb.firstName, "es");
    if (sortMode === "LAST_NAME") return pa.lastName.localeCompare(pb.lastName, "es");

    /* PRIORITY por defecto */
    const vca = isCrewVeryCritical(a) ? 1 : 0;
    const vcb = isCrewVeryCritical(b) ? 1 : 0;
    if (vcb !== vca) return vcb - vca;
    if ((b.global_status || 0) !== (a.global_status || 0)) {
      return (b.global_status || 0) - (a.global_status || 0);
    }
    if ((a.health_score ?? 100) !== (b.health_score ?? 100)) {
      return (a.health_score ?? 100) - (b.health_score ?? 100);
    }
    return pa.lastName.localeCompare(pb.lastName, "es");
  });

  if (counter) {
    counter.textContent = `${filtered.length} de ${crewList.length} tripulantes`;
  }

  if (filtered.length === 0) {
    grid.innerHTML = `<div class="detail-box" style="grid-column: 1 / -1; text-align:center; color:#cbd5e1;">
      No hay tripulantes que coincidan con los filtros combinados actuales.
    </div>`;
    return;
  }

  grid.innerHTML = filtered
    .map((a) => {
      const sev = severityClass(a.global_status);
      const isVC = isCrewVeryCritical(a);
      const isSelected = a.id === currentAstronautId ? "selected" : "";
      const avatarSrc = resolveAvatarUrl(a.avatar);
      const isFem = (a.sexo || "M").toUpperCase() === "F";
      const cycleName = isFem && a.fase_ciclo > 0 ? (CYCLE_PHASE_NAMES[a.fase_ciclo] || "") : "";
      const cycleTag = cycleName ? ` · ${cycleName}` : "";
      const sexBadgeHtml = isFem
        ? `<span class="sex-badge sex-f" title="Fisiología Femenina">♀ FEM${escapeHtml(cycleTag)}</span>`
        : `<span class="sex-badge sex-m" title="Fisiología Masculina">♂ MASC</span>`;
      const vcTagHtml = isVC ? `<span class="vc-priority-chip">🚨 URGENCIA</span>` : "";

      return `
        <article class="cred-card status-${sev} ${isVC ? "status-very-critical" : ""} ${isSelected}" onclick="selectAstronautCard('${escapeAttr(a.id)}')">
          <div class="cred-photo-wrap">
            <img class="cred-photo" src="${escapeAttr(avatarSrc)}" alt="${escapeAttr(a.nombre)}" onerror="this.src='assets/avatars/astro_01.svg'" />
          </div>
          <div class="cred-info">
            <div class="cred-line-name">
              <div class="cred-top-meta">
                <span class="cred-role-label">${escapeHtml(a.rol)}</span>
                ${sexBadgeHtml}
                ${vcTagHtml}
              </div>
              <strong class="cred-name">${escapeHtml(a.nombre)}</strong>
            </div>

            <div class="cred-line-status">
              <span class="cred-status-label">ESTADO</span>
              <span class="heart-icon heart-${sev}">♥</span>
              <span class="cred-score">${a.health_score}%</span>
            </div>

            <div class="cred-line-desc">
              <span class="cred-desc-label">Diagnóstico / Resumen</span>
              <div class="cred-desc-value">${escapeHtml(a.resumen_estado || "Normal")}</div>
            </div>
          </div>
        </article>
      `;
    })
    .join("");
}

/* =========================================================================
 * Detalle del Astronauta e Interactividad del Mapa Corporal
 * ========================================================================= */
let activeHighlightedSys = null;
let activeHighlightedOverlay = null;

function getMinimalistOrganIconSvg(kind) {
  const icons = {
    eye: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M2 12c2.7-4.4 6.2-6.6 10-6.6s7.3 2.2 10 6.6c-2.7 4.4-6.2 6.6-10 6.6S4.7 16.4 2 12z"/>
      <circle cx="12" cy="12" r="3.4"/>
      <circle cx="12" cy="12" r="1.1" fill="currentColor" stroke="none"/>
    </svg>`,
    head: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M12 3a6.5 6.5 0 0 0-6.5 6.5c0 2.5 1.1 4.2 1.8 5.6.4.8.7 1.8.7 2.9h8c0-1.1.3-2.1.7-2.9.7-1.4 1.8-3.1 1.8-5.6A6.5 6.5 0 0 0 12 3z"/>
      <path d="M9.5 10.2h5M12 7.8v4.8M9 21h6"/>
    </svg>`,
    ear: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M6.5 10.5a5.8 5.8 0 0 1 11.5-1c0 3.2-2.2 4.3-2.9 6.2-.5 1.4-.4 3.4-2.1 4.3-1.4.7-3.2 0-3.5-1.5"/>
      <path d="M9.5 10.5a2.6 2.6 0 0 1 5 .4c0 1.4-1.3 2.1-1.7 3.2"/>
    </svg>`,
    brain: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M12 4.5v15"/>
      <path d="M12 5.2C9.8 4 7 5.2 6.6 7.5c-1.8.6-2.6 2.7-1.8 4.4-1 1.4-.5 3.6 1.1 4.4.3 2 2.2 3.3 4.2 2.9.7-.1 1.4-.5 1.9-1.1"/>
      <path d="M12 5.2c2.2-1.2 5 0 5.4 2.3 1.8.6 2.6 2.7 1.8 4.4 1 1.4.5 3.6-1.1 4.4-.3 2-2.2 3.3-4.2 2.9-.7-.1-1.4-.5-1.9-1.1"/>
      <path d="M8.5 9.2c1.2.4 1.6 1.4 1.2 2.3M15.5 9.2c-1.2.4-1.6 1.4-1.2 2.3M8.2 14.5c1 .2 1.8-.1 2.4-.7M15.8 14.5c-1 .2-1.8-.1-2.4-.7"/>
    </svg>`,
    lungs: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M12 3v7M12 10L8.8 12.2M12 10l3.2 2.2"/>
      <path d="M9.8 8.2C7 8.4 4.2 11.2 3.6 15.6c-.4 2.7.7 4.4 2.8 4.4 2 0 3.6-.9 3.6-3V9.5c0-.8-.1-1.3-.2-1.3z"/>
      <path d="M14.2 8.2c2.8.2 5.6 3 6.2 7.4.4 2.7-.7 4.4-2.8 4.4-2 0-3.6-.9-3.6-3V9.5c0-.8.1-1.3.2-1.3z"/>
    </svg>`,
    heart: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M10.5 6.8V3.5h2.4v2.7"/>
      <path d="M14.5 7.4l2.2-2.4 1.5 1.4-1.8 2"/>
      <path d="M12 20.5c-4.5-2.8-7-6.2-6.8-9.7.2-2.8 2.4-4.5 4.8-4.1 1.1.2 1.8.9 2.3 1.7.7-1.1 2.2-1.8 3.6-1.4 2.2.6 3.5 2.7 3.1 5.4-.6 3.6-3 6-7 8.1z"/>
      <path d="M8.2 12.6h2.1l1-2.1 1.4 3.7 1.1-1.6H16"/>
    </svg>`,
    stomach: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M11.2 3.5v4.3c0 1.4-1.2 2.1-2.7 2.5-2.6.7-4.1 3-3.7 5.7.4 2.7 2.8 4.5 5.8 4.5 3.8 0 7.6-2.4 8.2-6.6.5-3.4-1.4-6-4-6-1.3 0-2.1.6-2.7 1.3"/>
      <path d="M9.6 15.2c1.1.7 2.6.8 3.9.2"/>
    </svg>`,
    joint: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M9 3.2v5.1c-1.3.4-2 1.4-1.7 2.4.3.9 1.4 1.3 2.5.9.7-.3 1.4-.3 2.2 0 1.1.4 2.2 0 2.5-.9.3-1-.4-2-1.7-2.4V3.2"/>
      <path d="M9 20.8v-5.1c-1.3-.4-2-1.4-1.7-2.4.3-.9 1.4-1.3 2.5-.9.7.3 1.4.3 2.2 0 1.1-.4 2.2 0 2.5.9.3 1-.4 2-1.7 2.4v5.1"/>
      <circle cx="12" cy="12" r="1.2" fill="currentColor" stroke="none"/>
    </svg>`,
    bone: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M7.5 4.2a2.2 2.2 0 0 0-3.3 2.9 2.2 2.2 0 0 0 2.6 3.1l7.1 7.1a2.2 2.2 0 0 0 3.1 2.6 2.2 2.2 0 0 0 2.9-3.3 2.2 2.2 0 0 0-2.7-2.8l-7-7a2.2 2.2 0 0 0-2.7-2.6z"/>
    </svg>`,
    skin: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M8 12V6.5a1.2 1.2 0 0 1 2.4 0V11M10.4 11V5.2a1.2 1.2 0 0 1 2.4 0V11M12.8 11V6a1.2 1.2 0 0 1 2.4 0v5.5M15.2 11.5V8.4a1.2 1.2 0 0 1 2.4 0v5.8c0 3.2-2.2 6.3-5.6 6.3-2.5 0-4.4-1.3-5.4-3.7l-1.5-3.4a1.3 1.3 0 0 1 2.2-1.3L8 13"/>
      <circle cx="12" cy="15.4" r="1" fill="currentColor" stroke="none"/>
      <circle cx="14.2" cy="14.2" r="0.8" fill="currentColor" stroke="none"/>
    </svg>`,
    pelvis: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M4.5 7.5c1.8-1.6 4.4-1.2 5.7 1.1.9 1.6 2.7 1.6 3.6 0 1.3-2.3 3.9-2.7 5.7-1.1 1.4 1.3 1.5 4.2.1 6.7-.9 1.6-2.4 3.4-4 3.6-1.2.1-2-1.1-3.6-1.1s-2.4 1.2-3.6 1.1c-1.6-.2-3.1-2-4-3.6-1.4-2.5-1.3-5.4.1-6.7z"/>
      <circle cx="12" cy="13.2" r="1.6"/>
    </svg>`,
    dna: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M7 3.5c0 4.5 10 4.5 10 9s-10 4.5-10 8"/>
      <path d="M17 3.5c0 4.5-10 4.5-10 9s10 4.5 10 8"/>
      <path d="M8.5 6.2h7M9 12h6M8.5 17.8h7"/>
    </svg>`
  };
  return icons[kind] || icons.heart;
}

function extractActiveBodySymptoms(a, latest, rep) {
  const symptoms = [];
  const hist = Array.isArray(a?.history) ? a.history : [];
  const flags = latest ? latest.sintomas_flags || 0 : 0;
  const sysStatus = rep.system_status || [0, 0, 0, 0, 0, 0, 0];
  const b = a.baseline || {};
  const isFem = (a.sexo || "M").toUpperCase() === "F";
  const totalRad = (a.radiacion_previa_msv || 0) + (a.radiacion_mision_msv || 0);

  const pushSym = (id, title, detail, organ, sysIdx, overlayId, sev) => {
    if (symptoms.some((s) => s.id === id)) return;
    symptoms.push({ id, title, detail, organ, sysIdx, overlayId, sev });
  };

  if (latest) {
    /* 1. Dolor Ocular / SANS / Agudeza Visual */
    const visionDrop = (b.vision_base || 1.0) - (latest.vision_actual || 1.0);
    if (latest.vision_actual <= 0.72 || visionDrop >= 0.25 || sysStatus[1] >= 2) {
      pushSym(
        "vision",
        "Dolor / Presión Ocular (SANS)",
        `Agudeza visual ${latest.vision_actual.toFixed(2)} (base ${(b.vision_base || 1.0).toFixed(2)}) · Posible edema papilar`,
        "eye",
        1,
        "sym-overlay-vision",
        latest.vision_actual <= 0.65 || visionDrop >= 0.35 ? 2 : 1
      );
    } else if (visionDrop >= 0.12 || sysStatus[1] === 1) {
      pushSym(
        "vision",
        "Fatiga / Tensión Ocular",
        `Descenso visual leve (${latest.vision_actual.toFixed(2)}) por desplazamiento cefálico`,
        "eye",
        1,
        "sym-overlay-vision",
        1
      );
    }

    /* 2. Cefalea / Presión Intracraneal */
    if ((latest.dolor_cabeza || 0) >= 4) {
      pushSym(
        "headache",
        latest.dolor_cabeza >= 7 ? "Cefalea Intensa / Migraña" : "Cefalea Frontal / Presión Craneal",
        `Dolor de cabeza nivel ${latest.dolor_cabeza}/10`,
        "head",
        1,
        "sym-overlay-headache",
        latest.dolor_cabeza >= 7 ? 2 : 1
      );
    }

    /* 3. Mareo Ortostático (bit 16) o Vestibular / SMS (mareo_vestibular >= 5) */
    const mareoVal = latest.mareo_vestibular || 0;
    if ((flags & 16) !== 0 || mareoVal >= 5 || (sysStatus[1] > 0 && mareoVal >= 4)) {
      pushSym(
        "vestibular",
        mareoVal >= 5 ? "Mareo Espacial / Vértigo Vestibular (SMS)" : "Mareo Ortostático / Inestabilidad Vestibular",
        mareoVal >= 5
          ? `Mareo vestibular ${mareoVal}/10 · Diferir EVA y pilotaje manual`
          : "Desorientación neurovestibular / ortostática · Indicar hidratación y prendas compresivas",
        "ear",
        1,
        "sym-overlay-vestibular",
        (flags & 16) !== 0 || mareoVal >= 5 ? 2 : 1
      );
    }

    /* 4. Sueño 3d / Estrés / Fatiga Cognitiva (+15%) / Apetito Nutricional (SYS_BEHAVIORAL = 5) */
    const recent3 = hist.slice(-3);
    const sleep3d = recent3.length > 0
      ? recent3.reduce((acc, s) => acc + (s.sueno_horas || 0), 0) / recent3.length
      : (latest.sueno_horas || 7.5);
    const cogRatio = (latest.cognitivo_ms || 260) / (b.cognitivo_base_ms || 260);
    const lowAppetite = (latest.apetito_comida ?? 4) <= 2;
    if (sleep3d < 6.0 || latest.estres >= 7 || cogRatio > 1.15 || latest.cohesion_equipo <= 2 || lowAppetite || sysStatus[5] > 0) {
      const sev = (sleep3d < 5.0 && latest.estres >= 8) || sysStatus[5] >= 2 ? 2 : 1;
      const behTitle = lowAppetite && sleep3d >= 6.0 && latest.estres < 7 && cogRatio <= 1.15
        ? "Fatiga de Menú / Déficit Nutricional"
        : "Fatiga Conductual / Privación de Sueño";
      pushSym(
        "sleep",
        behTitle,
        `Sueño 3d ${sleep3d.toFixed(1)} h · Estrés ${latest.estres}/10 · Reflejos ${latest.cognitivo_ms.toFixed(0)} ms · Apetito ${latest.apetito_comida}/5`,
        "brain",
        5,
        "sym-overlay-sleep",
        sev
      );
    }

    /* 5. Respiratorio / Hipoxia / O2-CO2 */
    if (latest.spo2 < 94.0 || sysStatus[2] > 0) {
      pushSym(
        "respiratory",
        latest.spo2 < 90.0 ? "Hipoxia Crítica / Disnea Respiratoria" : "Desaturación Respiratoria / Alerta ECLSS",
        `SpO₂ ${latest.spo2.toFixed(1)}% (base ${(b.spo2_base || 98).toFixed(1)}%)`,
        "lungs",
        2,
        "sym-overlay-respiratory",
        latest.spo2 < 90.0 || sysStatus[2] >= 2 ? 2 : 1
      );
    }

    /* 6. Cardiovascular (FC / PA / Hidratacion) — sincronizado con Regla 5 backend */
    const fcDiff = (latest.fc || 70) - (b.fc_base || 70);
    if (latest.fc > 105 || latest.fc < 45 || latest.pa_sis > 140 || latest.pa_sis < 90 || fcDiff > 25 || sysStatus[0] > 0) {
      const cardioCrit = latest.pa_sis < 85 || latest.fc > 130 || sysStatus[0] >= 2;
      pushSym(
        "cardio",
        latest.fc > 105 || fcDiff > 25 ? "Taquicardia / Sobrecarga Cardíaca" : "Alteración Hemodinámica / Estasis Venosa",
        `FC ${latest.fc.toFixed(0)} lpm · PA ${latest.pa_sis.toFixed(0)}/${latest.pa_dia.toFixed(0)} mmHg`,
        "heart",
        0,
        "sym-overlay-cardio",
        cardioCrit ? 2 : 1
      );
    }

    /* 7. Dolor Articular Post-EVA (bit 8) y Musculoesquelético (respetando dia_descanso) */
    if ((flags & 8) !== 0) {
      pushSym(
        "joints",
        "Dolor Articular Post-EVA",
        "Artralgia en hombros, codos o rodillas · Evaluar descompresión (DCS)",
        "joint",
        3,
        "sym-overlay-joints",
        2
      );
    } else if ((latest.dia_descanso !== 1 && latest.ejercicio_min < Math.round((b.ejercicio_objetivo_min || 90) * 0.6)) || sysStatus[3] > 0) {
      pushSym(
        "bone",
        "Déficit Osteomuscular",
        `Ejercicio ${latest.ejercicio_min} min (meta ${b.ejercicio_objetivo_min || 90} min) · Riesgo óseo/muscular`,
        "bone",
        3,
        "sym-overlay-joints",
        sysStatus[3] >= 2 ? 2 : 1
      );
    }

    /* 8. Sarpullido / Alergia Cutánea (bit 1) */
    if ((flags & 1) !== 0) {
      pushSym(
        "rash",
        "Sarpullido / Dermatitis de Contacto",
        "Lesiones cutáneas activas en antebrazo o torso",
        "skin",
        4,
        "sym-overlay-rash",
        1
      );
    }

    /* 9. Infeccion / Fiebre (>= 37.8 C) / Reactivacion Viral / Glucemia (SYS_IMMUNE = 4) */
    const hasInfection = (flags & 4) !== 0;
    const hasViral = (flags & 2) !== 0;
    const fever = latest.temp_c >= 37.8 || latest.temp_c <= 35.0;
    const glucAbn = latest.glucosa_mg_dl > 140 || latest.glucosa_mg_dl < 70;
    if (hasInfection || hasViral || fever || glucAbn || sysStatus[4] > 0) {
      const parts = [];
      if (hasInfection) parts.push("Signos de infección");
      if (hasViral) parts.push("Reactivación viral");
      if (fever) parts.push(`Temp ${latest.temp_c.toFixed(1)} °C`);
      if (glucAbn) parts.push(`Glucemia ${latest.glucosa_mg_dl.toFixed(0)} mg/dL`);
      pushSym(
        "immune",
        hasInfection || fever ? "Respuesta Inmune / Estado Febril" : "Alteración Metabólica / Viral",
        parts.length > 0 ? parts.join(" · ") : "Desajuste inmunometabólico registrado",
        "stomach",
        4,
        "sym-overlay-immune",
        latest.temp_c >= 39.0 || (fever && (hasInfection || hasViral || glucAbn)) || latest.glucosa_mg_dl < 55 || latest.glucosa_mg_dl > 200 || sysStatus[4] >= 2 ? 2 : 1
      );
    }

    /* 10. Salud Hormonal Femenina / Menstruacion y Colico Pelvico */
    if (isFem && (
      latest.fase_ciclo === 1 ||
      (latest.flujo_menstrual || 0) >= 2 ||
      ((latest.flujo_menstrual || 0) >= 1 && (latest.suplemento_hierro || 0) === 0) ||
      (latest.colico_menstrual || 0) >= 4
    )) {
      const critPelvic = (latest.colico_menstrual || 0) >= 8 || ((latest.flujo_menstrual || 0) === 3 && ((flags & 16) !== 0 || latest.pa_sis < 95));
      pushSym(
        "pelvic",
        critPelvic ? "Menorragia / Dismenorrea Severa" : "Monitoreo Menstrual / Balance de Hierro",
        `Dismenorrea ${latest.colico_menstrual || 0}/10 · Flujo nivel ${latest.flujo_menstrual || 0}/3 · Hierro ${latest.suplemento_hierro ? "Activo" : "Pendiente"}`,
        "pelvis",
        critPelvic ? 0 : 4,
        "sym-overlay-pelvic",
        critPelvic ? 2 : 1
      );
    }
  }

  /* 11. Radiacion Acumulada / Evento Solar */
  if (totalRad >= 400 || (a.radiacion_mision_msv || 0) >= 20 || sysStatus[6] > 0) {
    pushSym(
      "radiation",
      totalRad >= 600 ? "Sobreexposición a Radiación (Límite Superado)" : "Exposición Acumulada a Radiación",
      `${totalRad.toFixed(1)} / 600 mSv acumulados`,
      "dna",
      6,
      "sym-overlay-radiation",
      totalRad >= 600 || sysStatus[6] >= 2 ? 2 : 1
    );
  }

  /* Respaldo desde alertas generadas por el motor si ningun campo disparo */
  if (symptoms.length === 0 && Array.isArray(rep.alerts)) {
    rep.alerts.forEach((al, idx) => {
      const msg = `${al.titulo || al.title || ""} ${al.explicacion || al.explanation || ""}`.toLowerCase();
      let organ = "heart";
      let sysIdx = 0;
      let overlayId = "sym-overlay-cardio";
      if (msg.includes("ojo") || msg.includes("visual") || msg.includes("sans")) {
        organ = "eye"; sysIdx = 1; overlayId = "sym-overlay-vision";
      } else if (msg.includes("cefalea") || msg.includes("cabeza")) {
        organ = "head"; sysIdx = 1; overlayId = "sym-overlay-headache";
      } else if (msg.includes("mareo") || msg.includes("vestibular")) {
        organ = "ear"; sysIdx = 1; overlayId = "sym-overlay-vestibular";
      } else if (msg.includes("spo2") || msg.includes("hipox") || msg.includes("resp") || msg.includes("co2")) {
        organ = "lungs"; sysIdx = 2; overlayId = "sym-overlay-respiratory";
      } else if (msg.includes("articul")) {
        organ = "joint"; sysIdx = 3; overlayId = "sym-overlay-joints";
      } else if (msg.includes("óseo") || msg.includes("ejerc") || msg.includes("muscul")) {
        organ = "bone"; sysIdx = 3; overlayId = "sym-overlay-joints";
      } else if (msg.includes("sarpullido") || msg.includes("piel") || msg.includes("alerg")) {
        organ = "skin"; sysIdx = 4; overlayId = "sym-overlay-rash";
      } else if (msg.includes("menstru") || msg.includes("cólico") || msg.includes("hierro")) {
        organ = "pelvis"; sysIdx = 4; overlayId = "sym-overlay-pelvic";
      } else if (msg.includes("sueño") || msg.includes("estrés") || msg.includes("cognit")) {
        organ = "brain"; sysIdx = 5; overlayId = "sym-overlay-sleep";
      } else if (msg.includes("radiaci") || msg.includes("msv")) {
        organ = "dna"; sysIdx = 6; overlayId = "sym-overlay-radiation";
      } else if (msg.includes("inmun") || msg.includes("temp") || msg.includes("gluc") || msg.includes("viral")) {
        organ = "stomach"; sysIdx = 4; overlayId = "sym-overlay-immune";
      }
      pushSym(
        `alert_${idx}`,
        al.title || "Hallazgo Clínico",
        al.explanation || "Evaluar parámetro clínico",
        organ,
        sysIdx,
        overlayId,
        al.severity || 1
      );
    });
  }

  /* Orden clinico: mayor severidad primero, luego orden anatomico */
  const anatomicalOrder = {
    head: 1, eye: 2, ear: 3, brain: 4, lungs: 5, heart: 6,
    stomach: 7, skin: 8, joint: 9, bone: 10, pelvis: 11, dna: 12
  };
  symptoms.sort((x, y) => {
    if ((y.sev || 0) !== (x.sev || 0)) return (y.sev || 0) - (x.sev || 0);
    return (anatomicalOrder[x.organ] || 99) - (anatomicalOrder[y.organ] || 99);
  });
  return symptoms;
}

function highlightBodySystem(sysIdx, overlayId = null) {
  activeHighlightedSys = sysIdx;
  activeHighlightedOverlay = overlayId;
  document.querySelectorAll("#body-map-svg .body-zone").forEach((el) => {
    const zSys = parseInt(el.getAttribute("data-sys") || "-1", 10);
    el.classList.toggle("zone-active", sysIdx !== null && zSys === sysIdx);
    el.classList.toggle("zone-dim", sysIdx !== null && zSys !== sysIdx);
  });
  document.querySelectorAll("#body-map-svg .sym-overlay").forEach((el) => {
    el.classList.toggle("sym-overlay-focus", overlayId !== null && el.id === overlayId);
  });
  document.querySelectorAll("#system-legend-list .sys-legend-item").forEach((el) => {
    const lSys = parseInt(el.getAttribute("data-sys") || "-1", 10);
    el.classList.toggle("legend-active", sysIdx !== null && lSys === sysIdx);
  });
  document.querySelectorAll("#body-symptom-list .body-symptom-item").forEach((el) => {
    const sOvr = el.getAttribute("data-overlay") || "";
    const sSys = parseInt(el.getAttribute("data-sys") || "-1", 10);
    const match = (overlayId !== null && sOvr === overlayId) || (overlayId === null && sysIdx !== null && sSys === sysIdx);
    el.classList.toggle("symptom-active", match);
  });
}

function highlightBodySymptom(sysIdx, overlayId) {
  highlightBodySystem(sysIdx, overlayId);
}

function clearSelectedBodyPart() {
  selectedBodyPartKey = null;
  highlightBodySystem(null);
  document.querySelectorAll("#body-map-svg .body-part-clickable").forEach((el) => {
    el.classList.remove("part-selected");
  });
  const box = document.getElementById("body-part-detail-box");
  if (box) {
    box.classList.add("hidden");
    box.innerHTML = "";
  }
}

function getBodyPartClinicalInfo(a, latest, rep, partKey) {
  const sysStatus = rep.system_status || [0, 0, 0, 0, 0, 0, 0];
  const b = a.baseline || {};
  const isFem = (a.sexo || "M").toUpperCase() === "F";
  const totalRad = (a.radiacion_previa_msv || 0) + (a.radiacion_mision_msv || 0);
  const flags = latest ? latest.sintomas_flags || 0 : 0;
  const activeSymptoms = extractActiveBodySymptoms(a, latest, rep);

  const filterSymptoms = (ids) => activeSymptoms.filter((s) => ids.includes(s.id));

  const catalog = {
    head: {
      title: "Cabeza y Cráneo",
      organ: "head",
      sysIdx: 1,
      st: Math.max(sysStatus[1] || 0, sysStatus[5] || 0, (latest && latest.dolor_cabeza >= 7) ? 2 : (latest && latest.dolor_cabeza >= 4) ? 1 : 0),
      metrics: latest
        ? `Cefalea: ${latest.dolor_cabeza}/10 · Mareo vestibular: ${latest.mareo_vestibular}/10 · Visión: ${latest.vision_actual.toFixed(2)}`
        : "Sin muestras recientes",
      note: "Evalúa cefalea por desplazamiento cefálico de fluidos o hipercapnia, vértigo vestibular y presión neuro-ocular.",
      syms: filterSymptoms(["headache", "vestibular", "vision", "sleep"])
    },
    brain: {
      title: "Cerebro, Sueño y Conducta",
      organ: "brain",
      sysIdx: 5,
      st: sysStatus[5] || 0,
      metrics: latest
        ? `Sueño: ${latest.sueno_horas.toFixed(1)} h (meta ${(b.sueno_base_h || 7.5).toFixed(1)} h) · Estrés: ${latest.estres}/10 · Reflejos: ${latest.cognitivo_ms.toFixed(0)} ms`
        : "Sin muestras recientes",
      note: "Monitorea deuda de sueño, tiempo de reacción psicomotora, estado de ánimo y cohesión del equipo.",
      syms: filterSymptoms(["sleep", "headache"])
    },
    eyes: {
      title: "Ojos y Sistema Neuro-Ocular (SANS)",
      organ: "eye",
      sysIdx: 1,
      st: sysStatus[1] || 0,
      metrics: latest
        ? `Agudeza visual: ${latest.vision_actual.toFixed(2)} (base ${(b.vision_base || 1.0).toFixed(2)}) · PA Sistólica: ${latest.pa_sis.toFixed(0)} mmHg`
        : "Sin muestras recientes",
      note: "Vigila cambios visuales o presión del nervio óptico por microgravedad; indicar bandas en muslos y Complejo B ante alerta.",
      syms: filterSymptoms(["vision", "headache"])
    },
    neck: {
      title: "Cuello y Vías Superiores",
      organ: "stomach",
      sysIdx: 4,
      st: Math.max(sysStatus[4] || 0, ((flags & 4) !== 0 || (latest && latest.temp_c >= 37.8)) ? 1 : 0),
      metrics: latest
        ? `Temp: ${latest.temp_c.toFixed(1)} °C · Signos infecciosos: ${(flags & 4) !== 0 ? "Activos" : "Ninguno"}`
        : "Sin muestras recientes",
      note: "Control de drenaje venoso yugular en microgravedad, adenopatías o signos de infección respiratoria alta.",
      syms: filterSymptoms(["immune", "respiratory"])
    },
    chest: {
      title: "Tórax y Cavidad Torácica",
      organ: "lungs",
      sysIdx: 2,
      st: Math.max(sysStatus[2] || 0, sysStatus[0] || 0),
      metrics: latest
        ? `SpO₂: ${latest.spo2.toFixed(1)}% · FC: ${latest.fc.toFixed(0)} lpm · Pre-respiración O₂: ${latest.pre_resp_o2_min} min`
        : "Sin muestras recientes",
      note: "Integra la mecánica ventilatoria de cabina, saturación arterial y retorno venoso central.",
      syms: filterSymptoms(["respiratory", "cardio"])
    },
    lungs: {
      title: "Pulmones y Sistema Respiratorio / EVA",
      organ: "lungs",
      sysIdx: 2,
      st: sysStatus[2] || 0,
      metrics: latest
        ? `SpO₂: ${latest.spo2.toFixed(1)}% (base ${(b.spo2_base || 98).toFixed(1)}%) · EVA hoy: ${latest.tuvo_eva ? "Sí" : "No"}`
        : "Sin muestras recientes",
      note: "Evalúa intercambio gaseoso, riesgo de hipoxia o hipercapnia por CO₂ y prevención de enfermedad descompresiva.",
      syms: filterSymptoms(["respiratory"])
    },
    heart: {
      title: "Corazón y Sistema Cardiovascular",
      organ: "heart",
      sysIdx: 0,
      st: sysStatus[0] || 0,
      metrics: latest
        ? `FC: ${latest.fc.toFixed(0)} lpm (base ${(b.fc_base || 70).toFixed(0)}) · PA: ${latest.pa_sis.toFixed(0)}/${latest.pa_dia.toFixed(0)} mmHg · Hidratación: ${latest.hidratacion_ml} ml`
        : "Sin muestras recientes",
      note: "Encapsulado dentro de la cavidad pulmonar; controla estabilidad hemodinámica, ortostatismo y volemia.",
      syms: filterSymptoms(["cardio", "vestibular"])
    },
    abdomen: {
      title: "Abdomen y Estado Metabólico",
      organ: "stomach",
      sysIdx: 4,
      st: sysStatus[4] || 0,
      metrics: latest
        ? `Glucemia: ${latest.glucosa_mg_dl.toFixed(0)} mg/dL · Calorías: ${latest.calorias_kcal} kcal · Apetito: ${latest.apetito_comida}/5`
        : "Sin muestras recientes",
      note: "Control de tolerancia nutricional, glucemia basal, hidratación y estado metabólico general.",
      syms: filterSymptoms(["immune"])
    },
    stomach: {
      title: "Estómago, Inmunidad y Metabolismo",
      organ: "stomach",
      sysIdx: 4,
      st: sysStatus[4] || 0,
      metrics: latest
        ? `Temp: ${latest.temp_c.toFixed(1)} °C · Glucemia: ${latest.glucosa_mg_dl.toFixed(0)} mg/dL · Apetito: ${latest.apetito_comida}/5`
        : "Sin muestras recientes",
      note: "Detecta reactivación viral, fiebre, signos de infección, desbalance glucémico y aceptabilidad del menú.",
      syms: filterSymptoms(["immune", "rash"])
    },
    pelvis: {
      title: isFem ? "Pelvis, Cadera y Salud Hormonal Femenina" : "Pelvis, Cadera y Estabilidad Axial",
      organ: "pelvis",
      sysIdx: 4,
      st: isFem && latest && (latest.colico_menstrual >= 7 || latest.flujo_menstrual === 3)
        ? 2
        : isFem && latest && (latest.colico_menstrual >= 4 || latest.flujo_menstrual >= 2 || (latest.fase_ciclo === 1 && !latest.suplemento_hierro))
        ? 1
        : (sysStatus[3] || 0),
      metrics: isFem && latest
        ? `Ciclo: ${CYCLE_PHASE_NAMES[latest.fase_ciclo || 0] || "No Aplica"} (Día ${latest.dia_ciclo || 0}) · Cólico: ${latest.colico_menstrual || 0}/10 · Hierro: ${latest.suplemento_hierro ? "Activo" : "Pendiente"}`
        : latest
        ? `Ejercicio de carga: ${latest.ejercicio_min} min (meta ${b.ejercicio_objetivo_min || 90} min) · Densidad ósea base: ${(b.densidad_osea_base_pct || 100).toFixed(0)}%`
        : "Sin muestras recientes",
      note: isFem
        ? "Control de salud menstrual, suplementación de hierro y estabilidad ósea pélvica en microgravedad."
        : "Control de transferencia de carga pélvica durante ejercicios de resistencia y prevención de descalcificación ósea.",
      syms: filterSymptoms(["pelvic", "joints", "bone"])
    },
    arms: {
      title: "Extremidades Superiores y Piel",
      organ: (flags & 1) ? "skin" : "joint",
      sysIdx: 3,
      st: Math.max(sysStatus[3] || 0, (flags & 1) ? 1 : 0, (flags & 8) ? 2 : 0),
      metrics: latest
        ? `Ejercicio diario: ${latest.ejercicio_min} min · Dolor post-EVA: ${(flags & 8) ? "Sí" : "No"} · Sarpullido: ${(flags & 1) ? "Activo" : "No"}`
        : "Sin muestras recientes",
      note: "Vigilancia articular de hombros, codos y muñecas tras actividades extravehiculares y dermatitis de contacto.",
      syms: filterSymptoms(["joints", "bone", "rash"])
    },
    legs: {
      title: "Extremidades Inferiores (Muslos, Rodillas y Pies)",
      organ: "bone",
      sysIdx: 3,
      st: sysStatus[3] || 0,
      metrics: latest
        ? `Ejercicio: ${latest.ejercicio_min} min (meta ${b.ejercicio_objetivo_min || 90} min) · Banda en muslos: ${(flags & 32) ? "Activa" : "Inactiva"}`
        : "Sin muestras recientes",
      note: "Preservación muscular y ósea de miembros inferiores, prevención de estasis venosa y control articular.",
      syms: filterSymptoms(["joints", "bone"])
    },
    radiation: {
      title: "Campo de Exposición y Dosimetría",
      organ: "dna",
      sysIdx: 6,
      st: sysStatus[6] || 0,
      metrics: `Acumulado carrera: ${totalRad.toFixed(1)} / 600 mSv · Misión actual: ${(a.radiacion_mision_msv || 0).toFixed(2)} mSv${latest ? ` · Dosis día: ${latest.dosis_dia_msv.toFixed(2)} mSv` : ""}`,
      note: "Monitorea dosis diaria, tormentas de partículas solares y acercamiento al límite ocupacional de 600 mSv.",
      syms: filterSymptoms(["radiation"])
    }
  };

  return catalog[partKey] || null;
}

function renderBodyPartDetailBox(a, partKey, shouldScroll = false) {
  const box = document.getElementById("body-part-detail-box");
  if (!box || !a || !partKey) return;
  const rep = a.report || {};
  const hist = a.history || [];
  const latest = hist.length > 0 ? hist[hist.length - 1] : null;
  const info = getBodyPartClinicalInfo(a, latest, rep, partKey);
  if (!info) return;

  const colorMap = ["#4ADE80", "#FBBF24", "#F87171"];
  const labelMap = ["NORMAL", "PRECAUCIÓN", "CRÍTICO"];
  const st = Math.min(2, Math.max(0, info.st || 0));
  const stColor = colorMap[st];
  const stCls = st === 2 ? "part-critical" : st === 1 ? "part-caution" : "part-ok";

  document.querySelectorAll("#body-map-svg .body-part-clickable").forEach((el) => {
    el.classList.toggle("part-selected", el.getAttribute("data-part") === partKey);
  });

  const symsHtml = info.syms.length > 0
    ? `<div class="body-part-syms">
        ${info.syms
          .map(
            (s) => `<span class="body-part-sym-pill" style="border-color:${s.sev >= 2 ? "#F87171" : "#FBBF24"}; color:${s.sev >= 2 ? "#FCA5A5" : "#FDE68A"};">
              ${escapeHtml(s.title)}
            </span>`
          )
          .join("")}
      </div>`
    : `<div class="body-part-no-sym">✓ Sin síntomas activos localizados en esta zona</div>`;

  box.className = `body-part-detail-box ${stCls}`;
  box.innerHTML = `
    <div class="body-part-detail-header">
      <div class="body-part-title-wrap">
        <span class="organ-icon-box" style="color:${stColor};">${getMinimalistOrganIconSvg(info.organ)}</span>
        <div>
          <strong>${escapeHtml(info.title)}</strong>
          <span class="body-part-status-pill" style="color:${stColor}; border-color:${stColor};">${labelMap[st]}</span>
        </div>
      </div>
      <button type="button" class="body-part-close-btn" onclick="clearSelectedBodyPart()" title="Cerrar detalle de parte corporal">×</button>
    </div>
    <div class="body-part-metrics">${escapeHtml(info.metrics)}</div>
    <div class="body-part-note">${escapeHtml(info.note)}</div>
    ${symsHtml}
  `;

  if (shouldScroll) {
    box.classList.remove("part-flash");
    void box.offsetWidth;
    box.classList.add("part-flash");
    box.scrollIntoView({ behavior: "smooth", block: "nearest" });
  }
}

function inspectBodyPart(partKey, shouldScroll = true) {
  if (!partKey || !currentAstronautDetail) return;
  selectedBodyPartKey = partKey;
  const targetEl = document.querySelector(`#body-map-svg [data-part="${partKey}"]`);
  const sysIdx = targetEl ? parseInt(targetEl.getAttribute("data-sys") || "-1", 10) : null;
  if (sysIdx !== null && !Number.isNaN(sysIdx) && sysIdx >= 0) {
    highlightBodySystem(sysIdx);
  }
  renderBodyPartDetailBox(currentAstronautDetail, partKey, shouldScroll);
}

function setupBodyMapInteractions() {
  const svg = document.getElementById("body-map-svg");
  if (!svg || svg._bound) return;
  svg._bound = true;
  svg.querySelectorAll(".body-zone").forEach((zone) => {
    const sysIdx = parseInt(zone.getAttribute("data-sys") || "-1", 10);
    const partKey = zone.getAttribute("data-part") || "";
    zone.addEventListener("mouseenter", () => highlightBodySystem(sysIdx));
    zone.addEventListener("mouseleave", () => {
      if (selectedBodyPartKey) {
        const selEl = svg.querySelector(`[data-part="${selectedBodyPartKey}"]`);
        const selSys = selEl ? parseInt(selEl.getAttribute("data-sys") || "-1", 10) : null;
        highlightBodySystem(selSys !== null && !Number.isNaN(selSys) ? selSys : null);
      } else {
        highlightBodySystem(null);
      }
    });
    zone.addEventListener("click", (ev) => {
      ev.stopPropagation();
      if (partKey) {
        inspectBodyPart(partKey, true);
      } else {
        highlightBodySystem(activeHighlightedSys === sysIdx ? null : sysIdx);
      }
    });
  });
}

async function selectAstronautCard(astronautId) {
  if (astronautId !== currentAstronautId) {
    setDetailStatsExpanded(false);
    clearSelectedBodyPart();
  }
  currentAstronautId = astronautId;
  updateMostrarLayoutVisibility();
  await loadAstronautDetail(astronautId, true);
}

function closeAstronautDetail() {
  currentAstronautId = null;
  currentAstronautDetail = null;
  setDetailStatsExpanded(false);
  clearSelectedBodyPart();
  const panel = document.getElementById("astronaut-detail-panel");
  if (panel) panel.classList.add("hidden");
  updateMostrarLayoutVisibility();
  renderCrewGrid(allCrewList);
}

async function loadAstronautDetail(astronautId, scrollIntoView = false) {
  try {
    const res = await fetch(`/api/astronauts/${encodeURIComponent(astronautId)}`);
    if (!res.ok) return;
    const astro = await res.json();
    if (astronautId !== currentAstronautId) return;
    currentAstronautDetail = astro;

    const panel = document.getElementById("astronaut-detail-panel");
    if (panel) {
      panel.classList.remove("hidden");
    }

    renderAstronautDetailPanel(astro);
    updateMostrarLayoutVisibility();

    if (panel && scrollIntoView) {
      panel.scrollIntoView({ behavior: "smooth", block: "start" });
    }
  } catch (err) {
    console.error("Error cargando detalle de astronauta:", err);
  }
}

const BODY_SILHOUETTES = {
  male: {
    head: "M230 24 C215 24 208 35 209 50 C209 60 214 69 220 73 L240 73 C246 69 251 60 251 50 C252 35 245 24 230 24 Z",
    neck: "M220 75 L240 75 L244 87 L216 87 Z",
    chest: "M188 90 L272 90 L262 152 Q230 144 198 152 Z",
    abdomen: "M198 154 Q230 146 262 154 L259 184 Q230 189 201 184 Z",
    pelvis: "M201 187 Q230 192 259 187 L263 207 L241 219 L219 219 L197 207 Z",
    arms: [
      "M185 91 L167 101 L157 154 L173 156 L187 114 Z",
      "M275 91 L293 101 L303 154 L287 156 L273 114 Z",
      "M156 159 L172 161 L162 212 L147 209 Z",
      "M304 159 L288 161 L298 212 L313 209 Z",
      "M146 215 L161 218 L156 240 L140 236 L137 224 Z",
      "M314 215 L299 218 L304 240 L320 236 L323 224 Z"
    ],
    legs: [
      "M197 210 L219 222 L224 316 L200 316 Z",
      "M263 210 L241 222 L236 316 L260 316 Z",
      "M200 320 L224 320 L221 407 L204 407 Z",
      "M260 320 L236 320 L239 407 L256 407 Z",
      "M204 410 L221 410 L222 423 L196 423 Z",
      "M256 410 L239 410 L238 423 L264 423 Z"
    ]
  },
  female: {
    head: "M230 25 C217 25 210 35 211 49 C211 59 216 68 222 72 L238 72 C244 68 249 59 249 49 C250 35 243 25 230 25 Z",
    neck: "M222 75 L238 75 L241 87 L219 87 Z",
    chest: "M195 91 Q230 88 265 91 L258 140 Q256 149 253 152 Q230 145 207 152 Q204 149 202 140 Z",
    abdomen: "M207 154 Q230 147 253 154 C252 166 254 176 261 184 Q230 189 199 184 C206 176 208 166 207 154 Z",
    pelvis: "M199 187 Q230 192 261 187 C269 196 272 206 267 214 L242 220 L218 220 L193 214 C188 206 191 196 199 187 Z",
    arms: [
      "M193 92 L177 101 L167 153 L180 155 L193 113 Z",
      "M267 92 L283 101 L293 153 L280 155 L267 113 Z",
      "M166 158 L179 160 L166 211 L153 208 Z",
      "M294 158 L281 160 L294 211 L307 208 Z",
      "M152 214 L165 216 L159 237 L146 233 L144 222 Z",
      "M308 214 L295 216 L301 237 L314 233 L316 222 Z"
    ],
    legs: [
      "M193 216 L218 222 L223 316 L201 316 Z",
      "M267 216 L242 222 L237 316 L259 316 Z",
      "M201 320 L223 320 L220 407 L206 407 Z",
      "M259 320 L237 320 L240 407 L254 407 Z",
      "M206 410 L220 410 L221 423 L198 423 Z",
      "M254 410 L240 410 L239 423 L262 423 Z"
    ]
  }
};

function applyBodySilhouette(isFemale) {
  const shape = isFemale ? BODY_SILHOUETTES.female : BODY_SILHOUETTES.male;
  ["head", "neck", "chest", "abdomen", "pelvis"].forEach((part) => {
    const el = document.getElementById(`seg-${part}`);
    if (el && shape[part]) el.setAttribute("d", shape[part]);
  });
  const armSegs = document.querySelectorAll("#svg-sys-3-arms .limb-seg");
  armSegs.forEach((seg, idx) => {
    if (shape.arms[idx]) seg.setAttribute("d", shape.arms[idx]);
  });
  const legSegs = document.querySelectorAll("#svg-sys-3-legs .limb-seg");
  legSegs.forEach((seg, idx) => {
    if (shape.legs[idx]) seg.setAttribute("d", shape.legs[idx]);
  });
}

function renderAstronautDetailPanel(a) {
  const panel = document.getElementById("astronaut-detail-panel");
  if (panel) panel.classList.remove("hidden");

  const rep = a.report || {};
  const b = a.baseline || {};
  const hist = a.history || [];
  const latest = hist.length > 0 ? hist[hist.length - 1] : null;
  const sev = severityClass(rep.global_status || 0);

  document.getElementById("detail-avatar").src = resolveAvatarUrl(a.avatar);
  document.getElementById("detail-id-badge").textContent = a.id;
  document.getElementById("detail-name").textContent = a.nombre;
  document.getElementById("detail-role").textContent = a.rol || "";

  const isFem = (a.sexo || "M").toUpperCase() === "F";
  applyBodySilhouette(isFem);
  const sexBadge = document.getElementById("detail-sex-badge");
  if (sexBadge) {
    sexBadge.className = `sex-badge ${isFem ? "sex-f" : "sex-m"}`;
    sexBadge.textContent = isFem ? "♀ FEMENINO" : "♂ MASCULINO";
  }

  const evaBadge = document.getElementById("detail-eva-badge");
  evaBadge.className = `eva-badge ${evaBadgeClass(rep.eva_aptitude)}`;
  evaBadge.textContent = `Aptitud EVA: ${formatEvaAptitudeEs(rep.eva_aptitude, rep.eva_aptitude_str)}`;

  const autoTag = document.getElementById("detail-auto-tag");
  if (autoTag) {
    autoTag.classList.toggle("hidden", rep.autonomous_mode !== 1);
  }

  const closeBtn = document.getElementById("detail-close-btn");
  if (closeBtn) closeBtn.disabled = false;

  const heart = document.getElementById("detail-heart");
  heart.className = `heart-icon heart-${sev}`;
  document.getElementById("detail-score").textContent = `${rep.health_score ?? 100}%`;
  document.getElementById("detail-summary-text").textContent = rep.resumen_estado || "Normal";

  /* 1. Colorear Mapa Corporal Anatomico Segmentado, Corazon Encapsulado y Sintomas */
  const sysStatus = rep.system_status || [0, 0, 0, 0, 0, 0, 0];
  const colorMap = ["#4ADE80", "#FBBF24", "#F87171"];
  const heartColors = [
    { fill: "#4ADE80", stroke: "#052e16", capStroke: "#14532d", capFill: "rgba(5, 46, 22, 0.56)" },
    { fill: "#FBBF24", stroke: "#713f12", capStroke: "#854d0e", capFill: "rgba(113, 63, 18, 0.48)" },
    { fill: "#F87171", stroke: "#7f1d1d", capStroke: "#991b1b", capFill: "rgba(127, 29, 29, 0.50)" }
  ];

  const setSvgColor = (id, prop, statusIdx) => {
    const el = document.getElementById(id);
    if (el) el.setAttribute(prop, colorMap[sysStatus[statusIdx] || 0]);
  };
  const setSelectorColor = (selector, prop, statusIdx) => {
    const c = colorMap[sysStatus[statusIdx] || 0];
    document.querySelectorAll(selector).forEach((el) => el.setAttribute(prop, c));
  };

  /* Corazon encapsulado dentro de los pulmones con borde verde mas oscuro */
  const st0 = sysStatus[0] || 0;
  const hc = heartColors[st0] || heartColors[0];
  const heartCap = document.getElementById("svg-heart-capsule");
  const heartShape = document.getElementById("svg-heart-shape");
  if (heartCap) {
    heartCap.setAttribute("stroke", hc.capStroke);
    heartCap.setAttribute("fill", hc.capFill);
  }
  if (heartShape) {
    heartShape.setAttribute("fill", hc.fill);
    heartShape.setAttribute("stroke", hc.stroke);
  }

  setSelectorColor("#svg-sys-1 .eye-part", "fill", 1);
  setSvgColor("svg-sys-2-l", "fill", 2);
  setSvgColor("svg-sys-2-r", "fill", 2);
  setSelectorColor("#svg-sys-3-arms .limb-seg", "stroke", 3);
  setSelectorColor("#svg-sys-3-legs .limb-seg", "stroke", 3);
  setSvgColor("svg-sys-4", "fill", 4);
  setSvgColor("svg-sys-5", "fill", 5);
  setSvgColor("svg-sys-6", "stroke", 6);

  /* Expresiones graficas de sintomas particulares sobre el esquema humano */
  const activeSymptoms = extractActiveBodySymptoms(a, latest, rep);
  const activeOverlays = new Set(activeSymptoms.map((s) => s.overlayId));
  document.querySelectorAll("#body-map-svg .sym-overlay").forEach((ov) => {
    const isOn = activeOverlays.has(ov.id);
    ov.classList.toggle("hidden", !isOn);
    ov.classList.toggle("sym-overlay-active", isOn);
  });

  const symptomList = document.getElementById("body-symptom-list");
  if (symptomList) {
    if (activeSymptoms.length === 0) {
      symptomList.innerHTML = `<div class="body-symptom-item symptom-normal">
        <span class="organ-icon-box" style="color:#4ADE80;">${getMinimalistOrganIconSvg("heart")}</span>
        <div class="symptom-item-text">
          <strong>Sin síntomas agudos expresados</strong>
          <span>Órganos y segmentos anatómicos en rango normal</span>
        </div>
        <strong class="symptom-sev-tag" style="color:#4ADE80;">NORMAL</strong>
      </div>`;
    } else {
      symptomList.innerHTML = activeSymptoms
        .map((s) => {
          const stColor = s.sev >= 2 ? "#F87171" : "#FBBF24";
          const sevCls = s.sev >= 2 ? "sym-critical" : "sym-caution";
          const sevTxt = s.sev >= 2 ? "CRÍTICO" : "PRECAUCIÓN";
          return `<div class="body-symptom-item ${sevCls}" data-sys="${s.sysIdx}" data-overlay="${escapeHtml(s.overlayId)}"
                       onmouseenter="highlightBodySymptom(${s.sysIdx}, '${escapeHtml(s.overlayId)}')"
                       onmouseleave="highlightBodySystem(null)"
                       onclick="highlightBodySymptom(${s.sysIdx}, '${escapeHtml(s.overlayId)}')">
            <span class="organ-icon-box" style="color:${stColor};">${getMinimalistOrganIconSvg(s.organ)}</span>
            <div class="symptom-item-text">
              <strong>${escapeHtml(s.title)}</strong>
              <span>${escapeHtml(s.detail)}</span>
            </div>
            <strong class="symptom-sev-tag" style="color:${stColor};">${sevTxt}</strong>
          </div>`;
        })
        .join("");
    }
  }

  /* Leyenda ordenada de sistemas con icono minimalista monocromo */
  const systemOrganMap = ["heart", "eye", "lungs", "joint", "stomach", "brain", "dna"];
  const systemPartMap = ["heart", "eyes", "lungs", "legs", "stomach", "brain", "radiation"];
  const systemOrder = SYSTEM_NAMES.map((name, idx) => ({
    name,
    idx,
    st: sysStatus[idx] || 0,
    organ: systemOrganMap[idx] || "heart",
    part: systemPartMap[idx] || "heart"
  })).sort((x, y) => (y.st !== x.st ? y.st - x.st : x.idx - y.idx));

  const legendList = document.getElementById("system-legend-list");
  if (legendList) {
    legendList.innerHTML = systemOrder
      .map(({ name, idx, st, organ, part }) => {
        const cls = st === 2 ? "sys-critical" : st === 1 ? "sys-caution" : "";
        const label = st === 2 ? "CRÍTICO" : st === 1 ? "PRECAUCIÓN" : "NORMAL";
        const iconColor = colorMap[st];
        return `<div class="sys-legend-item ${cls}" data-sys="${idx}"
                     onmouseenter="highlightBodySystem(${idx})"
                     onmouseleave="highlightBodySystem(null)"
                     onclick="inspectBodyPart('${part}', true)">
          <span class="sys-legend-left">
            <span class="organ-icon-box organ-icon-sm" style="color:${iconColor};">${getMinimalistOrganIconSvg(organ)}</span>
            <span>${escapeHtml(name)}</span>
          </span>
          <strong style="color:${iconColor}">${label}</strong>
        </div>`;
      })
      .join("");
  }

  if (selectedBodyPartKey) {
    renderBodyPartDetailBox(a, selectedBodyPartKey, false);
  }

  /* 2. Signos Vitales vs Referencia Basal Personal (dentro de #detail-stats-panel) */
  const vitalsGrid = document.getElementById("vitals-comparison-grid");
  if (vitalsGrid) {
    if (!latest) {
      vitalsGrid.innerHTML = `<div class="vital-tile" style="grid-column: 1 / -1; text-align:center;">
        <span class="vital-label">Sin muestras clínicas registradas</span>
        <span class="vital-delta">Usa Opciones → Cargar → Control Diario para ingresar los primeros signos vitales.</span>
      </div>`;
    } else {
      const formatDelta = (curr, base, unit, decimals = 1) => {
        const diff = curr - base;
        const sign = diff >= 0 ? "+" : "";
        return `${sign}${diff.toFixed(decimals)} ${unit} (base ${base.toFixed(decimals)})`;
      };

      const tiles = [
        {
          label: "Frecuencia Cardíaca",
          val: `${latest.fc.toFixed(0)} lpm`,
          delta: formatDelta(latest.fc, b.fc_base || 70, "lpm", 0)
        },
        {
          label: "Presión Arterial",
          val: `${latest.pa_sis.toFixed(0)}/${latest.pa_dia.toFixed(0)} mmHg`,
          delta: `Base ${(b.pa_sis_base || 120).toFixed(0)}/${(b.pa_dia_base || 80).toFixed(0)} mmHg`
        },
        {
          label: "Saturación SpO₂",
          val: `${latest.spo2.toFixed(1)}%`,
          delta: formatDelta(latest.spo2, b.spo2_base || 98, "%", 1)
        },
        {
          label: "Agudeza Visual (SANS)",
          val: `${latest.vision_actual.toFixed(2)}`,
          delta: formatDelta(latest.vision_actual, b.vision_base || 1.0, "", 2)
        },
        {
          label: "Sueño / Estrés",
          val: `${latest.sueno_horas.toFixed(1)} h · Estrés ${latest.estres}/10`,
          delta: `Meta ${(b.sueno_base_h || 7.5).toFixed(1)} h · Cefalea ${latest.dolor_cabeza}/10`
        },
        {
          label: "Reflejos / Cognición",
          val: `${latest.cognitivo_ms.toFixed(0)} ms`,
          delta: formatDelta(latest.cognitivo_ms, b.cognitivo_base_ms || 260, "ms", 0)
        },
        {
          label: "Ejercicio / Hidratación",
          val: `${latest.ejercicio_min} min · ${latest.hidratacion_ml} ml`,
          delta: `Meta ${b.ejercicio_objetivo_min || 90}m · ${b.hidratacion_objetivo_ml || 2500}ml`
        },
        {
          label: "Temp / Glucemia",
          val: `${latest.temp_c.toFixed(1)} °C · ${latest.glucosa_mg_dl.toFixed(0)} mg/dL`,
          delta: `Cohesión ${latest.cohesion_equipo}/5 · Apetito ${latest.apetito_comida}/5`
        }
      ];

      vitalsGrid.innerHTML = tiles
        .map(
          (t) => `<div class="vital-tile">
            <span class="vital-label">${escapeHtml(t.label)}</span>
            <strong class="vital-val">${escapeHtml(t.val)}</strong>
            <span class="vital-delta">${escapeHtml(t.delta)}</span>
          </div>`
        )
        .join("");
    }
  }

  /* Barra de Radiacion Acumulada vs 600 mSv */
  const totalRad = (a.radiacion_previa_msv || 0) + (a.radiacion_mision_msv || 0);
  const radPct = Math.min(100, Math.max(0, (totalRad / 600.0) * 100));
  document.getElementById("detail-rad-text").textContent =
    `${totalRad.toFixed(1)} / 600 mSv (Reciente: ${(a.radiacion_mision_msv || 0).toFixed(2)} mSv)`;
  const radFill = document.getElementById("detail-rad-fill");
  if (radFill) {
    radFill.style.width = `${radPct.toFixed(1)}%`;
    radFill.style.background =
      totalRad >= 600 ? "#F87171" : totalRad >= 480 ? "#FBBF24" : "#4ADE80";
  }

  /* Estado de botones 1-clic de contramedidas y resumen de actuacion */
  const flags = latest ? latest.sintomas_flags || 0 : 0;
  const setChip = (id, mask) => {
    const btn = document.getElementById(id);
    if (!btn) return;
    const isActive = (flags & mask) !== 0;
    btn.classList.toggle("active", isActive);
    const pill = btn.querySelector(".cm-state-pill");
    if (pill) pill.textContent = isActive ? "ACTIVA" : "INACTIVA";
  };
  setChip("cm-btn-cuff", 32);
  setChip("cm-btn-bcomp", 64);
  setChip("cm-btn-vitd", 128);
  setChip("cm-btn-veggie", 256);

  renderDetailRoutineNudges(a);

  const cmSummaryList = document.getElementById("cm-action-summary-list");
  if (cmSummaryList) {
    const appliedCm = COUNTERMEASURE_GUIDANCE.filter((g) => (flags & g.mask) !== 0);
    if (appliedCm.length === 0) {
      cmSummaryList.innerHTML = `<div class="cm-summary-empty">
        Activa cualquiera de las contramedidas superiores para ver aquí la instrucción exacta de ejecución.
      </div>`;
    } else {
      cmSummaryList.innerHTML = appliedCm
        .map(
          (g) => `<div class="cm-summary-item">
            <strong>PASO ACTIVO · ${escapeHtml(g.title)}</strong>
            <span>${escapeHtml(g.howToAct)}</span>
          </div>`
        )
        .join("");
    }
  }

  /* Sub-panel de Fisiologia Hormonal: ESTRICTAMENTE solo para mujeres */
  const hormPanel = document.getElementById("detail-hormonal-panel");
  if (hormPanel) {
    if (isFem) {
      hormPanel.classList.remove("hidden");
      const phaseBadge = document.getElementById("detail-cycle-phase-badge");
      const metricsBox = document.getElementById("detail-hormonal-metrics");
      const ironBtn = document.getElementById("cm-btn-iron");
      const femSummary = document.getElementById("cm-fem-action-summary");
      if (latest) {
        const faseIdx = latest.fase_ciclo || 0;
        const faseName = CYCLE_PHASE_NAMES[faseIdx] || "No Aplica";
        if (phaseBadge) {
          phaseBadge.textContent = `${faseName} (Día ${latest.dia_ciclo || 0})`;
        }
        const flowLabels = ["0 (Ninguno)", "1 (Leve)", "2 (Moderado)", "3 (Abundante)"];
        const flowTxt = flowLabels[latest.flujo_menstrual || 0] || "0 (Ninguno)";
        const ironActive = (latest.suplemento_hierro || 0) === 1;
        if (metricsBox) {
          metricsBox.innerHTML = `
            <div class="hormonal-mini-tile"><span>Fase / Día Ciclo</span><strong>${escapeHtml(faseName)} · D${latest.dia_ciclo || 0}</strong></div>
            <div class="hormonal-mini-tile"><span>Flujo Menstrual</span><strong>${escapeHtml(flowTxt)}</strong></div>
            <div class="hormonal-mini-tile"><span>Cólico / Dismenorrea</span><strong>${latest.colico_menstrual || 0} / 10</strong></div>
            <div class="hormonal-mini-tile"><span>Suplemento Hierro (Fe)</span><strong style="color:${ironActive ? "#4ADE80" : "#FBBF24"}">${ironActive ? "ACTIVO" : "PENDIENTE"}</strong></div>
          `;
        }
        if (ironBtn) {
          ironBtn.classList.toggle("active", ironActive);
          const pill = ironBtn.querySelector(".cm-state-pill");
          if (pill) pill.textContent = ironActive ? "ACTIVA" : "INACTIVA";
        }
        if (femSummary) {
          femSummary.classList.toggle("hidden", !ironActive);
          femSummary.innerHTML = ironActive
            ? `<strong>PROTOCOLO ACTIVO · Hierro + Hidratación (+350 ml)</strong><span>1) Tomar 1 dosis de hierro elemental + folato/Complejo B con 350 ml de agua (evitar café/té 1 h). 2) Vigilar mareo ortostático y reevaluar intensidad del flujo en el próximo check-in.</span>`
            : "";
        }
      } else {
        if (phaseBadge) phaseBadge.textContent = "Sin registro";
        if (metricsBox) {
          metricsBox.innerHTML = `<div class="hormonal-mini-tile" style="grid-column:1 / -1;">
            <span>Registro Hormonal</span>
            <strong>Pendiente del primer control femenino</strong>
          </div>`;
        }
        if (ironBtn) {
          ironBtn.classList.remove("active");
          const pill = ironBtn.querySelector(".cm-state-pill");
          if (pill) pill.textContent = "INACTIVA";
        }
        if (femSummary) {
          femSummary.classList.add("hidden");
          femSummary.innerHTML = "";
        }
      }
    } else {
      hormPanel.classList.add("hidden");
      const metricsBox = document.getElementById("detail-hormonal-metrics");
      if (metricsBox) metricsBox.innerHTML = "";
    }
  }

  /* 3. Lista de Alertas y Contramedidas */
  const alertsList = document.getElementById("detail-alerts-list");
  const alerts = rep.alerts || [];
  if (alertsList) {
    if (alerts.length === 0) {
      alertsList.innerHTML = `<div class="alert-item" style="border-left-color:#4ADE80;">
        <div class="alert-header-row">
          <span class="alert-title" style="color:#4ADE80;">✓ Estado Fisiológico y Conductual Normal</span>
          <span class="alert-ref-tag">Medidas Clínicas Estándar</span>
        </div>
        <div class="alert-exp">Todos los parámetros vitales, dosimétricos, hormonales y conductuales se encuentran dentro de los márgenes normales respecto a su referencia personal.</div>
      </div>`;
    } else {
      alertsList.innerHTML = alerts
        .map((al) => {
          const isCrit = al.severity === 2;
          return `<div class="alert-item ${isCrit ? "alert-critical" : ""}">
            <div class="alert-header-row">
              <span class="alert-title" style="color:${isCrit ? "#F87171" : "#FBBF24"}">
                ${isCrit ? "🚨 CRÍTICO:" : "⚠️ PRECAUCIÓN:"} ${escapeHtml(al.titulo)}
              </span>
              <span class="alert-ref-tag">${escapeHtml(al.referencia_nasa)}</span>
            </div>
            <div class="alert-exp">${escapeHtml(al.explicacion)}</div>
            <div class="alert-cm"><strong>Protocolo Clínico:</strong> ${escapeHtml(al.contramedida)}</div>
          </div>`;
        })
        .join("");
    }
  }

  /* 4. Dibujar Graficos en <canvas> cuando el panel estadistico esta expandido */
  if (detailStatsExpanded) {
    renderAstronautCharts(a);
  }
}

function renderAstronautCharts(a) {
  if (!a) return;
  const b = a.baseline || {};
  const recentHist = (a.history || []).slice(-30);
  drawTrendChart(
    "canvas-fc",
    recentHist.map((s) => ({ day: s.dia_mision, value: s.fc })),
    b.fc_base || 70,
    "lpm",
    "#D4D4D4"
  );
  drawTrendChart(
    "canvas-spo2",
    recentHist.map((s) => ({ day: s.dia_mision, value: s.spo2 })),
    b.spo2_base || 98,
    "%",
    "#4ADE80"
  );
  drawTrendChart(
    "canvas-sueno",
    recentHist.map((s) => ({ day: s.dia_mision, value: s.sueno_horas })),
    b.sueno_base_h || 7.5,
    "h",
    "#D4D4D4"
  );
  drawTrendChart(
    "canvas-rad",
    recentHist.map((s) => ({ day: s.dia_mision, value: s.dosis_dia_msv })),
    b.dosis_diaria_nominal_msv || 0.5,
    "mSv",
    "#FBBF24"
  );
  drawTrendChart(
    "canvas-estres",
    recentHist.map((s) => ({ day: s.dia_mision, value: s.estres })),
    b.estres_base || 3,
    "/10",
    "#F87171"
  );
  drawTrendChart(
    "canvas-vision",
    recentHist.map((s) => ({ day: s.dia_mision, value: s.vision_actual })),
    b.vision_base || 1.0,
    "",
    "#D4D4D4"
  );
}

/* =========================================================================
 * Helper de Graficos en <canvas> Nativo (frontend_ui.md §4)
 * ========================================================================= */
function drawTrendChart(canvasId, series, baselineValue, unit, strokeColor = "#D4D4D4") {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  const W = canvas.width;
  const H = canvas.height;
  ctx.clearRect(0, 0, W, H);

  if (!series || series.length === 0) {
    ctx.fillStyle = "#A3A3A3";
    ctx.font = "12px 'Inter', sans-serif";
    ctx.fillText("Sin muestras históricas", 20, H / 2);
    return;
  }

  const padL = 38;
  const padR = 14;
  const padT = 16;
  const padB = 22;
  const plotW = W - padL - padR;
  const plotH = H - padT - padB;

  const vals = series.map((p) => p.value);
  if (typeof baselineValue === "number") vals.push(baselineValue);

  let minV = Math.min(...vals);
  let maxV = Math.max(...vals);
  if (Math.abs(maxV - minV) < 1e-4) {
    minV -= 1;
    maxV += 1;
  } else {
    const margin = (maxV - minV) * 0.18;
    minV -= margin;
    maxV += margin;
  }

  const xOf = (i) =>
    series.length === 1 ? padL + plotW / 2 : padL + (i / (series.length - 1)) * plotW;
  const yOf = (v) => padT + plotH - ((v - minV) / (maxV - minV)) * plotH;

  /* Lineas de grilla horizontales */
  ctx.strokeStyle = "rgba(212, 212, 212, 0.14)";
  ctx.lineWidth = 1;
  for (let g = 0; g <= 2; g++) {
    const gy = padT + (g / 2) * plotH;
    ctx.beginPath();
    ctx.moveTo(padL, gy);
    ctx.lineTo(W - padR, gy);
    ctx.stroke();

    const labelVal = maxV - (g / 2) * (maxV - minV);
    const decimals = maxV - minV < 0.5 || Math.abs(labelVal) < 2 ? 2 : labelVal < 10 ? 1 : 0;
    ctx.fillStyle = "#A3A3A3";
    ctx.font = "10px 'JetBrains Mono', monospace";
    ctx.fillText(labelVal.toFixed(decimals), 3, gy + 3);
  }

  /* Linea de referencia Baseline punteada */
  if (typeof baselineValue === "number") {
    const by = yOf(baselineValue);
    ctx.save();
    ctx.strokeStyle = "rgba(212, 212, 212, 0.45)";
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.moveTo(padL, by);
    ctx.lineTo(W - padR, by);
    ctx.stroke();
    ctx.restore();
  }

  /* Curva de datos */
  ctx.strokeStyle = strokeColor;
  ctx.lineWidth = 2.4;
  ctx.beginPath();
  series.forEach((pt, idx) => {
    const px = xOf(idx);
    const py = yOf(pt.value);
    if (idx === 0) ctx.moveTo(px, py);
    else ctx.lineTo(px, py);
  });
  ctx.stroke();

  /* Puntos y etiquetas de eje X (primer y ultimo Sol) */
  series.forEach((pt, idx) => {
    const px = xOf(idx);
    const py = yOf(pt.value);
    ctx.fillStyle = strokeColor;
    ctx.beginPath();
    ctx.arc(px, py, 2.8, 0, Math.PI * 2);
    ctx.fill();
  });

  ctx.fillStyle = "#A3A3A3";
  ctx.font = "10px 'JetBrains Mono', monospace";
  const firstDay = series[0].day;
  const lastDay = series[series.length - 1].day;
  const lastVal = series[series.length - 1].value;
  ctx.fillText(`Día ${firstDay}`, padL, H - 5);
  ctx.fillText(
    `Día ${lastDay}: ${lastVal.toFixed(lastVal < 10 ? 2 : 1)} ${unit}`,
    Math.max(padL + 55, W - padR - 115),
    H - 5
  );
}

/* =========================================================================
 * Acciones Rapidas desde la Ficha del Astronauta
 * ========================================================================= */
async function toggleQuickCountermeasure(bitMask) {
  if (!currentAstronautDetail) return;
  const hist = currentAstronautDetail.history || [];
  const latest = hist.length > 0 ? hist[hist.length - 1] : null;
  const currentFlags = latest ? latest.sintomas_flags || 0 : 0;
  const newFlags = currentFlags ^ bitMask;
  const targetDay = latest ? latest.dia_mision : currentMissionData?.dia_actual || 14;

  try {
    const res = await fetch(`/api/astronauts/${encodeURIComponent(currentAstronautDetail.id)}/samples`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        dia_mision: targetDay,
        sintomas_flags: newFlags
      })
    });
    if (res.ok) {
      showToast("Contramedida actualizada en la bitácora del tripulante.");
      await refreshMissionState(true);
    }
  } catch (err) {
    showToast("Error al actualizar contramedida", true);
  }
}

async function toggleFemaleIronCountermeasure() {
  if (!currentAstronautDetail) return;
  const hist = currentAstronautDetail.history || [];
  const latest = hist.length > 0 ? hist[hist.length - 1] : null;
  const targetDay = latest ? latest.dia_mision : currentMissionData?.dia_actual || 14;
  const currentIron = latest ? latest.suplemento_hierro || 0 : 0;
  const nextIron = currentIron === 1 ? 0 : 1;
  const currentFlags = latest ? latest.sintomas_flags || 0 : 0;
  const nextFlags = nextIron === 1 ? currentFlags | 64 : currentFlags;
  const nextHid = nextIron === 1
    ? Math.max(latest ? latest.hidratacion_ml : 2500, currentAstronautDetail.baseline?.hidratacion_objetivo_ml || 2500)
    : (latest ? latest.hidratacion_ml : 2400);

  try {
    const res = await fetch(`/api/astronauts/${encodeURIComponent(currentAstronautDetail.id)}/samples`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        dia_mision: targetDay,
        suplemento_hierro: nextIron,
        sintomas_flags: nextFlags,
        hidratacion_ml: nextHid
      })
    });
    if (res.ok) {
      showToast(
        nextIron === 1
          ? "Suplemento de Hierro + Complejo B e hidratación activados."
          : "Suplemento de Hierro desactivado."
      );
      await refreshMissionState(true);
    }
  } catch (err) {
    showToast("Error al actualizar protocolo de hierro", true);
  }
}

/* =========================================================================
 * Vista MODIFICAR ASTRONAUTA (Edicion Completa, Exportar Bitacora y Eliminar)
 * ========================================================================= */
let modifyRequestSeq = 0;

function populateModifyAstronautSelector(crewList, initialLoad = false) {
  const sel = document.getElementById("mod-astronaut-select");
  if (!sel) return;
  const list = Array.isArray(crewList) ? crewList : [];
  const prevVal = sel.value;

  sel.innerHTML = list
    .map((c) => `<option value="${escapeHtml(c.id)}">${escapeHtml(c.nombre)} (${escapeHtml(c.id)})</option>`)
    .join("");

  if (list.length === 0) {
    const badge = document.getElementById("mod-astro-meta-badge");
    if (badge) badge.textContent = "Sin tripulantes registrados";
    return;
  }

  if (prevVal && list.some((c) => c.id === prevVal)) {
    sel.value = prevVal;
  } else {
    sel.value = (currentAstronautId && list.some((c) => c.id === currentAstronautId))
      ? currentAstronautId
      : list[0].id;
  }

  if (initialLoad || activeOptionView === "modificar") {
    const modForm = document.getElementById("modify-astronaut-form");
    const isEditingMod = modForm && modForm.contains(document.activeElement);
    if (!isEditingMod) {
      loadAstronautForModify(sel.value);
    }
  }
}

function toggleModifyHormonalVisibility() {
  const sexEl = document.getElementById("mod-sexo");
  const hormBox = document.getElementById("mod-hormonal-box");
  if (sexEl && hormBox) {
    hormBox.classList.toggle("hidden", (sexEl.value || "M").toUpperCase() !== "F");
  }
}

function onModifyPredefinedAvatarChange(val) {
  if (!modCustomAvatarDataUrl) {
    const prev = document.getElementById("mod-avatar-preview");
    if (prev && val) prev.src = val;
  }
}

async function onModifyAvatarFileSelected(ev) {
  const file = ev.target.files && ev.target.files[0];
  if (!file) return;
  try {
    const dataUrl = await readImageFileAsDataUrl(file);
    modCustomAvatarDataUrl = dataUrl;
    const prev = document.getElementById("mod-avatar-preview");
    const status = document.getElementById("mod-avatar-status");
    if (prev) prev.src = dataUrl;
    if (status) status.textContent = `Nueva foto lista: ${file.name} (${Math.round(file.size / 1024)} KB)`;
    showToast("Nueva fotografía lista para guardar en la modificación.");
  } catch (err) {
    showToast(err.message || "Imagen inválida", true);
    ev.target.value = "";
  }
}

function clearModifyCustomAvatar() {
  modCustomAvatarDataUrl = null;
  const input = document.getElementById("mod-avatar-file");
  const prev = document.getElementById("mod-avatar-preview");
  const status = document.getElementById("mod-avatar-status");
  if (input) input.value = "";
  if (prev) prev.src = resolveAvatarUrl(modOriginalAvatarPath || "assets/avatars/astro_01.svg");
  if (status) status.textContent = "Se conserva la foto actual salvo que elijas otra";
}

async function loadAstronautForModify(astronautId) {
  if (!astronautId) return;
  const reqId = ++modifyRequestSeq;
  try {
    const res = await fetch(`/api/astronauts/${encodeURIComponent(astronautId)}`);
    if (!res.ok) return;
    const a = await res.json();
    if (reqId !== modifyRequestSeq) return;

    const setV = (id, v) => {
      const el = document.getElementById(id);
      if (el && v !== undefined && v !== null) el.value = v;
    };

    const rep = a.report || {};
    const b = a.baseline || {};
    const hist = a.history || [];
    const latest = hist.length > 0 ? hist[hist.length - 1] : null;
    const isFem = (a.sexo || "M").toUpperCase() === "F";

    const badge = document.getElementById("mod-astro-meta-badge");
    if (badge) {
      badge.textContent = `ID: ${a.id} · Estado: ${rep.resumen_estado || "Normal"} (${rep.health_score ?? 100}%)`;
    }

    setV("mod-nombre", a.nombre || "");
    setV("mod-sexo", isFem ? "F" : "M");
    setV("mod-rol", a.rol || "");
    setV("mod-rad-previa", (a.radiacion_previa_msv ?? 0).toFixed(1));
    setV("mod-rad-mision", (a.radiacion_mision_msv ?? 0).toFixed(2));

    modOriginalAvatarPath = a.avatar || "assets/avatars/astro_01.svg";
    modCustomAvatarDataUrl = null;
    const fileIn = document.getElementById("mod-avatar-file");
    if (fileIn) fileIn.value = "";
    const statusNote = document.getElementById("mod-avatar-status");
    if (statusNote) statusNote.textContent = "Se conserva la foto actual salvo que elijas otra";
    const avSel = document.getElementById("mod-avatar-select");
    if (avSel && ["assets/avatars/astro_01.svg", "assets/avatars/astro_02.svg", "assets/avatars/astro_03.svg", "assets/avatars/astro_04.svg"].includes(modOriginalAvatarPath)) {
      avSel.value = modOriginalAvatarPath;
    }
    const avPrev = document.getElementById("mod-avatar-preview");
    if (avPrev) avPrev.src = resolveAvatarUrl(modOriginalAvatarPath);

    /* Todos los parametros basales */
    setV("mod-b-fc", b.fc_base ?? 70);
    setV("mod-b-pasis", b.pa_sis_base ?? 120);
    setV("mod-b-padia", b.pa_dia_base ?? 80);
    setV("mod-b-spo2", b.spo2_base ?? 98);
    setV("mod-b-temp", b.temp_base ?? 36.6);
    setV("mod-b-glucosa", b.glucosa_base ?? 90);
    setV("mod-b-peso", b.peso_base ?? 72);
    setV("mod-b-sueno", b.sueno_base_h ?? 7.5);
    setV("mod-b-animo", b.animo_base ?? 8);
    setV("mod-b-estres", b.estres_base ?? 3);
    setV("mod-b-cog", b.cognitivo_base_ms ?? 260);
    setV("mod-b-cal", b.calorias_objetivo_kcal ?? 2500);
    setV("mod-b-hid", b.hidratacion_objetivo_ml ?? 2500);
    setV("mod-b-apetito", b.apetito_base ?? 4);
    setV("mod-b-ej", b.ejercicio_objetivo_min ?? 90);
    setV("mod-b-pre", b.pre_resp_o2_objetivo_min ?? 120);
    setV("mod-b-vision", b.vision_base ?? 1.0);
    setV("mod-b-osea", b.densidad_osea_base_pct ?? 100);
    setV("mod-b-dosis", b.dosis_diaria_nominal_msv ?? 0.5);

    /* Ultima muestra clinica actual */
    setV("mod-s-dia", latest ? latest.dia_mision : (currentMissionData?.dia_actual || 1));
    setV("mod-s-fc", latest ? latest.fc : (b.fc_base ?? 70));
    setV("mod-s-spo2", latest ? latest.spo2 : (b.spo2_base ?? 98));
    setV("mod-s-pasis", latest ? latest.pa_sis : (b.pa_sis_base ?? 120));
    setV("mod-s-padia", latest ? latest.pa_dia : (b.pa_dia_base ?? 80));
    setV("mod-s-temp", latest ? latest.temp_c : (b.temp_base ?? 36.6));
    setV("mod-s-glucosa", latest ? latest.glucosa_mg_dl : (b.glucosa_base ?? 90));
    setV("mod-s-peso", latest ? latest.peso_kg : (b.peso_base ?? 72));
    setV("mod-s-vision", latest ? latest.vision_actual : (b.vision_base ?? 1.0));
    setV("mod-s-sueno", latest ? latest.sueno_horas : (b.sueno_base_h ?? 7.5));
    setV("mod-s-estres", latest ? latest.estres : (b.estres_base ?? 3));
    setV("mod-s-cefalea", latest ? latest.dolor_cabeza : 0);
    setV("mod-s-mareo", latest ? latest.mareo_vestibular : 0);
    setV("mod-s-ejercicio", latest ? latest.ejercicio_min : (b.ejercicio_objetivo_min ?? 90));
    setV("mod-s-hidratacion", latest ? latest.hidratacion_ml : (b.hidratacion_objetivo_ml ?? 2500));
    const modDescCb = document.getElementById("mod-s-descanso");
    if (modDescCb) modDescCb.checked = latest ? (latest.dia_descanso === 1) : false;

    setV("mod-s-fase", latest ? (latest.fase_ciclo ?? 0) : 0);
    setV("mod-s-dia-ciclo", latest ? (latest.dia_ciclo ?? 0) : 0);
    setV("mod-s-flujo", latest ? (latest.flujo_menstrual ?? 0) : 0);
    setV("mod-s-colico", latest ? (latest.colico_menstrual ?? 0) : 0);
    const ironCb = document.getElementById("mod-s-hierro");
    if (ironCb) ironCb.checked = latest ? (latest.suplemento_hierro === 1) : false;

    toggleModifyHormonalVisibility();
  } catch (err) {
    console.error("Error cargando datos para modificar astronauta:", err);
  }
}

async function submitModifyAstronaut(ev) {
  ev.preventDefault();
  const sel = document.getElementById("mod-astronaut-select");
  const astroId = sel ? sel.value : "";
  if (!astroId) {
    showToast("Selecciona un astronauta primero.", true);
    return;
  }

  let avatarValue = modOriginalAvatarPath || document.getElementById("mod-avatar-select").value;
  if (modCustomAvatarDataUrl) {
    avatarValue = modCustomAvatarDataUrl;
  } else {
    const avSel = document.getElementById("mod-avatar-select");
    if (avSel && avSel.value && avSel.value !== modOriginalAvatarPath &&
        ["assets/avatars/astro_01.svg", "assets/avatars/astro_02.svg", "assets/avatars/astro_03.svg", "assets/avatars/astro_04.svg"].includes(avSel.value) &&
        ["assets/avatars/astro_01.svg", "assets/avatars/astro_02.svg", "assets/avatars/astro_03.svg", "assets/avatars/astro_04.svg"].includes(modOriginalAvatarPath)) {
      avatarValue = avSel.value;
    }
  }

  const sexVal = document.getElementById("mod-sexo").value;
  const isFem = sexVal.toUpperCase() === "F";

  const putPayload = {
    nombre: document.getElementById("mod-nombre").value.trim(),
    sexo: sexVal,
    rol: document.getElementById("mod-rol").value.trim(),
    avatar: avatarValue,
    radiacion_previa_msv: parseFloat(document.getElementById("mod-rad-previa").value),
    radiacion_mision_msv: parseFloat(document.getElementById("mod-rad-mision").value),
    baseline: {
      fc_base: parseFloat(document.getElementById("mod-b-fc").value),
      pa_sis_base: parseFloat(document.getElementById("mod-b-pasis").value),
      pa_dia_base: parseFloat(document.getElementById("mod-b-padia").value),
      spo2_base: parseFloat(document.getElementById("mod-b-spo2").value),
      temp_base: parseFloat(document.getElementById("mod-b-temp").value),
      glucosa_base: parseFloat(document.getElementById("mod-b-glucosa").value),
      peso_base: parseFloat(document.getElementById("mod-b-peso").value),
      sueno_base_h: parseFloat(document.getElementById("mod-b-sueno").value),
      animo_base: parseInt(document.getElementById("mod-b-animo").value, 10),
      estres_base: parseInt(document.getElementById("mod-b-estres").value, 10),
      cognitivo_base_ms: parseFloat(document.getElementById("mod-b-cog").value),
      calorias_objetivo_kcal: parseInt(document.getElementById("mod-b-cal").value, 10),
      hidratacion_objetivo_ml: parseInt(document.getElementById("mod-b-hid").value, 10),
      apetito_base: parseInt(document.getElementById("mod-b-apetito").value, 10),
      ejercicio_objetivo_min: parseInt(document.getElementById("mod-b-ej").value, 10),
      pre_resp_o2_objetivo_min: parseInt(document.getElementById("mod-b-pre").value, 10),
      vision_base: parseFloat(document.getElementById("mod-b-vision").value),
      densidad_osea_base_pct: parseFloat(document.getElementById("mod-b-osea").value),
      dosis_diaria_nominal_msv: parseFloat(document.getElementById("mod-b-dosis").value)
    }
  };

  try {
    const res = await fetch(`/api/astronauts/${encodeURIComponent(astroId)}`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(putPayload)
    });
    const data = await res.json();
    if (!res.ok) {
      showToast(data.error || "Error modificando datos del astronauta", true);
      return;
    }

    const updateSample = document.getElementById("mod-update-sample-cb")?.checked;
    if (updateSample) {
      const samplePayload = {
        dia_mision: parseInt(document.getElementById("mod-s-dia").value, 10) || 1,
        fc: parseFloat(document.getElementById("mod-s-fc").value),
        spo2: parseFloat(document.getElementById("mod-s-spo2").value),
        pa_sis: parseFloat(document.getElementById("mod-s-pasis").value),
        pa_dia: parseFloat(document.getElementById("mod-s-padia").value),
        temp_c: parseFloat(document.getElementById("mod-s-temp").value),
        glucosa_mg_dl: parseFloat(document.getElementById("mod-s-glucosa").value),
        peso_kg: parseFloat(document.getElementById("mod-s-peso").value),
        vision_actual: parseFloat(document.getElementById("mod-s-vision").value),
        sueno_horas: parseFloat(document.getElementById("mod-s-sueno").value),
        estres: parseInt(document.getElementById("mod-s-estres").value, 10),
        dolor_cabeza: parseInt(document.getElementById("mod-s-cefalea").value, 10),
        mareo_vestibular: parseInt(document.getElementById("mod-s-mareo").value, 10),
        ejercicio_min: parseInt(document.getElementById("mod-s-ejercicio").value, 10),
        dia_descanso: document.getElementById("mod-s-descanso")?.checked ? 1 : 0,
        hidratacion_ml: parseInt(document.getElementById("mod-s-hidratacion").value, 10),
        fase_ciclo: isFem ? (parseInt(document.getElementById("mod-s-fase").value, 10) || 0) : 0,
        dia_ciclo: isFem ? (parseInt(document.getElementById("mod-s-dia-ciclo").value, 10) || 0) : 0,
        flujo_menstrual: isFem ? (parseInt(document.getElementById("mod-s-flujo").value, 10) || 0) : 0,
        colico_menstrual: isFem ? (parseInt(document.getElementById("mod-s-colico").value, 10) || 0) : 0,
        suplemento_hierro: isFem && document.getElementById("mod-s-hierro")?.checked ? 1 : 0
      };
      const sRes = await fetch(`/api/astronauts/${encodeURIComponent(astroId)}/samples`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(samplePayload)
      });
      if (!sRes.ok) {
        const sData = await sRes.json().catch(() => ({}));
        showToast(sData.error || "Perfil guardado, pero hubo un error en los valores clínicos", true);
        await refreshMissionState(true);
        return;
      }
    }

    avatarCacheBust = Date.now();
    modCustomAvatarDataUrl = null;
    showToast(`Datos de ${putPayload.nombre} actualizados correctamente.`);
    await refreshMissionState(true);
    await loadAstronautForModify(astroId);
  } catch (err) {
    showToast("Error de red al guardar modificaciones", true);
  }
}

function exportSelectedModifyAstronautLog() {
  const sel = document.getElementById("mod-astronaut-select");
  const astroId = sel ? sel.value : currentAstronautId;
  if (!astroId) {
    showToast("Selecciona un astronauta para exportar su bitácora.", true);
    return;
  }
  window.location.href = `/api/astronauts/${encodeURIComponent(astroId)}/export`;
}

async function deleteSelectedModifyAstronaut() {
  const sel = document.getElementById("mod-astronaut-select");
  const astroId = sel ? sel.value : "";
  if (!astroId) {
    showToast("Selecciona un astronauta para eliminar.", true);
    return;
  }
  const crewObj = allCrewList.find((c) => c.id === astroId);
  const labelName = crewObj?.nombre || astroId;
  const confirmDel = window.confirm(
    `¿Confirmas eliminar el perfil completo de ${labelName} (${astroId})?`
  );
  if (!confirmDel) return;

  try {
    const res = await fetch(`/api/astronauts/${encodeURIComponent(astroId)}`, {
      method: "DELETE"
    });
    if (res.ok) {
      showToast(`Astronauta ${labelName} eliminado.`);
      if (currentAstronautId === astroId) {
        closeAstronautDetail();
      }
      await refreshMissionState(true);
    } else {
      showToast("No se pudo eliminar el astronauta.", true);
    }
  } catch (err) {
    showToast("Error al eliminar astronauta", true);
  }
}

/* =========================================================================
 * Vista PROMEDIO DE SALUD y Entorno de Cabina (CabinEnv)
 * ========================================================================= */
function renderPromedioView(m, crewList, forceEnvFill = false) {
  const sum = m.summary || {};
  const env = m.env || {};
  const list = Array.isArray(crewList) && crewList.length > 0 ? crewList : (m.crew || []);

  const setTxt = (id, txt) => {
    const el = document.getElementById(id);
    if (el) el.textContent = txt;
  };

  let avgVal = sum.mission_health_avg ?? m.mission_health_avg ?? 100;
  let okCount = sum.crew_ok_count ?? 0;
  let cautCount = sum.crew_caution_count ?? 0;
  let critCount = sum.crew_critical_count ?? 0;
  let vcCount = sum.crew_very_critical_count ?? 0;

  if (list.length > 0) {
    avgVal = Math.round(list.reduce((acc, c) => acc + (c.health_score ?? 100), 0) / list.length);
    okCount = list.filter((c) => c.global_status === 0).length;
    cautCount = list.filter((c) => c.global_status === 1).length;
    critCount = list.filter((c) => c.global_status === 2).length;
    vcCount = list.filter(isCrewVeryCritical).length;
  }

  const sumAvgEl = document.getElementById("sum-health-avg");
  if (sumAvgEl) {
    sumAvgEl.textContent = `${avgVal}%`;
    sumAvgEl.style.color = avgVal < 60 ? "#F87171" : avgVal < 85 ? "#FBBF24" : "#4ADE80";
  }
  setTxt(
    "sum-crew-breakdown",
    `Normal: ${okCount} · Precaución: ${cautCount} · Crítico: ${critCount}${vcCount > 0 ? ` · Urgencia: ${vcCount}` : ""}`
  );
  setTxt("sum-fc-avg", `${(sum.fc_avg ?? 70).toFixed(1)} lpm`);
  setTxt("sum-spo2-avg", `${(sum.spo2_avg ?? 98).toFixed(1)}%`);
  setTxt("sum-sueno-avg", `${(sum.sueno_avg_h ?? 7.5).toFixed(1)} h`);
  setTxt("sum-rad-avg", `${(sum.rad_mision_avg_msv ?? 0).toFixed(2)} mSv`);
  setTxt("sum-estres-sub", `Estrés medio: ${(sum.estres_avg ?? 3).toFixed(1)}/10`);

  /* Semaforo de Factores de Riesgo Fisiologico */
  const hazList = document.getElementById("hazards-semaphore-list");
  const hazStatus = sum.hazard_status || [0, 0, 0, 0, 0];
  if (hazList) {
    hazList.innerHTML = HAZARD_META.map((h) => {
      const st = hazStatus[h.id] || 0;
      const cls = st === 2 ? "haz-critical" : st === 1 ? "haz-caution" : "";
      const badgeColor = st === 2 ? "#F87171" : st === 1 ? "#FBBF24" : "#4ADE80";
      const badgeText = st === 2 ? "CRÍTICO" : st === 1 ? "PRECAUCIÓN" : "NORMAL";
      return `<div class="hazard-row ${cls}">
        <div>
          <strong>${escapeHtml(h.name)}</strong>
          <div style="font-size:0.76rem; color:#A3A3A3; margin-top:2px;">${escapeHtml(h.desc)}</div>
        </div>
        <span class="eva-badge" style="color:${badgeColor}; border:1px solid ${badgeColor};">
          ${badgeText}
        </span>
      </div>`;
    }).join("");
  }

  /* No pisar el formulario de cabina si el usuario tiene el foco dentro de el */
  const envForm = document.getElementById("cabin-env-form");
  const isEditingEnv = envForm && envForm.contains(document.activeElement);
  if (forceEnvFill || !isEditingEnv) {
    const setVal = (id, v) => {
      const el = document.getElementById(id);
      if (el && v !== undefined) el.value = v;
    };
    setVal("env-presion", env.presion_cabina_kpa);
    setVal("env-po2", env.po2_kpa);
    setVal("env-pco2", env.pco2_kpa);
    setVal("env-rad", env.tasa_rad_msv_h);
    setVal("env-gravedad", env.gravedad_actual);
    setVal("env-latencia", env.latencia_com_seg);
    const speCb = document.getElementById("env-spe");
    if (speCb) speCb.checked = env.evento_spe_activo === 1;
  }
}

async function submitCabinEnvUpdate(ev) {
  ev.preventDefault();
  const payload = {
    presion_cabina_kpa: parseFloat(document.getElementById("env-presion").value),
    po2_kpa: parseFloat(document.getElementById("env-po2").value),
    pco2_kpa: parseFloat(document.getElementById("env-pco2").value),
    tasa_rad_msv_h: parseFloat(document.getElementById("env-rad").value),
    gravedad_actual: parseInt(document.getElementById("env-gravedad").value, 10),
    latencia_com_seg: parseFloat(document.getElementById("env-latencia").value),
    evento_spe_activo: document.getElementById("env-spe").checked ? 1 : 0
  };

  try {
    const res = await fetch(`/api/missions/${encodeURIComponent(currentMissionId)}/env`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    const data = await res.json();
    if (!res.ok) {
      showToast(data.error || "Error validando entorno de cabina", true);
      return;
    }
    showToast("Entorno de cabina actualizado y tripulación re-evaluada.");
    await refreshMissionState(true);
  } catch (err) {
    showToast("Error de red local actualizando cabina", true);
  }
}

/* =========================================================================
 * Crew Check-in Diario + Mini-Test de Reflejos (3 toques)
 * ========================================================================= */
function populateCheckinAstronautSelector(crewList, initialLoad = false) {
  const sel = document.getElementById("ci-astronaut");
  if (!sel) return;

  if (!crewList || crewList.length === 0) {
    sel.innerHTML = `<option value="">Sin tripulantes registrados</option>`;
    const hormBox = document.getElementById("ci-hormonal-box");
    if (hormBox) hormBox.classList.add("hidden");
    return;
  }

  const prevVal = sel.value;
  const currentSignature = Array.from(sel.options)
    .map((o) => o.value)
    .join(",");
  const nextSignature = crewList
    .map((a) => a.id)
    .join(",");

  if (initialLoad || currentSignature !== nextSignature) {
    sel.innerHTML = crewList
      .map((a) => `<option value="${escapeAttr(a.id)}">${escapeHtml(a.nombre)} (${escapeHtml(a.id)})</option>`)
      .join("");
    if (prevVal && crewList.some((c) => c.id === prevVal)) {
      sel.value = prevVal;
    } else if (currentAstronautId && crewList.some((c) => c.id === currentAstronautId)) {
      sel.value = currentAstronautId;
    }
    if (initialLoad || !prevVal || !crewList.some((c) => c.id === prevVal)) {
      prefillCheckinFromAstronaut(sel.value);
    }
  }
}

async function prefillCheckinFromAstronaut(astronautId) {
  const hormBoxEl = document.getElementById("ci-hormonal-box");
  if (!astronautId) {
    if (hormBoxEl) hormBoxEl.classList.add("hidden");
    return;
  }
  const reqSeq = ++prefillRequestSeq;
  try {
    const res = await fetch(`/api/astronauts/${encodeURIComponent(astronautId)}`);
    if (!res.ok) return;
    const a = await res.json();
    crewDetailsMap[astronautId] = a;
    const sel = document.getElementById("ci-astronaut");
    if (reqSeq !== prefillRequestSeq || (sel && sel.value && sel.value !== astronautId)) {
      return;
    }
    const hist = a.history || [];
    const latest = hist.length > 0 ? hist[hist.length - 1] : null;
    const isFem = (a.sexo || "M").toUpperCase() === "F";

    renderCheckinRoutineNudges(astronautId);

    /* Mostrar/ocultar bloque hormonal femenino ANTES de verificar muestras previas */
    const sexNote = document.getElementById("ci-hormonal-sex-note");
    const hormBox = document.getElementById("ci-hormonal-box");
    if (sexNote) {
      sexNote.textContent = "♀ Fisiología Femenina";
    }
    if (hormBox) {
      hormBox.classList.toggle("hidden", !isFem);
    }

    const setV = (id, val) => {
      const el = document.getElementById(id);
      if (el && val !== undefined) el.value = val;
    };

    if (!isFem || !latest) {
      setV("ci-fase-ciclo", 0);
      setV("ci-dia-ciclo", 0);
      setV("ci-flujo-menstrual", 0);
      setV("ci-colico-menstrual", 0);
      const ironCb = document.getElementById("ci-suplemento-hierro");
      if (ironCb) ironCb.checked = false;
    }

    const restCb = document.getElementById("ci-dia-descanso");
    if (!latest) {
      if (restCb) restCb.checked = false;
      return;
    }

    setV("ci-dia", latest.dia_mision);
    setV("ci-sueno-horas", latest.sueno_horas);
    setV("ci-sueno-calidad", latest.sueno_calidad);
    setV("ci-animo", latest.animo);
    setV("ci-estres", latest.estres);
    setV("ci-cohesion", latest.cohesion_equipo);
    setV("ci-apetito", latest.apetito_comida);
    setV("ci-dolor-cabeza", latest.dolor_cabeza);
    setV("ci-mareo", latest.mareo_vestibular);
    setV("ci-ejercicio", latest.ejercicio_min);
    if (restCb) restCb.checked = latest.dia_descanso === 1;
    setV("ci-cognitivo", Math.round(latest.cognitivo_ms));
    setV("ci-fc", latest.fc);
    setV("ci-pa-sis", latest.pa_sis);
    setV("ci-pa-dia", latest.pa_dia);
    setV("ci-spo2", latest.spo2);
    setV("ci-temp", latest.temp_c);
    setV("ci-glucosa", latest.glucosa_mg_dl);
    setV("ci-peso", latest.peso_kg);
    setV("ci-vision", latest.vision_actual);
    setV("ci-dosis", latest.dosis_dia_msv);
    setV("ci-calorias", latest.calorias_kcal);
    setV("ci-hidratacion", latest.hidratacion_ml);
    setV("ci-preresp", latest.pre_resp_o2_min);

    const evaCb = document.getElementById("ci-tuvo-eva");
    if (evaCb) evaCb.checked = latest.tuvo_eva === 1;

    if (isFem) {
      setV("ci-fase-ciclo", latest.fase_ciclo ?? 0);
      setV("ci-dia-ciclo", latest.dia_ciclo ?? 0);
      setV("ci-flujo-menstrual", latest.flujo_menstrual ?? 0);
      setV("ci-colico-menstrual", latest.colico_menstrual ?? 0);
      const ironCb = document.getElementById("ci-suplemento-hierro");
      if (ironCb) ironCb.checked = latest.suplemento_hierro === 1;
    }

    const flags = latest.sintomas_flags || 0;
    document.querySelectorAll(".ci-flag-cb").forEach((cb) => {
      const bit = parseInt(cb.value, 10);
      cb.checked = (flags & bit) !== 0;
    });
  } catch (err) {
    console.error("Error prellenando check-in:", err);
  }
}

function onCheckinRestDayToggle(isRest) {
  const ejEl = document.getElementById("ci-ejercicio");
  if (!ejEl) return;
  if (isRest) {
    ejEl.value = 0;
  } else if (parseInt(ejEl.value, 10) === 0) {
    const astroId = document.getElementById("ci-astronaut")?.value;
    const astro = astroId ? crewDetailsMap[astroId] : null;
    ejEl.value = astro?.baseline?.ejercicio_objetivo_min ?? 90;
  }
}

/* Mini-Test de Reflejos con performance.now() */
function startReflexTest() {
  const arena = document.getElementById("reflex-arena");
  const prompt = document.getElementById("reflex-prompt");
  if (!arena || !prompt) return;

  clearTimeout(reflexState.timeoutId);
  reflexState.active = true;
  reflexState.samples = [];
  arena.classList.remove("hidden");
  scheduleNextReflexTrial();
}

function scheduleNextReflexTrial() {
  const arena = document.getElementById("reflex-arena");
  const prompt = document.getElementById("reflex-prompt");
  reflexState.phase = "waiting";
  arena.className = "reflex-arena waiting";
  prompt.textContent = `Intento ${reflexState.samples.length + 1}/3: Espera al color VERDE...`;

  const delay = 1000 + Math.random() * 1600;
  reflexState.timeoutId = setTimeout(() => {
    reflexState.phase = "ready";
    reflexState.startTimeMs = performance.now();
    arena.className = "reflex-arena ready";
    prompt.textContent = "¡TOCA AHORA!";
  }, delay);
}

function handleReflexArenaTap() {
  if (!reflexState.active) return;
  const arena = document.getElementById("reflex-arena");
  const prompt = document.getElementById("reflex-prompt");

  if (reflexState.phase === "waiting") {
    clearTimeout(reflexState.timeoutId);
    prompt.textContent = "¡Demasiado pronto! Reintentando...";
    setTimeout(scheduleNextReflexTrial, 700);
    return;
  }

  if (reflexState.phase === "ready") {
    const elapsed = Math.max(80, Math.round(performance.now() - reflexState.startTimeMs));
    reflexState.samples.push(elapsed);

    if (reflexState.samples.length < 3) {
      scheduleNextReflexTrial();
    } else {
      reflexState.active = false;
      reflexState.phase = "idle";
      const avg = Math.round(
        reflexState.samples.reduce((a, b) => a + b, 0) / reflexState.samples.length
      );
      arena.className = "reflex-arena";
      prompt.textContent = `✓ Promedio de 3 toques: ${avg} ms (${reflexState.samples.join(" / ")} ms)`;
      const cogInput = document.getElementById("ci-cognitivo");
      if (cogInput) cogInput.value = avg;
    }
  }
}

/* Codificacion de SymptomFlags y envio de Check-in */
function encodeSymptomFlagsFromCheckboxes() {
  let flags = 0;
  document.querySelectorAll(".ci-flag-cb").forEach((cb) => {
    if (cb.checked) {
      flags |= parseInt(cb.value, 10);
    }
  });
  return flags;
}

async function submitCrewCheckin(ev) {
  ev.preventDefault();
  const astroId = document.getElementById("ci-astronaut").value;
  if (!astroId) return;

  const crewObj = allCrewList.find((c) => c.id === astroId);
  const isFem = crewObj
    ? (crewObj.sexo || "M").toUpperCase() === "F"
    : !document.getElementById("ci-hormonal-box")?.classList.contains("hidden");

  const payload = {
    dia_mision: parseInt(document.getElementById("ci-dia").value, 10),
    sueno_horas: parseFloat(document.getElementById("ci-sueno-horas").value),
    sueno_calidad: parseInt(document.getElementById("ci-sueno-calidad").value, 10),
    animo: parseInt(document.getElementById("ci-animo").value, 10),
    estres: parseInt(document.getElementById("ci-estres").value, 10),
    cohesion_equipo: parseInt(document.getElementById("ci-cohesion").value, 10),
    apetito_comida: parseInt(document.getElementById("ci-apetito").value, 10),
    dolor_cabeza: parseInt(document.getElementById("ci-dolor-cabeza").value, 10),
    mareo_vestibular: parseInt(document.getElementById("ci-mareo").value, 10),
    ejercicio_min: parseInt(document.getElementById("ci-ejercicio").value, 10),
    dia_descanso: document.getElementById("ci-dia-descanso")?.checked ? 1 : 0,
    cognitivo_ms: parseFloat(document.getElementById("ci-cognitivo").value),
    fc: parseFloat(document.getElementById("ci-fc").value),
    pa_sis: parseFloat(document.getElementById("ci-pa-sis").value),
    pa_dia: parseFloat(document.getElementById("ci-pa-dia").value),
    spo2: parseFloat(document.getElementById("ci-spo2").value),
    temp_c: parseFloat(document.getElementById("ci-temp").value),
    glucosa_mg_dl: parseFloat(document.getElementById("ci-glucosa").value),
    peso_kg: parseFloat(document.getElementById("ci-peso").value),
    vision_actual: parseFloat(document.getElementById("ci-vision").value),
    dosis_dia_msv: parseFloat(document.getElementById("ci-dosis").value),
    calorias_kcal: parseInt(document.getElementById("ci-calorias").value, 10),
    hidratacion_ml: parseInt(document.getElementById("ci-hidratacion").value, 10),
    pre_resp_o2_min: parseInt(document.getElementById("ci-preresp").value, 10),
    tuvo_eva: document.getElementById("ci-tuvo-eva").checked ? 1 : 0,
    fase_ciclo: isFem ? (parseInt(document.getElementById("ci-fase-ciclo")?.value || "0", 10) || 0) : 0,
    dia_ciclo: isFem ? (parseInt(document.getElementById("ci-dia-ciclo")?.value || "0", 10) || 0) : 0,
    flujo_menstrual: isFem ? (parseInt(document.getElementById("ci-flujo-menstrual")?.value || "0", 10) || 0) : 0,
    colico_menstrual: isFem ? (parseInt(document.getElementById("ci-colico-menstrual")?.value || "0", 10) || 0) : 0,
    suplemento_hierro: isFem && document.getElementById("ci-suplemento-hierro")?.checked ? 1 : 0,
    sintomas_flags: encodeSymptomFlagsFromCheckboxes()
  };

  try {
    const res = await fetch(`/api/astronauts/${encodeURIComponent(astroId)}/samples`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    const data = await res.json();
    if (!res.ok) {
      showToast(data.error || "Error validando control diario", true);
      return;
    }
    const astroName = crewObj?.nombre || astroId;
    showToast(`Control diario registrado para ${astroName} (Puntaje de Salud: ${data.report?.health_score}%).`);
    currentAstronautId = astroId;
    switchOptionView("mostrar");
    await refreshMissionState(true);
  } catch (err) {
    showToast("Error enviando control diario", true);
  }
}

/* =========================================================================
 * Subir Archivo .JSON (CARGAR ARCHIVO) y Creacion de Perfiles
 * ========================================================================= */
function onJsonFileSelected(ev) {
  const file = ev.target.files && ev.target.files[0];
  if (!file) return;
  document.getElementById("selected-file-name").textContent = `${file.name} (${file.size} bytes)`;

  const reader = new FileReader();
  reader.onload = (e) => {
    document.getElementById("json-import-preview").value = e.target.result;
  };
  reader.readAsText(file);
}

function fillSampleImportTemplate() {
  const templateBase = Object.assign(
    {
      fc_base: 68,
      pa_sis_base: 118,
      pa_dia_base: 78,
      spo2_base: 98.2,
      temp_base: 36.6,
      glucosa_base: 90,
      peso_base: 74,
      sueno_base_h: 7.5,
      animo_base: 8,
      estres_base: 3,
      cognitivo_base_ms: 250,
      calorias_objetivo_kcal: 2500,
      hidratacion_objetivo_ml: 2500,
      apetito_base: 4,
      vision_base: 1.0,
      densidad_osea_base_pct: 100,
      ejercicio_objetivo_min: 90,
      pre_resp_o2_objetivo_min: 120,
      dosis_diaria_nominal_msv: 0.5
    },
    defaults || {}
  );
  delete templateBase.NASA_CAREER_RAD_LIMIT_MSV;

  const sampleObj = {
    id: "astro_05",
    nombre: "Dra. Valentina Ríos",
    sexo: "F",
    rol: "Especialista en Exobiología",
    mission_id: currentMissionId,
    avatar: "assets/avatars/astro_04.svg",
    radiacion_previa_msv: 28.5,
    baseline: templateBase,
    history: [
      {
        dia_mision: 12,
        fc: 70,
        pa_sis: 119,
        pa_dia: 78,
        spo2: 98.2,
        temp_c: 36.6,
        glucosa_mg_dl: 91,
        peso_kg: 64.0,
        dosis_dia_msv: 0.49,
        vision_actual: 0.99,
        dolor_cabeza: 0,
        mareo_vestibular: 0,
        ejercicio_min: 92,
        tuvo_eva: 0,
        pre_resp_o2_min: 120,
        sintomas_flags: 128,
        sueno_horas: 7.5,
        sueno_calidad: 4,
        animo: 8,
        estres: 3,
        cognitivo_ms: 249,
        cohesion_equipo: 5,
        apetito_comida: 4,
        calorias_kcal: 2380,
        hidratacion_ml: 2500,
        fase_ciclo: 2,
        dia_ciclo: 7,
        flujo_menstrual: 0,
        colico_menstrual: 0,
        suplemento_hierro: 1
      },
      {
        dia_mision: 13,
        fc: 71,
        pa_sis: 120,
        pa_dia: 79,
        spo2: 98.1,
        temp_c: 36.6,
        glucosa_mg_dl: 90,
        peso_kg: 63.9,
        dosis_dia_msv: 0.51,
        vision_actual: 0.99,
        dolor_cabeza: 0,
        mareo_vestibular: 0,
        ejercicio_min: 90,
        tuvo_eva: 0,
        pre_resp_o2_min: 120,
        sintomas_flags: 128,
        sueno_horas: 7.4,
        sueno_calidad: 4,
        animo: 8,
        estres: 3,
        cognitivo_ms: 251,
        cohesion_equipo: 5,
        apetito_comida: 4,
        calorias_kcal: 2390,
        hidratacion_ml: 2510,
        fase_ciclo: 2,
        dia_ciclo: 8,
        flujo_menstrual: 0,
        colico_menstrual: 0,
        suplemento_hierro: 1
      },
      {
        dia_mision: 14,
        fc: 69,
        pa_sis: 118,
        pa_dia: 78,
        spo2: 98.3,
        temp_c: 36.5,
        glucosa_mg_dl: 89,
        peso_kg: 63.9,
        dosis_dia_msv: 0.50,
        vision_actual: 0.99,
        dolor_cabeza: 0,
        mareo_vestibular: 0,
        ejercicio_min: 95,
        tuvo_eva: 0,
        pre_resp_o2_min: 120,
        sintomas_flags: 384,
        sueno_horas: 7.6,
        sueno_calidad: 5,
        animo: 9,
        estres: 2,
        cognitivo_ms: 246,
        cohesion_equipo: 5,
        apetito_comida: 5,
        calorias_kcal: 2420,
        hidratacion_ml: 2550,
        fase_ciclo: 2,
        dia_ciclo: 9,
        flujo_menstrual: 0,
        colico_menstrual: 0,
        suplemento_hierro: 1
      }
    ]
  };
  document.getElementById("json-import-preview").value = JSON.stringify(sampleObj, null, 2);
}

async function submitImportedJson() {
  const raw = document.getElementById("json-import-preview").value.trim();
  if (!raw) {
    showToast("Selecciona un archivo .json o carga la plantilla primero", true);
    return;
  }

  let parsed;
  try {
    parsed = JSON.parse(raw);
  } catch (err) {
    showToast("El contenido no es un JSON válido", true);
    return;
  }

  /* Si el JSON viene envuelto en {astronaut: {...}}, extraerlo */
  const payload = parsed.astronaut ? parsed.astronaut : parsed;
  if (!payload.mission_id) {
    payload.mission_id = currentMissionId;
  }
  if (jsonCustomAvatarDataUrl) {
    payload.avatar = jsonCustomAvatarDataUrl;
  }

  try {
    const res = await fetch("/api/astronauts", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    const data = await res.json();
    if (!res.ok) {
      showToast(data.error || "Error importando archivo JSON", true);
      return;
    }
    showToast(`Perfil ${data.nombre} (${data.id}) importado exitosamente.`);
    jsonCustomAvatarDataUrl = null;
    const jsonAvaInput = document.getElementById("json-avatar-file");
    if (jsonAvaInput) jsonAvaInput.value = "";
    const jsonAvaNote = document.getElementById("json-avatar-file-name");
    if (jsonAvaNote) jsonAvaNote.textContent = "Se usará el avatar del JSON o imagen subida";
    const jsonAvaPrev = document.getElementById("json-avatar-preview");
    if (jsonAvaPrev) jsonAvaPrev.src = "assets/avatars/astro_01.svg";

    currentAstronautId = data.id;
    switchOptionView("mostrar");
    await refreshMissionState(true);
  } catch (err) {
    showToast("Error de red importando JSON", true);
  }
}

async function submitNewAstronaut(ev) {
  ev.preventDefault();
  const base = Object.assign({}, defaults || {});
  delete base.NASA_CAREER_RAD_LIMIT_MSV;
  const fcVal = parseFloat(document.getElementById("na-fc-base").value);
  const suenoVal = parseFloat(document.getElementById("na-sueno-base").value);
  const radPrevVal = parseFloat(document.getElementById("na-rad-previa").value);
  base.fc_base = Number.isFinite(fcVal) ? fcVal : 70;
  base.sueno_base_h = Number.isFinite(suenoVal) ? suenoVal : 7.5;

  const payload = {
    id: document.getElementById("na-id").value.trim(),
    nombre: document.getElementById("na-nombre").value.trim(),
    sexo: document.getElementById("na-sexo")?.value || "M",
    rol: document.getElementById("na-rol").value.trim(),
    mission_id: currentMissionId || "artemis_sim",
    avatar: newAstronautCustomAvatarDataUrl || document.getElementById("na-avatar").value,
    radiacion_previa_msv: Number.isFinite(radPrevVal) ? radPrevVal : 0,
    baseline: base
  };

  try {
    const res = await fetch("/api/astronauts", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    const data = await res.json();
    if (!res.ok) {
      showToast(data.error || "Error creando astronauta", true);
      return;
    }
    showToast(`Astronauta ${data.nombre} registrado correctamente.`);
    clearNewAstronautCustomAvatar();
    currentAstronautId = data.id;
    switchOptionView("mostrar");
    await refreshMissionState(true);
  } catch (err) {
    showToast("Error creando astronauta", true);
  }
}

/* =========================================================================
 * Carga de Imagenes Personalizadas (Base64 -> Backend C POST /api/avatars)
 * ========================================================================= */
const ALLOWED_AVATAR_MIMES = new Set([
  "image/png",
  "image/jpeg",
  "image/webp",
  "image/gif",
  "image/svg+xml"
]);

function readImageFileAsDataUrl(file) {
  return new Promise((resolve, reject) => {
    if (!file) {
      reject(new Error("Archivo no seleccionado"));
      return;
    }
    if (!ALLOWED_AVATAR_MIMES.has(file.type)) {
      reject(new Error("Formato no soportado. Usa PNG, JPG, WEBP, GIF o SVG."));
      return;
    }
    if (file.size > 2 * 1024 * 1024) {
      reject(new Error("La imagen supera el máximo de 2 MB."));
      return;
    }
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result || ""));
    reader.onerror = () => reject(new Error("No se pudo leer el archivo de imagen."));
    reader.readAsDataURL(file);
  });
}

async function onNewAstronautAvatarFile(ev) {
  const file = ev.target.files && ev.target.files[0];
  if (!file) return;
  try {
    const dataUrl = await readImageFileAsDataUrl(file);
    newAstronautCustomAvatarDataUrl = dataUrl;
    const prev = document.getElementById("na-avatar-preview");
    const status = document.getElementById("na-avatar-status");
    if (prev) prev.src = dataUrl;
    if (status) status.textContent = `Foto personalizada lista: ${file.name} (${Math.round(file.size / 1024)} KB)`;
    showToast("Imagen personalizada cargada para el nuevo astronauta.");
  } catch (err) {
    showToast(err.message || "Imagen inválida", true);
    ev.target.value = "";
  }
}

function clearNewAstronautCustomAvatar() {
  newAstronautCustomAvatarDataUrl = null;
  const input = document.getElementById("na-avatar-file");
  const prev = document.getElementById("na-avatar-preview");
  const sel = document.getElementById("na-avatar");
  const status = document.getElementById("na-avatar-status");
  if (input) input.value = "";
  if (prev && sel) prev.src = sel.value || "assets/avatars/astro_01.svg";
  if (status) status.textContent = "Usando avatar predefinido o selecciona una imagen local (≤ 2 MB)";
}

function onPredefinedAvatarChange(val) {
  if (!newAstronautCustomAvatarDataUrl) {
    const prev = document.getElementById("na-avatar-preview");
    if (prev && val) prev.src = val;
  }
}

async function onJsonAvatarSelected(ev) {
  const file = ev.target.files && ev.target.files[0];
  if (!file) return;
  try {
    const dataUrl = await readImageFileAsDataUrl(file);
    jsonCustomAvatarDataUrl = dataUrl;
    const prev = document.getElementById("json-avatar-preview");
    const note = document.getElementById("json-avatar-file-name");
    if (prev) prev.src = dataUrl;
    if (note) note.textContent = `Foto personalizada adjuntada: ${file.name}`;
    showToast("Foto personalizada lista para sobrescribir el avatar del JSON.");
  } catch (err) {
    showToast(err.message || "Imagen inválida", true);
    ev.target.value = "";
  }
}

/* =========================================================================
 * Glosario Clinico y Operativo NASA HRP / CSA (GET /api/glossary)
 * ========================================================================= */
async function loadGlossaryData() {
  try {
    const res = await fetch("/api/glossary");
    if (!res.ok) return;
    glossaryData = await res.json();
    renderGlossary();
  } catch (err) {
    console.error("Error cargando glosario médico:", err);
  }
}

function syncGlossaryAutoExpand() {
  const rawQuery = (document.getElementById("glossary-search-input")?.value || "").trim();
  const queryNorm = normalizeText(rawQuery);
  if (queryNorm.length === 0) {
    glossaryExpandedIds.clear();
    return;
  }
  if (queryNorm.length >= 2) {
    const filtered = getFilteredGlossaryItems();
    if (filtered.length > 0 && filtered.length <= 5) {
      filtered.forEach((it) => glossaryExpandedIds.add(it.id));
    }
  }
}

function setGlossaryCategory(cat) {
  activeGlossaryCategory = cat || "ALL";
  document.querySelectorAll(".glossary-cat-btn").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.cat === activeGlossaryCategory);
  });
  syncGlossaryAutoExpand();
  renderGlossary();
}

function filterGlossaryView() {
  syncGlossaryAutoExpand();
  renderGlossary();
}

const GLOSSARY_SEVERITY_ORDER = {
  CRITICAL: 3,
  CAUTION: 2,
  OK: 1
};

function getFilteredGlossaryItems() {
  if (!glossaryData || !Array.isArray(glossaryData.items)) return [];
  const query = normalizeText(document.getElementById("glossary-search-input")?.value || "");
  const matched = glossaryData.items.filter((item) => {
    if (activeGlossaryCategory !== "ALL" && item.category !== activeGlossaryCategory) {
      return false;
    }
    if (!query) return true;
    const haystack = normalizeText([
      item.code,
      item.title,
      item.category,
      CATEGORY_LABELS_ES[item.category] || "",
      item.hazard,
      item.system,
      item.thresholds,
      item.summary,
      item.protocol,
      item.source_title,
      item.source_name,
      item.keywords
    ].join(" "));
    return haystack.includes(query);
  });
  return matched.sort((a, b) => {
    const sa = GLOSSARY_SEVERITY_ORDER[a.severity] || 0;
    const sb = GLOSSARY_SEVERITY_ORDER[b.severity] || 0;
    return sb - sa;
  });
}

function toggleGlossaryItem(itemId) {
  if (!itemId) return;
  if (glossaryExpandedIds.has(itemId)) {
    glossaryExpandedIds.delete(itemId);
  } else {
    glossaryExpandedIds.add(itemId);
  }
  renderGlossary();
}

function renderGlossary() {
  const grid = document.getElementById("glossary-cards-grid");
  const sourcesBox = document.getElementById("glossary-sources-list");
  const countBadge = document.getElementById("glossary-count-badge");
  if (!grid) return;

  if (!glossaryData || !Array.isArray(glossaryData.items)) {
    grid.innerHTML = `<div class="empty-grid-msg">Cargando glosario clínico...</div>`;
    return;
  }

  const rawQuery = (document.getElementById("glossary-search-input")?.value || "").trim();
  const filtered = getFilteredGlossaryItems();

  if (countBadge) {
    countBadge.textContent = `${filtered.length} de ${glossaryData.items.length} entradas clínicas`;
  }

  if (filtered.length === 0) {
    grid.innerHTML = `<div class="empty-grid-msg">No se encontraron entradas en el glosario para "${escapeHtml(rawQuery)}".</div>`;
  } else {
    grid.innerHTML = filtered
      .map((item) => {
        const sev = item.severity || "OK";
        const cat = item.category || "HAZARDS";
        const catLabel = CATEGORY_LABELS_ES[cat] || cat;
        const sevLabel = SEVERITY_LABELS_ES[sev] || "Normal";
        const safeUrl = /^https?:\/\//i.test(item.source_url || "") ? item.source_url : "#";
        const sourceText = item.source_title || item.source_name || "Fuente oficial NASA / CSA";
        const isExpanded = glossaryExpandedIds.has(item.id);
        return `
          <article class="glossary-card sev-${escapeAttr(sev)} cat-${escapeAttr(cat)} ${isExpanded ? "glossary-expanded" : "glossary-collapsed"}">
            <button type="button" class="glossary-card-header-btn" aria-expanded="${isExpanded ? "true" : "false"}" onclick="toggleGlossaryItem('${escapeAttr(item.id)}')">
              <div class="glossary-card-top">
                <span class="glossary-code-tag">${escapeHtml(item.code)}</span>
                <div class="glossary-badge-cluster">
                  <span class="mono-tag">${escapeHtml(catLabel)}</span>
                  <span class="alert-ref-tag">${escapeHtml(sevLabel)}</span>
                  <span class="glossary-chevron">${isExpanded ? "▴ Ocultar" : "▾ Ver detalle"}</span>
                </div>
              </div>
              <h3 class="glossary-card-title">${escapeHtml(item.title)}</h3>
            </button>
            <div class="glossary-card-body ${isExpanded ? "" : "hidden"}">
              <div class="glossary-thresholds"><strong>Definición / Umbrales:</strong> ${escapeHtml(item.thresholds)}</div>
              <p class="glossary-summary">${escapeHtml(item.summary)}</p>
              <div class="glossary-protocol"><strong>Aplicación Clínica / Protocolo:</strong> ${escapeHtml(item.protocol)}</div>
              <div class="glossary-source-row">
                <span class="mono-tag">${escapeHtml(catLabel)}</span>
                <a class="glossary-source-link" href="${escapeAttr(safeUrl)}" target="_blank" rel="noopener noreferrer">
                  ${escapeHtml(sourceText)} ↗
                </a>
              </div>
            </div>
          </article>
        `;
      })
      .join("");
  }

  if (sourcesBox && Array.isArray(glossaryData.sources)) {
    sourcesBox.innerHTML = glossaryData.sources
      .map((src) => {
        const safeUrl = /^https?:\/\//i.test(src.url || "") ? src.url : "#";
        return `
          <div class="glossary-source-item">
            <div>
              <div class="mono-tag">${escapeHtml(src.agency)}</div>
              <strong style="display:block; margin-top:4px; color:#ffffff;">${escapeHtml(src.title)}</strong>
            </div>
            <a class="glossary-source-link" href="${escapeAttr(safeUrl)}" target="_blank" rel="noopener noreferrer">Abrir ↗</a>
          </div>
        `;
      })
      .join("");
  }
}

/* =========================================================================
 * Utilidades de Escapado XSS
 * ========================================================================= */
function escapeHtml(str) {
  if (str === null || str === undefined) return "";
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function escapeAttr(str) {
  return escapeHtml(str);
}

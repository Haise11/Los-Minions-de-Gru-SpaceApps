/* =========================================================================
 * NASA HRP — Space Crew Health Monitor (Frontend 100% Offline JS)
 * ========================================================================= */

let currentMissionId = "artemis_sim";
let currentAstronautId = null;
let activeOptionView = "mostrar";
let crewFilter = "ALL";
let defaults = null;
let currentMissionData = null;
let currentAstronautDetail = null;
let pollTimerId = null;
let prefillRequestSeq = 0;

/* Estado de barras estadisticas colapsables (ocultas por defecto) */
let detailStatsExpanded = false;
let summaryStatsExpanded = false;

/* Estado de Prioridad Medica Total (Muy Critico) */
let isPriorityMode = false;
let priorityAstronautIds = new Set();
let topPriorityAstronautId = null;
let lastPriorityAutoFocused = null;

/* Estado de carga de imagenes personalizadas (Base64 Data URL) */
let newAstronautCustomAvatarDataUrl = null;
let jsonCustomAvatarDataUrl = null;
let avatarCacheBust = 0;

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

const SIM_LABELS_ES = {
  NOMINAL: "Restablecimiento Nominal",
  SPE_STORM: "Tormenta de Partículas Solares (SPE)",
  HYPOXIA: "Hipoxia en Cabina (O₂ Bajo)",
  HYPERCAPNIA: "Hipercapnia (CO₂ Elevado)",
  SANS_SLEEP: "Síndrome SANS + Deuda de Sueño",
  FATIGUE_ISOLATION: "Fatiga y Aislamiento (ARCHeR)",
  DCS_EVA: "Enfermedad Descompresiva Post-EVA",
  HORMONAL_FEMALE: "Alteración Hormonal / Menorragia Femenina"
};

const CATEGORY_LABELS_ES = {
  HAZARDS: "Peligro HRP",
  PROTOCOLS: "Protocolo",
  SYMPTOMS: "Síntoma / Clínica",
  HORMONAL: "Salud Femenina",
  THRESHOLDS: "Umbrales",
  MEASURES: "Medidas y Unidades",
  ACRONYMS: "Siglas y Acrónimos"
};

const SEVERITY_LABELS_ES = {
  OK: "Nominal",
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
  setDetailStatsExpanded(false);
  setSummaryStatsExpanded(false);

  await loadDefaults();
  await loadMissionsList();
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
  const chev = document.getElementById("sidebar-chevron");
  if (!shell) return;
  shell.classList.toggle("sidebar-collapsed", collapsed);
  if (toggleBtn) {
    toggleBtn.setAttribute("aria-expanded", collapsed ? "false" : "true");
  }
  if (chev) {
    chev.textContent = collapsed ? "▸" : "◂";
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
        ? "📊 Ocultar Estadísticas Generales de la Misión"
        : "📊 Mostrar Estadísticas Generales de la Misión";
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
  banner.style.borderColor = isError ? "#ff2a4b" : "#00e5ff";
  banner.style.background = isError
    ? "rgba(255, 42, 75, 0.24)"
    : "rgba(0, 229, 255, 0.18)";
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

async function loadMissionsList() {
  try {
    const res = await fetch("/api/missions");
    if (!res.ok) return;
    const missions = await res.json();
    const sel = document.getElementById("mission-select");
    const naMission = document.getElementById("na-mission");
    if (!Array.isArray(missions) || missions.length === 0) return;

    if (!missions.some((m) => m.id === currentMissionId)) {
      currentMissionId = missions[0].id;
    }

    if (sel) {
      sel.innerHTML = missions
        .map((m) => `<option value="${escapeHtml(m.id)}" ${m.id === currentMissionId ? "selected" : ""}>${escapeHtml(m.nombre)} (Sol ${m.dia_actual})</option>`)
        .join("");
    }
    if (naMission) {
      naMission.innerHTML = missions
        .map((m) => `<option value="${escapeHtml(m.id)}" ${m.id === currentMissionId ? "selected" : ""}>${escapeHtml(m.nombre)}</option>`)
        .join("");
    }
  } catch (err) {
    console.error("Error listando misiones:", err);
  }
}

async function refreshMissionState(initialLoad = false) {
  try {
    const res = await fetch(`/api/missions/${encodeURIComponent(currentMissionId)}`);
    if (!res.ok) return;
    currentMissionData = await res.json();

    const shouldScrollPriority = applyVeryCriticalPriorityMode(currentMissionData);

    renderMissionHeader(currentMissionData);
    renderCrewGrid(currentMissionData.crew || []);
    renderPromedioView(currentMissionData, initialLoad);
    populateCheckinAstronautSelector(currentMissionData.crew || [], initialLoad);

    if (currentAstronautId) {
      await loadAstronautDetail(currentAstronautId, shouldScrollPriority);
    }

    const syncLabel = document.getElementById("last-sync-label");
    if (syncLabel) {
      const now = new Date();
      syncLabel.textContent = `Último pulso local: ${now.toLocaleTimeString()}`;
    }
  } catch (err) {
    console.error("Error refrescando estado de misión:", err);
  }
}

function applyVeryCriticalPriorityMode(m) {
  const wasPriorityMode = isPriorityMode;
  const crew = m?.crew || [];
  const vcList = crew.filter(isCrewVeryCritical);
  isPriorityMode = vcList.length > 0;
  priorityAstronautIds = new Set(vcList.map((c) => c.id));
  topPriorityAstronautId = (m?.summary?.priority_astronaut_id && priorityAstronautIds.has(m.summary.priority_astronaut_id))
    ? m.summary.priority_astronaut_id
    : (vcList[0]?.id || null);

  document.body.classList.toggle("priority-mode-active", isPriorityMode);

  if (!wasPriorityMode && isPriorityMode) {
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
    crewFilter = "ALL";
    document.querySelectorAll(".filter-pill").forEach((btn) => {
      btn.classList.toggle("active", btn.getAttribute("data-filter") === "ALL");
    });
    const sevSelect = document.getElementById("filter-severity");
    if (sevSelect) sevSelect.value = "ALL";
  }

  const banner = document.getElementById("priority-emergency-banner");
  const labelEl = document.getElementById("priority-patient-label");
  const reasonEl = document.getElementById("priority-patient-reason");
  if (banner) {
    banner.classList.toggle("hidden", !isPriorityMode);
  }
  if (isPriorityMode && topPriorityAstronautId) {
    const topCrew = vcList.find((c) => c.id === topPriorityAstronautId) || vcList[0];
    if (labelEl && topCrew) {
      labelEl.textContent = `${topCrew.nombre} — Salud: ${topCrew.health_score}% (${topCrew.resumen_estado || "Crítico"})`;
    }
    if (reasonEl) {
      const extra = vcList.length > 1 ? ` · ${vcList.length} tripulantes en estado muy crítico` : "";
      reasonEl.textContent = `Atención clínica exclusiva habilitada${extra}. Funciones no prioritarias bloqueadas hasta estabilización.`;
    }
  }

  /* Bloqueo / desbloqueo de controles no prioritarios */
  const missionSel = document.getElementById("mission-select");
  if (missionSel) missionSel.disabled = isPriorityMode;

  const subFileBtn = document.getElementById("subtab-btn-file");
  const subCreateBtn = document.getElementById("subtab-btn-create");
  if (subFileBtn) subFileBtn.disabled = isPriorityMode;
  if (subCreateBtn) subCreateBtn.disabled = isPriorityMode;
  if (isPriorityMode) {
    const filePanel = document.getElementById("cargar-tab-file");
    const createPanel = document.getElementById("cargar-tab-create");
    if ((filePanel && !filePanel.classList.contains("hidden")) ||
        (createPanel && !createPanel.classList.contains("hidden"))) {
      switchCargarTab("checkin");
    }
  }

  document.querySelectorAll(".sim-btn").forEach((btn) => {
    const simName = btn.getAttribute("data-sim") || "";
    if (simName !== "NOMINAL") {
      btn.disabled = isPriorityMode;
    }
  });
  document.querySelectorAll(".lat-btn").forEach((btn) => {
    btn.disabled = isPriorityMode;
  });

  const delBtn = document.getElementById("detail-delete-btn");
  if (delBtn) {
    delBtn.disabled = isPriorityMode && priorityAstronautIds.has(currentAstronautId);
  }
  const closeBtn = document.getElementById("detail-close-btn");
  if (closeBtn) {
    closeBtn.disabled = isPriorityMode && priorityAstronautIds.has(currentAstronautId);
  }

  let shouldScroll = false;
  if (isPriorityMode && topPriorityAstronautId) {
    const needAutoFocus = !currentAstronautId || !priorityAstronautIds.has(currentAstronautId);
    if (needAutoFocus || lastPriorityAutoFocused !== topPriorityAstronautId) {
      if (needAutoFocus) {
        if (currentAstronautId !== topPriorityAstronautId) {
          setDetailStatsExpanded(false);
        }
        currentAstronautId = topPriorityAstronautId;
        shouldScroll = true;
      }
      lastPriorityAutoFocused = topPriorityAstronautId;
      const ciSel = document.getElementById("ci-astronaut");
      if (ciSel && ciSel.options.length > 0 && ciSel.value !== topPriorityAstronautId) {
        ciSel.value = topPriorityAstronautId;
        prefillCheckinFromAstronaut(topPriorityAstronautId);
      }
    }
  } else {
    lastPriorityAutoFocused = null;
  }
  return shouldScroll;
}

function focusPriorityAstronaut() {
  if (!topPriorityAstronautId) return;
  switchOptionView("mostrar");
  if (currentAstronautId !== topPriorityAstronautId) {
    setDetailStatsExpanded(false);
  }
  currentAstronautId = topPriorityAstronautId;
  loadAstronautDetail(topPriorityAstronautId, true);
}

/* =========================================================================
 * Navegacion entre Vistas (OPTIONS: CARGAR | MOSTRAR | PROMEDIO | GLOSARIO)
 * ========================================================================= */
function switchOptionView(viewName) {
  activeOptionView = viewName;
  const views = ["mostrar", "cargar", "promedio", "glosario"];
  views.forEach((v) => {
    const sec = document.getElementById(`view-${v}`);
    const btn = document.getElementById(`nav-${v}`);
    if (sec) sec.classList.toggle("hidden", v !== viewName);
    if (btn) btn.classList.toggle("active", v === viewName);
  });
  if (viewName === "cargar" && isPriorityMode) {
    switchCargarTab("checkin");
  }
  if (viewName === "glosario" && !glossaryData) {
    loadGlossaryData();
  }
}

function switchCargarTab(tabName) {
  if (isPriorityMode && tabName !== "checkin") {
    showToast("Prioridad Médica Total activa: solo está habilitado el Check-in del paciente prioritario.", true);
    tabName = "checkin";
  }
  const tabs = ["checkin", "file", "create"];
  tabs.forEach((t) => {
    const panel = document.getElementById(`cargar-tab-${t}`);
    const btn = document.getElementById(`subtab-btn-${t}`);
    if (panel) panel.classList.toggle("hidden", t !== tabName);
    if (btn) btn.classList.toggle("active", t === tabName);
  });
}

function onMissionSelectChange(newMissionId) {
  if (isPriorityMode) {
    const sel = document.getElementById("mission-select");
    if (sel) sel.value = currentMissionId;
    showToast("No puedes cambiar de misión mientras exista un tripulante en estado Muy Crítico.", true);
    return;
  }
  currentMissionId = newMissionId;
  currentAstronautId = null;
  closeAstronautDetail();
  refreshMissionState(true);
}

function setCrewFilter(filterVal) {
  crewFilter = filterVal || "ALL";
  document.querySelectorAll(".filter-pill").forEach((btn) => {
    btn.classList.toggle("active", btn.getAttribute("data-filter") === crewFilter);
  });
  const sevSelect = document.getElementById("filter-severity");
  if (sevSelect && sevSelect.value !== crewFilter) {
    sevSelect.value = crewFilter;
  }
  if (currentMissionData) {
    renderCrewGrid(currentMissionData.crew || []);
  }
  if (activeOptionView !== "mostrar") {
    switchOptionView("mostrar");
  }
}

function applyCrewFilters() {
  if (currentMissionData) {
    renderCrewGrid(currentMissionData.crew || []);
  }
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

function toggleSimulatorPanel() {
  const body = document.getElementById("simulator-body");
  const chev = document.getElementById("sim-chevron");
  if (!body) return;
  const isHidden = body.classList.toggle("hidden");
  if (chev) chev.textContent = isHidden ? "▸" : "▾";
}

/* =========================================================================
 * Renderizado de Cabecera y Grilla de Tarjetas
 * ========================================================================= */
function renderMissionHeader(m) {
  const titleEl = document.getElementById("mission-header-title");
  if (titleEl) {
    titleEl.textContent = `MISIÓN: ${m.nombre}`;
  }

  const avgEl = document.getElementById("header-health-avg");
  const avgVal = m.summary ? m.summary.mission_health_avg : m.mission_health_avg;
  if (avgEl) {
    avgEl.textContent = `${avgVal ?? 100}%`;
    avgEl.style.color = avgVal < 60 ? "#ff2a4b" : avgVal < 85 ? "#ffd000" : "#00ff66";
  }

  const autoBadge = document.getElementById("autonomous-badge");
  if (autoBadge) {
    const lat = m.env ? m.env.latencia_com_seg : 0;
    if (m.autonomous_mode === 1) {
      autoBadge.className = "auto-badge auto-active";
      autoBadge.textContent = `MODO AUTÓNOMO ACTIVO · LATENCIA ${lat.toFixed(1)}s`;
    } else {
      autoBadge.className = "auto-badge auto-realtime";
      autoBadge.textContent = `ENLACE TIERRA TIEMPO REAL (${lat.toFixed(1)}s)`;
    }
  }

  const orb = document.getElementById("mission-orb");
  if (orb && m.summary) {
    if (m.summary.crew_critical_count > 0) {
      orb.className = "mission-orb orb-critical";
    } else if (m.summary.crew_caution_count > 0) {
      orb.className = "mission-orb orb-caution";
    } else {
      orb.className = "mission-orb orb-ok";
    }
  }
}

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
    counter.textContent = `${filtered.length} de ${crewList.length} tripulantes visibles`;
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
      const isLockedCard = isPriorityMode && !isVC;
      const evaCls = evaBadgeClass(a.eva_aptitude);
      const isSelected = a.id === currentAstronautId ? "selected" : "";
      const avatarSrc = resolveAvatarUrl(a.avatar);
      const isFem = (a.sexo || "M").toUpperCase() === "F";
      const cycleName = isFem && a.fase_ciclo > 0 ? (CYCLE_PHASE_NAMES[a.fase_ciclo] || "") : "";
      const cycleTag = cycleName ? ` · ${cycleName}` : "";
      const sexBadgeHtml = isFem
        ? `<span class="sex-badge sex-f" title="Fisiología Femenina HRP">♀ FEM${escapeHtml(cycleTag)}</span>`
        : `<span class="sex-badge sex-m" title="Fisiología Masculina HRP">♂ MASC</span>`;
      const vcTagHtml = isVC ? `<span class="vc-priority-chip">🚨 MUY CRÍTICO</span>` : "";

      return `
        <article class="cred-card status-${sev} ${isVC ? "status-very-critical" : ""} ${isSelected} ${isLockedCard ? "priority-locked" : ""}" onclick="selectAstronautCard('${escapeAttr(a.id)}')">
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
              <span class="eva-badge ${evaCls}">EVA: ${escapeHtml(a.eva_aptitude_str || "GO")}</span>
            </div>

            <div class="cred-line-desc">
              <span class="cred-desc-label">Diagnóstico / Resumen</span>
              <div class="cred-desc-value">${escapeHtml(a.resumen_estado || "Nominal")}</div>
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

function highlightBodySystem(sysIdx) {
  activeHighlightedSys = sysIdx;
  document.querySelectorAll("#body-map-svg .body-zone").forEach((el) => {
    const zSys = parseInt(el.getAttribute("data-sys") || "-1", 10);
    el.classList.toggle("zone-active", sysIdx !== null && zSys === sysIdx);
    el.classList.toggle("zone-dim", sysIdx !== null && zSys !== sysIdx);
  });
  document.querySelectorAll("#system-legend-list .sys-legend-item").forEach((el) => {
    const lSys = parseInt(el.getAttribute("data-sys") || "-1", 10);
    el.classList.toggle("legend-active", sysIdx !== null && lSys === sysIdx);
  });
}

function setupBodyMapInteractions() {
  const svg = document.getElementById("body-map-svg");
  if (!svg || svg._bound) return;
  svg._bound = true;
  svg.querySelectorAll(".body-zone").forEach((zone) => {
    const sysIdx = parseInt(zone.getAttribute("data-sys") || "-1", 10);
    zone.addEventListener("mouseenter", () => highlightBodySystem(sysIdx));
    zone.addEventListener("mouseleave", () => highlightBodySystem(null));
    zone.addEventListener("click", () => {
      highlightBodySystem(activeHighlightedSys === sysIdx ? null : sysIdx);
    });
  });
}

async function selectAstronautCard(astronautId) {
  if (isPriorityMode && !priorityAstronautIds.has(astronautId)) {
    showToast("Prioridad Médica Total: atiende primero al tripulante en estado Muy Crítico.", true);
    return;
  }
  if (astronautId !== currentAstronautId) {
    setDetailStatsExpanded(false);
  }
  currentAstronautId = astronautId;
  await loadAstronautDetail(astronautId, true);
}

function closeAstronautDetail() {
  if (isPriorityMode && currentAstronautId && priorityAstronautIds.has(currentAstronautId)) {
    showToast("No puedes cerrar la ficha mientras exista un paciente Muy Crítico activo.", true);
    return;
  }
  currentAstronautId = null;
  currentAstronautDetail = null;
  setDetailStatsExpanded(false);
  const panel = document.getElementById("astronaut-detail-panel");
  if (panel) panel.classList.add("hidden");
  if (currentMissionData) {
    renderCrewGrid(currentMissionData.crew || []);
  }
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

    if (panel && scrollIntoView) {
      panel.scrollIntoView({ behavior: "smooth", block: "nearest" });
    }
    if (scrollIntoView && currentMissionData) {
      renderCrewGrid(currentMissionData.crew || []);
    }
  } catch (err) {
    console.error("Error cargando detalle de astronauta:", err);
  }
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
  const missionDisplay = (currentMissionData && currentMissionData.id === a.mission_id && currentMissionData.nombre)
    ? currentMissionData.nombre
    : a.mission_id;
  document.getElementById("detail-role").textContent = `${a.rol} · Misión: ${missionDisplay}`;

  const isFem = (a.sexo || "M").toUpperCase() === "F";
  const sexBadge = document.getElementById("detail-sex-badge");
  if (sexBadge) {
    sexBadge.className = `sex-badge ${isFem ? "sex-f" : "sex-m"}`;
    sexBadge.textContent = isFem ? "♀ FEMENINO" : "♂ MASCULINO";
  }

  const evaBadge = document.getElementById("detail-eva-badge");
  evaBadge.className = `eva-badge ${evaBadgeClass(rep.eva_aptitude)}`;
  evaBadge.textContent = `EVA / PILOTAJE: ${rep.eva_aptitude_str || "GO"}`;

  const autoTag = document.getElementById("detail-auto-tag");
  if (autoTag) {
    autoTag.classList.toggle("hidden", rep.autonomous_mode !== 1);
  }

  const delBtn = document.getElementById("detail-delete-btn");
  if (delBtn) delBtn.disabled = isPriorityMode && priorityAstronautIds.has(a.id);
  const closeBtn = document.getElementById("detail-close-btn");
  if (closeBtn) closeBtn.disabled = isPriorityMode && priorityAstronautIds.has(a.id);

  const heart = document.getElementById("detail-heart");
  heart.className = `heart-icon heart-${sev}`;
  document.getElementById("detail-score").textContent = `${rep.health_score ?? 100}%`;
  document.getElementById("detail-summary-text").textContent = rep.resumen_estado || "Nominal";

  /* 1. Colorear Mapa Corporal de 7 Sistemas */
  const sysStatus = rep.system_status || [0, 0, 0, 0, 0, 0, 0];
  const colorMap = ["#00ff66", "#ffd000", "#ff2a4b"];

  const setSvgColor = (id, prop, statusIdx) => {
    const el = document.getElementById(id);
    if (el) el.setAttribute(prop, colorMap[sysStatus[statusIdx] || 0]);
  };
  setSvgColor("svg-sys-0", "fill", 0);
  setSvgColor("svg-sys-1", "fill", 1);
  setSvgColor("svg-sys-2-l", "fill", 2);
  setSvgColor("svg-sys-2-r", "fill", 2);
  setSvgColor("svg-sys-3", "stroke", 3);
  setSvgColor("svg-sys-4", "fill", 4);
  setSvgColor("svg-sys-5", "stroke", 5);
  setSvgColor("svg-sys-6", "stroke", 6);

  const legendList = document.getElementById("system-legend-list");
  if (legendList) {
    legendList.innerHTML = SYSTEM_NAMES.map((name, idx) => {
      const st = sysStatus[idx] || 0;
      const cls = st === 2 ? "sys-critical" : st === 1 ? "sys-caution" : "";
      const label = st === 2 ? "CRÍTICO" : st === 1 ? "PRECAUCIÓN" : "OK";
      return `<div class="sys-legend-item ${cls}" data-sys="${idx}"
                   onmouseenter="highlightBodySystem(${idx})"
                   onmouseleave="highlightBodySystem(null)"
                   onclick="highlightBodySystem(${idx})">
        <span>${escapeHtml(name)}</span>
        <strong style="color:${colorMap[st]}">${label}</strong>
      </div>`;
    }).join("");
  }

  /* 2. Signos Vitales vs Referencia Basal Personal (dentro de #detail-stats-panel) */
  const vitalsGrid = document.getElementById("vitals-comparison-grid");
  if (vitalsGrid) {
    if (!latest) {
      vitalsGrid.innerHTML = `<div class="vital-tile" style="grid-column: 1 / -1; text-align:center;">
        <span class="vital-label">Sin muestras clínicas registradas</span>
        <span class="vital-delta">Usa Check-in Rápido para ingresar los primeros signos vitales.</span>
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
    `${totalRad.toFixed(1)} / 600 mSv (Misión: ${(a.radiacion_mision_msv || 0).toFixed(2)} mSv)`;
  const radFill = document.getElementById("detail-rad-fill");
  if (radFill) {
    radFill.style.width = `${radPct.toFixed(1)}%`;
    radFill.style.background =
      totalRad >= 600 ? "#ff2a4b" : totalRad >= 480 ? "#ffd000" : "#00ff66";
  }

  /* Estado de botones 1-clic de contramedidas */
  const flags = latest ? latest.sintomas_flags || 0 : 0;
  const setChip = (id, mask) => {
    const btn = document.getElementById(id);
    if (btn) btn.classList.toggle("active", (flags & mask) !== 0);
  };
  setChip("cm-btn-cuff", 32);
  setChip("cm-btn-bcomp", 64);
  setChip("cm-btn-vitd", 128);
  setChip("cm-btn-veggie", 256);

  /* Sub-panel de Fisiologia Hormonal: ESTRICTAMENTE solo para mujeres */
  const hormPanel = document.getElementById("detail-hormonal-panel");
  if (hormPanel) {
    if (isFem) {
      hormPanel.classList.remove("hidden");
      const phaseBadge = document.getElementById("detail-cycle-phase-badge");
      const metricsBox = document.getElementById("detail-hormonal-metrics");
      const ironBtn = document.getElementById("cm-btn-iron");
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
            <div class="hormonal-mini-tile"><span>Suplemento Hierro (Fe)</span><strong style="color:${ironActive ? "#00ff66" : "#ffd000"}">${ironActive ? "ACTIVO" : "PENDIENTE"}</strong></div>
          `;
        }
        if (ironBtn) ironBtn.classList.toggle("active", ironActive);
      } else {
        if (phaseBadge) phaseBadge.textContent = "Sin registro";
        if (metricsBox) {
          metricsBox.innerHTML = `<div class="hormonal-mini-tile" style="grid-column:1 / -1;">
            <span>Registro Hormonal</span>
            <strong>Pendiente de primer Check-in femenino</strong>
          </div>`;
        }
        if (ironBtn) ironBtn.classList.remove("active");
      }
    } else {
      hormPanel.classList.add("hidden");
      const metricsBox = document.getElementById("detail-hormonal-metrics");
      if (metricsBox) metricsBox.innerHTML = "";
    }
  }

  /* 3. Lista de Alertas y Contramedidas HRP */
  const alertsList = document.getElementById("detail-alerts-list");
  const alerts = rep.alerts || [];
  if (alertsList) {
    if (alerts.length === 0) {
      alertsList.innerHTML = `<div class="alert-item" style="border-left-color:#00ff66;">
        <div class="alert-header-row">
          <span class="alert-title" style="color:#00ff66;">✓ Estado Fisiológico y Conductual Nominal</span>
          <span class="alert-ref-tag">NASA HRP Standard Measures</span>
        </div>
        <div class="alert-exp">Todos los parámetros vitales, dosimétricos, hormonales y conductuales se encuentran dentro de los márgenes nominales respecto a su referencia personal.</div>
      </div>`;
    } else {
      alertsList.innerHTML = alerts
        .map((al) => {
          const isCrit = al.severity === 2;
          return `<div class="alert-item ${isCrit ? "alert-critical" : ""}">
            <div class="alert-header-row">
              <span class="alert-title" style="color:${isCrit ? "#ff2a4b" : "#ffd000"}">
                ${isCrit ? "🚨 CRÍTICO:" : "⚠️ PRECAUCIÓN:"} ${escapeHtml(al.titulo)}
              </span>
              <span class="alert-ref-tag">${escapeHtml(al.referencia_nasa)}</span>
            </div>
            <div class="alert-exp">${escapeHtml(al.explicacion)}</div>
            <div class="alert-cm"><strong>Protocolo HRP:</strong> ${escapeHtml(al.contramedida)}</div>
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
    "#00e5ff"
  );
  drawTrendChart(
    "canvas-spo2",
    recentHist.map((s) => ({ day: s.dia_mision, value: s.spo2 })),
    b.spo2_base || 98,
    "%",
    "#00ff66"
  );
  drawTrendChart(
    "canvas-sueno",
    recentHist.map((s) => ({ day: s.dia_mision, value: s.sueno_horas })),
    b.sueno_base_h || 7.5,
    "h",
    "#c084fc"
  );
  drawTrendChart(
    "canvas-rad",
    recentHist.map((s) => ({ day: s.dia_mision, value: s.dosis_dia_msv })),
    b.dosis_diaria_nominal_msv || 0.5,
    "mSv",
    "#ffd000"
  );
  drawTrendChart(
    "canvas-estres",
    recentHist.map((s) => ({ day: s.dia_mision, value: s.estres })),
    b.estres_base || 3,
    "/10",
    "#ff2a4b"
  );
  drawTrendChart(
    "canvas-vision",
    recentHist.map((s) => ({ day: s.dia_mision, value: s.vision_actual })),
    b.vision_base || 1.0,
    "",
    "#38bdf8"
  );
}

/* =========================================================================
 * Helper de Graficos en <canvas> Nativo (frontend_ui.md §4)
 * ========================================================================= */
function drawTrendChart(canvasId, series, baselineValue, unit, strokeColor = "#00e5ff") {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  const W = canvas.width;
  const H = canvas.height;
  ctx.clearRect(0, 0, W, H);

  if (!series || series.length === 0) {
    ctx.fillStyle = "#94a3b8";
    ctx.font = "12px sans-serif";
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
  ctx.strokeStyle = "rgba(148, 163, 184, 0.22)";
  ctx.lineWidth = 1;
  for (let g = 0; g <= 2; g++) {
    const gy = padT + (g / 2) * plotH;
    ctx.beginPath();
    ctx.moveTo(padL, gy);
    ctx.lineTo(W - padR, gy);
    ctx.stroke();

    const labelVal = maxV - (g / 2) * (maxV - minV);
    const decimals = maxV - minV < 0.5 || Math.abs(labelVal) < 2 ? 2 : labelVal < 10 ? 1 : 0;
    ctx.fillStyle = "#cbd5e1";
    ctx.font = "10px monospace";
    ctx.fillText(labelVal.toFixed(decimals), 3, gy + 3);
  }

  /* Linea de referencia Baseline punteada */
  if (typeof baselineValue === "number") {
    const by = yOf(baselineValue);
    ctx.save();
    ctx.strokeStyle = "rgba(255, 255, 255, 0.55)";
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

  ctx.fillStyle = "#cbd5e1";
  ctx.font = "10px monospace";
  const firstDay = series[0].day;
  const lastDay = series[series.length - 1].day;
  const lastVal = series[series.length - 1].value;
  ctx.fillText(`Sol ${firstDay}`, padL, H - 5);
  ctx.fillText(
    `Sol ${lastDay}: ${lastVal.toFixed(lastVal < 10 ? 2 : 1)} ${unit}`,
    Math.max(padL + 55, W - padR - 115),
    H - 5
  );
}

/* =========================================================================
 * Acciones Rapidas desde la Ficha del Astronauta
 * ========================================================================= */
async function toggleQuickCountermeasure(bitMask) {
  if (!currentAstronautDetail) return;
  if (isPriorityMode && !priorityAstronautIds.has(currentAstronautDetail.id)) {
    showToast("Prioridad Médica Total activa: atiende al tripulante en estado Muy Crítico.", true);
    return;
  }
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
  if (isPriorityMode && !priorityAstronautIds.has(currentAstronautDetail.id)) {
    showToast("Prioridad Médica Total activa: atiende al tripulante en estado Muy Crítico.", true);
    return;
  }
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
          ? "Suplemento de Hierro (Fe) + B-Complex e hidratación activados."
          : "Suplemento de Hierro desactivado."
      );
      await refreshMissionState(true);
    }
  } catch (err) {
    showToast("Error al actualizar protocolo de hierro", true);
  }
}

function openCheckinForCurrentAstronaut() {
  if (!currentAstronautId) return;
  if (isPriorityMode && !priorityAstronautIds.has(currentAstronautId)) {
    showToast("Prioridad Médica Total activa: solo se permite registrar controles al paciente prioritario.", true);
    return;
  }
  switchOptionView("cargar");
  switchCargarTab("checkin");
  const knownAstro =
    (currentAstronautDetail && currentAstronautDetail.id === currentAstronautId ? currentAstronautDetail : null) ||
    (currentMissionData?.crew || []).find((c) => c.id === currentAstronautId);
  const hormBox = document.getElementById("ci-hormonal-box");
  if (hormBox && knownAstro) {
    hormBox.classList.toggle("hidden", (knownAstro.sexo || "M").toUpperCase() !== "F");
  }
  const sel = document.getElementById("ci-astronaut");
  if (sel) {
    sel.value = currentAstronautId;
    prefillCheckinFromAstronaut(currentAstronautId);
  }
}

function exportCurrentAstronautLog() {
  if (!currentAstronautId) return;
  window.location.href = `/api/astronauts/${encodeURIComponent(currentAstronautId)}/export`;
}

async function deleteCurrentAstronaut() {
  if (!currentAstronautId || !currentAstronautDetail) return;
  if (isPriorityMode && priorityAstronautIds.has(currentAstronautId)) {
    showToast("Prioridad Médica Total: no se puede eliminar un tripulante en estado Muy Crítico.", true);
    return;
  }
  const confirmDel = window.confirm(
    `¿Confirmas eliminar el perfil de ${currentAstronautDetail.nombre} (${currentAstronautId})?`
  );
  if (!confirmDel) return;

  try {
    const res = await fetch(`/api/astronauts/${encodeURIComponent(currentAstronautId)}`, {
      method: "DELETE"
    });
    if (res.ok) {
      showToast(`Astronauta ${currentAstronautId} eliminado.`);
      closeAstronautDetail();
      await refreshMissionState(true);
    }
  } catch (err) {
    showToast("Error al eliminar astronauta", true);
  }
}

/* =========================================================================
 * Vista PROMEDIO DE SALUD y Entorno de Cabina (CabinEnv)
 * ========================================================================= */
function renderPromedioView(m, forceEnvFill = false) {
  const sum = m.summary || {};
  const env = m.env || {};

  const setTxt = (id, txt) => {
    const el = document.getElementById(id);
    if (el) el.textContent = txt;
  };

  const avgVal = sum.mission_health_avg ?? m.mission_health_avg ?? 100;
  const sumAvgEl = document.getElementById("sum-health-avg");
  if (sumAvgEl) {
    sumAvgEl.textContent = `${avgVal}%`;
    sumAvgEl.style.color = avgVal < 60 ? "#ff2a4b" : avgVal < 85 ? "#ffd000" : "#00ff66";
  }
  const vcCount = sum.crew_very_critical_count ?? 0;
  setTxt(
    "sum-crew-breakdown",
    `Nominal: ${sum.crew_ok_count ?? 0} · Precaución: ${sum.crew_caution_count ?? 0} · Crítico: ${sum.crew_critical_count ?? 0}${vcCount > 0 ? ` · Muy Crítico: ${vcCount}` : ""}`
  );
  setTxt("sum-fc-avg", `${(sum.fc_avg ?? 70).toFixed(1)} lpm`);
  setTxt("sum-spo2-avg", `${(sum.spo2_avg ?? 98).toFixed(1)}%`);
  setTxt("sum-sueno-avg", `${(sum.sueno_avg_h ?? 7.5).toFixed(1)} h`);
  setTxt("sum-rad-avg", `${(sum.rad_mision_avg_msv ?? 0).toFixed(2)} mSv`);
  setTxt("sum-estres-sub", `Estrés medio: ${(sum.estres_avg ?? 3).toFixed(1)}/10`);

  /* Semaforo de los 5 Peligros HRP */
  const hazList = document.getElementById("hazards-semaphore-list");
  const hazStatus = sum.hazard_status || [0, 0, 0, 0, 0];
  if (hazList) {
    hazList.innerHTML = HAZARD_META.map((h) => {
      const st = hazStatus[h.id] || 0;
      const cls = st === 2 ? "haz-critical" : st === 1 ? "haz-caution" : "";
      const badgeColor = st === 2 ? "#ff2a4b" : st === 1 ? "#ffd000" : "#00ff66";
      const badgeText = st === 2 ? "CRÍTICO" : st === 1 ? "PRECAUCIÓN" : "NOMINAL";
      return `<div class="hazard-row ${cls}">
        <div>
          <strong>${escapeHtml(h.name)}</strong>
          <div style="font-size:0.76rem; color:#94a3b8; margin-top:2px;">${escapeHtml(h.desc)}</div>
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
 * Simulador HRP (POST /api/missions/:id/simulate) y Latencia
 * ========================================================================= */
async function setMissionLatency(seconds) {
  if (isPriorityMode) {
    showToast("Prioridad Médica Total activa: los cambios de latencia están bloqueados hasta atender al paciente crítico.", true);
    return;
  }
  try {
    const res = await fetch(`/api/missions/${encodeURIComponent(currentMissionId)}/env`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ latencia_com_seg: seconds })
    });
    if (res.ok) {
      const modeTxt = seconds > 5 ? "MODO AUTÓNOMO ACTIVADO (Marte)" : "Tiempo Real activado";
      showToast(`Latencia ajustada a ${seconds}s · ${modeTxt}`);
      await refreshMissionState(true);
    }
  } catch (err) {
    showToast("Error cambiando latencia", true);
  }
}

async function triggerSimulation(scenarioName) {
  if (isPriorityMode && scenarioName !== "NOMINAL") {
    showToast("Prioridad Médica Total activa: usa Restablecer Nominal o atiende al paciente prioritario.", true);
    return;
  }
  try {
    const res = await fetch(`/api/missions/${encodeURIComponent(currentMissionId)}/simulate`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ scenario: scenarioName })
    });
    const data = await res.json();
    if (!res.ok) {
      showToast(data.error || "Error aplicando escenario", true);
      return;
    }
    showToast(`Escenario clínico aplicado: ${SIM_LABELS_ES[scenarioName] || scenarioName}`);
    await refreshMissionState(true);
  } catch (err) {
    showToast("Error ejecutando simulación", true);
  }
}

/* =========================================================================
 * Crew Check-in Diario + Mini-Test de Reflejos (3 toques)
 * ========================================================================= */
function populateCheckinAstronautSelector(crewList, initialLoad = false) {
  const sel = document.getElementById("ci-astronaut");
  if (!sel) return;

  if (!crewList || crewList.length === 0) {
    sel.innerHTML = `<option value="">Sin tripulantes asignados</option>`;
    const hormBox = document.getElementById("ci-hormonal-box");
    if (hormBox) hormBox.classList.add("hidden");
    return;
  }

  const prevVal = sel.value;
  const currentSignature = Array.from(sel.options)
    .map((o) => `${o.value}:${o.disabled ? "1" : "0"}`)
    .join(",");
  const nextSignature = crewList
    .map((a) => `${a.id}:${isPriorityMode && !priorityAstronautIds.has(a.id) ? "1" : "0"}`)
    .join(",");

  if (initialLoad || currentSignature !== nextSignature) {
    sel.innerHTML = crewList
      .map((a) => {
        const disabledAttr = isPriorityMode && !priorityAstronautIds.has(a.id) ? "disabled" : "";
        return `<option value="${escapeAttr(a.id)}" ${disabledAttr}>${escapeHtml(a.nombre)} (${escapeHtml(a.id)})</option>`;
      })
      .join("");
    if (isPriorityMode && topPriorityAstronautId && (!prevVal || !priorityAstronautIds.has(prevVal))) {
      sel.value = topPriorityAstronautId;
    } else if (prevVal && crewList.some((c) => c.id === prevVal)) {
      sel.value = prevVal;
    }
    if (initialLoad || !prevVal || !crewList.some((c) => c.id === prevVal) || (isPriorityMode && !priorityAstronautIds.has(prevVal))) {
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
    const sel = document.getElementById("ci-astronaut");
    if (reqSeq !== prefillRequestSeq || (sel && sel.value && sel.value !== astronautId)) {
      return;
    }
    const hist = a.history || [];
    const latest = hist.length > 0 ? hist[hist.length - 1] : null;
    const isFem = (a.sexo || "M").toUpperCase() === "F";

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

    if (!latest) return;

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
  if (isPriorityMode && !priorityAstronautIds.has(astroId)) {
    showToast("Prioridad Médica Total: solo se permite check-in para el paciente Muy Crítico.", true);
    return;
  }

  const crewObj = (currentMissionData?.crew || []).find((c) => c.id === astroId);
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
      showToast(data.error || "Error validando check-in", true);
      return;
    }
    const astroName = crewObj?.nombre || astroId;
    showToast(`Check-in registrado para ${astroName} (Puntaje de Salud: ${data.report?.health_score}%).`);
    currentAstronautId = astroId;
    switchOptionView("mostrar");
    await refreshMissionState(true);
  } catch (err) {
    showToast("Error enviando check-in", true);
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
  if (isPriorityMode) {
    showToast("Prioridad Médica Total: la importación de archivos está bloqueada hasta estabilizar al paciente crítico.", true);
    return;
  }
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

    currentMissionId = data.mission_id || payload.mission_id || currentMissionId;
    currentAstronautId = data.id;
    await loadMissionsList();
    switchOptionView("mostrar");
    await refreshMissionState(true);
  } catch (err) {
    showToast("Error de red importando JSON", true);
  }
}

async function submitNewAstronaut(ev) {
  ev.preventDefault();
  if (isPriorityMode) {
    showToast("Prioridad Médica Total: no se pueden crear tripulantes hasta estabilizar al paciente crítico.", true);
    return;
  }
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
    mission_id: document.getElementById("na-mission").value || currentMissionId,
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
    showToast(`Astronauta ${data.nombre} creado y asignado a ${data.mission_id}.`);
    clearNewAstronautCustomAvatar();
    currentMissionId = data.mission_id;
    currentAstronautId = data.id;
    switchOptionView("mostrar");
    await loadMissionsList();
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

async function onDetailAvatarSelected(ev) {
  const file = ev.target.files && ev.target.files[0];
  if (!file || !currentAstronautId) return;
  try {
    const dataUrl = await readImageFileAsDataUrl(file);
    const res = await fetch("/api/avatars", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        id: currentAstronautId,
        data_url: dataUrl
      })
    });
    const data = await res.json();
    if (!res.ok) {
      showToast(data.error || "Error guardando foto personalizada", true);
      return;
    }
    avatarCacheBust = Date.now();
    const imgEl = document.getElementById("detail-avatar");
    if (imgEl && data.avatar) {
      imgEl.src = resolveAvatarUrl(data.avatar);
    }
    showToast(`Fotografía actualizada para ${currentAstronautId}.`);
    await refreshMissionState(true);
  } catch (err) {
    showToast(err.message || "Error subiendo fotografía", true);
  } finally {
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
    console.error("Error cargando glosario HRP:", err);
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

function getFilteredGlossaryItems() {
  if (!glossaryData || !Array.isArray(glossaryData.items)) return [];
  const query = normalizeText(document.getElementById("glossary-search-input")?.value || "");
  return glossaryData.items.filter((item) => {
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

function expandAllGlossary(expand) {
  const items = getFilteredGlossaryItems();
  if (expand) {
    items.forEach((it) => glossaryExpandedIds.add(it.id));
  } else {
    glossaryExpandedIds.clear();
  }
  renderGlossary();
}

function renderGlossary() {
  const grid = document.getElementById("glossary-cards-grid");
  const sourcesBox = document.getElementById("glossary-sources-list");
  const countBadge = document.getElementById("glossary-count-badge");
  if (!grid) return;

  if (!glossaryData || !Array.isArray(glossaryData.items)) {
    grid.innerHTML = `<div class="empty-grid-msg">Cargando glosario médico NASA HRP / CSA...</div>`;
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
        const sevLabel = SEVERITY_LABELS_ES[sev] || "Nominal";
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

async function submitNewMission(ev) {
  ev.preventDefault();
  if (isPriorityMode) {
    showToast("Prioridad Médica Total: no se pueden crear misiones hasta estabilizar al paciente crítico.", true);
    return;
  }
  const diaVal = parseInt(document.getElementById("nm-dia").value, 10);
  const gravVal = parseInt(document.getElementById("nm-gravedad").value, 10);
  const latVal = parseFloat(document.getElementById("nm-latencia").value);
  const payload = {
    id: document.getElementById("nm-id").value.trim(),
    nombre: document.getElementById("nm-nombre").value.trim(),
    destino: document.getElementById("nm-destino").value.trim(),
    dia_actual: Number.isFinite(diaVal) ? diaVal : 1,
    env: {
      presion_cabina_kpa: 101.3,
      po2_kpa: 21.3,
      pco2_kpa: 0.35,
      tasa_rad_msv_h: 0.02,
      evento_spe_activo: 0,
      gravedad_actual: Number.isFinite(gravVal) ? gravVal : 0,
      latencia_com_seg: Number.isFinite(latVal) ? latVal : 1.3
    }
  };

  try {
    const res = await fetch("/api/missions", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    const data = await res.json();
    if (!res.ok) {
      showToast(data.error || "Error creando misión", true);
      return;
    }
    showToast(`Misión ${data.nombre} creada.`);
    currentMissionId = data.id;
    currentAstronautId = null;
    await loadMissionsList();
    switchOptionView("mostrar");
    await refreshMissionState(true);
  } catch (err) {
    showToast("Error creando misión", true);
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

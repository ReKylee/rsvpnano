const CATALOG_URL = "https://raw.githubusercontent.com/ionutdecebal/rsvpnano/main/themes/index.json";

const state = {
  rootHandle: null,
  catalog: [],
};

const elements = {};

function $(selector) {
  return document.querySelector(selector);
}

function setStatus(title, body, tone = "info") {
  if (!elements.status) return;
  elements.status.dataset.tone = tone;
  elements.status.innerHTML = `<strong>${escapeHtml(title)}</strong>${escapeHtml(body)}`;
}

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, (char) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#39;",
  })[char]);
}

function supportsFileSystemAccess() {
  return typeof window.showDirectoryPicker === "function";
}

async function ensureThemesDirectory() {
  if (!state.rootHandle) {
    throw new Error("Choose the SD root first.");
  }
  return state.rootHandle.getDirectoryHandle("themes", { create: true });
}

async function writeThemeFile(filename, blob) {
  if (!filename.endsWith(".rtheme") || filename.includes("/") || filename.includes("\\")) {
    throw new Error("Theme files must be simple .rtheme files.");
  }
  const themesHandle = await ensureThemesDirectory();
  const fileHandle = await themesHandle.getFileHandle(filename, { create: true });
  const writable = await fileHandle.createWritable();
  try {
    await writable.write(blob);
  } finally {
    await writable.close();
  }
}

async function chooseRoot() {
  if (!supportsFileSystemAccess()) {
    setStatus("Browser not supported", "Theme setup needs Chrome or Edge with folder access.", "error");
    return;
  }
  state.rootHandle = await window.showDirectoryPicker({ mode: "readwrite" });
  await ensureThemesDirectory();
  setStatus(`Selected /${state.rootHandle.name}`, "The /themes folder is ready.", "success");
}

async function loadCatalog() {
  try {
    const response = await fetch(CATALOG_URL, { cache: "no-store" });
    if (!response.ok) throw new Error(`Catalog returned HTTP ${response.status}`);
    state.catalog = await response.json();
    elements.onlineSelect.innerHTML = state.catalog
      .map((theme) => `<option value="${escapeHtml(theme.id)}">${escapeHtml(theme.name)}</option>`)
      .join("");
  } catch (error) {
    elements.onlineSelect.innerHTML = '<option value="">Catalog unavailable</option>';
    setStatus("Online themes unavailable", error.message || "Theme catalog could not be loaded.", "error");
  }
}

async function installCatalogTheme(theme) {
  if (!theme) {
    setStatus("Choose a theme", "Load the catalog and select a theme first.", "error");
    return;
  }
  setStatus("Downloading theme", theme.name, "busy");
  const response = await fetch(new URL(theme.file, CATALOG_URL), { cache: "no-store" });
  if (!response.ok) throw new Error(`Theme returned HTTP ${response.status}`);
  await writeThemeFile(theme.file, await response.blob());
  setStatus("Theme installed", `${theme.name} was written to /themes/${theme.file}.`, "success");
}

async function installNight() {
  const night = state.catalog.find((theme) => theme.id === "night");
  await installCatalogTheme(night);
}

async function installSelectedOnlineTheme() {
  const selected = elements.onlineSelect.value;
  await installCatalogTheme(state.catalog.find((theme) => theme.id === selected));
}

async function uploadLocalTheme() {
  const file = elements.uploadInput.files?.[0];
  if (!file) {
    setStatus("Choose a theme file", "Pick a local .rtheme file first.", "error");
    return;
  }
  await writeThemeFile(file.name, file);
  elements.uploadInput.value = "";
  setStatus("Theme uploaded", `${file.name} was written to /themes/${file.name}.`, "success");
}

function bindAsync(button, action) {
  button?.addEventListener("click", async () => {
    try {
      await action();
    } catch (error) {
      setStatus("Theme setup failed", error.message || "Unexpected error.", "error");
    }
  });
}

function initThemeSetup() {
  elements.rootButton = $("#theme-root-button");
  elements.nightButton = $("#theme-night-button");
  elements.onlineButton = $("#theme-online-button");
  elements.uploadButton = $("#theme-upload-button");
  elements.uploadInput = $("#theme-upload-input");
  elements.onlineSelect = $("#theme-online-select");
  elements.status = $("#theme-status");

  if (!elements.rootButton) return;
  bindAsync(elements.rootButton, chooseRoot);
  bindAsync(elements.nightButton, installNight);
  bindAsync(elements.onlineButton, installSelectedOnlineTheme);
  elements.uploadButton?.addEventListener("click", () => elements.uploadInput?.click());
  elements.uploadInput?.addEventListener("change", () => {
    uploadLocalTheme().catch((error) => {
      setStatus("Theme upload failed", error.message || "Unexpected error.", "error");
    });
  });
  if (!supportsFileSystemAccess()) {
    setStatus("Folder access unavailable", "Use Chrome or Edge to write themes directly to SD.", "error");
  }
  loadCatalog();
}

initThemeSetup();

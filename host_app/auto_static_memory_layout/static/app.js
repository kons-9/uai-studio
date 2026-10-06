"use strict";

const byId = (id) => document.getElementById(id);
const colors = ["#007b70", "#c86d57", "#448bac", "#bd951f", "#7670ab", "#65917d"];
const inputNames = ["board", "application", "model_config"];
let inputs;
let original;
let token;
let result = null;
let tab = "runtime";
let jsonName = "application";
let jsonDrafts = {};
let version = 0;
let timer;
let busy = false;
let storageKey;

function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function bytes(value) {
  if (value >= 1024 ** 2) return `${(value / 1024 ** 2).toFixed(2)} MiB`;
  if (value >= 1024) return `${(value / 1024).toFixed(1)} KiB`;
  return `${value} B`;
}

function size(value) {
  const match = String(value).replaceAll("_", "").trim().match(/^(0x[0-9a-f]+|[0-9]+)\s*([kmg])?$/i);
  return match ? Number(match[1]) * ({ k: 1024, m: 1024 ** 2, g: 1024 ** 3 }[match[2]?.toLowerCase()] || 1) : 0;
}

function hex(value) { return `0x${value.toString(16).toUpperCase().padStart(8, "0")}`; }

function status(text, kind = "") {
  byId("status").textContent = text;
  byId("status").className = kind;
}

function dirty() {
  return JSON.stringify(inputs) !== original || Object.keys(jsonDrafts).length > 0;
}

function persist() {
  try { localStorage.setItem(storageKey, JSON.stringify({ inputs, jsonDrafts })); } catch (_) {}
}

function download(blob, filename) {
  const url = URL.createObjectURL(blob);
  const link = element("a");
  link.href = url;
  link.download = filename;
  document.body.append(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function edited() {
  version += 1;
  result = null;
  byId("export").disabled = true;
  byId("dirty").textContent = dirty() ? "Unsaved draft" : "Original inputs";
  persist();
  renderMap();
  status("Pending validation", "pending");
  clearTimeout(timer);
  timer = setTimeout(validate, 500);
}

async function request(path, payload) {
  const response = await fetch(path, {
    method: "POST", headers: { "Content-Type": "application/json", "X-Editor-Token": token },
    body: JSON.stringify(payload),
  });
  if (!response.ok) {
    const failure = await response.json();
    throw new Error(failure.error || `HTTP ${response.status}`);
  }
  return response;
}

function checkDrafts() {
  for (const name of Object.keys(jsonDrafts)) {
    const parsed = JSON.parse(jsonDrafts[name]);
    if (!parsed || Array.isArray(parsed) || typeof parsed !== "object") throw new Error(`${name}: expected a JSON object`);
    inputs[name] = parsed;
    delete jsonDrafts[name];
  }
}

async function validate() {
  if (!inputs) return;
  clearTimeout(timer);
  const current = version;
  byId("export").disabled = true;
  status("Validating...", "pending");
  try {
    checkDrafts();
    const response = await request("/api/resolve", inputs);
    const resolved = await response.json();
    if (current !== version) return;
    result = resolved;
    status("Layout valid");
    byId("export").disabled = busy;
  } catch (error) {
    if (current !== version) return;
    result = null;
    status(error.message, "error");
  }
  persist();
  renderMap();
}

function field(label, object, key, options = {}) {
  const wrapper = element("label", "field", label);
  let control;
  if (options.choices) {
    control = element("select");
    for (const choice of options.choices) {
      const option = element("option", "", choice);
      option.value = choice;
      control.append(option);
    }
  } else {
    control = element("input");
    control.type = typeof object[key] === "boolean" ? "checkbox" : typeof object[key] === "number" ? "number" : "text";
    if (control.type === "number") { control.min = "1"; control.step = "1"; }
  }
  if (control.type === "checkbox") control.checked = object[key];
  else control.value = object[key] ?? "";
  control.addEventListener("input", () => {
    object[key] = control.type === "checkbox" ? control.checked : control.type === "number" ? control.valueAsNumber : control.value;
    edited();
  });
  wrapper.append(control);
  return wrapper;
}

function renderRuntime(container) {
  const application = inputs.application;
  const alignment = field("Default alignment (bytes)", application, "alignment");
  alignment.classList.add("wide-field");
  container.append(alignment);
  Object.entries(application.runtime || {}).forEach(([name, values], index) => {
    const group = element("section", "runtime-group");
    group.style.setProperty("--group-color", colors[index % colors.length]);
    group.append(element("h3", "group-title", name.replaceAll("_", " ")));
    const fields = element("div", "field-grid");
    const labels = { width: "Width (px)", height: "Height (px)", bytes_per_pixel: "Bytes / pixel", count: "Buffer count" };
    for (const key of Object.keys(values)) fields.append(field(labels[key] || key, values, key));
    group.append(fields);
    container.append(group);
  });
}

function renderRecords(container, records, type) {
  if (!Array.isArray(records)) return;
  for (const record of records) {
    const details = element("details", "reservation");
    const title = element("summary", "", record.name || "Unnamed");
    details.append(title);
    const fields = element("div", "field-grid");
    for (const key of Object.keys(record)) {
      const choices = type === "reservations" && key === "memory" ? (inputs.board.memory_regions || []).map((region) => region.name) : undefined;
      fields.append(field(key.replaceAll("_", " "), record, key, { choices }));
    }
    details.append(fields);
    container.append(details);
  }
}

function renderJson(container) {
  const toolbar = element("div", "json-toolbar");
  const select = element("select");
  select.setAttribute("aria-label", "JSON input document");
  for (const name of inputNames) {
    const option = element("option", "", name);
    option.value = name;
    select.append(option);
  }
  select.value = jsonName;
  select.addEventListener("change", () => { jsonName = select.value; renderEditor(); });
  const importButton = element("button", "", "Import JSON");
  const file = element("input");
  file.type = "file";
  file.accept = ".json,application/json";
  file.hidden = true;
  importButton.addEventListener("click", () => file.click());
  file.addEventListener("change", async () => {
    if (!file.files[0]) return;
    if (file.files[0].size > 1_000_000) { status("Input file exceeds 1 MB", "error"); return; }
    const name = jsonName;
    jsonDrafts[name] = await file.files[0].text();
    edited();
    renderEditor();
  });
  const exportButton = element("button", "", "Export JSON");
  const icon = element("img");
  icon.src = "/icons/download.svg";
  icon.alt = "";
  exportButton.prepend(icon);
  exportButton.addEventListener("click", () => {
    try {
      checkDrafts();
      const filenames = { board: "board_memory.json", application: "application_memory.json", model_config: "model_layout.json" };
      download(new Blob([JSON.stringify(inputs[jsonName], null, 2) + "\n"], { type: "application/json" }), filenames[jsonName]);
    } catch (error) { status(error.message, "error"); }
  });
  toolbar.append(select, importButton, exportButton, file);
  const textarea = element("textarea");
  textarea.spellcheck = false;
  textarea.setAttribute("aria-label", `${jsonName} JSON`);
  textarea.value = jsonDrafts[jsonName] ?? JSON.stringify(inputs[jsonName], null, 2);
  textarea.addEventListener("input", () => { jsonDrafts[jsonName] = textarea.value; edited(); });
  container.append(toolbar, textarea);
}

function renderEditor() {
  const container = byId("editor-content");
  container.replaceChildren();
  if (!inputs) return;
  byId("alignment-label").textContent = `${inputs.application.alignment ?? "--"} B aligned`;
  try {
    if (tab === "runtime") renderRuntime(container);
    if (tab === "reservations") renderRecords(container, inputs.application.reservations, tab);
    if (tab === "regions") renderRecords(container, inputs.board.memory_regions, tab);
    if (tab === "json") renderJson(container);
  } catch (_) {
    container.append(element("p", "footnote", "Invalid configuration structure. Open JSON."));
  }
}

function renderMap() {
  if (!inputs) return;
  const rawRegions = Array.isArray(inputs.board.memory_regions) ? inputs.board.memory_regions.filter((region) => region && typeof region === "object") : [];
  const regions = result?.regions || rawRegions.map((region) => ({
    name: String(region.name), origin_value: size(region.origin), length_value: size(region.length), allocations: [],
  }));
  byId("board-name").textContent = inputs.board.board || "Board";
  byId("region-count").textContent = regions.length;
  byId("capacity").textContent = bytes(regions.reduce((sum, region) => sum + region.length_value, 0));
  byId("reserved").textContent = result ? bytes(regions.reduce((sum, region) => sum + region.used, 0)) : "--";
  byId("allocation-count").textContent = result ? result.document.allocations.length : "--";
  byId("alignment-label").textContent = `${inputs.application.alignment ?? "--"} B aligned`;
  const modelOrder = inputs.model_config.model_order;
  byId("model-summary").textContent = Array.isArray(modelOrder) ? modelOrder.join(" / ") : "Models";
  const filter = byId("filter");
  const selected = filter.value;
  filter.replaceChildren(new Option("All regions", ""));
  for (const region of regions) filter.add(new Option(region.name, region.name));
  filter.value = regions.some((region) => region.name === selected) ? selected : "";
  byId("selection").textContent = filter.value || "All regions";
  const map = byId("memory-map");
  const rows = byId("allocations");
  map.replaceChildren();
  rows.replaceChildren();
  let allocationCount = 0;
  for (const region of regions) {
    if (filter.value && filter.value !== region.name) continue;
    const row = element("div", "memory-region");
    const heading = element("div", "region-heading");
    heading.append(element("strong", "region-name", region.name), element("span", "", result ? `${bytes(region.used)} / ${bytes(region.length_value)}` : `${bytes(region.length_value)} / Not calculated`));
    const track = element("div", `memory-track${result ? "" : " unknown"}`);
    track.setAttribute("aria-label", `${region.name} reservations`);
    region.allocations.forEach((allocation, index) => {
      allocationCount += 1;
      const block = element("button", "block");
      block.style.left = `${allocation.offset / region.length_value * 100}%`;
      block.style.width = `${allocation.size_value / region.length_value * 100}%`;
      block.style.setProperty("--block-color", colors[index % colors.length]);
      block.title = `${allocation.name}: ${bytes(allocation.size_value)}, +${hex(allocation.offset)}`;
      block.setAttribute("aria-label", block.title);
      const tableRow = element("tr");
      for (const value of [allocation.name, region.name, `+${hex(allocation.offset)}`, bytes(allocation.size_value), `${allocation.alignment_value} B`]) tableRow.append(element("td", "", value));
      block.addEventListener("click", () => {
        for (const previous of rows.querySelectorAll(".selected")) previous.classList.remove("selected");
        tableRow.classList.add("selected");
        tableRow.scrollIntoView({ behavior: "auto", block: "nearest" });
      });
      track.append(block);
      rows.append(tableRow);
    });
    const addresses = element("div", "region-address");
    addresses.append(element("span", "", hex(region.origin_value)), element("span", "", hex(region.origin_value + region.length_value)));
    row.append(heading, track, addresses);
    map.append(row);
  }
  if (!allocationCount) {
    const row = element("tr");
    const cell = element("td", "empty", result ? "No static reservations" : "Not calculated");
    cell.colSpan = 5;
    row.append(cell);
    rows.append(row);
  }
  byId("map-note").textContent = "Static reservations only. Code, model weights and command blobs are excluded. Offsets are relative to the reservation area; final addresses are linker-defined.";
}

async function load(reset = false) {
  if (reset && dirty() && !confirm("Discard the current draft and reload input files?")) return;
  version += 1;
  clearTimeout(timer);
  result = null;
  byId("export").disabled = true;
  status("Loading inputs...", "pending");
  try {
    const response = await fetch("/api/inputs");
    const data = await response.json();
    if (!response.ok) throw new Error(data.error);
    inputs = data.inputs;
    original = JSON.stringify(inputs);
    token = data.token;
    jsonDrafts = {};
    storageKey = `uai-memory:${JSON.stringify(data.paths)}`;
    try {
      const saved = reset ? null : JSON.parse(localStorage.getItem(storageKey));
      if (saved && inputNames.every((name) => saved.inputs?.[name] && typeof saved.inputs[name] === "object" && !Array.isArray(saved.inputs[name]))) {
        inputs = saved.inputs;
        jsonDrafts = saved.jsonDrafts || {};
      }
      if (reset) localStorage.removeItem(storageKey);
    } catch (_) {}
    const paths = byId("paths");
    paths.replaceChildren();
    for (const [name, value] of Object.entries({ ...data.paths, models_dir: data.models_dir })) paths.append(element("dt", "", name), element("dd", "", value));
    byId("dirty").textContent = dirty() ? "Restored draft" : "Original inputs";
    renderEditor();
    renderMap();
    await validate();
  } catch (error) { status(error.message, "error"); }
}

document.querySelectorAll("[data-tab]").forEach((button) => {
  button.addEventListener("click", () => {
    try { checkDrafts(); } catch (error) { status(error.message, "error"); return; }
    tab = button.dataset.tab;
    document.querySelectorAll("[data-tab]").forEach((item) => item.setAttribute("aria-selected", String(item === button)));
    renderEditor();
  });
});
byId("filter").addEventListener("change", renderMap);
byId("validate").addEventListener("click", validate);
byId("reload").addEventListener("click", () => load(true));
byId("export").addEventListener("click", async () => {
  const current = version;
  busy = true;
  byId("export").disabled = true;
  try {
    checkDrafts();
    const response = await request("/api/export", inputs);
    const blob = await response.blob();
    if (version !== current) return;
    download(blob, "memory-layout.zip");
    status("ZIP exported. Source files unchanged.");
  } catch (error) {
    if (version === current) status(error.message, "error");
  } finally {
    busy = false;
    byId("export").disabled = !result;
  }
});
window.addEventListener("beforeunload", (event) => {
  if (inputs && dirty()) { event.preventDefault(); event.returnValue = ""; }
});
load();
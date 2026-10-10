"use strict";

const byId = (id) => document.getElementById(id);
const states = {};
let selected = "ui-designer";
let token;
let tools = [];

function icons() { lucide.createIcons(); }
function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}
function icon(name) {
  const node = element("i");
  node.dataset.lucide = name;
  return node;
}
function button(text, name, className = "") {
  const node = element("button", className, text);
  node.type = "button";
  node.prepend(icon(name));
  return node;
}
function field(form, label, key, value, options = {}) {
  const wrapper = element("label", "", label);
  let input;
  if (options.choices) {
    input = element("select");
    for (const [id, text] of options.choices) {
      const choice = element("option", "", text);
      choice.value = id;
      input.append(choice);
    }
  } else {
    input = element("input");
    input.type = options.type || "number";
    if (options.min !== undefined) input.min = options.min;
    if (options.step !== undefined) input.step = options.step;
  }
  input.name = key;
  input.value = value;
  input.required = options.required ?? input.type === "number";
  wrapper.append(input);
  form.append(wrapper);
  return input;
}
function shellQuote(value) {
  return /^[a-zA-Z0-9_./:{}=-]+$/.test(value) ? value : "'" + value.replaceAll("'", "'\\''") + "'";
}
function selectTool(id) {
  states[selected].arguments = byId("cli-arguments").value;
  selected = id;
  for (const tab of document.querySelectorAll("[data-tool]")) {
    const active = tab.dataset.tool === id;
    tab.setAttribute("aria-selected", String(active));
    tab.tabIndex = active ? 0 : -1;
    byId(tab.dataset.tool).hidden = !active;
    if (active) tab.scrollIntoView({ block: "nearest", inline: "nearest" });
  }
  const frame = byId(id).querySelector("iframe");
  if (frame && !frame.getAttribute("src")) frame.src = frame.dataset.src;
  byId("cli-tool-name").textContent = tools.find((tool) => tool.id === id)?.name || id;
  byId("cli-arguments").value = states[id].arguments;
  restoreConsole(id);
  history.replaceState(null, "", "#" + id);
}
function restoreConsole(id) {
  const state = states[id];
  byId("cli-run").disabled = !token || state.busy;
  byId("command").textContent = state.result?.command || `python3 -m host_app ${id} ${state.arguments}`;
  byId("cli-output").textContent = state.result ? [state.result.stdout, state.result.stderr].filter(Boolean).join("\n") : "";
  byId("exit-status").textContent = state.busy ? "Running..." : state.result ? `Exit ${state.result.exit_code} / ${state.result.elapsed_ms} ms` : "Ready";
  byId("exit-status").classList.toggle("error", !!state.result?.exit_code);
}
function showResult(id, result) {
  const container = states[id].resultView;
  if (!container) return;
  container.replaceChildren();
  const toolbar = element("div", "result-toolbar");
  toolbar.append(element("strong", "", `Exit ${result.exit_code} / ${result.elapsed_ms} ms`));
  for (const file of result.artifacts) {
    const link = element("a", "", file.name);
    link.href = file.url;
    link.download = file.name;
    link.prepend(icon("download"));
    toolbar.append(link);
  }
  container.append(toolbar);
  const images = result.artifacts.filter((file) => ["image/png", "image/svg+xml"].includes(file.type));
  for (const image of images) {
    const node = element("img", "result-image");
    node.src = image.url;
    node.alt = tools.find((tool) => tool.id === id).name + " report";
    container.append(node);
  }
  const output = element("pre", result.exit_code ? "error" : "", [result.stdout, result.stderr].filter(Boolean).join("\n") || "Completed");
  container.append(output);
  states[id].status.textContent = result.exit_code ? `Exit ${result.exit_code}` : "Completed";
  states[id].status.classList.toggle("error", !!result.exit_code);
  icons();
}
async function execute(id, argumentsValue, upload = null, monitor = false) {
  const state = states[id];
  if (state.busy) return;
  state.busy = true;
  if (state.runButton) state.runButton.disabled = true;
  if (monitor && state.status) state.status.textContent = "Running...";
  if (id === selected) restoreConsole(id);
  try {
    const response = await fetch("api/run", {
      method: "POST", headers: { "Content-Type": "application/json", "X-Editor-Token": token },
      body: JSON.stringify({ tool: id, arguments: argumentsValue, upload }),
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || `HTTP ${response.status}`);
    state.result = result;
    if (monitor) showResult(id, result);
    if (result.exit_code) byId("cli-panel").open = true;
  } catch (error) {
    state.result = { command: `python3 -m host_app ${id}`, exit_code: 1, elapsed_ms: 0, artifacts: [], stdout: "", stderr: error.message };
    if (monitor) showResult(id, state.result);
    byId("cli-panel").open = true;
  } finally {
    state.busy = false;
    if (state.runButton) state.runButton.disabled = !token;
    if (id === selected) restoreConsole(id);
  }
}
function readFile(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve({ name: file.name, content: reader.result.split(",")[1] });
    reader.onerror = () => reject(new Error("Input file could not be read"));
    reader.readAsDataURL(file);
  });
}
function buildMonitor(id) {
  const ai = id === "ai-model-monitor";
  const state = states[id];
  const view = byId(id);
  const heading = element("div", "monitor-heading");
  state.status = element("span", "report-status", "Ready");
  state.status.setAttribute("role", "status");
  heading.append(element("h2", "", ai ? "AI Model Monitor" : "CPU Task Monitor"), state.status);
  const grid = element("div", "monitor-grid");
  const form = element("form", "settings");
  const source = element("div", "input-group");
  const path = field(source, "Input path", "input", ai ? "host_app/ai_model_monitor/sample/ai_model_monitor.bin" : "host_app/cpu_task_monitor/sample/cpu_task_monitor.bin", { type: "text", required: true });
  const file = element("input");
  file.type = "file";
  file.accept = ai ? ".bin,.json" : ".bin,.log,.txt";
  file.hidden = true;
  const actions = element("div", "file-actions");
  const browse = button("Open file", "folder-open");
  const sample = button("Sample", "flask-conical");
  actions.append(browse, sample);
  const filename = element("div", "file-name");
  browse.addEventListener("click", () => file.click());
  file.addEventListener("change", () => {
    if (!file.files[0]) return;
    path.value = "{input}";
    filename.textContent = file.files[0].name;
    update();
  });
  path.addEventListener("input", () => {
    if (path.value !== "{input}") { file.value = ""; filename.textContent = ""; }
  });
  sample.addEventListener("click", () => {
    path.value = ai ? "host_app/ai_model_monitor/sample/ai_model_monitor.bin" : "host_app/cpu_task_monitor/sample/cpu_task_monitor.bin";
    file.value = "";
    filename.textContent = "";
    update();
  });
  source.append(actions, filename, file);
  form.append(source);
  const mode = ai ? field(form, "Operation", "mode", "all", { choices: [["all", "All"], ["decode", "Decode"], ["analyze", "Analyze"], ["visualize", "Visualize"]] }) : null;
  const cpuHz = field(form, "CPU frequency (Hz)", "cpu_hz", 600000000, { min: 1 });
  const top = field(form, ai ? "Top epoch groups" : "Top tasks", "top", 12, { min: ai ? 1 : 0 });
  const count = field(form, ai ? "Recent inferences" : "Gantt repeats", "count", ai ? 12 : 3, { min: ai ? -1 : 1 });
  const dpi = ai ? field(form, "DPI", "dpi", 140, { min: 1 }) : null;
  const format = ai ? null : field(form, "Image format", "format", "png", { choices: [["png", "PNG"], ["svg", "SVG"]] });
  const title = ai ? null : field(form, "Report title", "title", "", { type: "text", required: false });
  const pretty = ai ? element("input") : null;
  if (pretty) {
    pretty.type = "checkbox";
    const label = element("label", "checkbox");
    label.append(pretty, element("span", "", "Pretty JSON"));
    form.append(label);
  }
  state.runButton = button("Run", "play", "primary run-monitor");
  state.runButton.type = "submit";
  state.runButton.disabled = true;
  form.append(state.runButton);
  state.resultView = element("div", "result");
  const empty = element("div", "empty-result");
  empty.append(icon(ai ? "chart-no-axes-combined" : "chart-gantt"), element("span", "", "No report"));
  state.resultView.append(empty);
  grid.append(form, state.resultView);
  view.append(heading, grid);
  function argumentsList() {
    if (ai) {
      const args = [mode.value, path.value];
      if (mode.value === "decode") args.push("-o", "{output}/trace.json");
      else args.push("--cpu-hz", cpuHz.value);
      if (["analyze", "all"].includes(mode.value)) args.push("--top", top.value);
      if (["visualize", "all"].includes(mode.value)) {
        args.push("--dpi", dpi.value, "--max-inferences", count.value);
        if (mode.value === "all") args.push("--json", "{output}/trace.json", "--png", "{output}/timeline.png");
        else args.push("-o", "{output}/timeline.png");
      }
      if (pretty.checked && ["decode", "all"].includes(mode.value)) args.push("--pretty");
      return args;
    }
    const args = [path.value, "-o", `{output}/cpu.${format.value}`, "--json", "{output}/cpu.json", "--csv", "{output}/cpu.csv", "--cpu-hz", cpuHz.value, "--top-tasks", top.value, "--gantt-repeats", count.value];
    if (title.value) args.push("--title", title.value);
    return args;
  }
  function update() {
    if (ai) {
      const operation = mode.value;
      cpuHz.disabled = operation === "decode";
      top.disabled = !["all", "analyze"].includes(operation);
      count.disabled = dpi.disabled = !["all", "visualize"].includes(operation);
      pretty.disabled = !["all", "decode"].includes(operation);
    }
    state.arguments = argumentsList().map(shellQuote).join(" ");
    if (selected === id) byId("cli-arguments").value = state.arguments;
  }
  form.addEventListener("input", update);
  form.addEventListener("change", update);
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    try {
      if (file.files[0]?.size > 10 * 1024 * 1024) throw new Error("Input exceeds 10 MiB");
      const upload = file.files[0] ? await readFile(file.files[0]) : null;
      await execute(id, argumentsList(), upload, true);
    } catch (error) { state.status.textContent = error.message; state.status.classList.add("error"); }
  });
  state.upload = async () => file.files[0] ? await readFile(file.files[0]) : null;
  update();
}

for (const tab of document.querySelectorAll("[data-tool]")) {
  states[tab.dataset.tool] = { arguments: "--help", busy: false, result: null };
  tab.addEventListener("click", () => selectTool(tab.dataset.tool));
  tab.addEventListener("keydown", (event) => {
    const tabs = [...document.querySelectorAll("[data-tool]")];
    const index = tabs.indexOf(tab);
    const next = event.key === "ArrowRight" ? tabs[(index + 1) % tabs.length] : event.key === "ArrowLeft" ? tabs[(index - 1 + tabs.length) % tabs.length] : event.key === "Home" ? tabs[0] : event.key === "End" ? tabs.at(-1) : null;
    if (next) { event.preventDefault(); next.focus(); selectTool(next.dataset.tool); }
  });
}
buildMonitor("ai-model-monitor");
buildMonitor("cpu-task-monitor");
byId("cli-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const id = selected;
  states[id].arguments = byId("cli-arguments").value;
  try { await execute(id, states[id].arguments, states[id].upload ? await states[id].upload() : null); }
  catch (error) { byId("cli-output").textContent = error.message; }
});
byId("copy-command").addEventListener("click", async () => {
  try { await navigator.clipboard.writeText(byId("command").textContent); }
  catch (_) { byId("cli-output").textContent = "Clipboard unavailable"; }
});
icons();
async function initialize() {
  try {
    const response = await fetch("api/session");
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const session = await response.json();
    token = session.token;
    tools = session.tools;
    const available = new Set(tools.map((tool) => tool.id));
    for (const tab of document.querySelectorAll("[data-tool]")) {
      if (available.has(tab.dataset.tool)) continue;
      byId(tab.dataset.tool).remove();
      tab.remove();
    }
    byId("root-path").textContent = session.root;
    byId("session-status").textContent = "Ready";
    for (const state of Object.values(states)) if (state.runButton) state.runButton.disabled = false;
    const initial = location.hash.slice(1);
    selectTool(available.has(initial) ? initial : "ui-designer");
  } catch (error) {
    byId("session-status").textContent = error.message;
    byId("session-status").classList.add("error");
  }
}
initialize();
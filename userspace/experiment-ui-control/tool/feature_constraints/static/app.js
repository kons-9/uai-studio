const byId = (id) => document.getElementById(id);
let declaration = null;
let filename = 'features.json';
let baseline = '';
let token = '';
let model = null;
let current = null;
let header = '';
let busy = false;
let activeMatrix = 'exclusions';

function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}
function icons() { if (window.lucide) window.lucide.createIcons(); }
function iconButton(icon, label, action, small = false) {
  const button = element('button', `icon-button${small ? ' small-button' : ''}`);
  button.title = label;
  button.setAttribute('aria-label', label);
  const glyph = element('i'); glyph.dataset.lucide = icon; button.append(glyph);
  button.addEventListener('click', () => { if (!busy) action(); });
  return button;
}
function badge(id, text, type = 'neutral') { byId(id).textContent = text; byId(id).className = `badge ${type}`; }
function notice(text = '', warning = false) {
  byId('notice').hidden = !text;
  byId('notice').textContent = text;
  byId('notice').className = warning ? 'notice warning' : 'notice';
}
function field(label, input) { const wrapper = element('label', 'field'); wrapper.append(element('span', '', label), input); return wrapper; }
function inputText(value, label, update) {
  const input = element('input'); input.type = 'text'; input.value = value; input.setAttribute('aria-label', label);
  input.addEventListener('change', () => mutate((draft) => update(draft, input.value.trim())));
  return input;
}
function select(states, value, label, update) {
  const input = element('select'); input.setAttribute('aria-label', label);
  for (const state of states) { const option = element('option', '', state); option.value = state; input.append(option); }
  input.value = value; input.addEventListener('change', () => update(input.value)); return input;
}
function isBinary(feature) { return feature.states.length === 2 && feature.states.includes('off') && feature.states.includes('on'); }
function references() { return declaration.features.flatMap(feature => feature.states.map(state => ({feature: feature.id, state}))); }
function same(left, right) { return left.feature === right.feature && left.state === right.state; }
function related(pair, left, right) { return same(pair[0], left) && same(pair[1], right); }
function pairs(document) { return [...(document.exclusions || []), ...(document.requirements || [])]; }
function rewriteReferences(document, change) { for (const pair of pairs(document)) for (const reference of pair) change(reference); }
function nextName(prefix, existing) { let index = 1; while (existing.includes(`${prefix}${index}`)) index += 1; return `${prefix}${index}`; }
function source() { return JSON.stringify(declaration); }
function validShape(document) {
  if (!document || document.schema_version !== 2) throw new Error('This editor opens matrix declarations (version 2). Older action declarations remain available through the CLI.');
  if (!Array.isArray(document.features) || !document.features.length || document.features.some(feature => typeof feature.id !== 'string' || !Array.isArray(feature.states) || !feature.states.length || feature.states.some(state => typeof state !== 'string'))) throw new Error('Features need names, states, and initial values');
  if (document.features.reduce((count, feature) => count + feature.states.length, 0) > 64) throw new Error('The matrix editor supports up to 64 state labels');
  for (const kind of ['exclusions', 'requirements']) if (document[kind] !== undefined && (!Array.isArray(document[kind]) || document[kind].some(pair => !Array.isArray(pair) || pair.length !== 2 || pair.some(reference => !reference || typeof reference.feature !== 'string' || typeof reference.state !== 'string')))) throw new Error('Relationships need pairs of feature states');
}
function invalidate() {
  model = null; current = null; header = '';
  byId('state-count').textContent = '-'; byId('states').replaceChildren(); byId('state-window').textContent = '';
  byId('cpp').firstElementChild.textContent = ''; byId('download-cpp').disabled = true;
  byId('trial').replaceChildren(); byId('trial-result').hidden = true; byId('reset').disabled = true;
  badge('trial-badge', 'Not checked'); badge('validation-badge', 'Modified');
  byId('dirty').hidden = source() === baseline;
  byId('feature-count').textContent = declaration.features.length;
  byId('exclusion-count').textContent = (declaration.exclusions || []).length;
  byId('requirement-count').textContent = (declaration.requirements || []).length;
  byId('editor-status').textContent = 'Not validated'; byId('elapsed').textContent = ''; notice();
}
function mutate(update) {
  if (busy || !declaration) return;
  try { const draft = structuredClone(declaration); update(draft); validShape(draft); declaration = draft; invalidate(); renderFeatures(); renderMatrix(); }
  catch (error) { renderFeatures(); renderMatrix(); notice(error.message); }
}
function renderFeatures() {
  const container = byId('features'); const focused = document.activeElement;
  const focusLabel = container.contains(focused) ? focused.getAttribute('aria-label') : null;
  const position = byId('feature-fields').scrollTop;
  const cursor = focused.selectionStart;
  container.replaceChildren();
  declaration.features.forEach((feature, index) => {
    const item = element('div', 'feature-item'); const heading = element('div', 'feature-heading');
    heading.append(field('Feature', inputText(feature.id, `Feature ${index + 1} name`, (draft, value) => {
      if (!value || draft.features.some((other, position) => position !== index && other.id === value)) throw new Error('Feature names must be nonempty and unique');
      draft.features[index].id = value; rewriteReferences(draft, reference => { if (reference.feature === feature.id) reference.feature = value; });
    })));
    heading.append(iconButton('trash-2', `Remove feature ${index + 1}`, () => mutate(draft => {
      if (draft.features.length === 1) throw new Error('At least one feature is required');
      if (pairs(draft).some(pair => pair.some(reference => reference.feature === feature.id))) throw new Error(`${feature.id} is used in relationships. Remove those relationships first.`);
      draft.features.splice(index, 1);
    })));
    item.append(heading);
    const stateHeading = element('div', 'field-heading');
    stateHeading.append(element('span', '', 'States'), iconButton('plus', `Add state to feature ${index + 1}`, () => mutate(draft => { draft.features[index].states.push(nextName('state', feature.states)); }), true));
    item.append(stateHeading);
    const states = element('div', 'state-inputs');
    feature.states.forEach((value, stateIndex) => {
      const row = element('div', 'state-input');
      row.append(inputText(value, `Feature ${index + 1} state ${stateIndex + 1}`, (draft, next) => {
        if (!next || feature.states.some((other, position) => position !== stateIndex && other === next)) throw new Error('State names must be nonempty and unique');
        draft.features[index].states[stateIndex] = next; if (feature.initial === value) draft.features[index].initial = next;
        rewriteReferences(draft, reference => { if (reference.feature === feature.id && reference.state === value) reference.state = next; });
      }));
      row.append(iconButton('x', `Remove state ${stateIndex + 1} from feature ${index + 1}`, () => mutate(draft => {
        if (feature.states.length === 1) throw new Error('At least one state is required');
        if (pairs(draft).some(pair => pair.some(reference => same(reference, {feature: feature.id, state: value})))) throw new Error(`${feature.id}=${value} is used in relationships`);
        draft.features[index].states.splice(stateIndex, 1); if (feature.initial === value) draft.features[index].initial = draft.features[index].states[0];
      }), true)); states.append(row);
    });
    item.append(states);
    if (isBinary(feature)) {
      const initial = element('label', 'binary-field'); const checkbox = element('input'); checkbox.type = 'checkbox'; checkbox.checked = feature.initial === 'on';
      checkbox.setAttribute('aria-label', `Feature ${index + 1} initial on`);
      checkbox.addEventListener('change', () => mutate(draft => { draft.features[index].initial = checkbox.checked ? 'on' : 'off'; }));
      initial.append(checkbox, element('span', '', 'Initially on')); item.append(initial);
    } else item.append(field('Initial state', select(feature.states, feature.initial, `Feature ${index + 1} initial state`, value => mutate(draft => { draft.features[index].initial = value; }))));
    container.append(item);
  });
  icons();
  const replacement = focusLabel && [...container.querySelectorAll('[aria-label]')].find(input => input.getAttribute('aria-label') === focusLabel);
  if (replacement) { replacement.focus({preventScroll: true}); if (replacement.type === 'text' && Number.isInteger(cursor)) replacement.setSelectionRange(cursor, cursor); }
  byId('feature-fields').scrollTop = position;
}
function referenceHeading(reference, scope) {
  const heading = element('th'); heading.scope = scope;
  heading.append(element('span', 'ref-feature', reference.feature), element('span', 'ref-state', reference.state)); return heading;
}
function renderMatrix() {
  const container = byId('matrix'); const viewport = byId('matrix-panel');
  const focusLabel = container.contains(document.activeElement) ? document.activeElement.getAttribute('aria-label') : null;
  const scrollTop = viewport.scrollTop; const scrollLeft = viewport.scrollLeft;
  container.replaceChildren(); const labels = references();
  const head = element('thead'); const heading = element('tr');
  heading.append(element('th', '', activeMatrix === 'exclusions' ? 'Cannot use together' : 'Row needs column'));
  labels.forEach(reference => heading.append(referenceHeading(reference, 'col'))); head.append(heading); container.append(head);
  const body = element('tbody');
  labels.forEach((left, rowIndex) => {
    const row = element('tr'); row.append(referenceHeading(left, 'row'));
    labels.forEach((right, columnIndex) => {
      const cell = element('td');
      if (left.feature === right.feature) { cell.className = 'same-feature'; cell.textContent = '-'; cell.title = 'One feature has one state'; }
      else {
        const checkbox = element('input'); checkbox.type = 'checkbox';
        checkbox.checked = (declaration[activeMatrix] || []).some(pair => related(pair, left, right) || activeMatrix === 'exclusions' && related(pair, right, left));
        checkbox.setAttribute('aria-label', activeMatrix === 'exclusions' ? `Exclude ${left.feature}=${left.state} and ${right.feature}=${right.state}` : `${left.feature}=${left.state} needs ${right.feature}=${right.state}`);
        if (activeMatrix === 'exclusions' && rowIndex > columnIndex) { checkbox.disabled = true; cell.className = 'mirrored'; }
        checkbox.addEventListener('change', () => mutate(draft => {
          const existing = draft[activeMatrix] || [];
          draft[activeMatrix] = existing.filter(pair => !related(pair, left, right) && !(activeMatrix === 'exclusions' && related(pair, right, left)));
          if (checkbox.checked) draft[activeMatrix].push([left, right]);
        }));
        const label = element('label'); label.title = checkbox.getAttribute('aria-label'); label.append(checkbox); cell.append(label);
      }
      row.append(cell);
    }); body.append(row);
  }); container.append(body);
  const replacement = focusLabel && [...container.querySelectorAll('input')].find(input => input.getAttribute('aria-label') === focusLabel);
  if (replacement) replacement.focus({preventScroll: true});
  viewport.scrollTop = scrollTop; viewport.scrollLeft = scrollLeft;
}
function renderTrial() {
  const container = byId('trial'); container.replaceChildren(); if (!current) return;
  for (const feature of declaration.features) {
    if (isBinary(feature)) {
      const label = element('label', 'binary-field'); const input = element('input'); input.type = 'checkbox'; input.checked = current[feature.id] === 'on'; input.setAttribute('aria-label', `Try ${feature.id} on`);
      input.addEventListener('change', () => task(() => tryChange(feature.id, input.checked ? 'on' : 'off')));
      label.append(element('span', '', feature.id), input); container.append(label);
    } else container.append(field(feature.id, select(feature.states, current[feature.id], `Try ${feature.id} state`, value => task(() => tryChange(feature.id, value)))));
  }
}
function renderStates() {
  const table = byId('states'); table.replaceChildren(); const head = element('thead'); const heading = element('tr');
  model.features.forEach(name => heading.append(element('th', '', name))); head.append(heading); table.append(head);
  const body = element('tbody'); model.states.slice(0, 250).forEach(state => { const row = element('tr'); state.forEach(value => row.append(element('td', '', value))); body.append(row); }); table.append(body);
  byId('state-window').textContent = model.states.length > 250 ? `(250 / ${model.states.length})` : `(${model.states.length})`;
}
async function request(command, extra = {}) {
  const response = await fetch(`api/${command}`, {method: 'POST', headers: {'Content-Type': 'application/json', 'X-Editor-Token': token}, body: JSON.stringify({document: declaration, ...extra})});
  const payload = await response.json(); if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
  byId('elapsed').textContent = `${payload.elapsed_ms} ms`; return payload.result;
}
async function validate() {
  model = await request('check');
  if (!current) resetTrial();
  byId('state-count').textContent = model.states.length; badge('validation-badge', 'Valid', 'success');
  byId('editor-status').textContent = 'Validated'; byId('reset').disabled = false;
  notice((model.warnings || []).join('\n'), true); renderTrial(); renderStates();
}
function resetTrial() {
  current = Object.fromEntries(declaration.features.map(feature => [feature.id, feature.initial]));
  renderTrial(); badge('trial-badge', 'Initial'); byId('trial-result').hidden = true;
}
async function tryChange(feature, state) {
  const result = await request('evaluate', {current, change: {feature, state}});
  current = result.state; renderTrial();
  const rejected = result.status === 'rejected'; badge('trial-badge', rejected ? 'Not changed' : 'Accepted', rejected ? 'error' : 'success');
  byId('trial-result').hidden = false; byId('trial-result').className = rejected ? 'trial-result' : 'trial-result success';
  byId('trial-result').textContent = rejected ? result.violations.map(violation => violation.message).join('\n') : `${feature}=${state}`;
}
async function task(work) {
  if (busy) return; busy = true;
  for (const id of ['feature-fields', 'matrix-fields', 'trial-fields']) byId(id).disabled = true;
  byId('validate').disabled = true; byId('generate').disabled = true;
  try { await work(); } catch (error) { notice(error.message); renderTrial(); byId('editor-status').textContent = 'Error'; }
  finally { busy = false; for (const id of ['feature-fields', 'matrix-fields', 'trial-fields']) byId(id).disabled = false; byId('validate').disabled = false; byId('generate').disabled = false; }
}
function load(document, name, saved = true) {
  validShape(document); declaration = document; filename = name; baseline = saved ? source() : '';
  byId('filename').textContent = name; invalidate(); renderFeatures(); renderMatrix();
}
function download(text, name, type) { const url = URL.createObjectURL(new Blob([text], {type})); const anchor = element('a'); anchor.href = url; anchor.download = name; anchor.click(); setTimeout(() => URL.revokeObjectURL(url), 1000); }
function mayDiscard() { return !declaration || source() === baseline || window.confirm('Discard unsaved changes?'); }

byId('add-feature').addEventListener('click', () => mutate(draft => { draft.features.push({id: nextName('feature', draft.features.map(feature => feature.id)), states: ['off', 'on'], initial: 'off'}); }));
byId('validate').addEventListener('click', () => task(validate));
byId('reset').addEventListener('click', () => { if (!busy && model) resetTrial(); });
byId('generate').addEventListener('click', () => task(async () => {
  if (!model) await validate(); header = await request('generate'); byId('cpp').firstElementChild.textContent = header; byId('cpp-details').open = true; byId('download-cpp').disabled = false;
}));
byId('download-cpp').addEventListener('click', () => { if (!busy) download(header, 'features_generated.hpp', 'text/plain'); });
byId('save').addEventListener('click', () => { if (busy || !declaration) return; download(JSON.stringify(declaration, null, 2) + '\n', filename, 'application/json'); baseline = source(); byId('dirty').hidden = true; });
byId('open').addEventListener('click', () => { if (!busy && mayDiscard()) { byId('file-input').value = ''; byId('file-input').click(); } });
byId('file-input').addEventListener('change', () => task(async () => {
  const file = byId('file-input').files[0]; if (!file) return; if (file.size > 1000000) throw new Error('File exceeds 1 MB'); load(JSON.parse(await file.text()), file.name); await validate();
}));
byId('new').addEventListener('click', () => task(async () => {
  if (!mayDiscard()) return; load({schema_version: 2, features: [{id: 'feature1', states: ['off', 'on'], initial: 'off'}], exclusions: [], requirements: []}, 'features.json', false); await validate();
}));
document.querySelectorAll('.tab').forEach(tab => tab.addEventListener('click', () => {
  if (busy) return; activeMatrix = tab.dataset.matrix;
  document.querySelectorAll('.tab').forEach(other => { other.classList.toggle('active', other === tab); other.setAttribute('aria-selected', String(other === tab)); });
  byId('matrix-panel').setAttribute('aria-labelledby', tab.id); renderMatrix();
}));
window.addEventListener('beforeunload', event => { if (declaration && source() !== baseline) { event.preventDefault(); event.returnValue = ''; } });
icons();
task(async () => { const response = await fetch('api/document'); if (!response.ok) throw new Error(`HTTP ${response.status}`); const payload = await response.json(); token = payload.token; load(payload.document, payload.filename); await validate(); });
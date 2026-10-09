# Feature Constraints

## Feature Constraints

This experimental tool lives inside `userspace/experiment-ui-control/tool/`.
It requires Python 3.10 or later, with no extra Python packages. It checks feature
coexistence, not OS mutexes, physical resource ownership, or hardware atomicity.

### GUI

Run from the repository root:

```sh
make -C userspace/experiment-ui-control ui-tool
make -C userspace/experiment-ui-control ui-tool UI_TOOL_PORT=8770
```

The default address is `http://127.0.0.1:8767/`. Choose another port if it is in use.
The GUI opens [example.json](feature_constraints/example.json). Its camera/AI/recording
relationships are illustrative rules, not claims about hardware limitations.

The input consists of feature names/states/initial values and two checked tables:

- Cannot use together: a symmetric exclusion. One triangular half is editable;
    the other mirrors it. An unchecked cell imposes no exclusion.
- Needs: a directed prerequisite. The row state needs the column state, not the
    reverse. Disabling a prerequisite while its dependent state is active is rejected.

Rows and columns identify a specific feature state. A feature has exactly one
state, so diagonal and same-feature cells are not editable. Multiple prerequisites
are all required. Users never define action names, cases, guards, or queues.

After validating, change a feature in Try Settings. The CLI accepts the change or
rejects it with the conflicting pair/required state. Rejection retains all previous
settings; it never disables a conflicting feature or enables a prerequisite for you.
Reset restores the initial values. Validate and Generate preserve existing trial
settings; changing a declaration invalidates the trial and generated output.

Feature/state renames update relationships; deleting a referenced feature/state is
blocked. Controls are disabled during CLI requests. JSON is only the stored and
transported format, not a GUI editing task. Open/Save download or read declaration
files; saving does not overwrite the server input. C++ can be previewed/downloaded.
The GUI accepts matrix declarations only; legacy action declarations are rejected
without replacing current work. There is no automatic conversion.

Architecture:

```text
Browser GUI
    -> local HTTP API
    -> python3 -m tool.feature_constraints check|evaluate|generate
    -> CLI stdout/stderr and exit code
    -> GUI result
```

[gui.py](feature_constraints/gui.py) runs the CLI as a separate process using
`sys.executable`, standard input, and argument lists. It never imports or duplicates
the constraint checker. GUI edits are passed directly to the CLI without writing
temporary declarations. The server binds only to loopback, restricts the Host
header, and requires a session token for API requests. GUI checks are limited to
10,000 possible states and a 10-second CLI timeout. The state list displays at most
250 reachable states; the full checked model is used for evaluation and generation.
The GUI supports up to 64 feature/state labels, keeping matrices bounded.

Lucide icons are bundled locally. IBM Plex fonts are optionally loaded from Google
Fonts, with local fallbacks. This is a local development tool, not a public service.
It neither controls hardware nor proves task/IRQ interleavings.

### CLI Backend

Run these commands from `userspace/experiment-ui-control/`:

```sh
python3 -m tool.feature_constraints check tool/feature_constraints/example.json
python3 -m tool.feature_constraints generate tool/feature_constraints/example.json --output ../../build/simple_features.hpp
python3 -m tool.feature_constraints serve tool/feature_constraints/example.json --no-browser
```

Matrix declarations have `schema_version: 2`, `features`, `exclusions`, and
`requirements`. Each relationship is a pair of `{feature, state}` references.
Exclusions are unordered; requirements are ordered (dependent, prerequisite).
Missing relationship lists impose no restrictions. Invalid initial settings are
errors. The backend derives single-feature setters and rejects invalid destinations.
Mutually dependent features can be unreachable from their initial values; `check`
reports those modes as warnings rather than enabling them automatically.

`check` explores reachable states and returns feature/setter names, states,
transitions, dead ends, and matrix warnings. Use
`--limit` to override the CLI's default limit of 1,000,000 possible states.

`evaluate` reads an envelope with `document`, `current` (every feature's value), and
`change` (`{feature, state}`). It checks that the current settings are reachable and
returns `status`, `state`, and structured `violations` with messages. Rejection
returns the input settings, not the invalid candidate. The CLI is stateless: the
caller sends the current settings each time. It performs no hardware access.

`generate` emits the existing `experiment::features` C++ contract:
`State`, `Action`, `Value`, `IsEnabled`, and `Apply`.
Matrix output adds `SetAction(feature_index, state_index)` to select a generated
setter without naming it. Invalid indices return an action rejected by `IsEnabled`.
Feature and value indices follow declaration order; do not persist table indices
across a changed declaration.

### Existing Firmware Declarations

Unversioned or version-1 declarations retain `actions`, `when`/`set`, `invariants`,
and `liveness` handling. They still report invariant counterexamples, ambiguous
actions, and unreachable requested targets. `simulate` remains a legacy action
sequence operation; it is not used by the matrix GUI.

```sh
python3 -m tool.feature_constraints check config/features.json
python3 -m tool.feature_constraints simulate config/features.json --actions ToggleBoxes ShowPipe2
```

The firmware's [config/features.json](../config/features.json) stays in this legacy
format so its named actions and existing hardware backend remain unchanged. The
matrix example and downloaded output are not applied to the board automatically.
For the existing unified GUI, explicitly select the matrix input from repo root:

```sh
python3 -m host_app gui --features userspace/experiment-ui-control/tool/feature_constraints/example.json --no-browser
```

Use `-` as the declaration path to read JSON from stdin, and `--output -` to emit
C++ to stdout. GUI-facing commands write only result JSON or C++ to stdout and
diagnostics to stderr. Exit codes are 0 for success, 1 for declaration/data errors,
and 2 for a rejected transaction (or argparse usage errors).

### Verification

```sh
cd userspace/experiment-ui-control
python3 -m unittest discover -s tool/feature_constraints/tests -v
make test
```

Tests cover declaration exploration, generated C++ execution, transaction
simulation, CLI stream contracts, and real HTTP-to-CLI subprocess calls including
invalid sessions, CLI errors, and timeouts. Matrix tests cover symmetry, dependency
direction/provider removal, retained rejected settings, reachability cycles, and
all generated setter results against the CLI. The embedded experiment uses this CLI
from CMake to regenerate its application-specific header; transactional hardware
application remains in [app_state.hpp](../src/app_state.hpp).
import argparse
import collections
import json
import math
import pathlib
import re
import sys


def matches(snapshot, predicate):
    return all(snapshot[name] in (value if isinstance(value, list) else [value])
               for name, value in predicate.items())


def violations(snapshot, invariants):
    result = []
    for invariant in invariants:
        if "forbid" in invariant and matches(snapshot, invariant["forbid"]):
            conditions = invariant["forbid"]
            labels = [f"{name}={value}" for name, value in conditions.items()]
            result.append({"kind": "exclusion", "conditions": conditions,
                           "message": "Cannot use together: " + ", ".join(labels)})
        elif "require" in invariant and matches(snapshot, invariant["require"]) and not matches(snapshot, invariant["implies"]):
            required = invariant["implies"]
            source = ", ".join(f"{name}={value}" for name, value in invariant["require"].items())
            target = ", ".join(f"{name}={value}" for name, value in required.items())
            result.append({"kind": "requirement", "when": invariant["require"], "needs": required,
                           "message": f"{source} needs {target}"})
    return result


def simple_rules(document, domains):
    if any(name in document for name in ("actions", "invariants", "liveness")):
        raise ValueError("matrix declarations do not contain actions, invariants, or liveness")
    invariants = []
    for name in ("exclusions", "requirements"):
        pairs = document.get(name, [])
        if not isinstance(pairs, list):
            raise ValueError(name + " must be a list of state pairs")
        for pair in pairs:
            if not isinstance(pair, list) or len(pair) != 2:
                raise ValueError(name + " needs exactly two states per rule")
            for reference in pair:
                if not isinstance(reference, dict) or set(reference) != {"feature", "state"}:
                    raise ValueError("state reference needs feature and state")
                feature, state = reference["feature"], reference["state"]
                if not isinstance(feature, str) or feature not in domains or state not in domains[feature]:
                    raise ValueError("unknown feature or state in " + name)
            left, right = pair
            if left["feature"] == right["feature"]:
                raise ValueError("matrix rules must connect different features")
            source = {left["feature"]: left["state"]}
            target = {right["feature"]: right["state"]}
            invariant = {"forbid": {**source, **target}} if name == "exclusions" else {"require": source, "implies": target}
            if invariant not in invariants:
                invariants.append(invariant)
    return invariants


def explore(document, limit=1000000):
    if not isinstance(document, dict):
        raise ValueError("declaration must be an object")
    if limit <= 0:
        raise ValueError("state limit must be positive")
    features = document["features"]
    if not isinstance(features, list) or not all(isinstance(feature, dict) for feature in features):
        raise ValueError("features must be a list of objects")
    names = [feature["id"] for feature in features]
    if not names or not all(isinstance(name, str) and name for name in names) or len(set(names)) != len(names):
        raise ValueError("feature IDs must be unique and nonempty")
    choices = [feature["states"] for feature in features]
    for states in choices:
        if not isinstance(states, list) or not states or not all(isinstance(value, str) and value for value in states) or len(set(states)) != len(states):
            raise ValueError("states must be unique nonempty string lists")
    if math.prod(map(len, choices)) > limit:
        raise ValueError("state space exceeds limit")
    domains = dict(zip(names, choices))
    version = document.get("schema_version", 1)
    if type(version) is not int or version not in (1, 2):
        raise ValueError("unsupported schema_version")
    simple = version == 2

    def validate(predicate):
        if not isinstance(predicate, dict):
            raise ValueError("predicate must be an object")
        for name, value in predicate.items():
            values = value if isinstance(value, list) else [value]
            if name not in domains or not values or any(item not in domains[name] for item in values):
                raise ValueError("unknown feature or state: " + str(name))

    initial = tuple(feature["initial"] for feature in features)
    if any(value not in choices[index] for index, value in enumerate(initial)):
        raise ValueError("initial state must name one declared state per feature")
    validate(dict(zip(names, initial)))
    actions = [{"id": f"Set{feature_index}_{state_index}", "set": {feature["id"]: value}}
               for feature_index, feature in enumerate(features) for state_index, value in enumerate(feature["states"])] if simple else document["actions"]
    if not isinstance(actions, list) or not all(isinstance(action, dict) for action in actions):
        raise ValueError("actions must be a list of objects")
    action_names = list(dict.fromkeys(action["id"] for action in actions))
    if not action_names or any(not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name) for name in action_names):
        raise ValueError("action IDs must be C++ compatible names")
    for action in actions:
        validate(action.get("when", {}))
        validate(action["set"])
        if any(isinstance(value, list) for value in action["set"].values()):
            raise ValueError("action assigns exactly one value per feature")
    invariants = simple_rules(document, domains) if simple else document.get("invariants", [])
    if not isinstance(invariants, list) or not all(isinstance(invariant, dict) for invariant in invariants):
        raise ValueError("invariants must be a list of objects")
    for invariant in invariants:
        if set(invariant) == {"forbid"}:
            validate(invariant["forbid"])
        elif set(invariant) == {"require", "implies"}:
            validate(invariant["require"])
            validate(invariant["implies"])
        else:
            raise ValueError("invalid invariant")

    def valid(state):
        return not violations(dict(zip(names, state)), invariants)

    if not valid(initial):
        raise ValueError("initial state violates invariant")
    states = [initial]
    indices = {initial: 0}
    paths = {initial: []}
    pending = collections.deque([initial])
    edges = []
    dead_ends = []
    while pending:
        current = pending.popleft()
        snapshot = dict(zip(names, current))
        row = []
        for name in action_names:
            matching = [action for action in actions if action["id"] == name and matches(snapshot, action.get("when", {}))]
            if len(matching) > 1:
                raise ValueError("ambiguous action: " + name)
            if not matching:
                row.append(-1)
                continue
            changes = matching[0]["set"]
            next_state = tuple(changes.get(feature, current[index]) for index, feature in enumerate(names))
            if not valid(next_state):
                if simple:
                    row.append(-1)
                    continue
                raise ValueError("counterexample: " + " -> ".join(paths[current] + [name]))
            if next_state not in indices:
                indices[next_state] = len(states)
                states.append(next_state)
                paths[next_state] = paths[current] + [name]
                pending.append(next_state)
            row.append(indices[next_state])
        if all(destination == -1 for destination in row):
            dead_ends.append(indices[current])
        edges.append(row)
    liveness = document.get("liveness", [])
    if not isinstance(liveness, list) or not all(isinstance(condition, dict) and set(condition) == {"reachable"} for condition in liveness):
        raise ValueError("liveness must be a list of reachable predicates")
    for condition in liveness:
        validate(condition["reachable"])
        if not any(matches(dict(zip(names, state)), condition["reachable"]) for state in states):
            raise ValueError("unreachable requested state")
    model = {"features": names, "actions": action_names, "states": states, "edges": edges, "dead_ends": dead_ends}
    if simple:
        model["domains"] = choices
        model["invariants"] = invariants
        model["warnings"] = [f"Unreachable: {name}={value}" for index, name in enumerate(names)
                             for value in choices[index] if not any(state[index] == value for state in states)]
    return model


def evaluate(document, snapshot, change, limit=1000000):
    if not isinstance(document, dict) or document.get("schema_version") != 2:
        raise ValueError("evaluate requires a matrix declaration (schema_version=2)")
    model = explore(document, limit)
    if not isinstance(snapshot, dict) or set(snapshot) != set(model["features"]):
        raise ValueError("current state must contain every feature exactly once")
    current = tuple(snapshot[name] for name in model["features"])
    if current not in model["states"]:
        raise ValueError("current state is invalid or unreachable from the initial state")
    if not isinstance(change, dict) or set(change) != {"feature", "state"}:
        raise ValueError("change needs feature and state")
    name, value = change["feature"], change["state"]
    if not isinstance(name, str) or name not in model["features"] or value not in model["domains"][model["features"].index(name)]:
        raise ValueError("unknown feature or state in change")
    candidate = {**snapshot, name: value}
    rejected = violations(candidate, model["invariants"])
    return {"status": "rejected" if rejected else "accepted", "state": snapshot if rejected else candidate,
            "violations": rejected}


def simulate(document, actions, limit=1000000):
    if isinstance(document, dict) and document.get("schema_version") == 2:
        raise ValueError("matrix declarations use evaluate rather than action sequences")
    model = explore(document, limit)
    current = 0
    steps = []
    status = "idle"
    for name in actions:
        if name not in model["actions"]:
            raise ValueError("unknown action: " + name)
        destination = model["edges"][current][model["actions"].index(name)]
        step = {"action": name, "accepted": destination >= 0}
        if destination < 0:
            step["requires_one_of"] = [action.get("when", {}) for action in document["actions"]
                                       if action["id"] == name]
            step["candidate"] = dict(zip(model["features"], model["states"][current]))
            steps.append(step)
            current = 0
            status = "rejected"
            break
        current = destination
        step["candidate"] = dict(zip(model["features"], model["states"][current]))
        steps.append(step)
        status = "committed"
    enabled = [name for name, destination in zip(model["actions"], model["edges"][current])
               if destination >= 0]
    blocked = {name: [action.get("when", {}) for action in document["actions"] if action["id"] == name]
               for name in model["actions"] if name not in enabled}
    return {"status": status, "state": dict(zip(model["features"], model["states"][current])),
            "enabled": enabled, "blocked": blocked, "steps": steps}


def cpp_string(value):
    encoded = "".join(chr(byte) if 32 <= byte < 127 and byte not in (34, 63, 92)
                      else f"\\{byte:03o}" for byte in value.encode("utf-8"))
    return '"' + encoded + '"'


def emit(model):
    names = ", ".join("k" + name for name in model["actions"])
    values = ",\n".join("    {" + ", ".join(cpp_string(value) for value in state) + "}" for state in model["states"])
    setters = ""
    if "domains" in model:
        width = max(map(len, model["domains"]))
        rows = []
        offset = 0
        for domain in model["domains"]:
            rows.append("    {" + ", ".join(map(str, [*range(offset, offset + len(domain)), *([-1] * (width - len(domain)))])) + "}")
            offset += len(domain)
        table = ",\n".join(rows)
        setters = f"""inline constexpr int setters[{len(model['features'])}][{width}] = {{
{table}
}};
inline constexpr Action SetAction(std::size_t feature, std::size_t value) {{
    const int column = feature < {len(model['features'])} && value < {width} ? setters[feature][value] : -1;
    return static_cast<Action>(column >= 0 ? column : {len(model['actions'])});
}}
"""
    transition_rows = ",\n".join("    {" + ", ".join(map(str, row)) + "}" for row in model["edges"])
    return f"""#pragma once
#include <cstddef>
namespace experiment::features {{
enum class Action : std::size_t {{ {names} }};
{setters}struct State {{ std::size_t index = 0; }};
inline constexpr const char *values[{len(model['states'])}][{len(model['features'])}] = {{
{values}
}};
inline constexpr const char *Value(State state, std::size_t feature) {{
    return state.index < {len(model['states'])} && feature < {len(model['features'])} ? values[state.index][feature] : nullptr;
}}
inline constexpr int transitions[{len(model['states'])}][{len(model['actions'])}] = {{
{transition_rows}
}};
inline constexpr bool IsEnabled(State state, Action action) {{
    const auto column = static_cast<std::size_t>(action);
    return state.index < {len(model['states'])} && column < {len(model['actions'])} && transitions[state.index][column] >= 0;
}}
inline constexpr bool Apply(State &state, Action action) {{
    if (!IsEnabled(state, action)) {{ return false; }}
    state.index = static_cast<std::size_t>(transitions[state.index][static_cast<std::size_t>(action)]);
    return true;
}}
}}
"""


def main(argv=None):
    parser = argparse.ArgumentParser(prog="feature-constraints")
    parser.add_argument("command", choices=["check", "generate", "simulate", "evaluate", "serve"])
    parser.add_argument("declaration", type=pathlib.Path, nargs="?")
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--actions", nargs="*", default=[])
    parser.add_argument("--limit", type=int, default=1000000)
    parser.add_argument("--port", type=int, default=8767)
    parser.add_argument("--no-browser", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.actions and arguments.command != "simulate":
        parser.error("--actions requires simulate")
    try:
        if arguments.command == "serve":
            from .gui import serve

            return serve(arguments.declaration, arguments.port, not arguments.no_browser)
        if arguments.declaration is None:
            parser.error("a declaration path or - for stdin is required")
        source = sys.stdin.read() if str(arguments.declaration) == "-" else arguments.declaration.read_text(encoding="utf-8")
        document = json.loads(source)
        if arguments.command == "evaluate":
            result = evaluate(document["document"], document["current"], document["change"], arguments.limit)
            print(json.dumps(result, indent=2))
            if result["status"] == "rejected":
                parser.exit(2)
        elif arguments.command == "simulate":
            result = simulate(document, arguments.actions, arguments.limit)
            print(json.dumps(result, indent=2))
            if result["status"] == "rejected":
                parser.exit(2)
        elif arguments.command == "generate":
            if arguments.output is None:
                parser.error("generate requires --output")
            header = emit(explore(document, arguments.limit))
            if str(arguments.output) == "-":
                print(header, end="")
            else:
                arguments.output.write_text(header, encoding="utf-8")
        else:
            print(json.dumps(explore(document, arguments.limit), indent=2))
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
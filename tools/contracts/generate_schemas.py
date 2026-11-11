#!/usr/bin/env python3
"""Generate RoofRoof's JSON Schemas (specs/001-platform-bootstrap/contracts/schemas)
from sample documents. There is no real RoofRoof device yet, so the samples come
from the demo's emulated device, which runs the firmware's own logic (the device
core) and emulates the hardware sections of /api/status. Write them with

  cd web && CONTRACT_SAMPLES=<samples-dir> npx vitest run contracts
  python3 tools/contracts/generate_schemas.py <samples-dir>

Once a real device exists, capture its documents as device-<document>.json too.

Samples are named <source>-<document>.json (source = device | demo). A key is
required when every sample of that object has it; any key no sample has is
rejected (additionalProperties: false), so a renamed or added field shows up.
Review the diff before committing: the schemas are the contract."""

import glob
import json
import os
import sys

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "specs", "001-platform-bootstrap", "contracts", "schemas")

# document name -> schema file; status is built from device samples only (its
# hardware sections are emulated by the demo and must match the firmware).
DOCUMENTS = {
    "status": "status",
    "settings_effective": "settings-effective",
    "config": "config",
    "roof_inputs": "roof-inputs",
    "alerts_recent": "alerts-recent",
    "alerts_armed": "alerts-armed",
    "management_v1_description": "alpaca-description",
    "management_v1_configureddevices": "alpaca-configured-devices",
}

# Objects keyed by data (a variable set of keys): only their values are described.
# "clients": /api/status alpaca.clients, keyed by Alpaca device type.
OPEN_MAPS = {("channels",), ("clients",)}

# Keys the firmware leaves out in some states, so no sample of the demo (which
# always has them) can show it. Paths in /api/status (src/WebServerStatus.cpp):
# ntp only with a TimeManager, mqtt only with an MQTT client, each partition
# field only when esp_ota / the partition table returns it.
OPTIONAL = {
    "status": {
        ("ntp",),
        ("mqtt",),
        ("partitions", "runningSlot"),
        ("partitions", "runningAddress"),
        ("partitions", "runningSize"),
        ("partitions", "bootSlot"),
        ("partitions", "nextSlot"),
        ("partitions", "nextSize"),
        ("partitions", "nvs"),
        ("partitions", "fsAddress"),
        ("partitions", "fsSize"),
    },
}
current_optional = set()

# Value rules a sample can't show, from the firmware: (schema, path) -> extra
# schema keys. "[]" is an array's items.
SCHEDULE_MODES = ["any", "whileConnected"]  # Alerts::sendModeName
SCHEDULE_REASONS = [  # Alerts::scheduleReasonName (lib/AlertLogic/src/AlertSchedule.cpp)
    "none",
    "user-ui",
    "user-rest",
    "user-mqtt",
    "client-connected",
    "client-disconnected",
    "waiting-for-client",
    "migrated",
]
SOURCE_STATES = ["off", "waiting", "good", "stale", "unreachable", "unreadable", "offline"]  # Safety::sourceStateName
CONSTRAINTS = {
    ("status", ("alerts", "mode")): {"enum": SCHEDULE_MODES},
    ("status", ("alerts", "reason")): {"enum": SCHEDULE_REASONS},
    ("status", ("alerts", "recentRevision")): {"type": "integer", "minimum": 0},
    ("status", ("configRevision",)): {"type": "integer", "minimum": 0},
    ("status", ("firmware", "product")): {"const": "roofroof"},
    ("status", ("firmware", "layout")): {"enum": ["l2", "other"]},  # FirmwareImage::layoutOf
    ("status", ("wifi", "ipv6", "addresses", "[]", "scope")): {"enum": ["link-local", "unique-local", "global"]},
    # The roof section (spec 003, lib/RoofLogic and lib/RoofIoLogic names).
    ("status", ("roof", "state")): {"enum": ["unknown", "closed", "open", "opening", "closing", "stopped", "error"]},
    ("status", ("roof", "target")): {"enum": ["open", "closed", None]},
    ("status", ("roof", "hall", "level")): {"enum": ["unsettled", "low", "between", "high", "rail"]},
    ("status", ("roof", "parkSwitches", "switch1")): {"enum": ["closed", "open", "unreadable", "notFitted"]},
    ("status", ("roof", "parkSwitches", "switch2")): {"enum": ["closed", "open", "unreadable", "notFitted"]},
    ("status", ("roof", "rainContact")): {"enum": ["notFitted", "dry", "raining", "unreadable"]},
    ("status", ("roof", "fault", "cause")): {
        "enum": ["logic", "watchdog", "parkInterlock", "storedUnreadable", "watchdogStopped", "benchSupply"]
    },
    # Spec 011: the real roof, bench firmware or the browser demo.
    ("status", ("roof", "simulation")): {"enum": ["off", "bench", "demo"]},
    ("status", ("roof", "fault", "kind")): {
        "enum": [
            "timeout",
            "didNotLeave",
            "wrongMagnet",
            "hallRail",
            "overCurrent",
            "currentUnreadable",
            "supplyLow",
            "lateStep",
            "driveBudget",
            "external",
        ]
    },
    ("alerts-armed", ("mode",)): {"enum": SCHEDULE_MODES},
    ("alerts-armed", ("reason",)): {"enum": SCHEDULE_REASONS},
    ("config", ("alerts", "sendMode")): {"enum": SCHEDULE_MODES},
    # Spec 015: the setup wizard's state (lib/ConfigModel setupStateName) and page.
    ("config", ("setup", "state")): {"enum": ["new", "offered", "done"]},
    ("config", ("setup", "step")): {"type": "integer", "minimum": 0, "maximum": 9},
    # GET /api/roof/inputs (lib/SafetyInputLogic InputsReport names).
    ("roof-inputs", ("weather", "verdict")): {"enum": ["safe", "unsafe", "unknown"]},
    ("roof-inputs", ("weather", "http", "state")): {"enum": SOURCE_STATES},
    ("roof-inputs", ("weather", "mqtt", "state")): {"enum": SOURCE_STATES},
    ("roof-inputs", ("weather", "rainContact")): {"enum": ["notFitted", "dry", "raining", "unreadable"]},
    ("roof-inputs", ("nina", "state")): {
        "enum": ["notConfigured", "waiting", "good", "unreachable", "unreadable", "apiError"]
    },
}


def constrain(schema_name, schema):
    for (name, path), extra in CONSTRAINTS.items():
        if name != schema_name:
            continue
        node = schema
        for key in path:
            node = node["items"] if key == "[]" else node["properties"][key]
        node.update(extra)


def type_of(value):
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, (int, float)):
        return "number"
    if isinstance(value, str):
        return "string"
    if isinstance(value, list):
        return "array"
    return "object"


def merge(samples, path=(), partial=()):
    types = sorted({type_of(s) for s in samples})
    schema = {"type": types[0] if len(types) == 1 else types}
    objects = [s for s in samples if isinstance(s, dict)]
    if objects:
        if path[-1:] and (path[-1],) in OPEN_MAPS:
            values = [v for o in objects for v in o.values()]
            if values:
                schema["additionalProperties"] = merge(values, path + ("*",))
            return schema
        full = [o for o in objects if not any(o is p for p in partial)]
        keys = sorted({k for o in objects for k in o})
        schema["properties"] = {
            k: merge([o[k] for o in objects if k in o], path + (k,), tuple(o[k] for o in partial if isinstance(o, dict) and k in o))
            for k in keys
        }
        schema["required"] = [k for k in keys if all(k in o for o in full)]
        schema["required"] = [k for k in schema["required"] if path + (k,) not in current_optional]
        schema["additionalProperties"] = False
    arrays = [s for s in samples if isinstance(s, list)]
    items = [i for a in arrays for i in a]
    if arrays and items:
        schema["items"] = merge(items, path + ("[]",))
    return schema


def main():
    samples_dir = sys.argv[1]
    grouped = {}
    for path in glob.glob(os.path.join(samples_dir, "*.json")):
        name = os.path.basename(path)[:-5]
        source, _, document = name.partition("-")
        if document not in DOCUMENTS or (document == "status" and not source.startswith("device")):
            continue
        with open(path) as f:
            sample = json.load(f)
        entry = grouped.setdefault(DOCUMENTS[document], {"samples": [], "partial": []})
        entry["samples"].append(sample)
        if document.endswith("_partial"):
            entry["partial"].append(sample)
    os.makedirs(OUT, exist_ok=True)
    for schema_name, entry in sorted(grouped.items()):
        docs = entry["samples"]
        current_optional.clear()
        current_optional.update(OPTIONAL.get(schema_name, ()))
        schema = {"$schema": "http://json-schema.org/draft-07/schema#", "title": schema_name, **merge(docs, (), tuple(entry["partial"]))}
        constrain(schema_name, schema)
        with open(os.path.join(OUT, schema_name + ".schema.json"), "w") as f:
            json.dump(schema, f, indent=2, sort_keys=False)
            f.write("\n")
        print(f"{schema_name}: {len(docs)} samples")


if __name__ == "__main__":
    main()

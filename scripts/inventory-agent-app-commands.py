#!/usr/bin/env python3
"""Inventory native Qt commands and writable controls before agent registration.

This is a coverage inventory, not an assertion that every public method is safe
to expose. Commands remain unclassified until a native owner assigns capability,
completion/cancellation, and resource reversal contracts.
"""

import argparse
import hashlib
import json
import re
from pathlib import Path


def balanced(text, start):
    depth = 0
    quote = None
    escaped = False
    for index in range(start, len(text)):
        char = text[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in "\"'":
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("Unclosed Qt declaration")


def split_arguments(text):
    result, start, depth = [], 0, 0
    quote, escaped = None, False
    for index, char in enumerate(text):
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in "\"'":
            quote = char
        elif char in "(<[{":
            depth += 1
        elif char in ")>]}":
            depth -= 1
        elif char == "," and depth == 0:
            result.append(text[start:index].strip())
            start = index + 1
    if text[start:].strip():
        result.append(text[start:].strip())
    return result


def schema_for(cpp_type):
    value = re.sub(r"\bconst\b|[&*]", "", cpp_type).strip()
    value = re.sub(r"\s+", " ", value)
    if value == "bool":
        return {"type": "boolean"}
    if value in {"int", "uint", "qint64", "quint64", "qsizetype", "long long", "std::size_t"}:
        return {"type": "integer"}
    if value in {"double", "float", "qreal"}:
        return {"type": "number"}
    if value == "QString":
        return {"type": "string"}
    if value == "QStringList":
        return {"type": "array", "items": {"type": "string"}}
    if value in {"QVariantMap", "QJsonObject"}:
        return {"type": "object", "needsNativeFieldSchema": True}
    if value in {"QVariantList", "QJsonArray"}:
        return {"type": "array", "needsNativeItemSchema": True}
    return {"cppType": value, "needsNativeTypeSchema": True}


def domain_for(filename):
    for prefix, domain in (
        ("editor_close", "App lifecycle"),
        ("editor_", "Edit"), ("preset_", "Edit"), ("photo_text", "Edit"),
        ("brush_", "Edit"), ("catalog_", "Library"), ("local_similarity", "Library"),
        ("visual_search", "Library"), ("timeline_", "Timeline"),
        ("carousel_", "Carousel"), ("scene_", "Scene"), ("gaussian_scene", "Scene"),
        ("export_", "Export"), ("app_shell", "Preferences and navigation"),
        ("preview_backend", "Preferences and navigation"), ("search_model_setup", "Preferences and navigation"),
        ("agent_", "Agent"), ("managed_agent", "Agent"), ("search_edit", "Agent"),
        ("job_", "Jobs"), ("customer_", "Account"), ("macos_updater", "Updates"),
        ("crash_", "Diagnostics"), ("qml_performance", "Diagnostics"),
    ):
        if filename.startswith(prefix):
            return domain
    return "Needs domain classification"


def inventory(root):
    rows, sources = [], {}
    # Include every QObject public surface, not just a hand-picked design subset.
    for path in sorted((root / "apps/desktop/src").glob("*.h")):
        raw = path.read_text()
        if "Q_OBJECT" not in raw:
            continue
        relative = str(path.relative_to(root))
        sources[relative] = hashlib.sha256(raw.encode()).hexdigest()
        # Preserve offsets and source lines while suppressing commented examples.
        text = re.sub(r"//[^\n]*|/\*[\s\S]*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group()), raw)
        classes = [item for item in re.finditer(r"\bclass\s+(\w+)\b[^;{]*\{", text)
                   if not re.search(r"\benum\s*$", text[max(0, item.start() - 20):item.start()])]
        object_classes = []
        for marker in re.finditer(r"\bQ_OBJECT\b", text):
            preceding = [item for item in classes if item.start() < marker.start()]
            if preceding:
                object_classes.append(preceding[-1])
        for match in re.finditer(r"\b(Q_INVOKABLE|Q_PROPERTY)\b", text):
            preceding = [item for item in object_classes if item.start() < match.start()]
            owner = preceding[-1].group(1) if preceding else path.stem
            base = {
                "owner": owner, "domain": domain_for(path.stem),
                "source": relative, "line": text.count("\n", 0, match.start()) + 1,
                "registrationStatus": "unclassified",
                "capability": None, "resourceOwner": None,
                "completion": None, "cancellation": None, "reversal": None,
            }
            opening = text.find("(", match.end())
            closing = balanced(text, opening)
            if match.group(1) == "Q_PROPERTY":
                declaration = " ".join(text[opening + 1:closing].split())
                setter = re.search(r"\bWRITE\s+(\w+)", declaration)
                if not setter:
                    continue
                head = declaration.split(" READ ")[0].split(" WRITE ")[0].split(" MEMBER ")[0]
                parts = head.rsplit(" ", 1)
                if len(parts) != 2:
                    raise ValueError(f"Cannot parse writable property {relative}: {declaration}")
                cpp_type, name = parts
                rows.append({**base, "id": f"{owner}.property.{name}", "kind": "property",
                             "name": name, "setter": setter.group(1), "cppType": cpp_type,
                             "schema": schema_for(cpp_type)})
                continue
            head = " ".join(text[match.end():opening].split())
            method = re.search(r"(\w+)\s*$", head)
            if not method:
                raise ValueError(f"Cannot parse invokable {relative}: {head}")
            name = method.group(1)
            arguments = []
            for argument in split_arguments(text[opening + 1:closing]):
                signature, _, default = argument.partition("=")
                signature = signature.strip()
                if signature == "void":
                    continue
                parameter = re.search(r"(\w+)\s*$", signature)
                if not parameter:
                    raise ValueError(f"Cannot parse argument {relative}: {argument}")
                cpp_type = signature[:parameter.start()].strip()
                arguments.append({"name": parameter.group(1), "cppType": cpp_type,
                                  "schema": schema_for(cpp_type),
                                  "required": not bool(default.strip()),
                                  **({"cppDefault": default.strip()} if default.strip() else {})})
            rows.append({**base, "id": f"{owner}.method.{name}", "kind": "method", "name": name,
                         "returnType": head[:method.start()].strip(), "arguments": arguments})
    tools_source = (root / "apps/desktop/src/photo_mcp_server.cpp").read_text()
    tools = sorted(set(re.findall(r'^\s*"((?:photo|timeline|carousel|scene)\.[^\"]+)"\s*,', tools_source, re.M)))
    return {
        "contract": "agent-native-command-inventory-v1",
        "purpose": "Coverage inventory; unclassified commands are not automatically exposed",
        "coverageBoundary": "Q_INVOKABLE methods and WRITE properties in QObject headers; QML-only workflows and public slots require a separate pass",
        "sourceSha256": sources, "nativeCommandCount": len(rows),
        "existingMcpTools": tools, "commands": rows,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = inventory(args.root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    counts = {}
    for command in result["commands"]:
        counts[command["domain"]] = counts.get(command["domain"], 0) + 1
    print(json.dumps({"nativeCommandCount": result["nativeCommandCount"],
                      "existingMcpToolCount": len(result["existingMcpTools"]), "domains": counts}))


if __name__ == "__main__":
    main()

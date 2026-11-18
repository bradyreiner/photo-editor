#!/usr/bin/env python3
"""Enforce shared QML chrome without needing Qt (also runs in headless CI).

This deliberately checks structure and bindings, not contract-description strings.
Native tests independently check the evaluated geometry and rendered surfaces.
"""
from dataclasses import dataclass
from pathlib import Path
import argparse
import json
import re

TOKEN = re.compile(r"//[^\n]*|/\*[\s\S]*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`|[A-Za-z_][\w.]*|[^\s]", re.MULTILINE)
BINDING = re.compile(r"(?:^|[;\n])[ \t]*(?:(?:readonly|required|default)[ \t]+)*(?:property[ \t]+[\w.<>]+[ \t]+)?([\w.]+)[ \t]*:[ \t]*([^;\n]*)")
SHARED = {"AppPopup": "Popup", "AppDialog": "Dialog", "AppDrawer": "Drawer", "AppToolTip": "ToolTip", "AppMenu": "Menu"}
COMMANDS = {"ActionButton", "MiniButton", "ToolButton", "MenuButton", "SplitButton", "EditRailButton", "FilterChip", "SidebarButton", "PopupMenuRow", "CarouselToolRailButton", "EditInspectorTabButton", "PreferenceNavRow"}
BUTTON_OWNERS = {"ActionButton", "MiniButton", "ToolButton", "FilterChip", "SidebarButton", "PopupMenuRow", "CarouselToolRailButton", "PreferenceNavRow"}
FIELDS = {"ToneTextField", "ToneTextArea", "ToneSpinBox", "ToneComboBox"}
CHOICES = {"ToneCheckBox", "ToneSwitch"}
BODY_SCOPES = {"AgentSessionTerminal.qml", "AgentTerminalScreen.qml", "StudioOfflineRecoveryBanner.qml", "CopyPhotoEditsDialog.qml"}


@dataclass
class Object:
    kind: str
    start: int
    end: int
    parent: object
    bindings: dict
    inline_name: str = ""
    declared_type: str = ""


def objects(source):
    # Tokenize before looking for braces: strings and comments are never QML.
    tokens = [m for m in TOKEN.finditer(source) if not m[0].startswith(("//", "/*"))]
    result, stack = [], []
    for i, token in enumerate(tokens):
        if token[0] == "{":
            previous = tokens[i - 1][0] if i else ""
            kind = previous.split(".")[-1]
            parent = next((entry[1] for entry in reversed(stack) if entry[1]), None)
            obj = Object(kind, token.end(), len(source), parent, {}) if re.fullmatch(r"[A-Z]\w*|border|background|font", kind) else None
            if obj:
                obj.declared_type = previous
                if i >= 4 and tokens[i - 4][0] == "component" and tokens[i - 2][0] == ":":
                    obj.inline_name = tokens[i - 3][0]
                result.append(obj)
            stack.append((token, obj))
        elif token[0] == "}" and stack:
            opening, obj = stack.pop()
            if obj:
                obj.end = token.start()
    for obj in result:
        body = list(source[obj.start:obj.end])
        depth = 0
        for token in TOKEN.finditer(source, obj.start, obj.end):
            value = token[0]
            if value == "{":
                depth += 1
            if depth or value.startswith(("//", "/*")):
                for pos in range(token.start(), token.end()):
                    if source[pos] != "\n":
                        body[pos - obj.start] = " "
            if value == "}":
                depth -= 1
        # Also mask whitespace/text between tokens inside nested scopes.
        depth, begin = 0, None
        for token in TOKEN.finditer(source, obj.start, obj.end):
            if token[0] == "{":
                if depth == 0:
                    begin = token.start()
                depth += 1
            elif token[0] == "}":
                depth -= 1
                if depth == 0:
                    for pos in range(begin, token.end()):
                        if source[pos] != "\n":
                            body[pos - obj.start] = " "
        obj.bindings = {m[1]: (m[2].strip(), obj.start + m.start(1)) for m in BINDING.finditer("".join(body))}
    return result


def audit_sources(sources):
    parsed = {path: objects(source) for path, source in sources.items()}
    bases = {Path(path).stem: nodes[0].kind for path, nodes in parsed.items() if nodes}
    icon_sources = [source for path, source in sources.items() if Path(path).name == "AppIcon.qml"]
    icon_names = set(re.findall(r'case\s+"([^"]+)"\s*:', "\n".join(icon_sources)))
    local_bases = {}
    def family(kind, names):
        visited = set()
        while kind not in visited:
            if kind in names:
                return kind
            visited.add(kind)
            kind = local_bases.get(kind, bases.get(kind, ""))
        return ""
    violations, counts = [], {kind: 0 for kind in (*SHARED, "input_fields", "inspector_sliders", "inspector_sections")}
    for path, nodes in parsed.items():
        local_bases = {node.inline_name: node.kind for node in nodes if node.inline_name}
        source = sources[path]
        qt_control_aliases = set(re.findall(r"(?m)^import[ \t]+QtQuick\.Controls(?:\.\w+)?(?:[ \t]+[\d.]+)?[ \t]+as[ \t]+(\w+)", source))
        if icon_names:
            tokens = [m for m in TOKEN.finditer(source) if not m[0].startswith(("//", "/*"))]
            for index in range(len(tokens) - 2):
                key, colon, value = tokens[index:index + 3]
                quoted_key = key[0].startswith(('"', "'"))
                map_key = index > 0 and tokens[index - 1][0] in ("{", ",")
                if key[0].strip('"\'') in ("iconName", "icon") and colon[0] == ":" and (not quoted_key or map_key):
                    literal = re.fullmatch(r'''["']([^"']+)["']''', value[0])
                    if literal and literal[1] not in icon_names:
                        violations.append(f"{path}:{source.count(chr(10), 0, key.start()) + 1}: shared-icon: static icons must belong to AppIcon's vocabulary")
        def reject(obj, rule, message, key=None):
            pos = obj.bindings[key][1] if key in obj.bindings else obj.start
            line = source.count("\n", 0, pos) + 1
            violations.append(f"{path}:{line}: {rule}: {message}")
        for obj in nodes:
            root_kind = Path(path).stem if obj is nodes[0] else obj.kind
            popup = family(obj.kind, set(SHARED)) or family(root_kind, set(SHARED))
            command = family(obj.kind, COMMANDS) or family(root_kind, COMMANDS)
            field = family(obj.kind, FIELDS) or family(root_kind, FIELDS)
            choice = family(obj.kind, CHOICES) or family(root_kind, CHOICES)
            body = family(obj.kind, {"BodyLabel"}) or family(root_kind, {"BodyLabel"})
            slider = family(obj.kind, {"InspectorSlider"}) or family(root_kind, {"InspectorSlider"})
            section = family(obj.kind, {"InspectorSection"}) or family(root_kind, {"InspectorSection"})
            panel = family(obj.kind, {"StudioPanel"}) or family(root_kind, {"StudioPanel"})
            character = family(obj.kind, {"LooksyAgentCharacter"}) or family(root_kind, {"LooksyAgentCharacter"})
            if character and Path(path).name not in ("LooksyAgentCharacter.qml", "AgentWalkthroughDialog.qml"):
                reject(obj, "agent-character-placement", "the character belongs only in the Agent view")
            if obj.kind in ("Button", "RoundButton") and not (obj is nodes[0] and Path(path).stem in BUTTON_OWNERS):
                reject(obj, "shared-command", "use a shared command; preview content belongs inside its contentItem")
            if obj.kind in ("TextField", "TextArea", "SpinBox", "ComboBox") and not (obj is nodes[0] and Path(path).stem == "Tone" + obj.kind):
                reject(obj, "shared-input", "use the shared Tone input control")
            if obj.kind in ("CheckBox", "Switch", "RadioButton") and not (obj is nodes[0] and Path(path).stem == "Tone" + obj.kind):
                reject(obj, "shared-choice", "use a shared choice control")
            if Path(path).name in BODY_SCOPES and obj.kind in ("Label", "Text"):
                reject(obj, "shared-body-text", "use BodyLabel for scalable interface text")
            if body and "font.pixelSize" in obj.bindings:
                value = obj.bindings["font.pixelSize"][0]
                branches = value.split("?", 1)[-1].split(":")
                if not all(re.fullmatch(r"[\w.]+\.type(?:Micro|Caption|Label|Body|Control|Section|Title|DialogTitle|Hero|PageTitle)", branch.strip()) for branch in branches):
                    reject(obj, "body-typography", "body text uses shared scalable type tokens", "font.pixelSize")
            if body and "fontSizeMode" in obj.bindings and obj.bindings["fontSizeMode"][0] != "Text.FixedSize":
                reject(obj, "body-typography", "body text must not shrink to fit", "fontSizeMode")
            if obj.kind == "font" and obj.parent and "pixelSize" in obj.bindings:
                if family(obj.parent.kind, CHOICES):
                    reject(obj, "choice-override", "choice type belongs to the shared primitive", "pixelSize")
                if family(obj.parent.kind, {"BodyLabel"}) and not re.fullmatch(r"[\w.]+\.type\w+", obj.bindings["pixelSize"][0]):
                    reject(obj, "body-typography", "grouped body type uses shared scalable tokens", "pixelSize")
            if obj.kind == "ToolButton" and obj.declared_type.partition(".")[0] in qt_control_aliases:
                reject(obj, "shared-command", "use the shared ToolButton rather than the stock Qt alias")
            if icon_names:
                keys = ["iconName", "icon"] + (["name"] if obj.kind == "AppIcon" else [])
                for key in keys:
                    value = obj.bindings.get(key, ("", 0))[0]
                    literal = re.fullmatch(r'''["']([^"']+)["']''', value)
                    if literal and literal[1] not in icon_names:
                        reject(obj, "shared-icon", "static icons must belong to AppIcon's vocabulary", key)
            if popup:
                counts[popup] += 1
            if field:
                counts["input_fields"] += 1
            if slider:
                counts["inspector_sliders"] += 1
            if section:
                counts["inspector_sections"] += 1
            if obj.kind in SHARED.values():
                owner = next(name for name, base in SHARED.items() if base == obj.kind)
                shared_root = Path(path).stem == owner and obj is nodes[0]
                ancestor = obj.parent
                while ancestor and ancestor.kind not in ("MenuBar", "PlatformMenuBar"):
                    ancestor = ancestor.parent
                native_menu = obj.kind == "Menu" and Path(path).name == "Main.qml" and ancestor is not None
                if not shared_root and not native_menu:
                    reject(obj, "shared-chrome", f"use {owner}; native menus are allowed only inside the application menu bar")
            if popup or command or field or choice:
                background = obj.bindings.get("background", ("", 0))[0]
                expected = "PopupSurface" if popup else "InputFieldSurface" if field else "CommandButtonSurface"
                if background and background != expected:
                    reject(obj, "shared-background", f"{obj.kind} background must use {expected}", "background")
                for key in obj.bindings:
                    if key.startswith("background."):
                        reject(obj, "shared-background", "do not override shared background properties at the use site", key)
            if choice and not (obj is nodes[0] and Path(path).stem in CHOICES):
                for key in ("indicator", "contentItem", "font.pixelSize"):
                    if key in obj.bindings:
                        reject(obj, "choice-override", "choice appearance belongs to the shared primitive", key)
            if command and "surfaceVerticalInset" in obj.bindings:
                if not re.fullmatch(r"0|\w+\.space[468]", obj.bindings["surfaceVerticalInset"][0]):
                    reject(obj, "command-inset", "command surface insets use the small shared spacing tokens", "surfaceVerticalInset")
            if command and "font.pixelSize" in obj.bindings and not (obj is nodes[0] and Path(path).stem in BUTTON_OWNERS):
                reject(obj, "command-typography", "font size belongs to the shared command", "font.pixelSize")
            if obj.kind == "font" and obj.parent and family(obj.parent.kind, COMMANDS) and "pixelSize" in obj.bindings:
                reject(obj, "command-typography", "grouped font sizes cannot override the shared command", "pixelSize")
            if obj.kind == "background" and obj.parent and (family(obj.parent.kind, set(SHARED)) or family(obj.parent.kind, COMMANDS) or family(obj.parent.kind, FIELDS)):
                reject(obj, "shared-background", "do not override shared background with a grouped property")
            if obj.kind == "border" and obj.parent and obj.parent.kind == "PopupSurface":
                reject(obj, "popup-surface-override", "grouped borders cannot override the shared surface")
            if popup:
                for key, (value, _) in obj.bindings.items():
                    if key in ("padding", "leftPadding", "rightPadding", "topPadding", "bottomPadding", "menuPopoverPadding", "standardPopupPadding"):
                        # Expressions are audited, not just numeric literals. Larger layout
                        # regions belong in content; popup edge insets have a bounded scale.
                        allowed = re.fullmatch(r"(?:0|(?:\w+\.)?(?:popupPadding|standardPopupPadding|menuPopoverPadding|space[468]|space10|space12|space16))", value)
                        allowed = allowed or re.fullmatch(r"(?:root\.)?docked\s*\?\s*\w+\.panelPaddingCompact\s*:\s*\w+\.popupPadding", value)
                        if not allowed:
                            reject(obj, "popup-inset", "use the shared popup inset or a compact menu/tooltip spacing token (maximum 16 at 100% scale)", key)
            if obj.kind == "PopupSurface" and Path(path).name != "PopupSurface.qml":
                for key in ("radius", "border.width", "border.color", "borderColor", "fillColor", "color"):
                    if key not in obj.bindings:
                        continue
                    value = obj.bindings[key][0]
                    approved = key == "radius" and re.fullmatch(r"\w+\.popupRadius", value)
                    if not approved:
                        reject(obj, "popup-surface-override", "popup geometry and palette belong to Theme/PopupSurface", key)
            if obj.kind == "InputFieldSurface" and Path(path).name != "InputFieldSurface.qml":
                for key in ("radius", "border.width", "border.color", "color"):
                    if key in obj.bindings:
                        reject(obj, "field-surface-override", "input geometry and palette belong to InputFieldSurface", key)
            if obj.kind == "CommandButtonSurface" and Path(path).name != "CommandButtonSurface.qml":
                for key in ("cornerRadius", "borderWidth", "borderColor"):
                    value = obj.bindings.get(key, ("", 0))[0]
                    if value and not (key == "cornerRadius" and re.fullmatch(r"\w+\.radiusControl", value)):
                        reject(obj, "command-surface-override", "command outlines and corners belong to CommandButtonSurface", key)
            if obj.bindings.get("objectName", ("", 0))[0] in ('"sharedCommandLabel"', '"sharedCommandCaption"', '"sharedChoiceLabel"', '"sharedChoiceCaption"'):
                if obj.bindings.get("fontSizeMode", ("", 0))[0] != "Text.FixedSize":
                    reject(obj, "command-typography", "command labels must keep their normal font size", "fontSizeMode")
            if obj.kind == "border" and obj.parent and obj.parent.kind == "InputFieldSurface":
                reject(obj, "field-surface-override", "grouped borders cannot override the shared input surface")
            if slider:
                for key, token in (("valueColumnWidth", "inspectorValueWidth"), ("resetColumnWidth", "inspectorResetWidth")):
                    value = obj.bindings.get(key, ("", 0))[0]
                    allowed = re.fullmatch(r"\w+\." + token, value)
                    if key == "valueColumnWidth":
                        # Only the compact HUD variant may select its narrower
                        # scaled value column. Other use-sites must use the
                        # shared Theme width rather than choosing their own.
                        variant_column = re.fullmatch(
                            r"(?:root\.)?styleVariant\s*===\s*\"compact-hud\"\s*\?\s*\w+\.scaled\(48\)\s*:\s*\w+\.inspectorValueWidth",
                            value,
                        )
                        allowed = allowed or (variant_column and Path(path).stem == "InspectorSlider" and obj is nodes[0])
                    if value and not allowed:
                        reject(obj, "inspector-column", "use the scaled shared value/reset columns", key)
            if field:
                hints = obj.bindings.get("inputMethodHints", ("", 0))[0]
                validator = obj.bindings.get("validator", ("", 0))[0]
                numeric = validator in ("DoubleValidator", "IntValidator") or "NumbersOnly" in hints or "DigitsOnly" in hints
                numeric_entry = obj.bindings.get("numericEntry", ("", 0))[0] == "true"
                if numeric and field == "ToneTextField" and not numeric_entry:
                    reject(obj, "numeric-typography", "numeric fields must opt into shared numericEntry styling")
                numeric_component = obj is nodes[0] and Path(path).stem == field and field in ("ToneTextField", "ToneSpinBox")
                if numeric_entry or field == "ToneSpinBox" or numeric_component:
                    family_value = obj.bindings.get("font.family", ("", 0))[0]
                    features_value = obj.bindings.get("font.features", ("", 0))[0]
                    compact_features_value = re.sub(r"\s+", "", features_value)
                    whole_font = obj.bindings.get("font", ("", 0))[0]
                    allowed_family = family_value == "theme.fontNumeric"
                    allowed_features = features_value == "theme.numericFontFeatures"
                    if numeric_component and field == "ToneTextField":
                        allowed_family = family_value == "root.numericEntry ? theme.fontNumeric : theme.fontUi"
                        allowed_features = compact_features_value == "root.numericEntry?theme.numericFontFeatures:()"
                    if numeric_component and field == "ToneSpinBox":
                        allowed_family = family_value == "theme.fontNumeric"
                        allowed_features = features_value == "theme.numericFontFeatures"
                    if family_value and not allowed_family:
                        reject(obj, "numeric-typography", "numeric values use Theme.fontNumeric", "font.family")
                    if features_value and not allowed_features:
                        reject(obj, "numeric-typography", "numeric values use Theme.numericFontFeatures", "font.features")
                    if whole_font:
                        reject(obj, "numeric-typography", "numeric fields inherit the shared face and tabular features", "font")
                    if numeric_component and (not allowed_family or not allowed_features):
                        missing = "font.family" if not allowed_family else "font.features"
                        reject(obj, "numeric-typography", "shared numeric components must bind the shared face and tabular features", missing)
            if obj.kind == "font" and obj.parent:
                parent_field = family(obj.parent.kind, FIELDS)
                parent_numeric = parent_field == "ToneSpinBox" or obj.parent.bindings.get("numericEntry", ("", 0))[0] == "true"
                if parent_numeric:
                    family_value = obj.bindings.get("family", ("", 0))[0]
                    features_value = obj.bindings.get("features", ("", 0))[0]
                    if family_value and family_value != "theme.fontNumeric":
                        reject(obj, "numeric-typography", "numeric values use Theme.fontNumeric", "family")
                    if features_value and features_value != "theme.numericFontFeatures":
                        reject(obj, "numeric-typography", "numeric values use Theme.numericFontFeatures", "features")
            if section:
                for key, expected in (("radius", "0"), ("border.width", "0"), ("color", '"transparent"')):
                    if key in obj.bindings and obj.bindings[key][0] != expected:
                        reject(obj, "flat-inspector", "inspector sections use flat shared grouping", key)
            if panel and not (Path(path).stem == "StudioPanel" and obj is nodes[0]):
                for key in ("radius", "border.width", "border.color", "color"):
                    if key in obj.bindings:
                        reject(obj, "shared-panel", "panel geometry and palette belong to StudioPanel/Theme", key)
            for key in obj.bindings:
                if re.fullmatch(r"(?:Controls\.)?ToolTip\.(?:visible|text|delay|timeout)", key):
                    reject(obj, "shared-tooltip", "use AppToolTip", key)
    violations = list(dict.fromkeys(violations))
    return {"counts": counts, "violations": violations, "passed": not violations}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    paths = sorted((args.root / "apps/desktop/qml").rglob("*.qml"))
    if not paths:
        parser.error("no production QML found")
    report = audit_sources({path.relative_to(args.root).as_posix(): path.read_text(encoding="utf-8")
                            for path in paths})
    report["qml_files"] = len(paths)
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

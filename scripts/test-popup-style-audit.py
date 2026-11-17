#!/usr/bin/env python3
"""Mutation coverage for the production QML style gate."""
import importlib.util
from pathlib import Path
import sys
import unittest

spec = importlib.util.spec_from_file_location('popup_audit', Path(__file__).with_name('audit-popup-system.py'))
audit = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = audit
spec.loader.exec_module(audit)


class StyleGateTest(unittest.TestCase):
    def check_source(self, source, rule=None, extras=None, path='apps/desktop/qml/workspaces/Example.qml'):
        sources = {path: source, **(extras or {})}
        report = audit.audit_sources(sources)
        if rule:
            self.assertTrue(any(f': {rule}:' in v for v in report['violations']), report)
        else:
            self.assertEqual(report['violations'], [])

    def test_shared_chrome_accepts_comments_strings_and_nested_javascript(self):
        self.check_source('''Item {
            // Dialog { background: Rectangle { radius: 99 } }
            property string help: "Popup { padding: 100 }"
            function options() { return { text: "Menu {", nested: {value: 1} }; }
            AppDialog { padding: theme.popupPadding; background: PopupSurface {} }
        }''')

    def test_stock_popup_families_cannot_bypass_shared_components(self):
        for kind in ('Popup', 'Dialog', 'Drawer', 'Menu', 'ToolTip', 'Controls.Popup'):
            with self.subTest(kind=kind):
                self.check_source(f'Item {{\n delegate: {kind} {{\n }}\n}}', 'shared-chrome')

    def test_custom_popup_subclass_cannot_hide_oversized_padding(self):
        self.check_source('Item {\n FancyDialog { padding: 40 }\n}', 'popup-inset',
                          {'apps/desktop/qml/components/FancyDialog.qml': 'AppDialog {}'})

    def test_inline_component_cannot_hide_custom_background(self):
        self.check_source('Item {\n component Fancy: AppPopup {\n background: Rectangle { radius: 24 }\n}\n}', 'shared-background')

    def test_padding_is_bounded_even_when_expressed_as_a_token(self):
        for value in ('48', 'theme.space32', 'theme.panelPaddingStandard', 'theme.space8 * 5'):
            with self.subTest(value=value):
                self.check_source(f'AppDialog {{\n padding: {value}\n}}', 'popup-inset')

    def test_surface_cannot_override_radius_borders_or_palette(self):
        for binding in ('radius: 20', 'border.width: 2', 'borderColor: "red"', 'color: "#404060"', 'fillColor: "blue"'):
            with self.subTest(binding=binding):
                self.check_source(f'AppPopup {{\n background: PopupSurface {{\n {binding}\n }}\n}}', 'popup-surface-override')

    def test_command_background_cannot_replace_shared_geometry(self):
        self.check_source('ActionButton {\n background: Rectangle { radius: 18 }\n}', 'shared-background')

    def test_stock_buttons_and_inline_alias_bypasses_are_rejected(self):
        self.check_source('Item {\n Button { text: "Continue" }\n}', 'shared-command')
        self.check_source('Item {\n component LocalCommand: ActionButton {}\n LocalCommand {\n background: Rectangle {}\n}\n}', 'shared-background')

    def test_qualified_stock_tool_button_cannot_bypass_shared_component(self):
        self.check_source('import QtQuick.Controls.Basic as Controls\nItem {\n Controls.ToolButton {}\n}', 'shared-command')

    def test_command_geometry_and_label_shrinking_are_rejected(self):
        self.check_source('CommandButtonSurface {\n cornerRadius: 20\n}', 'command-surface-override')
        self.check_source('CommandButtonSurface {\n borderWidth: 2\n}', 'command-surface-override')
        self.check_source('Text {\n objectName: "sharedCommandLabel"\n fontSizeMode: Text.HorizontalFit\n}', 'command-typography')

    def test_static_icon_names_must_exist(self):
        registry = {'apps/desktop/qml/components/AppIcon.qml': 'Canvas { function paint() { switch (name) { case "check": break; } } }'}
        self.check_source('AppIcon {\n name: "missing-icon"\n}', 'shared-icon', registry)
        self.check_source('AppIcon {\n name: "check"\n}', extras=registry)
        self.check_source('Item {\n property var actions: [{"icon": "missing-icon"}]\n}', 'shared-icon', registry)
        self.check_source('Item {\n property string role: square ? "icon" : "secondary"\n}', extras=registry)

    def test_command_font_overrides_cannot_bypass_shared_type(self):
        self.check_source('ActionButton {\n font.pixelSize: theme.typeCaption\n}', 'command-typography')
        self.check_source('ActionButton {\n font { pixelSize: 10 }\n}', 'command-typography')
        self.check_source('Item {\n component LocalTab: ActionButton {}\n LocalTab {\n font.pixelSize: 10\n}\n}', 'command-typography')

    def test_character_cannot_return_to_editor_or_loading_screens(self):
        self.check_source('Item {\n LooksyAgentCharacter {}\n}', 'agent-character-placement')
        self.check_source('Item {\n component LocalCharacter: LooksyAgentCharacter {}\n LocalCharacter {}\n}', 'agent-character-placement')
        self.check_source('Item {}', extras={
            'apps/desktop/qml/components/AgentWalkthroughDialog.qml': 'Item {\n LooksyAgentCharacter {}\n}'})

    def test_stock_inputs_cannot_bypass_shared_style(self):
        for kind in ('TextField', 'TextArea', 'SpinBox', 'ComboBox', 'Controls.TextField'):
            self.check_source(f'Item {{\n {kind} {{}}\n}}', 'shared-input')

    def test_choice_controls_cannot_bypass_shared_style(self):
        for kind in ('CheckBox', 'Switch', 'RadioButton', 'Controls.CheckBox'):
            self.check_source(f'Item {{\n {kind} {{}}\n}}', 'shared-choice')
        self.check_source('ToneSwitch {\n indicator: Rectangle {}\n}', 'choice-override')
        self.check_source('ToneCheckBox {\n font.pixelSize: 9\n}', 'choice-override')
        self.check_source('ToneCheckBox {\n font { pixelSize: 9 }\n}', 'choice-override')

    def test_body_text_uses_scalable_tokens_and_fixed_size(self):
        self.check_source('BodyLabel {\n font.pixelSize: 8\n}', 'body-typography')
        self.check_source('BodyLabel {\n font { pixelSize: 8 }\n}', 'body-typography')
        self.check_source('BodyLabel {\n fontSizeMode: Text.HorizontalFit\n}', 'body-typography')
        self.check_source('BodyLabel {\n font.pixelSize: theme.typeCaption\n}')

    def test_grouped_and_dotted_properties_cannot_bypass_policy(self):
        self.check_source('AppDialog {\n background.radius: 20\n}', 'shared-background')
        self.check_source('AppPopup {\n background { radius: 20 }\n}', 'shared-background')
        self.check_source('PopupSurface {\n border { width: 3 }\n}', 'popup-surface-override')

    def test_attached_tooltip_cannot_bypass_shared_style(self):
        self.check_source('Item {\n ToolTip.visible: hovered\n}', 'shared-tooltip')

    def test_input_backgrounds_must_use_shared_surface(self):
        for kind in ('ToneTextField', 'ToneTextArea', 'ToneSpinBox', 'ToneComboBox'):
            self.check_source(f'{kind} {{\n background: Rectangle {{ radius: 20 }}\n}}', 'shared-background')
        self.check_source('InputFieldSurface {\n color: "blue"\n}', 'field-surface-override')

    def test_inspector_columns_cannot_bypass_scaling(self):
        for value in ('48', '56', '80', 'theme.scaled(48)', 'theme.scaled(56)', 'theme.scaled(60)',
                      'root.styleVariant === "compact-hud" ? theme.scaled(48) : theme.inspectorValueWidth'):
            with self.subTest(value=value):
                self.check_source(f'InspectorSlider {{\n valueColumnWidth: {value}\n}}', 'inspector-column')
        self.check_source('InspectorSlider {\n resetColumnWidth: 28\n}', 'inspector-column')
        self.check_source('InspectorSlider {\n valueColumnWidth: theme.inspectorValueWidth\n}')
        self.check_source('''Item {
            property int valueColumnWidth: root.styleVariant === "compact-hud" ? theme.scaled(48) : theme.inspectorValueWidth
        }''', path='apps/desktop/qml/components/InspectorSlider.qml')

    def test_numeric_fields_must_use_numeric_typography(self):
        self.check_source('ToneTextField {\n validator: DoubleValidator {}\n}', 'numeric-typography')
        self.check_source('ToneTextField {\n numericEntry: true\n font.family: "Fancy font"\n}', 'numeric-typography')
        self.check_source('ToneTextField {\n numericEntry: true\n font.family: theme.fontMono\n}', 'numeric-typography')
        self.check_source('ToneTextField {\n numericEntry: true\n font.features: ({ "tnum": 0 })\n}', 'numeric-typography')
        self.check_source('ToneTextField {\n numericEntry: true\n font { family: "Fancy font" }\n}', 'numeric-typography')
        self.check_source('ToneTextField {\n numericEntry: true\n font: Qt.font({ family: "Fancy font" })\n}', 'numeric-typography')
        self.check_source('ToneTextField {\n numericEntry: true\n font.family: theme.fontNumeric\n font.features: theme.numericFontFeatures\n}')
        self.check_source('ToneSpinBox {\n font.family: theme.fontNumeric\n font.features: theme.numericFontFeatures\n}')
        self.check_source('ToneSpinBox {\n font.family: "Fancy font"\n}', 'numeric-typography')
        self.check_source('ToneSpinBox {\n font.features: ({ "tnum": 0 })\n}', 'numeric-typography')

    def test_shared_numeric_components_keep_the_face_and_tabular_features(self):
        field = 'TextField {\n id: root\n property bool numericEntry: false\n font.family: root.numericEntry ? theme.fontNumeric : theme.fontUi\n font.features: root.numericEntry ? theme.numericFontFeatures : ({})\n}'
        spin = 'SpinBox {\n font.family: theme.fontNumeric\n font.features: theme.numericFontFeatures\n}'
        self.check_source('Item {}', extras={
            'apps/desktop/qml/components/ToneTextField.qml': field,
            'apps/desktop/qml/components/ToneSpinBox.qml': spin,
        })
        self.check_source('Item {}', extras={
            'apps/desktop/qml/components/ToneTextField.qml': field.replace('theme.numericFontFeatures', '({ "tnum": 0 })'),
            'apps/desktop/qml/components/ToneSpinBox.qml': spin,
        }, rule='numeric-typography')
        self.check_source('Item {}', extras={
            'apps/desktop/qml/components/ToneTextField.qml': field,
            'apps/desktop/qml/components/ToneSpinBox.qml': spin.replace('font.features: theme.numericFontFeatures', ''),
        }, rule='numeric-typography')

    def test_inspector_sections_cannot_become_nested_cards(self):
        self.check_source('InspectorSection {\n radius: 16\n}', 'flat-inspector')
        self.check_source('InspectorSection {\n border.width: 1\n}', 'flat-inspector')
        self.check_source('StudioPanel {\n border.width: 1\n}', 'shared-panel')

    def test_native_menu_exception_is_structural_not_file_wide(self):
        valid = 'ApplicationWindow {\n menuBar: MenuBar {\n Menu {\n Menu {}\n}\n}\n}'
        self.assertTrue(audit.audit_sources({'apps/desktop/qml/Main.qml': valid})['passed'])
        invalid = 'ApplicationWindow {\n Menu {}\n}'
        self.assertFalse(audit.audit_sources({'apps/desktop/qml/Main.qml': invalid})['passed'])


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("inventory", Path(__file__).with_name("inventory-agent-app-commands.py"))
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)


class InventoryTest(unittest.TestCase):
    def test_nested_enum_does_not_steal_command_owner(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "apps/desktop/src"
            source.mkdir(parents=True)
            (source / "carousel_document_controller.h").write_text('''
class CarouselDocumentController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
 public:
  enum class ItemKind { Photo, Text };
  Q_INVOKABLE bool moveItem(const QString& id, const QVariantMap& options = {{"position", 2}});
  Q_INVOKABLE void setPoints(const QVariantList& points, bool enabled = true);
  // Q_INVOKABLE void neverExposeThisComment();
};
''')
            (source / "photo_mcp_server.cpp").write_text('  "photo.inspect_composition", "Inspect",\n')
            result = inventory.inventory(root)
            commands = result["commands"]
            self.assertEqual(len(commands), 3)
            self.assertTrue(all(command["owner"] == "CarouselDocumentController" for command in commands))
            self.assertTrue(all(command["registrationStatus"] == "unclassified" for command in commands))
            move = next(command for command in commands if command["name"] == "moveItem")
            self.assertEqual(move["arguments"][0]["schema"], {"type": "string"})
            self.assertFalse(move["arguments"][1]["required"])
            self.assertEqual(move["arguments"][1]["cppDefault"], '{{"position", 2}}')
            self.assertTrue(move["arguments"][1]["schema"]["needsNativeFieldSchema"])
            self.assertEqual(result["existingMcpTools"], ["photo.inspect_composition"])

    def test_commas_in_nested_defaults_are_not_parameters(self):
        result = inventory.split_arguments('const QVariantMap& options = {{"a", 1}, {"b", 2}}, QString text = "a,b", bool flag = false')
        self.assertEqual(len(result), 3)
        self.assertEqual(result[1], 'QString text = "a,b"')


if __name__ == "__main__":
    unittest.main()

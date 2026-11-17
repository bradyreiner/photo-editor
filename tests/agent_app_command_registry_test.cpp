#include "agent_app_command_registry.h"
#include "agent_design_schemas.h"

#include <QCoreApplication>
#include <iostream>

using photo_editor::desktop::AgentAppCommandRegistry;

class NativeFixture final : public QObject {
  Q_OBJECT
  Q_PROPERTY(double opacity READ opacity WRITE setOpacity)
 public:
  double opacity() const { return opacity_; }
  void setOpacity(double value) { opacity_ = std::clamp(value, 0.0, 1.0); }
  Q_INVOKABLE bool disabled() const { return false; }
  Q_INVOKABLE QVariant echo(const QVariant& value) const { return value; }
  Q_INVOKABLE int add(int first, int second = 5) { return first + second; }
  Q_INVOKABLE bool select(int index) { selected = index; return true; }
  Q_INVOKABLE bool configure(const QVariantMap& options) { configured = options; return true; }
  Q_INVOKABLE bool typography(const QVariantMap& changes) { configured = changes; return true; }
  Q_INVOKABLE int many(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, int k) {
    return a + b + c + d + e + f + g + h + i + j + k;
  }
  Q_INVOKABLE bool rejected() { return false; }
  Q_INVOKABLE bool exportFlattenedPhoto(const QString& file_path) const { ++const_export_calls; return !file_path.isEmpty(); }
  Q_INVOKABLE bool startPhotoLayerExport(const QString& file_path) { ++async_export_calls; return !file_path.isEmpty(); }
  Q_INVOKABLE QObject* internal() const { return nullptr; }
 signals:
  void internalSignal();
 public:
  int selected = -1;
  mutable int const_export_calls = 0;
  int async_export_calls = 0;
  QVariantMap configured;
 private:
  double opacity_ = 0.5;
};

#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  AgentAppCommandRegistry registry;
  NativeFixture fixture;
  int owned_mutations = 0;
  registry.registerObject("edit", &fixture, [](const QString& name, bool, bool) {
    AgentAppCommandRegistry::Policy policy;
    policy.available = name != QStringLiteral("exportFlattenedPhoto") &&
        name != QStringLiteral("startPhotoLayerExport");
    policy.resource = "photo";
    policy.reversal = "recipe-baseline";
    if (!policy.available)
      policy.reason = QStringLiteral("Photo output needs its task-owned asynchronous resource adapter.");
    if (name == "select") policy.arguments.insert("index", {"layerId", {{"type", "string"}},
        [](const QVariant& id) -> QVariant { return id == "stable-layer" ? QVariant(7) : QVariant{}; }});
    if (name == "configure") policy.arguments.insert("options", {"options", photo_editor::desktop::agent_shape_fill_schema(), {}});
    if (name == "typography") policy.arguments.insert("changes", {"changes", photo_editor::desktop::agent_typography_schema(), {}});
    return policy;
  }, [&owned_mutations](const QString&, const QVariantMap&, const auto& invoke) {
    ++owned_mutations;
    return invoke();
  });
  CHECK(registry.invoke("edit.method.disabled()", {}).value("ok").toBool(), "False getter is a valid result");
  CHECK(!registry.invoke("edit.method.disabled()", {}).value("result").toBool(), "Preserve false result");
  CHECK(owned_mutations == 0, "Getters do not acquire mutation ownership");
  CHECK(registry.invoke("edit.method.echo(QVariant)", {{"value", "kept"}}).value("result") == "kept", "QVariant returns retain their payload");
  CHECK(registry.invoke("edit.method.add(int,int)", {{"first", 3}}).value("result") == 8, "Use actual native default argument clone");
  CHECK(!registry.invoke("edit.method.add(int,int)", {{"second", 3}}).value("ok").toBool(), "Reject missing non-trailing argument");
  const auto mutations_before_invalid = owned_mutations;
  CHECK(!registry.invoke("edit.method.add(int,int)", {{"first", "3"}}).value("ok").toBool(), "No string-to-number coercion");
  CHECK(!registry.invoke("edit.method.add(int,int)", {{"first", 1e20}}).value("ok").toBool(), "Reject native integer overflow");
  CHECK(!registry.invoke("edit.method.add(int,int)", {{"first", 3}, {"extra", 1}}).value("ok").toBool(), "Reject unknown keys");
  CHECK(!registry.invoke("edit.method.select(int)", {{"layerId", "deleted"}}).value("ok").toBool(), "Resolve stable IDs before any mutation");
  CHECK(owned_mutations == mutations_before_invalid, "Invalid calls cannot mutate");
  const auto stale = registry.invoke("edit.method.select(int)", {{"layerId", "deleted"}});
  CHECK(stale.value("recoverableError").toBool() && stale.value("mutationNotApplied").toBool(),
      "Pre-dispatch rejection can return to planning without replaying completed work");
  CHECK(registry.invoke("edit.method.select(int)", {{"layerId", "stable-layer"}}).value("ok").toBool() && fixture.selected == 7, "Resolve current native index from stable ID");
  const auto properties = registry.schema("edit.method.select(int)").value("inputSchema").toMap().value("properties").toMap();
  CHECK(properties.contains("layerId") && !properties.contains("index"), "Discovery and invocation use the same stable ID schema");
  QVariantMap many;
  for (char letter = 'a'; letter <= 'k'; ++letter) many.insert(QString(QChar::fromLatin1(letter)), 1);
  CHECK(registry.invoke("edit.method.many(int,int,int,int,int,int,int,int,int,int,int)", many).value("result") == 11, "Dispatch native controls with more than ten parameters");
  CHECK(registry.invoke("edit.property.opacity", {{"value", 0.8}}).value("ok").toBool() && fixture.opacity() == 0.8, "Writable controls use the same mutation owner");
  CHECK(registry.invoke("edit.property.opacity", {{"value", 0.8}}).value("alreadySatisfied").toBool(), "Properties report verified no-op state");
  CHECK(!registry.invoke("edit.property.opacity", {{"value", 5.0}}).value("ok").toBool(), "Constrained property cannot claim requested value applied");
  CHECK(!registry.invoke("edit.method.rejected()", {}).value("ok").toBool(), "Native mutation rejection remains a failure");
  CHECK(!registry.invoke("edit.method.rejected()", {}).value("recoverableError").toBool(),
      "Reflection alone cannot certify an invoked mutation's failure as safe to retry");
  CHECK(!registry.schema("edit.method.internal()").value("available").toBool(), "Unsupported native pointer return is not a tool");
  CHECK(!registry.schema("edit.method.internalSignal()").value("ok").toBool(), "QObject signals are not commands");
  CHECK(!registry.schema("edit.method.deleteLater()").value("ok").toBool(), "QObject lifecycle is not exposed");
  CHECK(!registry.schema("edit.method.exportFlattenedPhoto(QString)").value("available").toBool() &&
      !registry.schema("edit.method.startPhotoLayerExport(QString)").value("available").toBool() &&
      fixture.const_export_calls == 0 && fixture.async_export_calls == 0,
      "Reflected synchronous and unowned asynchronous export methods stay unavailable");
  CHECK(registry.commands("edit", "add", 0, 1).value("commands").toList().size() == 1, "Discovery is paginated and filterable");
  const auto configured_count = owned_mutations;
  for (const auto& invalid : QVariantList{QVariantMap{{"fillMode", "unsupported"}},
      QVariantMap{{"gradientCenterX", 1.1}}, QVariantMap{{"gradientCenterY", "0.5"}},
      QVariantMap{{"gradientCentreX", 0.5}}, QVariantMap{}}) {
    const auto rejection = registry.invoke("edit.method.configure(QVariantMap)", {{"options", invalid}});
    CHECK(!rejection.value("ok").toBool() && rejection.value("mutationNotApplied").toBool(),
        "Reject invalid nested design semantics before native invocation");
  }
  CHECK(owned_mutations == configured_count && fixture.configured.isEmpty(), "Semantic rejection cannot acquire ownership or mutate");
  CHECK(registry.invoke("edit.method.configure(QVariantMap)", {{"options", QVariantMap{
      {"fillMode", "radial"}, {"gradientCenterX", 0.5}, {"gradientRadius", 1.0}}}}).value("ok").toBool(),
      "Admit native partial updates using normalized gradient coordinates");
  const auto fill_schema = registry.schema("edit.method.configure(QVariantMap)").value("inputSchema").toMap()
      .value("properties").toMap().value("options").toMap();
  CHECK(fill_schema.value("properties").toMap().value("fillMode").toMap().value("enum").toStringList().contains("texture"),
      "Discovery carries the same native fill semantics as dispatch");
  CHECK(!registry.invoke("edit.method.typography(QVariantMap)", {{"changes", QVariantMap{{"pathPoints", QVariantList{
      QVariantMap{{"x", 0.2}, {"y", 0.3}}, QVariantMap{{"x", 0.4}, {"y", 2.0}}}}}}}).value("ok").toBool(),
      "Reject out-of-range nested contour coordinates");
  CHECK(!registry.invoke("edit.method.typography(QVariantMap)", {{"changes", QVariantMap{{"axes", QVariantMap{{"wght", "heavy"}}}}}}).value("ok").toBool(),
      "Font axis values require actual numbers");
  CHECK(registry.invoke("edit.method.typography(QVariantMap)", {{"changes", QVariantMap{{"size", 96.0},
      {"weight", 700}, {"axes", QVariantMap{{"wght", 650.0}}}}}}).value("ok").toBool(),
      "Admit semantic typography updates without forcing complete style objects");

  const QVariantMap output_schema{
      {"type", "object"},
      {"properties", QVariantMap{
          {"assetId", QVariantMap{{"type", "string"}, {"minLength", 1}}},
          {"operation", QVariantMap{{"type", "string"}, {"enum", QStringList{"flattened", "layer"}}}},
          {"outputPath", QVariantMap{{"type", "string"}, {"minLength", 1}}},
          {"layerId", QVariantMap{{"type", "string"}, {"minLength", 1}}},
      }},
      {"required", QStringList{"assetId", "operation", "outputPath"}},
      {"additionalProperties", false},
  };
  AgentAppCommandRegistry::Policy output_policy;
  output_policy.available = true;
  output_policy.resource = QStringLiteral("agent-created-photo-output");
  output_policy.completion = QStringLiteral("asynchronous-terminal-receipt");
  output_policy.reversal = QStringLiteral("exact-created-file");
  bool invocation_authorized = true;
  int authorization_checks = 0;
  output_policy.authorize = [&invocation_authorized, &authorization_checks]() {
    ++authorization_checks;
    return invocation_authorized
        ? QVariantMap{{QStringLiteral("ok"), true}}
        : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("The task is no longer approved.")}};
  };
  int output_mutations = 0;
  int output_handler_calls = 0;
  QVariantMap output_handler_values;
  const auto output_owner = [&output_mutations](const QString& command_id, const QVariantMap& values,
      const AgentAppCommandRegistry::Invocation& invoke) {
    ++output_mutations;
    auto receipt = invoke();
    receipt.insert(QStringLiteral("ownerCommandId"), command_id);
    receipt.insert(QStringLiteral("ownerSawOutputPath"), values.value(QStringLiteral("outputPath")));
    return receipt;
  };
  const auto output_handler = [&output_handler_calls, &output_handler_values](const QVariantMap& values) {
    ++output_handler_calls;
    output_handler_values = values;
    return QVariantMap{
        {QStringLiteral("ok"), true}, {QStringLiteral("accepted"), true},
        {QStringLiteral("pending"), true}, {QStringLiteral("taskId"), QStringLiteral("task-7")},
        {QStringLiteral("resourceId"), QStringLiteral("output:task-7:proof.jpg")},
        {QStringLiteral("taskStepId"), 14},
    };
  };
  CHECK(registry.registerAction(QStringLiteral("edit"), QStringLiteral("startPhotoOutput"), &fixture,
      output_schema, output_policy, output_owner, output_handler),
      "Register a custom action through the existing resource-owner registry");
  CHECK(!registry.registerAction(QStringLiteral("edit"), QStringLiteral("startPhotoOutput"), &fixture,
      output_schema, output_policy, output_owner, output_handler), "A duplicate custom action cannot replace its handler");
  const auto output_id = QStringLiteral("edit.action.startPhotoOutput");
  const auto output_description = registry.schema(output_id);
  CHECK(output_description.value(QStringLiteral("available")).toBool() &&
      output_description.value(QStringLiteral("kind")).toString() == QStringLiteral("action") &&
      output_description.value(QStringLiteral("completion")).toString() == QStringLiteral("asynchronous-terminal-receipt") &&
      output_description.value(QStringLiteral("resource")).toString() == QStringLiteral("agent-created-photo-output") &&
      output_description.value(QStringLiteral("reversal")).toString() == QStringLiteral("exact-created-file"),
      "Discovery describes a task-owned asynchronous action and its reversal contract");
  CHECK(output_description.value(QStringLiteral("inputSchema")).toMap() == output_schema,
      "Discovery publishes the exact typed action schema used by invocation");
  const auto listed_output = registry.commands(QStringLiteral("edit"), QStringLiteral("action.startPhotoOutput"))
      .value(QStringLiteral("commands")).toList();
  CHECK(listed_output.size() == 1 && listed_output.front().toMap().value(QStringLiteral("kind")) == QStringLiteral("action") &&
      listed_output.front().toMap().value(QStringLiteral("invokeTool")) == QStringLiteral("app.invoke_command"),
      "Custom actions use the existing paginated command discovery and invoke tool");
  const QVariantMap output_values{
      {QStringLiteral("assetId"), QStringLiteral("asset-2")},
      {QStringLiteral("operation"), QStringLiteral("flattened")},
      {QStringLiteral("outputPath"), QStringLiteral("/exports/proof.jpg")},
  };
  for (const auto& invalid_output : QVariantList{
      QVariantMap{{QStringLiteral("assetId"), QStringLiteral("asset-2")},
          {QStringLiteral("operation"), QStringLiteral("flattened")}},
      QVariantMap{{QStringLiteral("assetId"), QStringLiteral("asset-2")},
          {QStringLiteral("operation"), QStringLiteral("video")},
          {QStringLiteral("outputPath"), QStringLiteral("/exports/proof.jpg")}},
      QVariantMap{{QStringLiteral("assetId"), QStringLiteral("asset-2")},
          {QStringLiteral("operation"), QStringLiteral("flattened")},
          {QStringLiteral("outputPath"), QStringLiteral("/exports/proof.jpg")},
          {QStringLiteral("overwrite"), true}},
      QVariantMap{{QStringLiteral("assetId"), 22},
          {QStringLiteral("operation"), QStringLiteral("flattened")},
          {QStringLiteral("outputPath"), QStringLiteral("/exports/proof.jpg")}},
  }) {
    const auto rejected_action = registry.invoke(output_id, invalid_output.toMap());
    CHECK(!rejected_action.value(QStringLiteral("ok")).toBool() &&
        rejected_action.value(QStringLiteral("recoverableError")).toBool() &&
        rejected_action.value(QStringLiteral("mutationNotApplied")).toBool(),
        "Custom action schema rejects missing, unknown, invalid-enum and mistyped arguments before ownership");
  }
  CHECK(output_mutations == 0 && output_handler_calls == 0,
      "Invalid custom actions never enter the task owner or handler");
  invocation_authorized = false;
  const auto denied_output = registry.invoke(output_id, output_values);
  CHECK(!denied_output.value(QStringLiteral("ok")).toBool() &&
      denied_output.value(QStringLiteral("error")).toString() == QStringLiteral("The task is no longer approved."),
      "Custom actions re-evaluate task authorization at invocation time");
  CHECK(output_mutations == 0 && output_handler_calls == 0,
      "Denied custom actions never enter the resource owner");
  invocation_authorized = true;
  const auto output_receipt = registry.invoke(output_id, output_values);
  CHECK(output_receipt.value(QStringLiteral("ok")).toBool() &&
      output_receipt.value(QStringLiteral("accepted")).toBool() &&
      output_receipt.value(QStringLiteral("pending")).toBool() &&
      output_receipt.value(QStringLiteral("resourceId")).toString() == QStringLiteral("output:task-7:proof.jpg") &&
      output_receipt.value(QStringLiteral("taskStepId")).toInt() == 14 &&
      output_receipt.value(QStringLiteral("ownerCommandId")).toString() == output_id,
      "The handler outcome and async receipt fields pass through the same mutation-owner dispatch");
  CHECK(output_handler_calls == 1 && output_mutations == 1 &&
      output_handler_values == output_values && authorization_checks == 6,
      "Invocation uses validated values once and runs the current authorization evaluator");

  const AgentAppCommandRegistry::Invocation legacy_invocation = []() {
    return QVariantMap{{QStringLiteral("ok"), true},
                       {QStringLiteral("path"), QStringLiteral("legacy-no-arg")}};
  };
  CHECK(legacy_invocation &&
            legacy_invocation().value(QStringLiteral("path")).toString() ==
                QStringLiteral("legacy-no-arg") &&
            legacy_invocation.withPreimage(QByteArrayLiteral("ignored"))
                    .value(QStringLiteral("path")).toString() ==
                QStringLiteral("legacy-no-arg"),
        "The scoped preimage wrapper preserves bool checks and no-argument Invocation behavior");

  AgentAppCommandRegistry preimage_registry;
  NativeFixture preimage_fixture;
  auto preimage_policy = output_policy;
  preimage_policy.resource = QStringLiteral("timeline-document");
  preimage_policy.reversal = QStringLiteral("exact-task-baseline");
  QByteArray observed_task_preimage;
  auto preimage_action_calls = 0;
  const auto preimage_owner = [](const QString&, const QVariantMap&,
      const AgentAppCommandRegistry::Invocation& invoke) {
    return invoke.withPreimage(QByteArrayLiteral("journal-preimage"));
  };
  CHECK(preimage_registry.registerActionWithPreimage(
      QStringLiteral("timeline"), QStringLiteral("preimageAction"),
      &preimage_fixture, output_schema, preimage_policy, preimage_owner,
      [&observed_task_preimage, &preimage_action_calls](
          const QVariantMap&, const QByteArray& preimage) {
        ++preimage_action_calls;
        observed_task_preimage = preimage;
        return QVariantMap{{QStringLiteral("ok"), true},
                           {QStringLiteral("preimageBytes"), preimage.size()}};
      }),
      "Register the one action that can consume the journal's scoped preimage");
  auto invalid_preimage_values = output_values;
  invalid_preimage_values.remove(QStringLiteral("outputPath"));
  CHECK(!preimage_registry.invoke(QStringLiteral("timeline.action.preimageAction"),
              invalid_preimage_values).value(QStringLiteral("ok")).toBool() &&
            preimage_action_calls == 0,
        "The scoped action still rejects invalid values before its interceptor or handler runs");
  invocation_authorized = false;
  CHECK(!preimage_registry.invoke(QStringLiteral("timeline.action.preimageAction"),
              output_values).value(QStringLiteral("ok")).toBool() &&
            preimage_action_calls == 0,
        "Invocation-time authorization still blocks the scoped action before preimage dispatch");
  invocation_authorized = true;
  const auto preimage_receipt = preimage_registry.invoke(
      QStringLiteral("timeline.action.preimageAction"), output_values);
  CHECK(preimage_receipt.value(QStringLiteral("ok")).toBool() &&
            preimage_receipt.value(QStringLiteral("preimageBytes")).toInt() ==
                QByteArrayLiteral("journal-preimage").size() &&
            observed_task_preimage == QByteArrayLiteral("journal-preimage") &&
            preimage_action_calls == 1,
        "The action interceptor forwards exact bytes only through withPreimage()");

  const auto fallback_owner = [](const QString&, const QVariantMap&,
      const AgentAppCommandRegistry::Invocation& invoke) { return invoke(); };
  const auto fallback_action = QStringLiteral("timeline.action.preimageFallback");
  CHECK(preimage_registry.registerActionWithPreimage(
      QStringLiteral("timeline"), QStringLiteral("preimageFallback"),
      &preimage_fixture, output_schema, preimage_policy, fallback_owner,
      [&observed_task_preimage, &preimage_action_calls](
          const QVariantMap&, const QByteArray& preimage) {
        ++preimage_action_calls;
        observed_task_preimage = preimage;
        return QVariantMap{{QStringLiteral("ok"), true},
                           {QStringLiteral("usedFreshFallback"), preimage.isEmpty()}};
      }),
      "Register a scoped action with a no-preimage compatibility fallback");
  const auto fallback_receipt = preimage_registry.invoke(fallback_action, output_values);
  CHECK(fallback_receipt.value(QStringLiteral("ok")).toBool() &&
            fallback_receipt.value(QStringLiteral("usedFreshFallback")).toBool() &&
            observed_task_preimage.isEmpty() && preimage_action_calls == 2,
        "Legacy no-argument action interception uses the explicit fresh-state fallback");

  CHECK(registry.registerAction(QStringLiteral("edit"), QStringLiteral("missingReceipt"), &fixture,
      output_schema, output_policy, output_owner, [](const QVariantMap&) {
        return QVariantMap{{QStringLiteral("accepted"), true}};
      }), "A custom descriptor can register for handler contract validation");
  const auto invalid_receipt = registry.invoke(QStringLiteral("edit.action.missingReceipt"), output_values);
  CHECK(!invalid_receipt.value(QStringLiteral("ok")).toBool() &&
      invalid_receipt.value(QStringLiteral("recoveryRequired")).toBool() &&
      invalid_receipt.value(QStringLiteral("receiptInvalid")).toBool() &&
      !invalid_receipt.value(QStringLiteral("mutationNotApplied")).toBool(),
      "A post-dispatch handler without an outcome receipt is uncertain, never marked safe to replay");

  AgentAppCommandRegistry unowned_action_registry;
  CHECK(unowned_action_registry.registerAction(QStringLiteral("edit"), QStringLiteral("unownedOutput"), &fixture,
      output_schema, output_policy, {}, output_handler), "Register unavailable custom action metadata without an owner");
  CHECK(!unowned_action_registry.schema(QStringLiteral("edit.action.unownedOutput")).value(QStringLiteral("available")).toBool() &&
      !unowned_action_registry.invoke(QStringLiteral("edit.action.unownedOutput"), output_values).value(QStringLiteral("ok")).toBool(),
      "Custom actions cannot bypass the registered resource and reversal owner requirement");

  AgentAppCommandRegistry composed_registry;
  NativeFixture composed_fixture;
  int child_owner_calls = 0;
  int child_authorization_calls = 0;
  int child_value_authorization_calls = 0;
  int child_action_calls = 0;
  const QVariantMap composed_schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), QVariantMap{
          {QStringLiteral("value"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
              {QStringLiteral("minLength"), 1}}}}},
      {QStringLiteral("required"), QStringList{QStringLiteral("value")}},
      {QStringLiteral("additionalProperties"), false}};
  AgentAppCommandRegistry::Policy composed_policy;
  composed_policy.available = true;
  composed_policy.resource = QStringLiteral("timeline-document");
  composed_policy.reversal = QStringLiteral("task-baseline");
  composed_policy.authorize = [&child_authorization_calls]() {
    ++child_authorization_calls;
    return QVariantMap{{QStringLiteral("ok"), true}};
  };
  composed_policy.authorize_values = [&child_value_authorization_calls](const QVariantMap& values) {
    ++child_value_authorization_calls;
    return values.value(QStringLiteral("value")).toString() == QStringLiteral("allowed")
        ? QVariantMap{{QStringLiteral("ok"), true}}
        : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("child values denied")}};
  };
  const auto composed_owner = [&child_owner_calls](const QString&, const QVariantMap&,
      const AgentAppCommandRegistry::Invocation& invoke) {
    ++child_owner_calls;
    return invoke();
  };
  CHECK(composed_registry.registerAction(QStringLiteral("timeline"), QStringLiteral("typedChild"),
      &composed_fixture, composed_schema, composed_policy, composed_owner,
      [&child_action_calls](const QVariantMap& values) {
        ++child_action_calls;
        return QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("stableId"), values.value(QStringLiteral("value"))}};
      }), "Register a child action for one parent-owned batch");
  const auto composed_id = QStringLiteral("timeline.action.typedChild");
  CHECK(composed_registry.actionInputSchema(composed_id).value(QStringLiteral("inputSchema")).toMap() == composed_schema,
      "A parent action can retrieve the exact child schema before preparing its transaction");
  CHECK(!composed_registry.actionInputSchema(QStringLiteral("edit.method.add(int,int)")).value(QStringLiteral("ok")).toBool(),
      "Composed dispatch does not accept reflected methods");
  CHECK(!composed_registry.invokeActionWithinMutation(composed_id,
      {{QStringLiteral("value"), 5}}).value(QStringLiteral("ok")).toBool() &&
      child_authorization_calls == 0 && child_value_authorization_calls == 0 && child_action_calls == 0,
      "Composed child schemas reject mistyped values before native authorization or action");
  const auto denied_composed = composed_registry.invokeActionWithinMutation(composed_id,
      {{QStringLiteral("value"), QStringLiteral("denied")}});
  CHECK(!denied_composed.value(QStringLiteral("ok")).toBool() && child_action_calls == 0 &&
      child_authorization_calls == 1 && child_value_authorization_calls == 1,
      "Composed dispatch retains both native authorization gates before mutation");
  const auto composed_result = composed_registry.invokeActionWithinMutation(composed_id,
      {{QStringLiteral("value"), QStringLiteral("allowed")}});
  CHECK(composed_result.value(QStringLiteral("ok")).toBool() &&
      composed_result.value(QStringLiteral("commandId")).toString() == composed_id &&
      composed_result.value(QStringLiteral("stableId")).toString() == QStringLiteral("allowed") &&
      child_action_calls == 1 && child_owner_calls == 0,
      "Composed child actions invoke their typed handler without opening a second mutation owner");

  AgentAppCommandRegistry unowned;
  unowned.registerObject("edit", &fixture, [](const QString&, bool, bool) { AgentAppCommandRegistry::Policy p; p.available = true; return p; });
  CHECK(!unowned.invoke("edit.method.add(int,int)", {{"first", 1}}).value("ok").toBool(), "Mutation requires a resource owner and reversal adapter");
  auto* closing = new NativeFixture;
  registry.registerObject("closed", closing, [](const QString&, bool, bool) { AgentAppCommandRegistry::Policy p; p.available = true; return p; });
  delete closing;
  CHECK(!registry.invoke("closed.method.disabled()", {}).value("ok").toBool(), "Closed documents cannot be invoked");
  std::cout << "Native registry types, defaults, stable IDs, ownership and rejection passed.\n";
  return 0;
}

#include "agent_app_command_registry_test.moc"

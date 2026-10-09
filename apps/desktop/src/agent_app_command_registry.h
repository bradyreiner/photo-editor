#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace photo_editor::desktop {

// Discovery and invocation share one native descriptor. Qt reflection supplies
// types, not authorization: native owners must classify each mutation and route
// it through their resource journal. QObject lifecycle/signals are never tools.
class AgentAppCommandRegistry final {
 public:
  using Invocation = std::function<QVariantMap()>;
  using MutationOwner = std::function<QVariantMap(const QString&, const QVariantMap&, const Invocation&)>;
  using ActionHandler = std::function<QVariantMap(const QVariantMap&)>;
  struct Argument final {
    QString name;
    QVariantMap schema;
    std::function<QVariant(const QVariant&)> resolve;
  };
  struct Policy final {
    bool available = false;
    QString resource;
    QString completion = QStringLiteral("synchronous");
    QString reversal;
    QString reason = QStringLiteral("The native resource/cancellation/reversal contract is not registered yet.");
    QHash<QString, Argument> arguments;
    std::function<bool(const QVariantMap&)> confirm_satisfied;
    std::function<QVariantMap()> authorize;
  };
  using Classifier = std::function<Policy(const QString&, bool, bool)>;

  // Register a task-owned action that does not correspond to a reflected
  // QObject method. It shares the registry's schema validation, invocation-time
  // authorization and MutationOwner path. The handler must return an explicit
  // outcome receipt in a QVariantMap with an `ok` field; async owners can also
  // return accepted/pending/resource/step fields without the registry
  // interpreting or collapsing them.
  bool registerAction(const QString& domain, const QString& name, QObject* owner,
      const QVariantMap& input_schema, const Policy& policy,
      const MutationOwner& mutate, const ActionHandler& action) {
    if (domain.trimmed().isEmpty() || name.trimmed().isEmpty() || !owner ||
        !isStrictObjectSchema(input_schema)) return false;
    Entry entry;
    entry.object = owner;
    entry.domain = domain;
    entry.name = name;
    entry.id = domain + QStringLiteral(".action.") + name;
    entry.action = action;
    entry.input_schema = input_schema;
    entry.policy = policy;
    entry.policy.available = policy.available && static_cast<bool>(action) &&
        static_cast<bool>(mutate) && !policy.resource.trimmed().isEmpty() &&
        !policy.reversal.trimmed().isEmpty();
    if (policy.available && !entry.policy.available)
      entry.policy.reason = QStringLiteral("A task-owned action requires a handler, resource owner and reversal contract.");
    entry.mutate = mutate;
    if (entries_.contains(entry.id)) return false;
    entries_.insert(entry.id, entry);
    return true;
  }

  void registerObject(const QString& domain, QObject* object, const Classifier& classify,
                      const MutationOwner& mutate = {}) {
    if (!object || domain.isEmpty()) return;
    const auto* meta = object->metaObject();
    for (int index = meta->methodOffset(); index < meta->methodCount(); ++index) {
      const auto method = meta->method(index);
      if (method.access() != QMetaMethod::Public ||
          (method.methodType() != QMetaMethod::Method && method.methodType() != QMetaMethod::Slot) ||
          (method.attributes() & QMetaMethod::Cloned) != 0) continue;
      const auto name = QString::fromLatin1(method.name());
      auto policy = classify(name, false, method.isConst());
      Entry entry;
      entry.object = object;
      entry.domain = domain;
      entry.name = name;
      entry.method = index;
      entry.read_only = method.isConst();
      entry.policy = policy;
      entry.mutate = mutate;
      entry.id = domain + QStringLiteral(".method.") + QString::fromLatin1(method.methodSignature());
      entry.minimum_arguments = method.parameterCount();
      for (int clone_index = index + 1; clone_index < meta->methodCount(); ++clone_index) {
        const auto clone = meta->method(clone_index);
        if ((clone.attributes() & QMetaMethod::Cloned) == 0 || clone.name() != method.name()) break;
        entry.minimum_arguments = std::min(entry.minimum_arguments, clone.parameterCount());
      }
      const auto names = method.parameterNames();
      for (int parameter = 0; parameter < method.parameterCount(); ++parameter) {
        const auto native_name = QString::fromLatin1(names.at(parameter));
        const auto replacement = policy.arguments.value(native_name);
        const auto schema = replacement.schema.isEmpty() ? typeSchema(method.parameterMetaType(parameter)) : replacement.schema;
        if (native_name.isEmpty() || schema.isEmpty() || typeSchema(method.parameterMetaType(parameter)).isEmpty()) {
          policy.available = false;
          entry.policy.reason = QStringLiteral("A native parameter has no supported typed JSON representation.");
        }
        entry.native_names.append(native_name);
        entry.argument_names.append(replacement.name.isEmpty() ? native_name : replacement.name);
        entry.argument_schemas.append(schema);
      }
      if (method.returnMetaType().id() != QMetaType::Void && typeSchema(method.returnMetaType()).isEmpty()) {
        policy.available = false;
        entry.policy.reason = QStringLiteral("The native return value has no supported typed JSON representation.");
      }
      entry.policy.available = policy.available && (entry.read_only || (mutate && !policy.resource.isEmpty() && !policy.reversal.isEmpty()));
      entries_.insert(entry.id, entry);
    }
    for (int index = meta->propertyOffset(); index < meta->propertyCount(); ++index) {
      const auto property = meta->property(index);
      if (!property.isWritable()) continue;
      const auto name = QString::fromLatin1(property.name());
      const auto policy = classify(name, true, false);
      Entry entry;
      entry.object = object;
      entry.domain = domain;
      entry.name = name;
      entry.id = domain + QStringLiteral(".property.") + name;
      entry.property = index;
      entry.policy = policy;
      entry.policy.available = policy.available && mutate && !policy.resource.isEmpty() && !policy.reversal.isEmpty();
      entry.mutate = mutate;
      entry.minimum_arguments = 1;
      entry.native_names = {QStringLiteral("value")};
      entry.argument_names = {QStringLiteral("value")};
      entry.argument_schemas = {typeSchema(property.metaType())};
      if (entry.argument_schemas.first().toMap().isEmpty()) entry.policy.available = false;
      entries_.insert(entry.id, entry);
    }
  }

  QVariantMap commands(const QString& domain = {}, const QString& query = {}, int offset = 0, int limit = 24) const {
    QVariantList rows;
    auto ids = entries_.keys();
    std::sort(ids.begin(), ids.end());
    int matched = 0;
    for (const auto& id : ids) {
      const auto& entry = entries_[id];
      if ((!domain.isEmpty() && entry.domain != domain) ||
          (!query.isEmpty() && !id.contains(query, Qt::CaseInsensitive))) continue;
      if (matched >= std::max(0, offset) && rows.size() < std::clamp(limit, 1, 64)) {
        auto row = describe(entry, false);
        rows.append(row);
      }
      ++matched;
    }
    return {{"ok", true}, {"commands", rows}, {"totalCount", matched},
        {"nextOffset", offset + rows.size() < matched ? QVariant(offset + rows.size()) : QVariant{}},
        {"contract", "agent-native-command-registry-v1"}};
  }

  QVariantMap schema(const QString& id) const {
    if (!entries_.contains(id)) return correctableFailure(QStringLiteral("Unknown native command ID."));
    auto result = describe(entries_.value(id), true);
    result.insert(QStringLiteral("ok"), true);
    return result;
  }

  // Composition tools share the registry's strict partial-update validation.
  static bool valueMatchesSchema(const QVariant& value, const QVariantMap& schema) {
    return validValue(value, schema);
  }

  QVariantMap invoke(const QString& id, const QVariantMap& values) const {
    if (!entries_.contains(id)) return correctableFailure(QStringLiteral("Unknown native command ID."));
    const auto entry = entries_.value(id);
    if (!entry.object || entry.object->thread() != QThread::currentThread())
      return failure(QStringLiteral("The native command owner is unavailable on this workspace thread."));
    if (!entry.policy.available) return correctableFailure(entry.policy.reason);
    if (entry.policy.authorize) {
      const auto admission = entry.policy.authorize();
      if (!admission.value("ok").toBool()) return admission;
    }
    if (entry.action) {
      if (!validValue(values, entry.input_schema))
        return correctableFailure(QStringLiteral("Supply values that match the command's typed input schema."));
      const Invocation action = [entry, values]() {
        if (!entry.object) return failure(QStringLiteral("The native action owner closed before invocation."));
        auto receipt = entry.action(values);
        if (!receipt.contains(QStringLiteral("ok"))) {
          return QVariantMap{{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("The native action returned no outcome receipt.")},
              {QStringLiteral("recoveryRequired"), true},
              {QStringLiteral("receiptInvalid"), true}};
        }
        return receipt;
      };
      auto result = entry.mutate(id, values, action);
      if (!result.contains(QStringLiteral("ok"))) {
        result = {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("The task owner returned no action outcome receipt.")},
            {QStringLiteral("recoveryRequired"), true},
            {QStringLiteral("receiptInvalid"), true}};
      }
      result.insert(QStringLiteral("commandId"), id);
      return result;
    }
    const auto count = values.size();
    if (count < entry.minimum_arguments || count > entry.argument_names.size())
      return correctableFailure(QStringLiteral("Supply the command's required arguments and optional trailing arguments."));
    for (auto it = values.cbegin(); it != values.cend(); ++it)
      if (!entry.argument_names.contains(it.key())) return correctableFailure(QStringLiteral("Unknown native argument: %1").arg(it.key()));
    const auto* meta = entry.object->metaObject();
    QMetaMethod method;
    if (entry.method >= 0) {
      method = meta->method(entry.method);
      if (count != method.parameterCount()) {
        bool found = false;
        for (int index = entry.method + 1; index < meta->methodCount(); ++index) {
          const auto candidate = meta->method(index);
          if ((candidate.attributes() & QMetaMethod::Cloned) == 0 || candidate.name() != method.name()) break;
          if (candidate.parameterCount() == count) { method = candidate; found = true; break; }
        }
        if (!found) return correctableFailure(QStringLiteral("No native overload accepts these arguments."));
      }
    }
    std::vector<QVariant> arguments;
    arguments.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
      const auto name = entry.argument_names.at(index);
      if (!values.contains(name)) return correctableFailure(QStringLiteral("Optional arguments must follow the native parameter order."));
      const auto value = values.value(name);
      const auto argument_schema = entry.argument_schemas.at(index).toMap();
      if (!validValue(value, argument_schema)) return correctableFailure(QStringLiteral("Invalid typed argument: %1").arg(name));
      const auto replacement = entry.policy.arguments.value(entry.native_names.at(index));
      auto resolved = replacement.resolve ? replacement.resolve(value) : value;
      if (!resolved.isValid()) return correctableFailure(QStringLiteral("The target ID for %1 no longer exists.").arg(name));
      const auto type = entry.property >= 0 ? meta->property(entry.property).metaType() : method.parameterMetaType(index);
      if (type.id() == QMetaType::QVariant) arguments.push_back(QVariant::fromValue(resolved));
      else {
        const auto original_number = resolved.toDouble();
        const auto native_schema = typeSchema(type);
        if (native_schema.value(QStringLiteral("type")) == QStringLiteral("integer")) {
          double minimum = -9007199254740991.0;
          double maximum = 9007199254740991.0;
          if (type.id() == QMetaType::Int || type.flags().testFlag(QMetaType::IsEnumeration)) {
            minimum = std::numeric_limits<int>::lowest(); maximum = std::numeric_limits<int>::max();
          } else if (type.id() == QMetaType::UInt) {
            minimum = 0; maximum = std::numeric_limits<unsigned int>::max();
          } else if (type.id() == QMetaType::ULongLong) minimum = 0;
          if (original_number < minimum || original_number > maximum)
            return correctableFailure(QStringLiteral("The argument is outside the native integer range: %1").arg(name));
        }
        if (!resolved.convert(type)) return correctableFailure(QStringLiteral("The argument cannot be represented by the native type: %1").arg(name));
        if (typeSchema(type).value(QStringLiteral("type")) == QStringLiteral("integer") && resolved.toDouble() != original_number)
          return correctableFailure(QStringLiteral("The argument is outside the native integer range: %1").arg(name));
        if (native_schema.value(QStringLiteral("type")) == QStringLiteral("number") && !std::isfinite(resolved.toDouble()))
          return correctableFailure(QStringLiteral("The argument is outside the native number range: %1").arg(name));
        arguments.push_back(resolved);
      }
    }
    const Invocation native = [entry, method, arguments, values]() mutable -> QVariantMap {
      if (!entry.object) return failure(QStringLiteral("The native owner closed before invocation."));
      if (entry.property >= 0) {
        const auto property = entry.object->metaObject()->property(entry.property);
        const auto before = property.read(entry.object);
        if (!property.write(entry.object, arguments.front())) return failure(QStringLiteral("The native property rejected the value."));
        const auto after = property.read(entry.object);
        if (after != arguments.front()) return {{"ok", false}, {"error", "The native control rejected or constrained the requested value."}, {"actualValue", after}};
        return {{"ok", true}, {"value", after}, {"alreadySatisfied", before == after}};
      }
      const auto type = method.returnMetaType();
      QVariant result(type.id() == QMetaType::Void ? QVariant{} : QVariant(type));
      QVariant variant_result;
      std::vector<void*> argv(arguments.size() + 1);
      argv[0] = type.id() == QMetaType::Void ? nullptr : type.id() == QMetaType::QVariant
          ? static_cast<void*>(&variant_result) : result.data();
      for (std::size_t index = 0; index < arguments.size(); ++index)
        argv[index + 1] = method.parameterMetaType(static_cast<int>(index)).id() == QMetaType::QVariant
            ? static_cast<void*>(&arguments[index]) : arguments[index].data();
      // Direct workspace-thread dispatch handles native functions with more than
      // ten parameters, including shape/effect controls, without a JS bypass.
      QMetaObject::metacall(entry.object, QMetaObject::InvokeMetaMethod, method.methodIndex(), argv.data());
      if (type.id() == QMetaType::QVariant) result = variant_result;
      if (!entry.read_only && type.id() == QMetaType::Bool && !result.toBool()) {
        if (entry.policy.confirm_satisfied && entry.policy.confirm_satisfied(values))
          return {{"ok", true}, {"alreadySatisfied", true}};
        return failure(QStringLiteral("The native command rejected the operation or made no change."));
      }
      if (type.id() == QMetaType::QVariantMap && result.toMap().contains(QStringLiteral("ok"))) return result.toMap();
      return {{"ok", true}, {"result", result}};
    };
    auto result = entry.read_only ? native() : entry.mutate(id, values, native);
    result.insert(QStringLiteral("commandId"), id);
    return result;
  }

 private:
  struct Entry final {
    QString id, domain, name;
    QPointer<QObject> object;
    int method = -1, property = -1, minimum_arguments = 0;
    bool read_only = false;
    QStringList native_names, argument_names;
    QVariantList argument_schemas;
    QVariantMap input_schema;
    ActionHandler action;
    Policy policy;
    MutationOwner mutate;
  };
  static QVariantMap failure(const QString& error) { return {{"ok", false}, {"error", error}}; }
  // These checks precede dispatch. Never attach this contract to a failure
  // after native invocation, where mutation/completion may be uncertain.
  static QVariantMap correctableFailure(const QString& error) {
    return {{"ok", false}, {"error", error}, {"errorCode", "command_admission_rejected"},
        {"recoverableError", true}, {"mutationNotApplied", true}};
  }
  static QVariantMap typeSchema(QMetaType type) {
    switch (type.id()) {
      case QMetaType::Void: return {{"type", "null"}};
      case QMetaType::Bool: return {{"type", "boolean"}};
      case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong: return {{"type", "integer"}};
      case QMetaType::Double: case QMetaType::Float: return {{"type", "number"}};
      case QMetaType::QString: return {{"type", "string"}};
      case QMetaType::QStringList: return {{"type", "array"}, {"items", QVariantMap{{"type", "string"}}}};
      case QMetaType::QVariantList: case QMetaType::QJsonArray: return {{"type", "array"}};
      case QMetaType::QVariantMap: case QMetaType::QJsonObject: return {{"type", "object"}};
      case QMetaType::QVariant: return {{"type", QVariantList{"object", "array", "string", "number", "boolean", "null"}}};
      default: return type.flags().testFlag(QMetaType::IsEnumeration) ? QVariantMap{{"type", "integer"}} : QVariantMap{};
    }
  }
  static bool validValue(const QVariant& value, const QVariantMap& schema) {
    if (schema.isEmpty()) return true;
    if (schema.value(QStringLiteral("type")).metaType().id() == QMetaType::QVariantList) {
      for (const auto& alternative : schema.value(QStringLiteral("type")).toList()) {
        auto candidate = schema;
        candidate.insert(QStringLiteral("type"), alternative);
        if (validValue(value, candidate)) return true;
      }
      return false;
    }
    if (schema.contains(QStringLiteral("enum")) &&
        !schema.value(QStringLiteral("enum")).toList().contains(value)) return false;
    const auto type = schema.value(QStringLiteral("type")).toString();
    const auto id = value.metaType().id();
    if (type == QStringLiteral("null")) return !value.isValid() || value.isNull();
    if (type == QStringLiteral("string")) {
      if (id != QMetaType::QString) return false;
      const auto text = value.toString();
      if (schema.contains("minLength") && text.size() < schema.value("minLength").toLongLong()) return false;
      if (schema.contains("maxLength") && text.size() > schema.value("maxLength").toLongLong()) return false;
      if (schema.contains("pattern") && !QRegularExpression(schema.value("pattern").toString()).match(text).hasMatch()) return false;
      return true;
    }
    if (type == QStringLiteral("boolean")) return id == QMetaType::Bool;
    if (type == QStringLiteral("object")) {
      if (id != QMetaType::QVariantMap && id != QMetaType::QJsonObject) return false;
      const auto object = id == QMetaType::QJsonObject ? value.value<QJsonObject>().toVariantMap() : value.toMap();
      if (schema.contains("minProperties") && object.size() < schema.value("minProperties").toLongLong()) return false;
      if (schema.contains("maxProperties") && object.size() > schema.value("maxProperties").toLongLong()) return false;
      for (const auto& name : schema.value("required").toStringList()) if (!object.contains(name)) return false;
      const auto properties = schema.value("properties").toMap();
      const auto additional = schema.value("additionalProperties", true);
      for (auto it = object.cbegin(); it != object.cend(); ++it) {
        if (properties.contains(it.key())) {
          if (!validValue(it.value(), properties.value(it.key()).toMap())) return false;
        } else if (additional.metaType().id() == QMetaType::Bool && !additional.toBool()) {
          return false;
        } else if (additional.metaType().id() == QMetaType::QVariantMap && !validValue(it.value(), additional.toMap())) {
          return false;
        }
      }
      return true;
    }
    if (type == QStringLiteral("array")) {
      if (id != QMetaType::QVariantList && id != QMetaType::QStringList && id != QMetaType::QJsonArray) return false;
      const auto array = id == QMetaType::QJsonArray ? value.value<QJsonArray>().toVariantList() : value.toList();
      if (schema.contains("minItems") && array.size() < schema.value("minItems").toLongLong()) return false;
      if (schema.contains("maxItems") && array.size() > schema.value("maxItems").toLongLong()) return false;
      const auto items = schema.value(QStringLiteral("items")).toMap();
      QSet<QByteArray> seen;
      for (const auto& item : array) {
        if (!items.isEmpty() && !validValue(item, items)) return false;
        if (schema.value("uniqueItems").toBool()) {
          const auto key = QJsonDocument(QJsonArray{QJsonValue::fromVariant(item)}).toJson(QJsonDocument::Compact);
          if (seen.contains(key)) return false;
          seen.insert(key);
        }
      }
      return true;
    }
    if (type == QStringLiteral("integer") || type == QStringLiteral("number")) {
      if (id != QMetaType::Int && id != QMetaType::UInt && id != QMetaType::LongLong && id != QMetaType::ULongLong &&
          id != QMetaType::Double && id != QMetaType::Float) return false;
      const auto number = value.toDouble();
      if (!std::isfinite(number) || (type == QStringLiteral("integer") && std::floor(number) != number)) return false;
      if (schema.contains("minimum") && number < schema.value("minimum").toDouble()) return false;
      if (schema.contains("maximum") && number > schema.value("maximum").toDouble()) return false;
      if (schema.contains("exclusiveMinimum") && number <= schema.value("exclusiveMinimum").toDouble()) return false;
      if (schema.contains("exclusiveMaximum") && number >= schema.value("exclusiveMaximum").toDouble()) return false;
      return true;
    }
    return false;
  }
  static bool isStrictObjectSchema(const QVariantMap& schema) {
    if (schema.value(QStringLiteral("type")).toString() != QStringLiteral("object") ||
        schema.value(QStringLiteral("additionalProperties")).metaType().id() != QMetaType::Bool ||
        schema.value(QStringLiteral("additionalProperties")).toBool()) return false;
    const auto properties = schema.value(QStringLiteral("properties")).toMap();
    const auto required_value = schema.value(QStringLiteral("required"));
    QStringList required;
    if (required_value.metaType().id() == QMetaType::QStringList) {
      required = required_value.toStringList();
    } else if (required_value.metaType().id() == QMetaType::QVariantList) {
      for (const auto& value : required_value.toList()) {
        if (value.metaType().id() != QMetaType::QString) return false;
        required.append(value.toString());
      }
    } else {
      return false;
    }
    if (properties.isEmpty()) return false;
    for (const auto& key : required)
      if (!properties.contains(key)) return false;
    for (auto it = properties.cbegin(); it != properties.cend(); ++it)
      if (it.key().trimmed().isEmpty() || it.value().toMap().value(QStringLiteral("type")).isNull()) return false;
    return true;
  }
  static QVariantMap describe(const Entry& entry, bool include_schema) {
    QVariantMap result{{"commandId", entry.id}, {"domain", entry.domain}, {"name", entry.name},
        {"kind", entry.action ? "action" : entry.property >= 0 ? "property" : "method"}, {"readOnly", entry.read_only},
        {"capability", entry.read_only ? "read" : "draft"},
        {"invokeTool", entry.read_only ? "app.inspect_command" : "app.invoke_command"},
        {"available", entry.policy.available && !entry.object.isNull()},
        {"resource", entry.policy.resource}, {"completion", entry.policy.completion}, {"reversal", entry.policy.reversal}};
    if (!entry.policy.available) result.insert(QStringLiteral("unavailableReason"), entry.policy.reason);
    if (include_schema) {
      if (entry.action) {
        result.insert(QStringLiteral("inputSchema"), entry.input_schema);
        return result;
      }
      QVariantMap properties;
      QStringList required;
      for (qsizetype index = 0; index < entry.argument_names.size(); ++index) {
        properties.insert(entry.argument_names.at(index), entry.argument_schemas.at(index));
        if (index < entry.minimum_arguments) required.append(entry.argument_names.at(index));
      }
      result.insert(QStringLiteral("inputSchema"), QVariantMap{{"type", "object"}, {"properties", properties},
          {"required", required}, {"additionalProperties", false}});
    }
    return result;
  }
  QHash<QString, Entry> entries_;
};

}  // namespace photo_editor::desktop

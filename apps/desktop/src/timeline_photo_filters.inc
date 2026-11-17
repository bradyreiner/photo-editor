QVariantList TimelineDocumentController::availableClipPhotoFilters() const {
  QVariantList result;
  for (const auto& entry : photo_editor::pipeline::photo_effect_registry) {
    const auto type = QString::fromUtf8(entry.type_id.data(), static_cast<qsizetype>(entry.type_id.size()));
    result.push_back(QVariantMap{{"type", type},
        {"name", QString::fromUtf8(entry.display_name.data(), static_cast<qsizetype>(entry.display_name.size()))},
        {"category", QString::fromUtf8(entry.category.data(), static_cast<qsizetype>(entry.category.size()))},
        {"description", QString::fromUtf8(entry.description.data(), static_cast<qsizetype>(entry.description.size()))},
        {"enabled", type != QStringLiteral("displacement_map")},
        {"disabledReason", QStringLiteral("A displacement map must be bound to an image asset; use the Edit workspace for map authoring.")}});
  }
  return result;
}

QVariantList TimelineDocumentController::clipPhotoFilters(const QString& clip_id) const {
  const auto recipe = QJsonDocument::fromJson(colorGradeRecipeJsonForClip(clip_id).toUtf8()).object();
  const auto layers = recipe.value(QStringLiteral("photoLayers")).toArray();
  QVariantList result;
  for (qsizetype index = 0; index < layers.size(); ++index) {
    const auto layer = layers.at(index).toObject();
    const auto id = layer.value(QStringLiteral("id")).toString();
    if (!id.startsWith(QStringLiteral("timeline-filter-"))) continue;
    const auto effects = decode_photo_layer_effects(layer.toVariantMap());
    if (!effects || effects->size() != 1) continue;
    const auto& effect = effects->front();
    const auto* descriptor = photo_editor::pipeline::find_photo_effect(effect.type_id, effect.algorithm_version);
    const auto name = descriptor ? QString::fromUtf8(descriptor->display_name.data(),
        static_cast<qsizetype>(descriptor->display_name.size())) : QString::fromStdString(effect.type_id);
    auto fields = timeline_photo_filter_fields(effect);
    auto defaults = photo_editor::pipeline::PhotoEffectInstance{};
    defaults.type_id = effect.type_id;
    const auto default_fields = timeline_photo_filter_fields(defaults);
    for (auto& field : fields) {
      auto row = field.toMap();
      for (const auto& default_field : default_fields) {
        const auto default_row = default_field.toMap();
        if (row.value("key") == default_row.value("key")) {
          row.insert("defaultValue", default_row.value("value"));
          break;
        }
      }
      field = row;
    }
    result.push_back(QVariantMap{{"id", id}, {"type", QString::fromStdString(effect.type_id)},
        {"name", name}, {"position", index}, {"enabled", effect.enabled},
        {"amount", effect.amount}, {"fields", fields}});
  }
  return result;
}

QVariantMap TimelineDocumentController::editClipPhotoFilter(
    const QString& clip_id, const QString& operation, const QString& filter_id,
    const QString& type, const QVariantMap& values) {
  const auto error = [](const QString& message) {
    return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
  };
  auto json = colorGradeRecipeJsonForClip(clip_id);
  if (json.isEmpty()) json = serialize_edit_recipe_json(EditRecipeState{});
  auto recipe = QJsonDocument::fromJson(json.toUtf8()).object();
  auto layers = recipe.value(QStringLiteral("photoLayers")).toArray();
  int index = -1;
  for (qsizetype position = 0; position < layers.size(); ++position) {
    if (layers.at(position).toObject().value(QStringLiteral("id")).toString() == filter_id &&
        filter_id.startsWith(QStringLiteral("timeline-filter-"))) index = static_cast<int>(position);
  }
  if (operation == QStringLiteral("add")) {
    const auto type_id = type.toStdString();
    const auto* descriptor = photo_editor::pipeline::find_photo_effect(type_id, 1);
    if (!descriptor || type == QStringLiteral("displacement_map"))
      return error(QStringLiteral("This filter is unavailable without a bound map asset."));
    photo_editor::pipeline::PhotoEffectInstance effect;
    effect.type_id = type_id;
    effect.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    effect.input_scope = photo_editor::pipeline::PhotoEffectInputScope::CompositeBelow;
    QString detail;
    const auto envelope = encode_photo_layer_effects(std::span(&effect, 1), &detail);
    if (!envelope) return error(detail);
    const auto layer_id = QStringLiteral("timeline-filter-") + QString::fromStdString(effect.id);
    const auto name = QString::fromUtf8(descriptor->display_name.data(), static_cast<qsizetype>(descriptor->display_name.size()));
    const QJsonObject layer{{"id", layer_id}, {"name", name}, {"kind", "adjustment"},
        {"enabled", true}, {"opacity", 1.0}, {"blendMode", "normal"}, {"effectStack", *envelope}};
    layers.append(layer);
  } else {
    if (index < 0) return error(QStringLiteral("The filter no longer exists on this clip."));
    auto layer = layers.at(index).toObject();
    if (operation == QStringLiteral("remove")) layers.removeAt(index);
    else if (operation == QStringLiteral("move")) {
      bool converted = false;
      const int position = values.value(QStringLiteral("position")).toInt(&converted);
      if (!converted || position < 0 || position >= layers.size())
        return error(QStringLiteral("The destination is outside the filter stack."));
      layers.removeAt(index);
      layers.insert(position, layer);
    } else {
      QString detail;
      auto effects = decode_photo_layer_effects(layer.toVariantMap(), &detail);
      if (!effects || effects->size() != 1) return error(QStringLiteral("The filter envelope could not be read."));
      auto& effect = effects->front();
      if (operation == QStringLiteral("enabled")) effect.enabled = values.value(QStringLiteral("enabled")).toBool();
      else if (operation == QStringLiteral("amount")) {
        bool converted = false;
        const double value = values.value(QStringLiteral("amount")).toDouble(&converted);
        if (!converted || !std::isfinite(value) || value < 0 || value > 1)
          return error(QStringLiteral("Filter amount must be between zero and one."));
        effect.amount = value;
      } else if (operation == QStringLiteral("parameter")) {
        bool converted = false;
        const double value = values.value(QStringLiteral("value")).toDouble(&converted);
        if (!converted || !set_timeline_photo_filter_value(&effect,
              values.value(QStringLiteral("parameter")).toString(), value))
          return error(QStringLiteral("This filter parameter is unavailable or outside its supported range."));
      } else return error(QStringLiteral("Unknown filter operation."));
      const auto envelope = encode_photo_layer_effects(*effects, &detail);
      if (!envelope) return error(detail);
      layer.insert(QStringLiteral("effectStack"), *envelope);
      layers[index] = layer;
    }
  }
  recipe.insert(QStringLiteral("photoLayers"), layers);
  recipe.insert(QStringLiteral("recipeVersion"), qMax(
      recipe.value(QStringLiteral("recipeVersion")).toInt(), kPhotoEffectsEditRecipeVersion));
  return setClipColorGradeRecipeForAutomation(clip_id, recipe);
}

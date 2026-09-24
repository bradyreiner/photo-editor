#include "preset_service.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  photo_editor::desktop::PresetService service;
  photo_editor::desktop::EditRecipeState built_in;
  built_in.exposure = 0.5;
  service.replaceBuiltIns(
      {{QStringLiteral("Clean"), built_in}},
      {{QStringLiteral("Clean"), QStringLiteral("Essentials")}},
      {{QStringLiteral("Clean"), QStringLiteral("Natural")}},
      {{QStringLiteral("Clean"), QStringList{QStringLiteral("tone")}}},
      {});
  service.setBuiltInContract(
      QStringLiteral("Clean"),
      {{QStringLiteral("sourceRenderer"), QStringLiteral("aces2-ocio-v1")}});
  if (service.builtInPresets().size() != 1 ||
      service.builtInPacks().value(QStringLiteral("Clean")) !=
          QStringLiteral("Essentials") ||
      service.builtInContracts().value(QStringLiteral("Clean"))
              .value(QStringLiteral("sourceRenderer")).toString() !=
          QStringLiteral("aces2-ocio-v1")) {
    return fail("built-in preset state and provenance metadata must replace atomically");
  }

  photo_editor::desktop::EditRecipeState user;
  user.exposure = 1.25;
  service.upsertUser(
      QStringLiteral("Portrait"),
      user,
      QStringLiteral("People"),
      QStringLiteral("Portrait"),
      {QStringLiteral("tone"), QStringLiteral("color")},
      QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("skin")}}},
      {{QStringLiteral("presetVersion"), 2}});
  if (service.userPresets().value(QStringLiteral("Portrait")).exposure != 1.25 ||
      service.userPacks().value(QStringLiteral("Portrait")) != QStringLiteral("People") ||
      service.userAdaptiveRules().value(QStringLiteral("Portrait")).isEmpty() ||
      service.userContracts().value(QStringLiteral("Portrait"))
              .value(QStringLiteral("presetVersion")).toInt() != 2) {
    return fail("user preset state and all metadata must upsert as one record");
  }

  if (!service.setFavorite(QStringLiteral("Portrait"), true) ||
      service.setFavorite(QStringLiteral("Portrait"), true) ||
      service.setFavorite(QStringLiteral("Missing"), true) ||
      !service.renameUser(QStringLiteral("Portrait"), QStringLiteral("Portrait Warm")) ||
      service.userPresets().contains(QStringLiteral("Portrait")) ||
      !service.favorites().contains(QStringLiteral("Portrait Warm")) ||
      service.renameUser(QStringLiteral("Portrait Warm"), QStringLiteral("Clean"))) {
    return fail("favorite and rename operations must preserve identity and reject collisions");
  }

  if (!service.moveUserToPack(
          QStringLiteral("Portrait Warm"),
          QStringLiteral("Editorial"),
          QStringLiteral("People")) ||
      service.userPacks().value(QStringLiteral("Portrait Warm")) !=
          QStringLiteral("Editorial") ||
      !service.removeUser(QStringLiteral("Portrait Warm")) ||
      service.removeUser(QStringLiteral("Portrait Warm")) ||
      !service.favorites().isEmpty()) {
    return fail("moving and deleting a user preset must update every dependent collection");
  }

  if (!service.setIntensity(0.5) || service.intensity() != 0.5 ||
      !service.setIntensity(1.5) || service.intensity() != 1.0 ||
      service.setIntensity(1.0) || !service.setIntensity(-0.5) ||
      service.intensity() != 0.0) {
    return fail("preset intensity must be clamped and publish only real changes");
  }

  service.setCameraDefault(QStringLiteral("camera-a"), built_in);
  if (!service.cameraDefaults().contains(QStringLiteral("camera-a")) ||
      !service.removeCameraDefault(QStringLiteral("camera-a")) ||
      service.removeCameraDefault(QStringLiteral("camera-a"))) {
    return fail("camera defaults must share preset-service replacement semantics");
  }

  service.clearUsers();
  if (!service.userPresets().isEmpty() || !service.favorites().isEmpty() ||
      service.builtInPresets().isEmpty()) {
    return fail("reloading users must not erase the built-in preset catalog");
  }

  return EXIT_SUCCESS;
}

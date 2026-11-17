#pragma once
#include <QCryptographicHash>

// Explicit retained-recipe benchmark, separate from the short CTest oracles.
// Never changes image size, samples, scene geometry or opacity conventions.
int gaussian_scan_visibility_perf(const QStringList& args) {
  if (args.size() != 5) return 2;
  const QDir project(args[2]); const QString output = args[4];
  if (!QFileInfo(output).isAbsolute() || QFileInfo::exists(output) || !QDir().mkpath(output)) return 2;
  const auto read = [](QString path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; };
  const auto manifest = QJsonDocument::fromJson(read(project.filePath("scene.json"))).toVariant().toMap();
  const auto source = QJsonDocument::fromJson(read(args[3])).toVariant().toMap();
  const auto camera_data = source.value("metrics").toMap().value("camera").toMap();
  if (camera_data.isEmpty() || camera_data.contains("photo") || camera_data.contains("physicalCalibration")) return 2;
  const auto loaded = load_gaussian_scene(project.filePath("trained/scene.ply"),project.filePath("dataset/sparse"),manifest.value("artifactValidation").toMap().value("artifactSha256").toString());
  if (!loaded.scene || !loaded.scene->temporal.empty()) return 2;
  const auto composition = load_gaussian_composition(project.path(),loaded.scene->artifact_sha256);
  if (!composition.composition || !composition.composition->scan_shadows || !composition.composition->environmentLighting()) return 2;
  GaussianCamera camera;
  const auto xyz = [&](const char* key) { const auto v = camera_data.value(key).toList(); return QVector3D(v.value(0).toFloat(),v.value(1).toFloat(),v.value(2).toFloat()); };
  camera.position = xyz("position"); camera.right = xyz("right"); camera.down = xyz("down"); camera.forward = xyz("forward");
  camera.fx = camera_data.value("fx").toFloat(); camera.fy = camera_data.value("fy").toFloat();
  camera.cx = camera_data.value("cx").toFloat(); camera.cy = camera_data.value("cy").toFloat();
  camera.image_size = {camera_data.value("width").toInt(),camera_data.value("height").toInt()};
  camera.composition = composition.composition;
  // CPU traversal counts isolate acceleration quality without instrumenting
  // the timed GPU shader. Rays leave the visible hemisphere of the test sphere.
  auto index = build_gaussian_scan_index(*loaded.scene,0);
  if (!index.error.isEmpty() || index.nodes.empty() || composition.composition->objects.empty()) return 2;
  const auto& object = composition.composition->objects.front();
  if (object.kind != 0 || object.size.x() != object.size.y() || object.size.x() != object.size.z()) return 2;
  const auto normal = (camera.position-object.position).normalized();
  const auto tangent = QVector3D::crossProduct(std::abs(normal.z()) < 0.999F ? QVector3D(0,0,1) : QVector3D(0,1,0),normal).normalized();
  const auto bitangent = QVector3D::crossProduct(normal,tangent);
  const auto origin = object.position+normal*(object.size.x()+0.0001F);
  quint64 total_nodes = 0, total_particles = 0, max_nodes = 0, max_particles = 0, total_responses = 0;
  QVariantList rays;
  for (int sample = 0; sample < 128; ++sample) {
    const double u = (sample+0.5)/128, phi = sample*2.399963229728653;
    const auto direction = tangent*static_cast<float>(std::sqrt(u)*std::cos(phi))+bitangent*static_cast<float>(std::sqrt(u)*std::sin(phi))+normal*static_cast<float>(std::sqrt(1-u));
    std::vector<quint32> stack{0}; quint64 nodes = 0, particles = 0, responses = 0; double transmittance = 1;
    const auto bounds = [&](quint32 id) {
      const auto& node = index.nodes[id]; double enter = 0, leave = 1e30;
      for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-20) { if (origin[axis] < node.lower[static_cast<size_t>(axis)] || origin[axis] > node.upper[static_cast<size_t>(axis)]) return 1e30; }
        else {
          const double a = (double(node.lower[static_cast<size_t>(axis)])-origin[axis])/direction[axis], b = (double(node.upper[static_cast<size_t>(axis)])-origin[axis])/direction[axis];
          enter = std::max(enter,std::min(a,b)); leave = std::min(leave,std::max(a,b));
        }
      }
      return leave < enter ? 1e30 : enter;
    };
    while (!stack.empty() && transmittance > 1e-4) {
      const auto id = stack.back(); stack.pop_back(); ++nodes;
      const auto& node = index.nodes[id]; if (bounds(id) >= 1e30) continue;
      if (node.links[1] == 0) {
        const auto left = id+1, right = index.nodes[left].links[2]; const auto a = bounds(left), b = bounds(right);
        if (a < b) { if (b < 1e30) stack.push_back(right); if (a < 1e30) stack.push_back(left); }
        else { if (a < 1e30) stack.push_back(left); if (b < 1e30) stack.push_back(right); }
        continue;
      }
      for (quint32 j = 0; j < node.links[1] && transmittance > 1e-4; ++j) {
        ++particles; const auto& p = index.particles[node.links[0]+j];
        const std::array<float,4>* rows[]{&p.row0,&p.row1,&p.row2}; double o[3]{},d[3]{};
        for (size_t a = 0; a < 3; ++a) for (size_t b = 0; b < 3; ++b) { o[a] += (*rows[a])[b]*(double(origin[static_cast<int>(b)])-p.center_opacity[b]); d[a] += (*rows[a])[b]*double(direction[static_cast<int>(b)]); }
        const double denominator = d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
        const double t = std::max(0.0,-(o[0]*d[0]+o[1]*d[1]+o[2]*d[2])/denominator);
        double response = 0; for (size_t a = 0; a < 3; ++a) response += std::pow(o[a]+t*d[a],2);
        const double alpha = std::min(double(gaussian_alpha_limit(composition.composition->alpha_model)),p.center_opacity[3]*std::exp(-0.5*response));
        if (alpha >= gaussian_scan_cutoff) { transmittance *= 1-alpha; ++responses; }
      }
    }
    total_responses += responses; total_nodes += nodes; total_particles += particles; max_nodes = std::max(max_nodes,nodes); max_particles = std::max(max_particles,particles);
    rays.append(QVariantMap{{"nodes",nodes},{"particles",particles},{"responses",responses},{"transmittance",transmittance}});
  }
  const QVariantMap diagnostics{{"rayCount",128},{"meanRayResponses",double(total_responses)/128},{"meanRayNodes",double(total_nodes)/128},{"meanRayParticles",double(total_particles)/128},{"maxRayNodes",max_nodes},{"maxRayParticles",max_particles},{"rays",rays}};
  { QFile file(output+"/traversal.json"); const auto bytes = QJsonDocument::fromVariant(diagnostics).toJson(); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return 2; }
  index = {}; // Do not retain the separate CPU diagnostic index during GPU timing.
  auto renderer = make_gaussian_scene_renderer(); const auto base = camera; QVariantList frames;
  for (int i = 0; i < 3; ++i) {
    camera = i == 0 ? base : navigate_gaussian_camera(base,loaded.scene->focus_distance,static_cast<float>(i)*0.01F,0,0,0,0);
    const auto frame = renderer->render(loaded.scene,camera,true);
    const auto name = QStringLiteral("frame-%1.png").arg(i);
    if (!frame.error.isEmpty() || frame.image.isNull() || !frame.image.save(output+"/"+name)) return 1;
    frames.append(QVariantMap{{"file",name},{"metrics",frame.metrics}});
    std::cerr << "visibility benchmark completed " << i+1 << "/3\n";
  }
  const QVariantMap result{{"schema","looksy-gaussian-visibility-perf-v1"},{"artifactSha256",loaded.scene->artifact_sha256},
      {"camera",base.description()},{"composition",composition.composition->description()},
      {"rayCount",128},{"meanRayResponses",double(total_responses)/128},{"meanRayNodes",double(total_nodes)/128},{"meanRayParticles",double(total_particles)/128},
      {"maxRayNodes",max_nodes},{"maxRayParticles",max_particles},{"rays",rays},{"frames",frames},
      {"scope","matched full-resolution frozen-recipe GPU benchmark and separate CPU traversal diagnostics"}};
  QFile file(output+"/receipt.json"); const auto bytes = QJsonDocument::fromVariant(result).toJson();
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() ? 0 : 2;
}

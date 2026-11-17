#pragma once
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QVector3D>
#include <QVector2D>
#include <QStringList>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <numeric>
#include <vector>

namespace photo_editor::desktop {
struct alignas(16) GaussianMeshTriangle {
  std::array<float,4> position{}, edge1{}, edge2{}, normal0{}, normal1{}, normal2{}, uv01{}, uv2_material{};
};
struct alignas(16) GaussianMeshNode {
  std::array<float,4> lower{}, upper{};
  std::array<quint32,4> links{}; // left, right, first triangle, count (zero for branches).
};
static_assert(sizeof(GaussianMeshTriangle) == 128 && sizeof(GaussianMeshNode) == 48);
struct GaussianSceneMesh {
  QString sha256;
  std::vector<GaussianMeshTriangle> triangles;
  std::vector<GaussianMeshNode> nodes;
  QVector3D lower, upper;
  int skipped_degenerate = 0;
  QStringList material_libraries, material_names{QString()};
};
struct GaussianMeshLoad {
  std::shared_ptr<const GaussianSceneMesh> mesh;
  QString error;
  QByteArray source;
};
inline QVector3D gaussian_mesh_vector(const std::array<float,4>& v) { return {v[0],v[1],v[2]}; }
inline GaussianMeshLoad parse_gaussian_obj(const QByteArray& bytes, const std::shared_ptr<std::atomic_bool>& canceled = {}) {
  const auto fail = [](QString text) { return GaussianMeshLoad{{},std::move(text),{}}; };
  if (bytes.isEmpty() || bytes.size() > 64*1024*1024 || bytes.contains('\0')) return fail("Choose a text OBJ no larger than 64 MiB.");
  auto mesh = std::make_shared<GaussianSceneMesh>();
  mesh->sha256 = QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
  std::vector<QVector3D> positions, normals; std::vector<QVector2D> texcoords; int material = 0;
  struct Corner { size_t vertex; int normal = -1, uv = -1; };
  const auto number = [](const QByteArray& token, float& value) {
    bool ok = false; value = token.toFloat(&ok); return ok && std::isfinite(value) && std::abs(value) <= 1e8F;
  };
  const auto index = [](const QByteArray& token, size_t count, size_t& out) {
    bool ok = false; const auto value = token.toLongLong(&ok);
    if (!ok || value == 0 || value < -static_cast<qint64>(count) || value > static_cast<qint64>(count)) return false;
    out = static_cast<size_t>(value > 0 ? value-1 : static_cast<qint64>(count)+value); return true;
  };
  int line_number = 0;
  qsizetype offset = 0;
  while (offset < bytes.size()) {
    if (canceled && canceled->load()) return fail("Geometry import canceled.");
    auto end = bytes.indexOf('\n',offset); if (end < 0) end = bytes.size();
    auto line = bytes.mid(offset,end-offset); offset = end+1; ++line_number;
    if (line.size() > 65536) return fail("An OBJ line exceeds 64 KiB.");
    const auto comment = line.indexOf('#'); if (comment >= 0) line.truncate(comment);
    const auto tokens = line.simplified().split(' '); if (tokens.isEmpty() || tokens[0].isEmpty()) continue;
    const auto error = [&](QString reason) { return fail(QStringLiteral("OBJ line %1: %2").arg(line_number).arg(reason)); };
    const auto tag = tokens[0];
    if (tag == "v" || tag == "vn") {
      if ((tag == "vn" && tokens.size() != 4) || (tag == "v" && tokens.size() != 4 && tokens.size() != 5 && tokens.size() != 7)) return error("Expected XYZ vertices or XYZ normals.");
      float xyz[3]{};
      for (int k = 0; k < 3; ++k) if (!number(tokens[k+1],xyz[k])) return error("Invalid or non-finite coordinate.");
      if (tag == "v" && tokens.size() == 5) {
        float w = 0; if (!number(tokens[4],w) || std::abs(w) < 1e-12F) return error("Invalid homogeneous vertex weight.");
        for (auto& value : xyz) { value /= w; if (!std::isfinite(value) || std::abs(value) > 1e8F) return error("Vertex exceeds supported coordinates."); }
      }
      QVector3D value(xyz[0],xyz[1],xyz[2]);
      if (tag == "vn") { if (value.lengthSquared() < 1e-20F) return error("Zero normal."); normals.push_back(value/std::sqrt(value.lengthSquared())); }
      else positions.push_back(value);
      if (positions.size() > 500000 || normals.size() > 1500000) return error("Geometry exceeds 500,000 positions or 1,500,000 normals.");
    } else if (tag == "vt") {
      if (tokens.size() < 2 || tokens.size() > 4) return error("Invalid texture coordinate.");
      QVector2D uv;
      for (qsizetype k = 1; k < tokens.size(); ++k) { float value; if (!number(tokens[k],value)) return error("Invalid texture coordinate."); if (k <= 2) uv[static_cast<int>(k-1)] = value; }
      texcoords.push_back(uv);
      if (texcoords.size() > 1500000) return error("Too many texture coordinates.");
    } else if (tag == "f") {
      if (tokens.size() < 4 || tokens.size() > 257) return error("Faces must have 3–256 corners.");
      std::vector<Corner> polygon;
      for (qsizetype k = 1; k < tokens.size(); ++k) {
        const auto indices = tokens[k].split('/'); Corner corner{}; size_t normal = 0, uv = 0;
        if (indices.isEmpty() || indices.size() > 3 || !index(indices[0],positions.size(),corner.vertex) ||
            (indices.size() >= 2 && !indices[1].isEmpty() && !index(indices[1],texcoords.size(),uv)) ||
            (indices.size() == 3 && (!index(indices[2],normals.size(),normal)))) return error("Face references a missing or zero vertex/UV/normal index.");
        if (indices.size() >= 2 && !indices[1].isEmpty()) corner.uv = static_cast<int>(uv);
        if (indices.size() == 3) corner.normal = static_cast<int>(normal);
        polygon.push_back(corner);
      }
      const auto emit_triangle = [&](const Corner& a,const Corner& b,const Corner& c) {
        const auto p = positions[a.vertex], e1 = positions[b.vertex]-p, e2 = positions[c.vertex]-p;
        const auto cross = QVector3D::crossProduct(e1,e2);
        const float length = cross.length();
        if (length <= 1e-9F*std::max(e1.lengthSquared(),e2.lengthSquared())) { ++mesh->skipped_degenerate; return; }
        const auto n = cross/length;
        const auto array = [](QVector3D v) { return std::array<float,4>{v.x(),v.y(),v.z(),0}; };
        GaussianMeshTriangle triangle; triangle.position = array(p); triangle.edge1 = array(e1); triangle.edge2 = array(e2);
        triangle.normal0 = array(a.normal < 0 ? n : normals[static_cast<size_t>(a.normal)]);
        triangle.normal1 = array(b.normal < 0 ? n : normals[static_cast<size_t>(b.normal)]);
        triangle.normal2 = array(c.normal < 0 ? n : normals[static_cast<size_t>(c.normal)]);
        triangle.uv2_material[2] = static_cast<float>(material);
        if (a.uv >= 0 && b.uv >= 0 && c.uv >= 0) {
          const auto uva = texcoords[static_cast<size_t>(a.uv)], uvb = texcoords[static_cast<size_t>(b.uv)], uvc = texcoords[static_cast<size_t>(c.uv)];
          triangle.uv01 = {uva.x(),uva.y(),uvb.x(),uvb.y()}; triangle.uv2_material = {uvc.x(),uvc.y(),static_cast<float>(material),1};
        }
        mesh->triangles.push_back(triangle);
      };
      if (polygon.size() == 3) emit_triangle(polygon[0],polygon[1],polygon[2]);
      else {
        // Project onto the dominant Newell plane and clip ears, including
        // concave faces. A stalled triangulation is rejected, never fan-filled.
        QVector3D newell; const auto anchor = positions[polygon[0].vertex];
        for (size_t k = 0; k < polygon.size(); ++k) newell += QVector3D::crossProduct(positions[polygon[k].vertex]-anchor,positions[polygon[(k+1)%polygon.size()].vertex]-anchor);
        int drop = 0; if (std::abs(newell.y()) > std::abs(newell[drop])) drop = 1; if (std::abs(newell.z()) > std::abs(newell[drop])) drop = 2;
        const int u = (drop+1)%3, v = (drop+2)%3;
        const auto cross2 = [&](const Corner& a,const Corner& b,const Corner& c) {
          const auto p = positions[a.vertex], q = positions[b.vertex], r = positions[c.vertex];
          return (double(q[u])-p[u])*(double(r[v])-p[v])-(double(q[v])-p[v])*(double(r[u])-p[u]);
        };
        double area = 0; for (size_t k = 0; k < polygon.size(); ++k) {
          const auto a = positions[polygon[k].vertex]-anchor, b = positions[polygon[(k+1)%polygon.size()].vertex]-anchor; area += double(a[u])*b[v]-double(b[u])*a[v];
        }
        if (std::abs(area) < 1e-20) return error("Cannot triangulate a degenerate polygon.");
        const double sign = area > 0 ? 1 : -1;
        while (polygon.size() > 3) {
          bool clipped = false;
          for (size_t k = 0; k < polygon.size(); ++k) {
            const size_t a = (k+polygon.size()-1)%polygon.size(), c = (k+1)%polygon.size();
            if (sign*cross2(polygon[a],polygon[k],polygon[c]) <= 1e-20) continue;
            bool inside = false;
            for (size_t j = 0; j < polygon.size(); ++j) if (j != a && j != k && j != c &&
                sign*cross2(polygon[a],polygon[k],polygon[j]) >= 0 && sign*cross2(polygon[k],polygon[c],polygon[j]) >= 0 &&
                sign*cross2(polygon[c],polygon[a],polygon[j]) >= 0) { inside = true; break; }
            if (inside) continue;
            emit_triangle(polygon[a],polygon[k],polygon[c]); polygon.erase(polygon.begin()+static_cast<ptrdiff_t>(k)); clipped = true; break;
          }
          if (!clipped) return error("Cannot triangulate a self-intersecting or degenerate polygon. Export triangles.");
        }
        emit_triangle(polygon[0],polygon[1],polygon[2]);
      }
      if (mesh->triangles.size() > 500000) return error("Geometry exceeds 500,000 triangles.");
    } else if (tag == "mtllib") {
      const auto name = QString::fromUtf8(line.simplified().mid(7).trimmed());
      if (name.isEmpty() || name.size() > 1024 || mesh->material_libraries.size() >= 16) return error("Invalid material-library reference.");
      mesh->material_libraries.append(name);
    } else if (tag == "usemtl") {
      const auto name = QString::fromUtf8(line.simplified().mid(7).trimmed());
      if (name.isEmpty() || name.size() > 80) return error("Invalid material name.");
      auto slot = mesh->material_names.indexOf(name);
      if (slot < 0) { if (mesh->material_names.size() >= 64) return error("A mesh supports up to 64 material slots."); mesh->material_names.append(name); slot = mesh->material_names.size()-1; }
      material = static_cast<int>(slot);
    } else if (tag != "o" && tag != "g" && tag != "s" && tag != "usemtl" && tag != "mtllib") return error("Unsupported geometry statement. Export a polygon OBJ.");
  }
  if (mesh->triangles.empty()) return fail("The OBJ contains no usable triangles.");
  std::function<quint32(size_t,size_t)> build = [&](size_t first,size_t count) {
    const auto node_index = static_cast<quint32>(mesh->nodes.size()); mesh->nodes.emplace_back();
    if (canceled && canceled->load()) return node_index;
    QVector3D lower(1e30F,1e30F,1e30F), upper(-1e30F,-1e30F,-1e30F);
    for (size_t i = first; i < first+count; ++i) {
      const auto& t = mesh->triangles[i]; const auto p = gaussian_mesh_vector(t.position);
      for (const auto& vertex : {p,p+gaussian_mesh_vector(t.edge1),p+gaussian_mesh_vector(t.edge2)})
        for (int k = 0; k < 3; ++k) { lower[k] = std::min(lower[k],vertex[k]); upper[k] = std::max(upper[k],vertex[k]); }
    }
    mesh->nodes[node_index].lower = {lower.x(),lower.y(),lower.z(),0}; mesh->nodes[node_index].upper = {upper.x(),upper.y(),upper.z(),0};
    if (count <= 4) mesh->nodes[node_index].links = {0,0,static_cast<quint32>(first),static_cast<quint32>(count)};
    else {
      const auto extent = upper-lower; int axis = extent.y() > extent.x() ? 1 : 0; if (extent.z() > extent[axis]) axis = 2;
      const size_t half = count/2;
      const auto centroid = [axis](const GaussianMeshTriangle& t) { return t.position[static_cast<size_t>(axis)]+(t.edge1[static_cast<size_t>(axis)]+t.edge2[static_cast<size_t>(axis)])/3; };
      std::nth_element(mesh->triangles.begin()+static_cast<ptrdiff_t>(first),mesh->triangles.begin()+static_cast<ptrdiff_t>(first+half),mesh->triangles.begin()+static_cast<ptrdiff_t>(first+count),
          [&](const auto& a,const auto& b) { return centroid(a) < centroid(b); });
      const auto left = build(first,half), right = build(first+half,count-half);
      mesh->nodes[node_index].links = {left,right,0,0};
    }
    return node_index;
  };
  build(0,mesh->triangles.size());
  if (canceled && canceled->load()) return fail("Geometry import canceled.");
  mesh->lower = gaussian_mesh_vector(mesh->nodes[0].lower); mesh->upper = gaussian_mesh_vector(mesh->nodes[0].upper);
  return {mesh,{},bytes};
}
inline GaussianMeshLoad load_gaussian_obj(const QString& path, const std::shared_ptr<std::atomic_bool>& canceled = {}) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 64*1024*1024) return {{},"Cannot read an OBJ up to 64 MiB.",{}};
  return parse_gaussian_obj(file.readAll(),canceled);
}
inline bool valid_gaussian_mesh_hash(const QString& hash) {
  if (hash.size() != 64) return false;
  for (const auto c : hash) if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f')))) return false;
  return true;
}
inline QString gaussian_mesh_asset_path(const QString& project,const QString& hash) { return QDir(project).filePath("geometry/"+hash+".obj"); }
inline GaussianMeshLoad load_gaussian_mesh_asset(const QString& project,const QString& hash, const std::shared_ptr<std::atomic_bool>& canceled = {}) {
  const auto path = gaussian_mesh_asset_path(project,hash), directory = QDir(project).filePath("geometry");
  if (!valid_gaussian_mesh_hash(hash) || QFileInfo(directory).isSymLink() || QFileInfo(path).isSymLink()) return {{},"Geometry must remain inside the project.",{}};
  auto loaded = load_gaussian_obj(path,canceled);
  if (loaded.mesh && loaded.mesh->sha256 != hash) return {{},"Project geometry failed its integrity check.",{}};
  loaded.source.clear(); return loaded;
}
inline QString store_gaussian_mesh_asset(const QString& project,const GaussianMeshLoad& loaded) {
  if (!loaded.mesh || !valid_gaussian_mesh_hash(loaded.mesh->sha256) || loaded.source.isEmpty()) return "No validated geometry to store.";
  const auto directory = QDir(project).filePath("geometry"), path = gaussian_mesh_asset_path(project,loaded.mesh->sha256);
  if (QFileInfo(directory).isSymLink() || QFileInfo(path).isSymLink() || !QDir().mkpath(directory)) return "Cannot create the project's geometry directory.";
  if (QFileInfo::exists(path)) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() != loaded.source.size() || file.readAll() != loaded.source) return "Existing geometry failed its integrity check.";
    return {};
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(loaded.source) != loaded.source.size() || !file.commit()) return "Cannot save project geometry.";
  return {};
}
// Ray parameter is preserved through nonuniform object transforms. Used by
// focus picking; GPU traversal uses the same data, with an independent kernel.
inline double gaussian_mesh_hit(const GaussianSceneMesh& mesh,QVector3D origin,QVector3D ray) {
  double nearest = 1e30; std::array<quint32,64> stack{}; size_t top = 1;
  while (top > 0) {
    const auto& node = mesh.nodes[stack[--top]];
    double enter = 0, leave = nearest;
    for (int k = 0; k < 3; ++k) {
      const double margin = 4.768371582e-7*std::max({1.0,std::abs(double(node.lower[static_cast<size_t>(k)])),std::abs(double(node.upper[static_cast<size_t>(k)]))});
      if (std::abs(ray[k]) < 1e-20F) { if (origin[k] < node.lower[static_cast<size_t>(k)]-margin || origin[k] > node.upper[static_cast<size_t>(k)]+margin) leave = -1; }
      else { const double a = (double(node.lower[static_cast<size_t>(k)])-margin-origin[k])/ray[k], b = (double(node.upper[static_cast<size_t>(k)])+margin-origin[k])/ray[k]; enter = std::max(enter,std::min(a,b)); leave = std::min(leave,std::max(a,b)); }
    }
    if (leave < enter) continue;
    if (node.links[3] == 0) { stack[top++] = node.links[0]; stack[top++] = node.links[1]; continue; }
    for (quint32 j = 0; j < node.links[3]; ++j) {
      const auto& t = mesh.triangles[node.links[2]+j];
      const auto e1 = gaussian_mesh_vector(t.edge1), e2 = gaussian_mesh_vector(t.edge2), p = QVector3D::crossProduct(ray,e2);
      const double det = QVector3D::dotProduct(e1,p); if (std::abs(det) < 1e-20) continue;
      const auto delta = origin-gaussian_mesh_vector(t.position); const double u = QVector3D::dotProduct(delta,p)/det;
      if (u < -1e-6 || u > 1+1e-6) continue;
      const auto q = QVector3D::crossProduct(delta,e1); const double v = QVector3D::dotProduct(ray,q)/det;
      if (v < -1e-6 || u+v > 1+1e-6) continue;
      const double distance = QVector3D::dotProduct(e2,q)/det;
      if (distance > 1e-5) nearest = std::min(nearest,distance);
    }
  }
  return nearest;
}
} // namespace photo_editor::desktop

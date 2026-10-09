#include "job_admission_policy.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif
using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  int checks = 0;
  const auto check = [&](bool value, const char* reason) {
    ++checks; if (!value) std::cerr << reason << '\n'; return value;
  };
  const auto write = [&](const QString& path) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write("fixture") == 7;
  };
  QDir().mkpath(root.filePath("card")); QDir().mkpath(root.filePath("heavy"));
  QDir().mkpath(root.filePath("exports")); QDir().mkpath(root.filePath("managed"));
  if (!write(root.filePath("card/frame.jpg")) || !write(root.filePath("heavy/document.json")) ||
      !write(root.filePath("heavy/output.mp4"))) return 1;
  PersistentJobRecord heavy;
  heavy.job_kind = "timeline_render"; heavy.source_path = root.filePath("heavy/document.json");
  heavy.output_path = root.filePath("heavy/output.mp4");
  PersistentJobRecord copy;
  copy.source_path = root.filePath("card/frame.jpg"); copy.destination_path = root.filePath("exports");
  copy.output_path = root.filePath("exports/frame.jpg"); copy.media_type = "photo"; copy.preset_name = "Original Copy";
  const auto h = inspect_job_resources(heavy), c = inspect_job_resources(copy);
  const auto budget = job_admission_budget(8, 16ULL << 30, false, false);
  if (!check(h.lane == JobResourceLane::HeavyRender && c.lane == JobResourceLane::LightIo, "classification") ||
      !check(admit_job_resources(c, {h}, budget) && admit_job_resources(h, {c}, budget), "heavy/light symmetry") ||
      !check(!admit_job_resources(h, {h}, budget), "GPU/heavy limit") ||
      !check(!admit_job_resources(c, {c}, budget), "I/O lane limit") ||
      !check(!admit_job_resources(c, {h,c}, budget), "total concurrency limit") ||
      !check(!admit_job_resources(c, {h}, {2,1}), "memory reserve") ||
      !check(admit_job_resources({}, {}, {}), "unknown jobs still run alone") ||
      !check(!admit_job_resources({}, {h}, budget) && !admit_job_resources(c, {{}}, budget), "unknown jobs exclusive")) return 1;
  for (const auto limits : {job_admission_budget(2,16ULL<<30,false,false), job_admission_budget(8,0,false,false),
       job_admission_budget(8,4ULL<<30,false,false), job_admission_budget(8,16ULL<<30,true,false),
       job_admission_budget(8,16ULL<<30,false,true)}) {
    if (!check(!admit_job_resources(c,{h},limits), "capacity/pressure/foreground guards")) return 1;
  }
  if (!check(job_resource_path("  " + copy.source_path + "  ") == job_resource_path(copy.source_path), "execution path trimming") ||
      !check(job_paths_overlap(QStringLiteral("/root/\u00e9/file"), QStringLiteral("/root/e\u0301")), "Unicode path aliases")) return 1;
  auto shared = copy; shared.destination_path = root.filePath("heavy");
  if (!check(!admit_job_resources(inspect_job_resources(shared), {h}, budget), "shared output tree")) return 1;
  shared = copy; shared.source_path = heavy.output_path;
  if (!check(!admit_job_resources(inspect_job_resources(shared), {h}, budget), "read/write dependency")) return 1;
  shared = copy; shared.source_path = heavy.source_path;
  if (!check(!admit_job_resources(inspect_job_resources(shared), {h}, budget), "timeline document update")) return 1;
#if defined(Q_OS_UNIX)
  if (::symlink(QFile::encodeName(root.filePath("heavy")).constData(), QFile::encodeName(root.filePath("alias")).constData()) != 0) return 1;
  shared = copy; shared.destination_path = root.filePath("alias/not-created");
  if (!check(!admit_job_resources(inspect_job_resources(shared), {h}, budget), "symlinked missing descendant")) return 1;
  if (::link(QFile::encodeName(heavy.output_path).constData(), QFile::encodeName(root.filePath("hardlink.mp4")).constData()) != 0) return 1;
  shared = copy; shared.source_path = root.filePath("hardlink.mp4");
  if (!check(!admit_job_resources(inspect_job_resources(shared), {h}, budget), "hardlink input/output alias")) return 1;
#endif
  if (!check(job_paths_overlap("/a/OUT/file", "/a/out") && !job_paths_overlap("/a/outside", "/a/out"), "case and component boundaries")) return 1;
  shared = copy; shared.preset_name = "JPEG High Quality";
  if (!check(inspect_job_resources(shared).lane == JobResourceLane::HeavyRender, "rendered photo needs heavy lane")) return 1;
  shared = copy; shared.destination_path.clear();
  if (!check(inspect_job_resources(shared).lane == JobResourceLane::Exclusive, "missing write target")) return 1;
  PersistentJobRecord import;
  import.job_kind = "catalog_import"; import.source_path = root.filePath("card");
  import.metadata_json = {{"selectedPaths", QJsonArray{root.filePath("card/frame.jpg")}}, {"importMode", "managed"},
      {"duplicatePolicy", "skip"}, {"importPreflight", QJsonObject{{"photoLibraryRoot", root.filePath("managed")}}}};
  auto claim = inspect_job_resources(import);
  if (!check(claim.lane == JobResourceLane::LightIo && claim.import_metadata_threads == 2 &&
      admit_job_resources(claim, {h}, budget), "bounded managed import")) return 1;
  auto changed = import; changed.metadata_json.insert("duplicatePolicy", "replace");
  if (!check(inspect_job_resources(changed).lane == JobResourceLane::Exclusive, "replacement import exclusive")) return 1;
  changed = import; changed.metadata_json.insert("selectedPaths", QJsonArray{});
  if (!check(inspect_job_resources(changed).lane == JobResourceLane::Exclusive, "unbounded directory import exclusive")) return 1;
  changed = import; changed.metadata_json.insert("selectedPaths", QJsonArray{root.filePath("heavy/document.json")});
  if (!check(inspect_job_resources(changed).lane == JobResourceLane::Exclusive, "escaped source exclusive")) return 1;
  changed = import; changed.metadata_json.insert("importPreflight", QJsonObject{{"photoLibraryRoot", root.filePath("heavy")}});
  if (!check(!admit_job_resources(inspect_job_resources(changed), {h}, budget), "managed root conflict")) return 1;
  changed = import; changed.metadata_json.insert("importMode", "referenced");
  if (!check(inspect_job_resources(changed).lane == JobResourceLane::LightIo, "bounded referenced import")) return 1;
  if (!check(job_execution_specification(copy) != job_execution_specification(shared), "resource specification change")) return 1;
  QFile large(root.filePath("card/large.jpg"));
  if (!large.open(QIODevice::WriteOnly) || !large.resize(257LL * 1024 * 1024)) return 1;
  large.close(); shared = copy; shared.source_path = large.fileName();
  if (!check(inspect_job_resources(shared).lane == JobResourceLane::Exclusive, "large copy exclusive")) return 1;
  changed = import; changed.metadata_json.insert("selectedPaths", QJsonArray{large.fileName()});
  if (!check(inspect_job_resources(changed).lane == JobResourceLane::Exclusive, "large import exclusive")) return 1;
  PersistentJobRecord a, b, legacy;
  a.metadata_json = {{"queueOrder", 1}}; a.created_at = "2026-09-03";
  b.metadata_json = {{"queueOrder", 2}}; b.created_at = "2026-09-01";
  legacy.created_at = "2026-09-02";
  if (!check(job_order_less_than(a,b) && job_order_less_than(b,legacy) &&
      job_order_less_than(a,legacy) && !job_order_less_than(legacy,a), "mixed ordering is transitive") ||
      !check(potential_job_resource_lane(copy) == JobResourceLane::LightIo &&
          potential_job_resource_lane(heavy) == JobResourceLane::HeavyRender, "lane prefilter")) return 1;
  std::cout << checks << " admission, capacity, memory, path, alias and workload checks passed\n";
}

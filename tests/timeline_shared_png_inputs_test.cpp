#include "timeline_shared_png_inputs.h"

#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
QByteArray execute(const QStringList& arguments) {
  QProcess process;
  process.start(QStringLiteral("ffmpeg"), arguments);
  require(process.waitForStarted(5000), "FFmpeg did not start");
  require(process.waitForFinished(60000), "FFmpeg timed out");
  if (process.exitCode() != 0) std::cerr << process.readAllStandardError().constData();
  require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
          "FFmpeg failed");
  return process.readAllStandardOutput();
}
void write_graph(const QString& path, const QString& graph) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly), "Cannot create graph");
  require(file.write(graph.toUtf8()) == graph.toUtf8().size(), "Graph write failed");
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) return 77;
  using photo_editor::desktop::timeline_motion::bounded_h264::share_identical_png_inputs;
  QTemporaryDir temporary;
  require(temporary.isValid(), "Missing temporary directory");
  const auto png = temporary.filePath(QStringLiteral("title.png"));
  QImage image(64, 48, QImage::Format_RGBA8888);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x)
      image.setPixelColor(x, y, QColor(x * 4, y * 5, (x + y) * 2, 30 + (x * 3 + y) % 225));
  }
  require(image.save(png), "Cannot save PNG");
  const auto graph_path = temporary.filePath(QStringLiteral("before.ffgraph"));
  QStringList arguments{
      "-hide_banner", "-loglevel", "error", "-filter_complex_threads", "1",
      "-f", "lavfi", "-i", "color=c=blue:s=64x48:r=30:d=0.1",
      "-framerate", "30.000000000", "-i", png,
      "-framerate", "30.000000000", "-i", png,
      "-loop", "1", "-framerate", "30.000000000", "-t", "0.033333333", "-i", png,
      "-loop", "1", "-framerate", "30.000000000", "-t", "0.033333333", "-i", png,
      "-f", "lavfi", "-i", "testsrc2=s=64x48:r=30:d=0.1",
      "-filter_complex_script", graph_path, "-map", "[outv]", "-an", "-vsync", "0",
      "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1"};
  const QString graph =
      "[0:v]trim=end_frame=1,setpts=PTS-STARTPTS,format=rgba,setsar=1[s0];"
      "[1:v]loop=loop=1:size=1:start=0,setpts=N/(30*TB),"
      "trim=end_frame=2,format=rgba,rotate='0.02*t':ow=64:oh=48,setsar=1[s1];"
      "[2:v]loop=loop=2:size=1:start=0,setpts=N/(30*TB),"
      "trim=end_frame=3,format=rgba,colorchannelmixer=aa=0.7,setsar=1[s2];"
      "[3:v]trim=end_frame=1,setpts=PTS-STARTPTS,format=rgba,setsar=1[s3];"
      "[4:v]trim=end_frame=1,setpts=PTS-STARTPTS,format=rgba,hflip,setsar=1[s4];"
      "[5:v]trim=end_frame=1,setpts=PTS-STARTPTS,format=rgba,setsar=1[s5];"
      "[s0][s1][s2][s3][s4][s5]concat=n=6:v=1:a=0[outv]";
  const auto result = share_identical_png_inputs(arguments, graph);
  require(result.removed_inputs == 2, "Expected two identical PNG inputs removed");
  require(result.arguments.count("-i") == 4, "Wrong rewritten input count");
  write_graph(graph_path, graph);
  const auto before = execute(arguments);
  const auto rewritten_path = temporary.filePath(QStringLiteral("after.ffgraph"));
  write_graph(rewritten_path, result.graph);
  auto after_arguments = result.arguments;
  after_arguments[after_arguments.indexOf("-filter_complex_script") + 1] = rewritten_path;
  const auto after = execute(after_arguments);
  require(before.size() == 9 * 64 * 48 * 4, "Wrong original frame count");
  require(before == after, "Decoded RGBA pixels changed");
  auto hash_arguments = arguments;
  hash_arguments[hash_arguments.indexOf("rawvideo")] = "framemd5";
  auto optimized_hash_arguments = after_arguments;
  optimized_hash_arguments[optimized_hash_arguments.indexOf("rawvideo")] = "framemd5";
  require(execute(hash_arguments) == execute(optimized_hash_arguments), "Frame PTS/hash changed");

  auto different_rate = arguments;
  different_rate[different_rate.indexOf(png, different_rate.indexOf(png) + 1) - 2] = "24";
  require(share_identical_png_inputs(different_rate, graph).removed_inputs == 1,
          "Different decoder frame rates shared");
  auto unequal_duration = arguments;
  unequal_duration[unequal_duration.lastIndexOf(png) - 2] = "0.100000000";
  require(share_identical_png_inputs(unequal_duration, graph).removed_inputs == 1,
          "Unequal input durations shared");
  auto seek_input = arguments;
  const auto first_path = seek_input.indexOf(png);
  seek_input.insert(first_path - 3, "-ss");
  seek_input.insert(first_path - 2, "0");
  require(share_identical_png_inputs(seek_input, graph).removed_inputs == 1,
          "Seek input shared");
  auto numeric_map = arguments;
  numeric_map[numeric_map.indexOf("-map") + 1] = "0:v";
  require(share_identical_png_inputs(numeric_map, graph).removed_inputs == 0,
          "Numeric output map was not preserved");
  require(share_identical_png_inputs(arguments, graph + ";[999:v]null[bad]").removed_inputs == 0,
          "Unknown input index did not preserve original");
  require(share_identical_png_inputs(arguments, graph + ";[2:a]anullsink").removed_inputs == 0,
          "Audio or multiple-use input shared");
  const auto quoted = graph + ";nullsrc,drawtext=text='literal [2:v]'[unused]";
  require(share_identical_png_inputs(arguments, quoted).graph.contains("literal [2:v]"),
          "Quoted text was rewritten");
  auto sequence = arguments;
  sequence[sequence.indexOf(png)] = temporary.filePath("frame-%06d.png");
  require(share_identical_png_inputs(sequence, graph).removed_inputs == 1,
          "Image sequence shared");
  std::cout << "shared PNG inputs: exact 9-frame RGBA/PTS parity, 6 to 4 inputs, conservative exclusions passed\n";
}

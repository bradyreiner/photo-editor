#pragma once
#include <QFile>
#include <QString>
#include <cerrno>
#include <cstring>
#ifdef Q_OS_MACOS
#include <stdio.h>
#include <sys/stat.h>
#endif

namespace photo_editor::desktop {
struct GaussianFilmPublication {
  bool ok = false;
  bool receipt_retained = false;
  QString error;
};

// Both staged files must be closed and on the destination filesystem. Never
// fall back to copying: the final name must not expose an incomplete movie.
inline GaussianFilmPublication publish_gaussian_media_files(const QString& movie,
    const QString& receipt, const QString& destination, const QString& receipt_suffix) {
#ifdef Q_OS_MACOS
  const auto staged_movie = QFile::encodeName(movie);
  const auto staged_receipt = QFile::encodeName(receipt);
  const auto final_movie = QFile::encodeName(destination);
  const auto final_receipt = QFile::encodeName(destination + receipt_suffix);
  struct stat movie_info {}, receipt_info {};
  if (::lstat(staged_movie.constData(), &movie_info) != 0 || !S_ISREG(movie_info.st_mode) ||
      ::lstat(staged_receipt.constData(), &receipt_info) != 0 || !S_ISREG(receipt_info.st_mode) ||
      movie_info.st_size <= 0 || receipt_info.st_size <= 0)
    return {false, false, "Media publication requires complete, regular staged files."};
  if (::renamex_np(staged_receipt.constData(), final_receipt.constData(), RENAME_EXCL) != 0)
    return {false, false, "Cannot publish camera receipt without replacing an existing file: " +
        QString::fromLocal8Bit(std::strerror(errno))};
  // The movie name is the commit marker. An ordinary failure rolls back only
  // our receipt. A process/power loss between these two renames can still leave
  // an orphan receipt; this is not a crash-atomic two-file transaction.
  if (::renamex_np(staged_movie.constData(), final_movie.constData(), RENAME_EXCL) == 0)
    return {true, false, {}};
  const auto error = QString::fromLocal8Bit(std::strerror(errno));
  struct stat published_info {};
  const bool owned = ::lstat(final_receipt.constData(), &published_info) == 0 &&
      published_info.st_dev == receipt_info.st_dev && published_info.st_ino == receipt_info.st_ino;
  const bool rolled_back = owned &&
      ::renamex_np(final_receipt.constData(), staged_receipt.constData(), RENAME_EXCL) == 0;
  return {false, !rolled_back, "Cannot publish media without replacing an existing file: " + error +
      (rolled_back ? ". Camera receipt rolled back." : ". Camera receipt could not be rolled back; inspect " +
          destination + receipt_suffix)};
#else
  Q_UNUSED(movie);
  Q_UNUSED(receipt);
  Q_UNUSED(destination);
  Q_UNUSED(receipt_suffix);
  return {false, false, "Atomic Gaussian media publication requires macOS."};
#endif
}
inline GaussianFilmPublication publish_gaussian_film_files(const QString& movie,
    const QString& receipt, const QString& destination) {
  return publish_gaussian_media_files(movie, receipt, destination, QStringLiteral(".camera-path.json"));
}
// A sequence and its receipt are published together by one exclusive directory
// rename. Staging must be beside the destination, on the same filesystem.
inline GaussianFilmPublication publish_gaussian_frame_sequence(const QString& staged, const QString& destination) {
  if (staged.contains(QChar::Null) || destination.contains(QChar::Null)) return {false,false,"Invalid sequence publication path."};
#ifdef Q_OS_MACOS
  const auto source = QFile::encodeName(staged), target = QFile::encodeName(destination);
  struct stat info {};
  if (::lstat(source.constData(),&info) != 0 || !S_ISDIR(info.st_mode))
    return {false,false,"Sequence publication requires a complete staged directory."};
  if (::renamex_np(source.constData(),target.constData(),RENAME_EXCL) == 0) return {true,false,{}};
  return {false,false,"Cannot publish frame sequence without replacing an existing destination: " + QString::fromLocal8Bit(std::strerror(errno))};
#else
  Q_UNUSED(staged); Q_UNUSED(destination);
  return {false,false,"Atomic Gaussian sequence publication requires macOS."};
#endif
}
}  // namespace photo_editor::desktop

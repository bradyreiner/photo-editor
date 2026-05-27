use std::collections::hash_map::DefaultHasher;
use std::collections::HashSet;
use std::ffi::OsStr;
use std::fs::File;
use std::hash::{Hash, Hasher};
use std::io::BufReader;
use std::path::{Path, PathBuf};
use std::thread;
use std::time::SystemTime;

use exif::{In, Reader, Tag, Value};
use time::{Month, OffsetDateTime};
use walkdir::WalkDir;

use crate::{AssetLocation, CatalogAssetRecord, DriveRecord, ImportMode};

const SUPPORTED_EXTENSIONS: &[&str] = &[
    "3fr", "arw", "avif", "bmp", "cr2", "cr3", "dng", "erf", "gif", "heic", "heif", "iiq", "jpeg",
    "jpg", "kdc", "m4v", "mef", "mos", "mov", "mp4", "mpeg", "mpg", "nef", "nrw", "orf", "pef",
    "png", "raf", "raw", "rw2", "sr2", "srf", "tif", "tiff", "webm", "webp", "x3f",
];

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImportRequest {
    pub drive_id: String,
    pub drive_name: String,
    pub mount_path: PathBuf,
    pub import_mode: ImportMode,
    pub selected_paths: Vec<PathBuf>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ImportSummary {
    pub drive: DriveRecord,
    pub assets: Vec<CatalogAssetRecord>,
    pub skipped_unsupported: usize,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct ExtractedMetadata {
    pub capture_year: u16,
    pub capture_month: u8,
    pub capture_day: u8,
    pub camera_make: String,
    pub camera_model: String,
    pub lens_model: String,
    pub iso: Option<i32>,
    pub shutter_speed: String,
    pub aperture: Option<f64>,
}

pub fn discover_assets(request: &ImportRequest) -> std::io::Result<ImportSummary> {
    let mut skipped_unsupported = 0_usize;

    let mount_path_str = path_to_utf8(&request.mount_path)?;
    let mut candidates = Vec::new();

    for path in candidate_file_paths(request)? {
        if !is_supported_photo_path(&path) {
            skipped_unsupported += 1;
            continue;
        }

        let relative_path = match path.strip_prefix(&request.mount_path) {
            Ok(relative) => path_to_utf8(relative)?.replace('\\', "/"),
            Err(_) => path_to_utf8(&path)?,
        };
        candidates.push((path, relative_path));
    }

    let candidate_paths = candidates
        .iter()
        .map(|(path, _)| path.clone())
        .collect::<Vec<_>>();
    let metadata = extract_metadata_parallel(&candidate_paths);

    let mut assets = Vec::with_capacity(candidates.len());
    for ((_, relative_path), metadata) in candidates.into_iter().zip(metadata.into_iter()) {
        assets.push(CatalogAssetRecord {
            asset_id: stable_asset_id(&request.drive_id, &relative_path),
            capture_year: metadata.capture_year,
            capture_month: metadata.capture_month,
            capture_day: metadata.capture_day,
            camera_make: metadata.camera_make,
            camera_model: metadata.camera_model,
            lens_model: metadata.lens_model,
            iso: metadata.iso,
            shutter_speed: metadata.shutter_speed,
            aperture: metadata.aperture,
            import_mode: request.import_mode.clone(),
            location: AssetLocation {
                drive_id: request.drive_id.clone(),
                root_path: mount_path_str.clone(),
                relative_path,
                online: true,
            },
        });
    }

    let drive = DriveRecord {
        drive_id: request.drive_id.clone(),
        display_name: request.drive_name.clone(),
        mount_path: mount_path_str,
        online: true,
    };

    Ok(ImportSummary {
        drive,
        assets,
        skipped_unsupported,
    })
}

fn extract_metadata_parallel(paths: &[PathBuf]) -> Vec<ExtractedMetadata> {
    if paths.len() < 2 {
        return paths
            .iter()
            .map(|path| extract_metadata(path).unwrap_or_else(|_| fallback_metadata(path)))
            .collect();
    }

    let worker_count = thread::available_parallelism()
        .map(|value| value.get())
        .unwrap_or(1)
        .clamp(1, paths.len());
    let chunk_size = (paths.len() + worker_count - 1) / worker_count;
    let mut indexed_metadata = thread::scope(|scope| {
        let mut handles = Vec::with_capacity(worker_count);
        for (chunk_index, chunk) in paths.chunks(chunk_size).enumerate() {
            let begin = chunk_index * chunk_size;
            handles.push(scope.spawn(move || {
                chunk
                    .iter()
                    .enumerate()
                    .map(|(offset, path)| {
                        (
                            begin + offset,
                            extract_metadata(path).unwrap_or_else(|_| fallback_metadata(path)),
                        )
                    })
                    .collect::<Vec<_>>()
            }));
        }

        let mut values = Vec::with_capacity(paths.len());
        for handle in handles {
            values.extend(handle.join().expect("metadata worker should not panic"));
        }
        values
    });
    indexed_metadata.sort_by_key(|(index, _)| *index);
    indexed_metadata
        .into_iter()
        .map(|(_, metadata)| metadata)
        .collect()
}

fn candidate_file_paths(request: &ImportRequest) -> std::io::Result<Vec<PathBuf>> {
    if request.selected_paths.is_empty() {
        let mut paths = Vec::new();
        for entry in WalkDir::new(&request.mount_path) {
            let entry = match entry {
                Ok(entry) => entry,
                Err(error) => return Err(std::io::Error::other(error)),
            };

            if entry.file_type().is_file() {
                paths.push(entry.into_path());
            }
        }
        return Ok(paths);
    }

    let mut paths = Vec::new();
    let mut seen = HashSet::new();

    for selected_path in &request.selected_paths {
        let absolute_path = if selected_path.is_absolute() {
            selected_path.clone()
        } else {
            request.mount_path.join(selected_path)
        };

        if absolute_path.is_dir() {
            for entry in WalkDir::new(&absolute_path) {
                let entry = match entry {
                    Ok(entry) => entry,
                    Err(error) => return Err(std::io::Error::other(error)),
                };

                if !entry.file_type().is_file() {
                    continue;
                }

                let path = entry.into_path();
                if seen.insert(path.clone()) {
                    paths.push(path);
                }
            }
            continue;
        }

        if absolute_path.is_file() && seen.insert(absolute_path.clone()) {
            paths.push(absolute_path);
        }
    }

    Ok(paths)
}

pub fn is_supported_photo_path(path: &Path) -> bool {
    path.extension()
        .and_then(OsStr::to_str)
        .map(|extension| {
            let lowercase = extension.to_ascii_lowercase();
            SUPPORTED_EXTENSIONS.contains(&lowercase.as_str())
        })
        .unwrap_or(false)
}

pub fn extract_metadata(path: &Path) -> std::io::Result<ExtractedMetadata> {
    let file = File::open(path)?;
    let mut reader = BufReader::new(file);
    let exif = Reader::new()
        .read_from_container(&mut reader)
        .map_err(std::io::Error::other)?;

    let mut metadata = ExtractedMetadata::default();

    if let Some(date) = field_string(&exif, Tag::DateTimeOriginal)
        .or_else(|| field_string(&exif, Tag::DateTimeDigitized))
        .or_else(|| field_string(&exif, Tag::DateTime))
    {
        if let Some((year, month, day)) = parse_exif_date(&date) {
            metadata.capture_year = year;
            metadata.capture_month = month;
            metadata.capture_day = day;
        }
    }

    metadata.camera_make = field_string(&exif, Tag::Make).unwrap_or_default();
    metadata.camera_model = field_string(&exif, Tag::Model).unwrap_or_default();
    metadata.lens_model = field_string(&exif, Tag::LensModel)
        .or_else(|| field_string(&exif, Tag::LensSpecification))
        .unwrap_or_default();
    metadata.iso = field_unsigned_i32(&exif, Tag::PhotographicSensitivity)
        .or_else(|| field_unsigned_i32(&exif, Tag::ISOSpeed));
    metadata.shutter_speed = field_string(&exif, Tag::ExposureTime).unwrap_or_default();
    metadata.aperture = field_rational_f64(&exif, Tag::FNumber);

    if metadata.capture_year == 0 || metadata.capture_month == 0 || metadata.capture_day == 0 {
        merge_missing_capture_date(&mut metadata, fallback_metadata(path));
    }

    Ok(metadata)
}

pub fn fallback_metadata(path: &Path) -> ExtractedMetadata {
    let mut metadata = ExtractedMetadata::default();

    if let Ok(file_metadata) = std::fs::metadata(path) {
        if let Ok(system_time) = file_metadata
            .modified()
            .or_else(|_| file_metadata.created())
        {
            if let Some((year, month, day)) = system_time_to_ymd(system_time) {
                metadata.capture_year = year;
                metadata.capture_month = month;
                metadata.capture_day = day;
            }
        }
    }

    metadata
}

fn merge_missing_capture_date(metadata: &mut ExtractedMetadata, fallback: ExtractedMetadata) {
    if metadata.capture_year != 0 && metadata.capture_month != 0 && metadata.capture_day != 0 {
        return;
    }

    if fallback.capture_year == 0 || fallback.capture_month == 0 || fallback.capture_day == 0 {
        return;
    }

    metadata.capture_year = fallback.capture_year;
    metadata.capture_month = fallback.capture_month;
    metadata.capture_day = fallback.capture_day;
}

fn field_string(exif: &exif::Exif, tag: Tag) -> Option<String> {
    exif.get_field(tag, In::PRIMARY)
        .map(|field| field.display_value().with_unit(exif).to_string())
        .map(|value| value.trim().to_string())
        .filter(|value| !value.is_empty())
}

fn field_unsigned_i32(exif: &exif::Exif, tag: Tag) -> Option<i32> {
    exif.get_field(tag, In::PRIMARY)
        .and_then(|field| match &field.value {
            Value::Byte(values) => values.first().map(|value| i32::from(*value)),
            Value::Short(values) => values.first().map(|value| i32::from(*value)),
            Value::Long(values) => values.first().and_then(|value| i32::try_from(*value).ok()),
            _ => None,
        })
}

fn field_rational_f64(exif: &exif::Exif, tag: Tag) -> Option<f64> {
    exif.get_field(tag, In::PRIMARY)
        .and_then(|field| match &field.value {
            Value::Rational(values) => values.first().and_then(|value| {
                if value.denom == 0 {
                    None
                } else {
                    Some(f64::from(value.num) / f64::from(value.denom))
                }
            }),
            _ => None,
        })
}

fn parse_exif_date(value: &str) -> Option<(u16, u8, u8)> {
    let mut parts = value.split_whitespace();
    let date = parts.next()?;
    let mut date_parts = date.split(':');

    let year = date_parts.next()?.parse::<u16>().ok()?;
    let month = date_parts.next()?.parse::<u8>().ok()?;
    let day = date_parts.next()?.parse::<u8>().ok()?;

    Some((year, month, day))
}

fn path_to_utf8(path: &Path) -> std::io::Result<String> {
    path.to_str().map(|s| s.to_owned()).ok_or_else(|| {
        std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            format!("path is not valid UTF-8: {}", path.display()),
        )
    })
}

fn stable_asset_id(drive_id: &str, relative_path: &str) -> String {
    let mut hasher = DefaultHasher::new();
    drive_id.hash(&mut hasher);
    relative_path.hash(&mut hasher);
    format!("asset-{:016x}", hasher.finish())
}

fn system_time_to_ymd(system_time: SystemTime) -> Option<(u16, u8, u8)> {
    let datetime = OffsetDateTime::from(system_time);
    let year = u16::try_from(datetime.year()).ok()?;
    let month = month_to_u8(datetime.month());
    let day = datetime.day();

    Some((year, month, day))
}

fn month_to_u8(month: Month) -> u8 {
    match month {
        Month::January => 1,
        Month::February => 2,
        Month::March => 3,
        Month::April => 4,
        Month::May => 5,
        Month::June => 6,
        Month::July => 7,
        Month::August => 8,
        Month::September => 9,
        Month::October => 10,
        Month::November => 11,
        Month::December => 12,
    }
}

#[cfg(test)]
mod tests {
    use std::fs;

    use tempfile::tempdir;

    use super::*;

    #[test]
    fn supported_extensions_include_raw_and_jpeg() {
        assert!(is_supported_photo_path(Path::new("frame.CR3")));
        assert!(is_supported_photo_path(Path::new("frame.jpg")));
        assert!(!is_supported_photo_path(Path::new("notes.txt")));
    }

    #[test]
    fn discover_assets_builds_relative_records() {
        let directory = tempdir().expect("tempdir should succeed");
        let root = directory.path().join("shoot");
        fs::create_dir_all(root.join("day-1")).expect("create dir should succeed");
        fs::write(root.join("day-1").join("image001.CR3"), b"not-a-real-raw")
            .expect("write should succeed");
        fs::write(root.join("day-1").join("notes.txt"), b"skip me").expect("write should succeed");

        let request = ImportRequest {
            drive_id: "drive-main".to_string(),
            drive_name: "Main".to_string(),
            mount_path: root.clone(),
            import_mode: ImportMode::Referenced,
            selected_paths: Vec::new(),
        };

        let summary = discover_assets(&request).expect("discovery should succeed");
        assert_eq!(summary.assets.len(), 1);
        assert_eq!(summary.skipped_unsupported, 1);
        assert_eq!(
            summary.assets[0].location.relative_path,
            "day-1/image001.CR3"
        );
        assert_eq!(summary.assets[0].location.drive_id, "drive-main");
    }

    #[test]
    fn discover_assets_can_limit_import_to_selected_files() {
        let directory = tempdir().expect("tempdir should succeed");
        let root = directory.path().join("card");
        fs::create_dir_all(root.join("set-a")).expect("create dir should succeed");
        fs::create_dir_all(root.join("set-b")).expect("create dir should succeed");
        fs::write(root.join("set-a").join("keep.NEF"), b"raw-a").expect("write should succeed");
        fs::write(root.join("set-b").join("skip.NEF"), b"raw-b").expect("write should succeed");

        let request = ImportRequest {
            drive_id: "drive-card".to_string(),
            drive_name: "Card".to_string(),
            mount_path: root.clone(),
            import_mode: ImportMode::Referenced,
            selected_paths: vec![root.join("set-a").join("keep.NEF")],
        };

        let summary = discover_assets(&request).expect("discovery should succeed");
        assert_eq!(summary.assets.len(), 1);
        assert_eq!(summary.assets[0].location.relative_path, "set-a/keep.NEF");
    }

    #[test]
    fn discover_assets_preserves_selected_path_order_after_parallel_metadata() {
        let directory = tempdir().expect("tempdir should succeed");
        let root = directory.path().join("card");
        fs::create_dir_all(&root).expect("create dir should succeed");
        for file_name in ["third.jpg", "first.jpg", "fourth.jpg", "second.jpg"] {
            fs::write(root.join(file_name), b"not-real-jpeg").expect("write should succeed");
        }

        let request = ImportRequest {
            drive_id: "drive-card".to_string(),
            drive_name: "Card".to_string(),
            mount_path: root.clone(),
            import_mode: ImportMode::Referenced,
            selected_paths: vec![
                root.join("third.jpg"),
                root.join("first.jpg"),
                root.join("fourth.jpg"),
                root.join("second.jpg"),
            ],
        };

        let summary = discover_assets(&request).expect("discovery should succeed");
        let relative_paths = summary
            .assets
            .iter()
            .map(|asset| asset.location.relative_path.as_str())
            .collect::<Vec<_>>();
        assert_eq!(
            relative_paths,
            vec!["third.jpg", "first.jpg", "fourth.jpg", "second.jpg"]
        );
    }

    #[test]
    fn fallback_capture_date_preserves_extracted_camera_settings() {
        let mut metadata = ExtractedMetadata {
            camera_make: "Canon".to_string(),
            camera_model: "EOS R5".to_string(),
            lens_model: "RF 28-70mm F2L USM".to_string(),
            iso: Some(400),
            shutter_speed: "1/250 s".to_string(),
            aperture: Some(2.8),
            ..ExtractedMetadata::default()
        };

        merge_missing_capture_date(
            &mut metadata,
            ExtractedMetadata {
                capture_year: 2026,
                capture_month: 4,
                capture_day: 8,
                ..ExtractedMetadata::default()
            },
        );

        assert_eq!(metadata.capture_year, 2026);
        assert_eq!(metadata.capture_month, 4);
        assert_eq!(metadata.capture_day, 8);
        assert_eq!(metadata.camera_make, "Canon");
        assert_eq!(metadata.camera_model, "EOS R5");
        assert_eq!(metadata.lens_model, "RF 28-70mm F2L USM");
        assert_eq!(metadata.iso, Some(400));
        assert_eq!(metadata.shutter_speed, "1/250 s");
        assert_eq!(metadata.aperture, Some(2.8));
    }
}

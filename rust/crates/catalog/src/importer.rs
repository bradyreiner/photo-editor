use std::collections::hash_map::DefaultHasher;
use std::collections::{HashMap, HashSet};
use std::ffi::OsStr;
use std::fs::File;
use std::hash::{Hash, Hasher};
use std::io::{BufReader, Read};
use std::path::{Path, PathBuf};
use std::thread;
use std::time::SystemTime;

use exif::{In, Reader, Tag, Value};
use sha2::{Digest, Sha256};
use time::{Month, OffsetDateTime, PrimitiveDateTime, UtcOffset};
use walkdir::WalkDir;

use crate::{AssetLocation, CatalogAssetRecord, DriveRecord, ImportMode};

const SUPPORTED_EXTENSIONS: &[&str] = &[
    "3fr", "aac", "aif", "aiff", "arw", "avif", "bmp", "cr2", "cr3", "dng", "erf", "flac", "gif",
    "heic", "heif", "iiq", "jpeg", "jpg", "kdc", "m4a", "m4v", "mef", "mos", "mov", "mp3", "mp4",
    "mpeg", "mpg", "nef", "nrw", "ogg", "opus", "orf", "pef", "png", "raf", "raw", "rw2", "sr2",
    "srf", "tif", "tiff", "wav", "webm", "webp", "x3f",
];

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum DuplicatePolicy {
    #[default]
    Skip,
    ReuseIdentical,
    Replace,
}

impl DuplicatePolicy {
    pub fn parse(value: &str) -> std::io::Result<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "" | "skip" => Ok(Self::Skip),
            "reuse-identical" => Ok(Self::ReuseIdentical),
            "replace" => Ok(Self::Replace),
            other => Err(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                format!("unsupported duplicate policy: {other}"),
            )),
        }
    }

    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Skip => "skip",
            Self::ReuseIdentical => "reuse-identical",
            Self::Replace => "replace",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImportRequest {
    pub drive_id: String,
    pub drive_name: String,
    pub mount_path: PathBuf,
    pub import_mode: ImportMode,
    pub selected_paths: Vec<PathBuf>,
    pub managed_root: Option<PathBuf>,
    pub backup_root: Option<PathBuf>,
    pub organization_template: String,
    pub raw_jpeg_policy: String,
    pub duplicate_policy: DuplicatePolicy,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ImportSummary {
    pub drive: DriveRecord,
    pub assets: Vec<CatalogAssetRecord>,
    pub skipped_unsupported: usize,
    pub reused_existing: usize,
    pub backed_up: usize,
    pub reused_backup: usize,
    pub skipped_policy: usize,
    pub skipped_duplicates: usize,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct ExtractedMetadata {
    pub capture_year: u16,
    pub capture_month: u8,
    pub capture_day: u8,
    pub capture_timestamp: Option<String>,
    pub camera_make: String,
    pub camera_model: String,
    pub lens_model: String,
    pub iso: Option<i32>,
    pub shutter_speed: String,
    pub aperture: Option<f64>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ImportDestinationPlan {
    pub source_path: PathBuf,
    pub source_relative_path: String,
    pub relative_path: String,
    pub managed_destination: Option<PathBuf>,
    pub backup_destination: Option<PathBuf>,
    pub metadata: ExtractedMetadata,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ImportPlanningSummary {
    pub destinations: Vec<ImportDestinationPlan>,
    pub skipped_unsupported: usize,
    pub skipped_policy: usize,
}

pub fn plan_import_destinations(request: &ImportRequest) -> std::io::Result<ImportPlanningSummary> {
    let mut skipped_unsupported = 0_usize;
    let mut skipped_policy = 0_usize;

    let discovered_paths = candidate_file_paths(request)?;
    let canonical_mount_path = std::fs::canonicalize(&request.mount_path)?;
    let raw_pair_keys = discovered_paths
        .iter()
        .filter(|path| is_raw_path(path))
        .map(|path| pair_key(path))
        .collect::<HashSet<_>>();
    let jpeg_pair_keys = discovered_paths
        .iter()
        .filter(|path| is_jpeg_path(path))
        .map(|path| pair_key(path))
        .collect::<HashSet<_>>();
    let mut source_candidates = Vec::new();
    for path in discovered_paths {
        if !is_supported_photo_path(&path) {
            skipped_unsupported += 1;
            continue;
        }
        let raw_jpeg_policy = request.raw_jpeg_policy.trim().to_ascii_lowercase();
        if (raw_jpeg_policy == "raw-only"
            && is_jpeg_path(&path)
            && raw_pair_keys.contains(&pair_key(&path)))
            || (raw_jpeg_policy == "jpeg-only"
                && is_raw_path(&path)
                && jpeg_pair_keys.contains(&pair_key(&path)))
        {
            skipped_policy += 1;
            continue;
        }

        let canonical_source_path = std::fs::canonicalize(&path)?;
        let relative_path = canonical_source_path
            .strip_prefix(&canonical_mount_path)
            .map_err(|_| {
                std::io::Error::new(
                    std::io::ErrorKind::InvalidInput,
                    format!(
                        "import source is outside the selected root: {}",
                        path.display()
                    ),
                )
            })?;
        if relative_path
            .components()
            .any(|component| !matches!(component, std::path::Component::Normal(_)))
        {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                format!(
                    "import source has an unsafe relative path: {}",
                    path.display()
                ),
            ));
        }
        let relative_path = path_to_utf8(relative_path)?.replace('\\', "/");
        source_candidates.push((canonical_source_path, relative_path));
    }

    let source_paths = source_candidates
        .iter()
        .map(|(path, _)| path.clone())
        .collect::<Vec<_>>();
    let extracted_metadata = extract_metadata_parallel(&source_paths);
    let managed_drive_root = if request.import_mode == ImportMode::Managed {
        Some(managed_drive_root(request)?)
    } else {
        None
    };
    let backup_drive_root = request
        .backup_root
        .as_ref()
        .map(|root| root.join(sanitize_path_segment(&request.drive_id)));
    if managed_drive_root.is_some() && managed_drive_root == backup_drive_root {
        return Err(std::io::Error::other(
            "the originals backup must be separate from the managed library",
        ));
    }

    let mut used_relative_paths = HashSet::new();
    let destinations = source_candidates
        .into_iter()
        .zip(extracted_metadata)
        .map(|((path, source_relative_path), metadata)| {
            let mut relative_path =
                organized_relative_path(request, &source_relative_path, &metadata);
            if !used_relative_paths.insert(destination_collision_key(&relative_path)) {
                let source_hash = stable_asset_id("collision", &source_relative_path);
                relative_path = disambiguated_relative_path(&relative_path, &source_hash);
                used_relative_paths.insert(destination_collision_key(&relative_path));
            }
            ImportDestinationPlan {
                managed_destination: managed_drive_root
                    .as_ref()
                    .map(|root| root.join(&relative_path)),
                backup_destination: backup_drive_root
                    .as_ref()
                    .map(|root| root.join(&relative_path)),
                source_path: path,
                source_relative_path,
                relative_path,
                metadata,
            }
        })
        .collect::<Vec<_>>();

    Ok(ImportPlanningSummary {
        destinations,
        skipped_unsupported,
        skipped_policy,
    })
}

pub fn discover_assets(request: &ImportRequest) -> std::io::Result<ImportSummary> {
    let planning = plan_import_destinations(request)?;
    let mut sources = ImportSourceVerification::default();
    let mut reused_existing = 0_usize;
    let mut backed_up = 0_usize;
    let mut reused_backup = 0_usize;
    let mut skipped_duplicates = 0_usize;
    let mut destinations = Vec::with_capacity(planning.destinations.len());

    for destination in planning.destinations {
        let Some(managed_destination) = destination.managed_destination.as_ref() else {
            destinations.push(destination);
            continue;
        };
        if !managed_destination.exists() {
            destinations.push(destination);
            continue;
        }

        let identical = sources.identical(&destination.source_path, managed_destination)?;
        match request.duplicate_policy {
            DuplicatePolicy::Skip => skipped_duplicates += 1,
            DuplicatePolicy::ReuseIdentical => {
                if identical {
                    reused_existing += 1;
                    destinations.push(destination);
                } else {
                    skipped_duplicates += 1;
                }
            }
            DuplicatePolicy::Replace => {
                if identical {
                    reused_existing += 1;
                }
                destinations.push(destination);
            }
        }
    }

    // Back up the original source paths before any managed destination can be
    // replaced. An existing different backup is never overwritten.
    for destination in &destinations {
        let Some(backup_destination) = destination.backup_destination.as_ref() else {
            continue;
        };
        if let Some(parent) = backup_destination.parent() {
            std::fs::create_dir_all(parent)?;
        }
        if backup_destination.exists() {
            if sources.identical(&destination.source_path, backup_destination)? {
                reused_backup += 1;
            } else {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::AlreadyExists,
                    format!(
                        "refusing to overwrite a different originals backup at {}",
                        backup_destination.display()
                    ),
                ));
            }
        } else {
            copy_file_verified_atomically(
                &mut sources,
                &destination.source_path,
                backup_destination,
                PublishPolicy::CreateNew,
            )?;
        }
        backed_up += 1;
    }

    let managed_drive_root = if request.import_mode == ImportMode::Managed {
        Some(managed_drive_root(request)?)
    } else {
        None
    };
    if let Some(root) = &managed_drive_root {
        std::fs::create_dir_all(root)?;
        for destination in &destinations {
            let managed_destination = destination
                .managed_destination
                .as_ref()
                .expect("managed plans must have a managed destination");
            if let Some(parent) = managed_destination.parent() {
                std::fs::create_dir_all(parent)?;
            }
            if managed_destination.exists()
                && sources.identical(&destination.source_path, managed_destination)?
            {
                continue;
            }
            copy_file_verified_atomically(
                &mut sources,
                &destination.source_path,
                managed_destination,
                if managed_destination.exists() {
                    PublishPolicy::Replace
                } else {
                    PublishPolicy::CreateNew
                },
            )?;
        }
    }
    let catalog_root_path = managed_drive_root.as_ref().unwrap_or(&request.mount_path);
    let mount_path_str = path_to_utf8(catalog_root_path)?;

    let mut assets = Vec::with_capacity(destinations.len());
    for destination in destinations {
        assets.push(CatalogAssetRecord {
            asset_id: stable_asset_id(&request.drive_id, &destination.relative_path),
            capture_year: destination.metadata.capture_year,
            capture_month: destination.metadata.capture_month,
            capture_day: destination.metadata.capture_day,
            capture_timestamp: destination.metadata.capture_timestamp,
            camera_make: destination.metadata.camera_make,
            camera_model: destination.metadata.camera_model,
            lens_model: destination.metadata.lens_model,
            iso: destination.metadata.iso,
            shutter_speed: destination.metadata.shutter_speed,
            aperture: destination.metadata.aperture,
            import_mode: request.import_mode.clone(),
            location: AssetLocation {
                drive_id: request.drive_id.clone(),
                root_path: mount_path_str.clone(),
                relative_path: destination.relative_path,
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
        skipped_unsupported: planning.skipped_unsupported,
        reused_existing,
        backed_up,
        reused_backup,
        skipped_policy: planning.skipped_policy,
        skipped_duplicates,
    })
}

fn files_have_identical_contents(left: &Path, right: &Path) -> std::io::Result<bool> {
    if std::fs::metadata(left)?.len() != std::fs::metadata(right)?.len() {
        return Ok(false);
    }

    let mut left_reader = BufReader::new(File::open(left)?);
    let mut right_reader = BufReader::new(File::open(right)?);
    let mut left_buffer = [0_u8; 64 * 1024];
    let mut right_buffer = [0_u8; 64 * 1024];
    loop {
        let left_read = left_reader.read(&mut left_buffer)?;
        let right_read = right_reader.read(&mut right_buffer)?;
        #[cfg(test)]
        {
            copy_test::record_read(left, left_read, false);
            copy_test::record_read(right, right_read, false);
        }
        if left_read != right_read || left_buffer[..left_read] != right_buffer[..right_read] {
            return Ok(false);
        }
        if left_read == 0 {
            return Ok(true);
        }
    }
}

fn sha256_file(path: &Path) -> std::io::Result<[u8; 32]> {
    sha256_reader(File::open(path)?, path)
}

fn sha256_reader(file: File, _path: &Path) -> std::io::Result<[u8; 32]> {
    let mut reader = BufReader::new(file);
    let mut digest = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let read = reader.read(&mut buffer)?;
        #[cfg(test)]
        copy_test::record_read(_path, read, true);
        if read == 0 {
            break;
        }
        digest.update(&buffer[..read]);
    }
    #[cfg(test)]
    copy_test::digest_finished(_path);
    Ok(digest.finalize().into())
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum PublishPolicy {
    CreateNew,
    Replace,
}

// This cache lives for one import only. A source version is pinned at its first
// comparison/copy; a later change fails the import rather than mixing versions.
#[derive(Debug, PartialEq, Eq)]
struct SourceVersion {
    len: u64,
    modified: Option<SystemTime>,
    created: Option<SystemTime>,
    #[cfg(unix)]
    unix_identity: (u64, u64, i64, i64, i64, i64),
}

impl SourceVersion {
    fn from_metadata(metadata: &std::fs::Metadata) -> Self {
        Self {
            len: metadata.len(),
            modified: metadata.modified().ok(),
            created: metadata.created().ok(),
            #[cfg(unix)]
            unix_identity: {
                use std::os::unix::fs::MetadataExt;
                (
                    metadata.dev(),
                    metadata.ino(),
                    metadata.mtime(),
                    metadata.mtime_nsec(),
                    metadata.ctime(),
                    metadata.ctime_nsec(),
                )
            },
        }
    }

    fn can_reuse_digest(&self) -> bool {
        // Require identity and subsecond change timestamps. Coarse timestamps
        // and platforms without this fingerprint keep full source rehashing.
        #[cfg(unix)]
        {
            self.unix_identity.1 != 0 && self.unix_identity.5 != 0
        }
        #[cfg(not(unix))]
        {
            false
        }
    }
}

struct VerifiedSource {
    version: SourceVersion,
    reuse_digest: bool,
    digest: Option<[u8; 32]>,
}

impl VerifiedSource {
    fn changed(path: &Path) -> std::io::Error {
        std::io::Error::other(format!(
            "import source changed during verification: {}",
            path.display()
        ))
    }

    fn check_metadata(&self, path: &Path, metadata: &std::fs::Metadata) -> std::io::Result<()> {
        if self.version != SourceVersion::from_metadata(metadata) {
            return Err(Self::changed(path));
        }
        Ok(())
    }

    fn read_digest(&mut self, path: &Path) -> std::io::Result<[u8; 32]> {
        let file = File::open(path)?;
        self.check_metadata(path, &file.metadata()?)?;
        // Retain the handle to detect writes to an unlinked/replaced source too.
        let digest = sha256_reader(file.try_clone()?, path)?;
        self.check_metadata(path, &file.metadata()?)?;
        self.check_metadata(path, &std::fs::metadata(path)?)?;
        if self.digest.is_some_and(|previous| previous != digest) {
            return Err(Self::changed(path));
        }
        self.digest = Some(digest);
        Ok(digest)
    }

    fn digest(&mut self, path: &Path) -> std::io::Result<[u8; 32]> {
        self.check_metadata(path, &std::fs::metadata(path)?)?;
        if self.reuse_digest {
            if let Some(digest) = self.digest {
                return Ok(digest);
            }
        }
        self.read_digest(path)
    }

    fn verify(&mut self, path: &Path) -> std::io::Result<()> {
        self.check_metadata(path, &std::fs::metadata(path)?)?;
        if self.digest.is_some() && !self.reuse_digest {
            self.read_digest(path)?;
        }
        Ok(())
    }
}

#[derive(Default)]
struct ImportSourceVerification {
    sources: HashMap<PathBuf, VerifiedSource>,
}

impl ImportSourceVerification {
    fn source(&mut self, path: &Path) -> std::io::Result<&mut VerifiedSource> {
        use std::collections::hash_map::Entry;
        match self.sources.entry(path.to_owned()) {
            Entry::Occupied(entry) => Ok(entry.into_mut()),
            Entry::Vacant(entry) => {
                let version = SourceVersion::from_metadata(&std::fs::metadata(path)?);
                let reuse_digest = version.can_reuse_digest();
                Ok(entry.insert(VerifiedSource {
                    version,
                    reuse_digest,
                    digest: None,
                }))
            }
        }
    }

    fn identical(&mut self, source: &Path, destination: &Path) -> std::io::Result<bool> {
        let pinned = self.source(source)?;
        pinned.verify(source)?;
        let identical = files_have_identical_contents(source, destination)?;
        pinned.verify(source)?;
        Ok(identical)
    }
}

fn copy_file_verified_atomically(
    sources: &mut ImportSourceVerification,
    source: &Path,
    destination: &Path,
    publish_policy: PublishPolicy,
) -> std::io::Result<()> {
    let pinned = sources.source(source)?;
    let expected = if pinned.reuse_digest {
        Some(pinned.digest(source)?)
    } else {
        pinned.check_metadata(source, &std::fs::metadata(source)?)?;
        None
    };
    let mut partial_name = destination.as_os_str().to_os_string();
    partial_name.push(".virel-partial");
    let partial_path = PathBuf::from(partial_name);
    if partial_path.exists() {
        std::fs::remove_file(&partial_path)?;
    }

    let result = (|| {
        #[cfg(test)]
        copy_test::point("before_copy", source, &partial_path);
        let _copied = std::fs::copy(source, &partial_path)?;
        #[cfg(test)]
        copy_test::record_copy(_copied);
        #[cfg(test)]
        copy_test::point("after_copy", source, &partial_path);
        // Without a reusable fingerprint, retain a fresh source hash for every
        // copy, after copying just as in the uncached path. read_digest also
        // rejects a different checksum from an earlier copy in this import.
        let expected = match expected {
            Some(digest) => digest,
            None => pinned.read_digest(source)?,
        };
        if expected != sha256_file(&partial_path)? {
            return Err(std::io::Error::other(format!(
                "SHA-256 verification failed while copying {}",
                source.display()
            )));
        }
        pinned.check_metadata(source, &std::fs::metadata(source)?)?;
        if publish_policy == PublishPolicy::CreateNew && destination.exists() {
            return Err(std::io::Error::new(
                std::io::ErrorKind::AlreadyExists,
                format!(
                    "refusing to overwrite existing file at {}",
                    destination.display()
                ),
            ));
        }
        publish_verified_partial(&partial_path, destination, publish_policy)
    })();
    if result.is_err() {
        let _ = std::fs::remove_file(&partial_path);
    }
    result
}

#[cfg(not(windows))]
fn publish_verified_partial(
    partial_path: &Path,
    destination: &Path,
    _publish_policy: PublishPolicy,
) -> std::io::Result<()> {
    // POSIX rename publishes the verified file atomically and replaces an
    // existing destination without first creating a missing-file window.
    std::fs::rename(partial_path, destination)
}

#[cfg(windows)]
fn publish_verified_partial(
    partial_path: &Path,
    destination: &Path,
    publish_policy: PublishPolicy,
) -> std::io::Result<()> {
    if publish_policy == PublishPolicy::CreateNew || !destination.exists() {
        return std::fs::rename(partial_path, destination);
    }

    let mut previous_name = destination.as_os_str().to_os_string();
    previous_name.push(".virel-previous");
    let previous_path = PathBuf::from(previous_name);
    if previous_path.exists() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::AlreadyExists,
            format!(
                "replacement recovery is required before importing {}",
                destination.display()
            ),
        ));
    }

    std::fs::rename(destination, &previous_path)?;
    match std::fs::rename(partial_path, destination) {
        Ok(()) => {
            std::fs::remove_file(previous_path)?;
            Ok(())
        }
        Err(error) => {
            let restore_result = std::fs::rename(&previous_path, destination);
            match restore_result {
                Ok(()) => Err(error),
                Err(restore_error) => Err(std::io::Error::other(format!(
                    "replacement failed for {} and the previous file remains at {}: {}; restore failed: {}",
                    destination.display(),
                    previous_path.display(),
                    error,
                    restore_error
                ))),
            }
        }
    }
}

fn import_metadata_worker_count(paths: usize, available: usize, requested: Option<&str>) -> usize {
    let available = available.max(1);
    requested
        .and_then(|value| value.parse::<usize>().ok())
        .filter(|value| *value > 0)
        .unwrap_or(available)
        .min(available)
        .min(paths)
}

fn extract_metadata_parallel(paths: &[PathBuf]) -> Vec<ExtractedMetadata> {
    if paths.len() < 2 {
        return paths
            .iter()
            .map(|path| extract_metadata(path).unwrap_or_else(|_| fallback_metadata(path)))
            .collect();
    }

    let available = thread::available_parallelism()
        .map(|value| value.get())
        .unwrap_or(1);
    let requested = std::env::var("PHOTO_EDITOR_IMPORT_METADATA_THREADS").ok();
    let worker_count = import_metadata_worker_count(paths.len(), available, requested.as_deref());
    let chunk_size = paths.len().div_ceil(worker_count);
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

fn managed_drive_root(request: &ImportRequest) -> std::io::Result<PathBuf> {
    let managed_root = request
        .managed_root
        .clone()
        .ok_or_else(|| std::io::Error::other("managed import requested without a managed root"))?;
    Ok(managed_root.join(sanitize_path_segment(&request.drive_id)))
}

fn is_raw_path(path: &Path) -> bool {
    matches!(
        path.extension()
            .and_then(OsStr::to_str)
            .unwrap_or_default()
            .to_ascii_lowercase()
            .as_str(),
        "3fr"
            | "arw"
            | "cr2"
            | "cr3"
            | "dng"
            | "erf"
            | "iiq"
            | "kdc"
            | "mef"
            | "mos"
            | "nef"
            | "nrw"
            | "orf"
            | "pef"
            | "raf"
            | "raw"
            | "rw2"
            | "sr2"
            | "srf"
            | "x3f"
    )
}

fn is_jpeg_path(path: &Path) -> bool {
    matches!(
        path.extension()
            .and_then(OsStr::to_str)
            .unwrap_or_default()
            .to_ascii_lowercase()
            .as_str(),
        "jpg" | "jpeg"
    )
}

fn pair_key(path: &Path) -> String {
    let parent = path.parent().unwrap_or_else(|| Path::new(""));
    let stem = path
        .file_stem()
        .and_then(OsStr::to_str)
        .unwrap_or_default()
        .to_ascii_lowercase();
    format!("{}/{}", parent.to_string_lossy().to_ascii_lowercase(), stem)
}

fn organized_relative_path(
    request: &ImportRequest,
    source_relative_path: &str,
    metadata: &ExtractedMetadata,
) -> String {
    if request.import_mode != ImportMode::Managed
        || !request
            .organization_template
            .trim()
            .eq_ignore_ascii_case("date-camera")
    {
        return source_relative_path.to_string();
    }
    let date_path =
        if metadata.capture_year > 0 && metadata.capture_month > 0 && metadata.capture_day > 0 {
            format!(
                "{:04}/{:02}/{:02}",
                metadata.capture_year, metadata.capture_month, metadata.capture_day
            )
        } else {
            "Unknown Date".to_string()
        };
    let camera = match (metadata.camera_make.trim(), metadata.camera_model.trim()) {
        ("", "") => "Unknown Camera".to_string(),
        (make, "") => make.to_string(),
        ("", model) => model.to_string(),
        (make, model)
            if model
                .to_ascii_lowercase()
                .starts_with(&make.to_ascii_lowercase()) =>
        {
            model.to_string()
        }
        (make, model) => format!("{make} {model}"),
    };
    let file_name = Path::new(source_relative_path)
        .file_name()
        .and_then(OsStr::to_str)
        .unwrap_or("imported-file");
    format!(
        "{}/{}/{}",
        date_path,
        sanitize_folder_name(&camera),
        file_name
    )
}

fn destination_collision_key(relative_path: &str) -> String {
    relative_path.replace('\\', "/").to_lowercase()
}

fn sanitize_folder_name(value: &str) -> String {
    let sanitized = value
        .chars()
        .map(|character| match character {
            '/' | '\\' | ':' | '\0' => '_',
            _ => character,
        })
        .collect::<String>();
    let trimmed = sanitized.trim();
    if trimmed.is_empty() {
        "Unknown Camera".to_string()
    } else {
        trimmed.to_string()
    }
}

fn disambiguated_relative_path(relative_path: &str, identity: &str) -> String {
    let path = Path::new(relative_path);
    let parent = path.parent().unwrap_or_else(|| Path::new(""));
    let stem = path
        .file_stem()
        .and_then(OsStr::to_str)
        .unwrap_or("imported-file");
    let extension = path.extension().and_then(OsStr::to_str).unwrap_or_default();
    let suffix = identity.chars().take(8).collect::<String>();
    let file_name = if extension.is_empty() {
        format!("{stem}-{suffix}")
    } else {
        format!("{stem}-{suffix}.{extension}")
    };
    parent.join(file_name).to_string_lossy().replace('\\', "/")
}

fn sanitize_path_segment(value: &str) -> String {
    let sanitized = value
        .chars()
        .map(|character| {
            if character.is_ascii_alphanumeric() || character == '-' || character == '_' {
                character
            } else {
                '_'
            }
        })
        .collect::<String>();
    if sanitized.is_empty() {
        "drive".to_string()
    } else {
        sanitized
    }
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

    let original_date = field_ascii_string(&exif, Tag::DateTimeOriginal);
    let original_subseconds = field_ascii_string(&exif, Tag::SubSecTimeOriginal);
    let original_offset = field_ascii_string(&exif, Tag::OffsetTimeOriginal);
    metadata.capture_timestamp = original_date.as_deref().and_then(|date| {
        parse_exif_capture_timestamp(
            date,
            original_subseconds.as_deref(),
            original_offset.as_deref(),
        )
    });

    let capture_datetime = original_date
        .as_deref()
        .and_then(parse_exif_capture_datetime)
        .or_else(|| {
            field_ascii_string(&exif, Tag::DateTimeDigitized)
                .as_deref()
                .and_then(parse_exif_capture_datetime)
        })
        .or_else(|| {
            field_ascii_string(&exif, Tag::DateTime)
                .as_deref()
                .and_then(parse_exif_capture_datetime)
        });
    if let Some(datetime) = capture_datetime {
        metadata.capture_year = u16::try_from(datetime.year()).unwrap_or_default();
        metadata.capture_month = month_to_u8(datetime.month());
        metadata.capture_day = datetime.day();
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

fn field_ascii_string(exif: &exif::Exif, tag: Tag) -> Option<String> {
    exif.get_field(tag, In::PRIMARY)
        .and_then(|field| match &field.value {
            Value::Ascii(values) => values.first(),
            _ => None,
        })
        .map(|value| String::from_utf8_lossy(value).trim().to_string())
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

const EXIF_DATE_TIME_FORMAT: &[time::format_description::FormatItem<'static>] =
    time::macros::format_description!("[year]:[month]:[day] [hour]:[minute]:[second]");
const ISO_LOCAL_DATE_TIME_FORMAT: &[time::format_description::FormatItem<'static>] =
    time::macros::format_description!("[year]-[month]-[day]T[hour]:[minute]:[second]");
const EXIF_OFFSET_FORMAT: &[time::format_description::FormatItem<'static>] =
    time::macros::format_description!("[offset_hour sign:mandatory]:[offset_minute]");

fn parse_exif_capture_datetime(value: &str) -> Option<PrimitiveDateTime> {
    let datetime = PrimitiveDateTime::parse(value.trim(), EXIF_DATE_TIME_FORMAT).ok()?;
    (1..=9999).contains(&datetime.year()).then_some(datetime)
}

fn parse_exif_capture_timestamp(
    value: &str,
    subseconds: Option<&str>,
    offset: Option<&str>,
) -> Option<String> {
    let datetime = parse_exif_capture_datetime(value)?;
    let mut timestamp = datetime.format(ISO_LOCAL_DATE_TIME_FORMAT).ok()?;
    if let Some(subseconds) = subseconds.map(str::trim).filter(|value| {
        (1..=9).contains(&value.len()) && value.bytes().all(|byte| byte.is_ascii_digit())
    }) {
        timestamp.push('.');
        timestamp.push_str(subseconds);
    }
    if let Some(offset) = offset
        .map(str::trim)
        .filter(|value| !value.is_empty() && UtcOffset::parse(value, EXIF_OFFSET_FORMAT).is_ok())
    {
        // Keep the camera's local clock reading and attach its stated offset;
        // never reinterpret a missing or invalid offset as UTC.
        timestamp.push_str(offset);
    }
    Some(timestamp)
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
#[path = "importer_copy_test.rs"]
mod copy_test;

#[cfg(test)]
mod tests {
    use std::fs;
    use std::io::Cursor;

    use exif::experimental::Writer;
    use exif::Field;
    use tempfile::tempdir;

    use super::*;

    #[test]
    fn supported_extensions_include_raw_and_jpeg() {
        assert!(is_supported_photo_path(Path::new("frame.CR3")));
        assert!(is_supported_photo_path(Path::new("frame.jpg")));
        assert!(is_supported_photo_path(Path::new("voice.wav")));
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
            managed_root: None,
            backup_root: None,
            organization_template: "source".into(),
            raw_jpeg_policy: "both".into(),
            duplicate_policy: DuplicatePolicy::Skip,
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
            managed_root: None,
            backup_root: None,
            organization_template: "source".into(),
            raw_jpeg_policy: "both".into(),
            duplicate_policy: DuplicatePolicy::Skip,
        };

        let summary = discover_assets(&request).expect("discovery should succeed");
        assert_eq!(summary.assets.len(), 1);
        assert_eq!(summary.assets[0].location.relative_path, "set-a/keep.NEF");
    }

    #[test]
    fn managed_import_copies_assets_into_managed_root() {
        let directory = tempdir().expect("tempdir should succeed");
        let root = directory.path().join("card");
        let managed_root = directory.path().join("managed");
        let backup_root = directory.path().join("backup");
        fs::create_dir_all(root.join("dcim")).expect("create dir should succeed");
        fs::write(root.join("dcim").join("frame.CR2"), b"raw-bytes").expect("write should succeed");

        let request = ImportRequest {
            drive_id: "drive-card".to_string(),
            drive_name: "Card".to_string(),
            mount_path: root.clone(),
            import_mode: ImportMode::Managed,
            selected_paths: Vec::new(),
            managed_root: Some(managed_root.clone()),
            backup_root: Some(backup_root.clone()),
            organization_template: "source".into(),
            raw_jpeg_policy: "both".into(),
            duplicate_policy: DuplicatePolicy::ReuseIdentical,
        };

        let interrupted_partial_path = managed_root
            .join("drive-card")
            .join("dcim")
            .join("frame.CR2.virel-partial");
        fs::create_dir_all(
            interrupted_partial_path
                .parent()
                .expect("partial path should have a parent"),
        )
        .expect("partial parent should be created");
        fs::write(&interrupted_partial_path, b"interrupted-copy")
            .expect("interrupted partial fixture should be created");

        let summary = discover_assets(&request).expect("discovery should succeed");
        let copied_path = managed_root
            .join("drive-card")
            .join("dcim")
            .join("frame.CR2");
        assert_eq!(summary.assets.len(), 1);
        assert!(copied_path.exists());
        let backup_path = backup_root
            .join("drive-card")
            .join("dcim")
            .join("frame.CR2");
        assert!(backup_path.exists());
        assert_eq!(summary.backed_up, 1);
        assert_eq!(summary.reused_backup, 0);
        assert_eq!(
            fs::read(&backup_path).expect("backup should be readable"),
            b"raw-bytes"
        );
        assert_eq!(
            fs::read(&copied_path).expect("copy should be readable"),
            b"raw-bytes"
        );
        assert!(!PathBuf::from(format!("{}.virel-partial", copied_path.display())).exists());
        assert!(!interrupted_partial_path.exists());
        assert_eq!(
            summary.assets[0].location.root_path,
            managed_root
                .join("drive-card")
                .to_string_lossy()
                .to_string()
        );
        assert_eq!(summary.assets[0].location.relative_path, "dcim/frame.CR2");
        assert_eq!(summary.assets[0].import_mode, ImportMode::Managed);

        let resumed_summary = discover_assets(&request).expect("resumed discovery should succeed");
        assert_eq!(resumed_summary.assets.len(), 1);
        assert_eq!(resumed_summary.reused_existing, 1);
        assert_eq!(resumed_summary.backed_up, 1);
        assert_eq!(resumed_summary.reused_backup, 1);
        assert!(!PathBuf::from(format!("{}.virel-partial", copied_path.display())).exists());
    }

    #[test]
    fn managed_skip_preserves_a_different_organized_destination() {
        let directory = tempdir().expect("tempdir should succeed");
        let root = directory.path().join("card");
        let managed_root = directory.path().join("managed");
        let backup_root = directory.path().join("backup");
        let source_path = root.join("DCIM").join("frame.jpg");
        fs::create_dir_all(source_path.parent().expect("source should have a parent"))
            .expect("source parent should be created");
        fs::write(&source_path, b"new-source-bytes").expect("source should be written");

        let request = ImportRequest {
            drive_id: "drive-card".to_string(),
            drive_name: "Card".to_string(),
            mount_path: root,
            import_mode: ImportMode::Managed,
            selected_paths: vec![source_path],
            managed_root: Some(managed_root),
            backup_root: Some(backup_root),
            organization_template: "date-camera".into(),
            raw_jpeg_policy: "both".into(),
            duplicate_policy: DuplicatePolicy::Skip,
        };
        let plan = plan_import_destinations(&request).expect("planning should succeed");
        let destination = plan.destinations[0]
            .managed_destination
            .clone()
            .expect("managed destination should exist in the plan");
        let backup_destination = plan.destinations[0]
            .backup_destination
            .clone()
            .expect("backup destination should exist in the plan");
        fs::create_dir_all(
            destination
                .parent()
                .expect("destination should have a parent"),
        )
        .expect("destination parent should be created");
        fs::write(&destination, b"existing-original-bytes")
            .expect("existing destination should be written");

        let summary = discover_assets(&request).expect("skip import should succeed");
        assert!(summary.assets.is_empty());
        assert_eq!(summary.skipped_duplicates, 1);
        assert_eq!(
            fs::read(&destination).expect("existing destination should remain readable"),
            b"existing-original-bytes"
        );
        assert!(!backup_destination.exists());
    }

    #[test]
    fn managed_replace_backs_up_source_and_replaces_organized_destination() {
        let directory = tempdir().expect("tempdir should succeed");
        let root = directory.path().join("card");
        let source_path = root.join("DCIM").join("frame.jpg");
        fs::create_dir_all(source_path.parent().expect("source should have a parent"))
            .expect("source parent should be created");
        fs::write(&source_path, b"new-source-bytes").expect("source should be written");

        let request = ImportRequest {
            drive_id: "drive-card".to_string(),
            drive_name: "Card".to_string(),
            mount_path: root,
            import_mode: ImportMode::Managed,
            selected_paths: vec![source_path],
            managed_root: Some(directory.path().join("managed")),
            backup_root: Some(directory.path().join("backup")),
            organization_template: "date-camera".into(),
            raw_jpeg_policy: "both".into(),
            duplicate_policy: DuplicatePolicy::Replace,
        };
        let plan = plan_import_destinations(&request).expect("planning should succeed");
        let destination = plan.destinations[0]
            .managed_destination
            .clone()
            .expect("managed destination should exist in the plan");
        let backup_destination = plan.destinations[0]
            .backup_destination
            .clone()
            .expect("backup destination should exist in the plan");
        fs::create_dir_all(
            destination
                .parent()
                .expect("destination should have a parent"),
        )
        .expect("destination parent should be created");
        fs::write(&destination, b"existing-original-bytes")
            .expect("existing destination should be written");

        let summary = discover_assets(&request).expect("replace import should succeed");
        assert_eq!(summary.assets.len(), 1);
        assert_eq!(summary.skipped_duplicates, 0);
        assert_eq!(
            fs::read(&destination).expect("replacement should be readable"),
            b"new-source-bytes"
        );
        assert_eq!(
            fs::read(&backup_destination).expect("backup should be readable"),
            b"new-source-bytes"
        );
    }

    #[test]
    fn managed_import_refuses_to_overwrite_a_different_backup() {
        let directory = tempdir().expect("tempdir should succeed");
        let root = directory.path().join("card");
        let source_path = root.join("DCIM").join("frame.jpg");
        fs::create_dir_all(source_path.parent().expect("source should have a parent"))
            .expect("source parent should be created");
        fs::write(&source_path, b"new-source-bytes").expect("source should be written");

        let request = ImportRequest {
            drive_id: "drive-card".to_string(),
            drive_name: "Card".to_string(),
            mount_path: root,
            import_mode: ImportMode::Managed,
            selected_paths: vec![source_path],
            managed_root: Some(directory.path().join("managed")),
            backup_root: Some(directory.path().join("backup")),
            organization_template: "date-camera".into(),
            raw_jpeg_policy: "both".into(),
            duplicate_policy: DuplicatePolicy::Replace,
        };
        let plan = plan_import_destinations(&request).expect("planning should succeed");
        let destination = plan.destinations[0]
            .managed_destination
            .clone()
            .expect("managed destination should exist in the plan");
        let backup_destination = plan.destinations[0]
            .backup_destination
            .clone()
            .expect("backup destination should exist in the plan");
        fs::create_dir_all(
            backup_destination
                .parent()
                .expect("backup destination should have a parent"),
        )
        .expect("backup parent should be created");
        fs::write(&backup_destination, b"existing-backup-bytes")
            .expect("existing backup should be written");

        let error = discover_assets(&request).expect_err("different backup should stop import");
        assert_eq!(error.kind(), std::io::ErrorKind::AlreadyExists);
        assert_eq!(
            fs::read(&backup_destination).expect("existing backup should remain readable"),
            b"existing-backup-bytes"
        );
        assert!(!destination.exists());
    }

    #[test]
    fn duplicate_policy_parsing_is_typed_and_fail_closed() {
        assert_eq!(
            DuplicatePolicy::parse("skip").unwrap(),
            DuplicatePolicy::Skip
        );
        assert_eq!(
            DuplicatePolicy::parse("reuse-identical").unwrap(),
            DuplicatePolicy::ReuseIdentical
        );
        assert_eq!(
            DuplicatePolicy::parse("replace").unwrap(),
            DuplicatePolicy::Replace
        );
        assert!(DuplicatePolicy::parse("overwrite-anything").is_err());
    }

    #[test]
    fn selected_source_outside_import_root_is_rejected() {
        let directory = tempdir().expect("tempdir should succeed");
        let root = directory.path().join("card");
        let outside_path = directory.path().join("outside.jpg");
        fs::create_dir_all(&root).expect("root should be created");
        fs::write(&outside_path, b"outside-source").expect("outside source should be written");
        let request = ImportRequest {
            drive_id: "drive-card".to_string(),
            drive_name: "Card".to_string(),
            mount_path: root,
            import_mode: ImportMode::Managed,
            selected_paths: vec![outside_path],
            managed_root: Some(directory.path().join("managed")),
            backup_root: None,
            organization_template: "source".into(),
            raw_jpeg_policy: "both".into(),
            duplicate_policy: DuplicatePolicy::Replace,
        };

        let error = plan_import_destinations(&request)
            .expect_err("sources outside the import root must be rejected");
        assert_eq!(error.kind(), std::io::ErrorKind::InvalidInput);
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
            managed_root: None,
            backup_root: None,
            organization_template: "source".into(),
            raw_jpeg_policy: "both".into(),
            duplicate_policy: DuplicatePolicy::Skip,
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
    fn managed_date_camera_organization_is_stable_and_sanitized() {
        let request = ImportRequest {
            drive_id: "drive-card".to_string(),
            drive_name: "Card".to_string(),
            mount_path: PathBuf::from("/card"),
            import_mode: ImportMode::Managed,
            selected_paths: Vec::new(),
            managed_root: Some(PathBuf::from("/library")),
            backup_root: None,
            organization_template: "date-camera".into(),
            raw_jpeg_policy: "both".into(),
            duplicate_policy: DuplicatePolicy::Skip,
        };
        const METADATA: ExtractedMetadata = ExtractedMetadata {
            capture_year: 2026,
            capture_month: 8,
            capture_day: 4,
            capture_timestamp: None,
            camera_make: String::new(),
            camera_model: String::new(),
            lens_model: String::new(),
            iso: None,
            shutter_speed: String::new(),
            aperture: None,
        };
        let mut metadata = METADATA;
        metadata.camera_make = "Canon".into();
        metadata.camera_model = "EOS R5:Studio/A".into();

        assert_eq!(
            organized_relative_path(&request, "DCIM/IMG_0001.CR3", &metadata),
            "2026/08/04/Canon EOS R5_Studio_A/IMG_0001.CR3"
        );
    }

    #[test]
    fn raw_jpeg_pair_policy_keeps_unpaired_jpegs() {
        let directory = tempdir().expect("tempdir should succeed");
        let root = directory.path().join("card");
        fs::create_dir_all(&root).expect("create dir should succeed");
        fs::write(root.join("paired.CR3"), b"raw").expect("write raw should succeed");
        fs::write(root.join("paired.jpg"), b"jpeg").expect("write jpeg should succeed");
        fs::write(root.join("only.jpg"), b"standalone").expect("write standalone should succeed");

        let request = ImportRequest {
            drive_id: "drive-card".to_string(),
            drive_name: "Card".to_string(),
            mount_path: root,
            import_mode: ImportMode::Referenced,
            selected_paths: Vec::new(),
            managed_root: None,
            backup_root: None,
            organization_template: "source".into(),
            raw_jpeg_policy: "raw-only".into(),
            duplicate_policy: DuplicatePolicy::Skip,
        };

        let summary = discover_assets(&request).expect("discovery should succeed");
        let paths = summary
            .assets
            .iter()
            .map(|asset| asset.location.relative_path.as_str())
            .collect::<HashSet<_>>();
        assert_eq!(summary.skipped_policy, 1);
        assert_eq!(paths, HashSet::from(["paired.CR3", "only.jpg"]));
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

    fn ascii_exif_field(tag: Tag, value: &str) -> Field {
        Field {
            tag,
            ifd_num: In::PRIMARY,
            value: Value::Ascii(vec![value.as_bytes().to_vec()]),
        }
    }

    fn write_tiff_with_exif(path: &Path, fields: &[Field]) {
        let mut writer = Writer::new();
        for field in fields {
            writer.push_field(field);
        }
        let mut output = Cursor::new(Vec::new());
        writer
            .write(&mut output, false)
            .expect("encode EXIF fixture");
        fs::write(path, output.into_inner()).expect("write EXIF fixture");
    }

    #[test]
    fn extracts_exif_capture_timestamp_with_subseconds_and_offset() {
        let directory = tempdir().expect("temporary directory");
        let path = directory.path().join("capture.tif");
        write_tiff_with_exif(
            &path,
            &[
                ascii_exif_field(Tag::DateTimeOriginal, "2024:02:29 23:59:58"),
                ascii_exif_field(Tag::SubSecTimeOriginal, "037"),
                ascii_exif_field(Tag::OffsetTimeOriginal, "-07:00"),
            ],
        );

        let metadata = extract_metadata(&path).expect("read EXIF fixture");
        assert_eq!(
            metadata.capture_timestamp.as_deref(),
            Some("2024-02-29T23:59:58.037-07:00")
        );
        assert_eq!(metadata.capture_year, 2024);
        assert_eq!(metadata.capture_month, 2);
        assert_eq!(metadata.capture_day, 29);
    }

    #[test]
    fn preserves_local_exif_time_when_offset_is_missing_or_invalid() {
        assert_eq!(
            parse_exif_capture_timestamp("2024:07:08 09:10:11", Some("5"), None).as_deref(),
            Some("2024-07-08T09:10:11.5")
        );
        assert_eq!(
            parse_exif_capture_timestamp("2024:07:08 09:10:11", None, Some("-25:00")).as_deref(),
            Some("2024-07-08T09:10:11")
        );
    }

    #[test]
    fn malformed_subseconds_are_dropped_without_losing_a_valid_capture_time() {
        assert_eq!(
            parse_exif_capture_timestamp("2024:07:08 09:10:11", Some("1x"), Some("+05:30"))
                .as_deref(),
            Some("2024-07-08T09:10:11+05:30")
        );
        assert_eq!(
            parse_exif_capture_timestamp("2024:07:08 09:10:11", Some("1234567890"), None)
                .as_deref(),
            Some("2024-07-08T09:10:11")
        );
    }

    #[test]
    fn invalid_exif_calendar_or_time_falls_back_to_file_date_without_timestamp() {
        let directory = tempdir().expect("temporary directory");
        let path = directory.path().join("invalid-date.tif");
        write_tiff_with_exif(
            &path,
            &[ascii_exif_field(
                Tag::DateTimeOriginal,
                "2023:02:29 25:61:61",
            )],
        );

        let fallback = fallback_metadata(&path);
        let metadata = extract_metadata(&path).expect("read invalid-date EXIF fixture");
        assert_eq!(metadata.capture_timestamp, None);
        assert_eq!(metadata.capture_year, fallback.capture_year);
        assert_eq!(metadata.capture_month, fallback.capture_month);
        assert_eq!(metadata.capture_day, fallback.capture_day);
        assert!(metadata.capture_year > 0);
    }
}

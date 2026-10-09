use super::*;
use serde::Serialize;
use std::cell::RefCell;
use std::io::Write;
use std::time::Instant;

#[derive(Clone, Default, Serialize)]
struct ReadCounts {
    source_digest_bytes: u64,
    destination_digest_bytes: u64,
    source_compare_bytes: u64,
    destination_compare_bytes: u64,
    native_copy_bytes: u64,
    native_copy_calls: u64,
}

type Hook = (&'static str, Box<dyn FnOnce(&Path, &Path)>);
#[derive(Default)]
struct Probe {
    root: PathBuf,
    counts: ReadCounts,
    hook: Option<Hook>,
}
thread_local! { static PROBE: RefCell<Option<Probe>> = const { RefCell::new(None) }; }

fn activate(root: &Path) {
    PROBE.with(|probe| {
        *probe.borrow_mut() = Some(Probe {
            root: std::fs::canonicalize(root).unwrap(),
            ..Probe::default()
        })
    });
}

pub(super) fn record_read(path: &Path, bytes: usize, digest: bool) {
    let source = PROBE.with(|probe| {
        let mut state = probe.borrow_mut();
        let Some(probe) = state.as_mut() else {
            return false;
        };
        let source = path.starts_with(&probe.root);
        let target = match (source, digest) {
            (true, true) => &mut probe.counts.source_digest_bytes,
            (false, true) => &mut probe.counts.destination_digest_bytes,
            (true, false) => &mut probe.counts.source_compare_bytes,
            (false, false) => &mut probe.counts.destination_compare_bytes,
        };
        *target += bytes as u64;
        source
    });
    if source && bytes > 0 {
        point("source_read", path, path);
    }
}

pub(super) fn record_copy(bytes: u64) {
    PROBE.with(|probe| {
        if let Some(probe) = probe.borrow_mut().as_mut() {
            probe.counts.native_copy_bytes += bytes;
            probe.counts.native_copy_calls += 1;
        }
    });
}

pub(super) fn digest_finished(path: &Path) {
    let source = PROBE.with(|probe| {
        probe
            .borrow()
            .as_ref()
            .is_some_and(|p| path.starts_with(&p.root))
    });
    point(
        if source {
            "source_digest"
        } else {
            "destination_digest"
        },
        path,
        path,
    );
}

pub(super) fn point(phase: &str, source: &Path, destination: &Path) {
    let hook = PROBE.with(|probe| {
        let mut state = probe.borrow_mut();
        let probe = state.as_mut()?;
        if probe.hook.as_ref().is_some_and(|hook| hook.0 == phase) {
            probe.hook.take()
        } else {
            None
        }
    });
    if let Some((_, hook)) = hook {
        hook(source, destination);
    }
}

fn request(root: &Path, scenario: &str) -> ImportRequest {
    ImportRequest {
        drive_id: "fixture-card".into(),
        drive_name: "Fixture".into(),
        mount_path: root.join("source"),
        import_mode: if scenario.starts_with("reference") {
            ImportMode::Referenced
        } else {
            ImportMode::Managed
        },
        selected_paths: (0..4)
            .map(|index| root.join(format!("source/frame-{index}.jpg")))
            .collect(),
        managed_root: Some(root.join("managed")),
        backup_root: if scenario == "one" || scenario == "reference" {
            None
        } else {
            Some(root.join("backup"))
        },
        organization_template: "source".into(),
        raw_jpeg_policy: "both".into(),
        duplicate_policy: if scenario == "skip" {
            DuplicatePolicy::Skip
        } else {
            DuplicatePolicy::ReuseIdentical
        },
    }
}

fn write_fixture(path: &Path, bytes: usize, seed: u8) -> [u8; 32] {
    let mut file = File::create(path).unwrap();
    let mut block = vec![0; 64 * 1024];
    for (index, value) in block.iter_mut().enumerate() {
        *value = (index.wrapping_mul(73) as u8) ^ seed;
    }
    let mut remaining = bytes;
    let mut digest = Sha256::new();
    while remaining > 0 {
        let count = remaining.min(block.len());
        file.write_all(&block[..count]).unwrap();
        digest.update(&block[..count]);
        remaining -= count;
    }
    file.flush().unwrap();
    digest.finalize().into()
}

fn independent_digest(path: &Path) -> [u8; 32] {
    let mut file = File::open(path).unwrap();
    let mut digest = Sha256::new();
    let mut buffer = [0; 64 * 1024];
    loop {
        let bytes = file.read(&mut buffer).unwrap();
        if bytes == 0 {
            break;
        }
        digest.update(&buffer[..bytes]);
    }
    digest.finalize().into()
}

#[test]
#[ignore = "explicit isolated filesystem benchmark"]
fn verified_import_benchmark() {
    let output = std::env::var("LOOKSY_IMPORT_BENCH_OUTPUT").unwrap();
    let scenario =
        std::env::var("LOOKSY_IMPORT_BENCH_SCENARIO").unwrap_or_else(|_| "backup".into());
    let mb: usize = std::env::var("LOOKSY_IMPORT_BENCH_MB")
        .unwrap_or_else(|_| "16".into())
        .parse()
        .unwrap();
    assert!(mb > 0 && mb <= 128);
    assert!([
        "one",
        "backup",
        "reference",
        "reference_backup",
        "reuse",
        "skip"
    ]
    .contains(&scenario.as_str()));
    let root = tempfile::tempdir().unwrap();
    std::fs::create_dir(root.path().join("source")).unwrap();
    let mut expected = Vec::new();
    for index in 0..4 {
        expected.push(write_fixture(
            &root.path().join(format!("source/frame-{index}.jpg")),
            mb * 1024 * 1024,
            index as u8,
        ));
    }
    let mut timings = Vec::new();
    let mut samples = Vec::new();
    let mut final_summary = serde_json::Value::Null;
    for iteration in 0..5 {
        let mut request = request(root.path(), &scenario);
        request.managed_root = Some(root.path().join(format!("managed-{iteration}")));
        if request.backup_root.is_some() {
            request.backup_root = Some(root.path().join(format!("backup-{iteration}")));
        }
        if scenario == "reuse" || scenario == "skip" {
            discover_assets(&request).unwrap();
        }
        activate(&request.mount_path);
        let start = Instant::now();
        let summary = discover_assets(&request).unwrap();
        timings.push(start.elapsed().as_secs_f64() * 1000.0);
        samples.push(PROBE.with(|probe| probe.borrow().as_ref().unwrap().counts.clone()));
        PROBE.with(|probe| *probe.borrow_mut() = None);
        assert_eq!(summary.assets.len(), if scenario == "skip" { 0 } else { 4 });
        for (index, digest) in expected.iter().enumerate() {
            let relative = format!("frame-{index}.jpg");
            assert_eq!(
                independent_digest(&request.mount_path.join(&relative)),
                *digest
            );
            if request.import_mode == ImportMode::Managed {
                assert_eq!(
                    independent_digest(
                        &request
                            .managed_root
                            .as_ref()
                            .unwrap()
                            .join("fixture-card")
                            .join(&relative)
                    ),
                    *digest
                );
            }
            if let Some(backup) = &request.backup_root {
                assert_eq!(
                    independent_digest(&backup.join("fixture-card").join(&relative)),
                    *digest
                );
            }
        }
        final_summary = serde_json::json!({"assets": summary.assets.iter().map(|a| (&a.asset_id, &a.location.relative_path)).collect::<Vec<_>>(),
            "backed_up": summary.backed_up, "reused_backup": summary.reused_backup, "reused_existing": summary.reused_existing,
            "skipped_duplicates": summary.skipped_duplicates, "skipped_unsupported": summary.skipped_unsupported});
    }
    let mut sorted = timings.clone();
    sorted.sort_by(f64::total_cmp);
    let output_hash: String = Sha256::digest(serde_json::to_vec(&expected).unwrap())
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect();
    let report = serde_json::json!({"scenario": scenario, "files": 4, "mb_per_file": mb,
        "elapsed_ms": timings, "p95_ms": sorted[4], "counts": samples, "summary": final_summary,
        "output_sha256": output_hash, "exact_all_destinations": true,
        "read_scope": "Instrumented logical digest/comparison reads and native copy requests; excludes metadata parsing and does not measure physical device reads"});
    std::fs::write(output, serde_json::to_vec_pretty(&report).unwrap()).unwrap();
}

struct Fixture {
    root: tempfile::TempDir,
    source: PathBuf,
    destination: PathBuf,
}

impl Fixture {
    fn new(bytes: usize) -> Self {
        let root = tempfile::tempdir().unwrap();
        let source_root = root.path().join("source");
        std::fs::create_dir(&source_root).unwrap();
        let source = source_root.join("frame.jpg");
        write_fixture(&source, bytes, 17);
        let source = std::fs::canonicalize(source).unwrap();
        let destination = root.path().join("managed.jpg");
        activate(&source_root);
        Self {
            root,
            source,
            destination,
        }
    }

    fn hook(&self, phase: &'static str, hook: impl FnOnce(&Path, &Path) + 'static) {
        PROBE.with(|probe| {
            probe.borrow_mut().as_mut().unwrap().hook = Some((phase, Box::new(hook)))
        });
    }

    fn partial(&self) -> PathBuf {
        PathBuf::from(format!("{}.virel-partial", self.destination.display()))
    }

    fn copy(&self, sources: &mut ImportSourceVerification) -> std::io::Result<()> {
        copy_file_verified_atomically(
            sources,
            &self.source,
            &self.destination,
            PublishPolicy::Replace,
        )
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        PROBE.with(|probe| *probe.borrow_mut() = None);
    }
}

#[test]
fn copies_share_source_digest_and_independently_verify_destinations() {
    for bytes in [0, 1, 131_073] {
        let f = Fixture::new(bytes);
        let expected = independent_digest(&f.source);
        let mut sources = ImportSourceVerification::default();
        f.copy(&mut sources).unwrap();
        let backup = f.root.path().join("backup.jpg");
        copy_file_verified_atomically(&mut sources, &f.source, &backup, PublishPolicy::CreateNew)
            .unwrap();
        assert_eq!(independent_digest(&f.destination), expected);
        assert_eq!(independent_digest(&backup), expected);
        assert!(!f.partial().exists());
        let counts = PROBE.with(|p| p.borrow().as_ref().unwrap().counts.clone());
        if sources
            .source(&f.source)
            .unwrap()
            .version
            .can_reuse_digest()
        {
            assert_eq!(counts.source_digest_bytes, bytes as u64);
        }
        assert_eq!(counts.destination_digest_bytes, 2 * bytes as u64);
        assert_eq!(counts.native_copy_calls, 2);
    }
}

#[test]
fn source_mutations_at_every_copy_phase_preserve_previous_destination() {
    for phase in [
        "source_read",
        "source_digest",
        "before_copy",
        "after_copy",
        "destination_digest",
    ] {
        let f = Fixture::new(131_073);
        std::fs::write(&f.destination, b"last good destination").unwrap();
        let source = f.source.clone();
        f.hook(phase, move |_, _| {
            write_fixture(&source, 131_073, 91);
        });
        assert!(
            f.copy(&mut ImportSourceVerification::default()).is_err(),
            "{phase}"
        );
        assert_eq!(
            std::fs::read(&f.destination).unwrap(),
            b"last good destination",
            "{phase}"
        );
        assert!(!f.partial().exists(), "{phase}");
    }
}

#[test]
fn partial_corruption_or_read_failure_is_cleaned_without_publication() {
    for delete in [false, true] {
        let f = Fixture::new(131_073);
        std::fs::write(&f.destination, b"last good destination").unwrap();
        f.hook("after_copy", move |_, partial| {
            if delete {
                std::fs::remove_file(partial).unwrap();
            } else {
                write_fixture(partial, 131_073, 91);
            }
        });
        assert!(f.copy(&mut ImportSourceVerification::default()).is_err());
        assert_eq!(
            std::fs::read(&f.destination).unwrap(),
            b"last good destination"
        );
        assert!(!f.partial().exists());
    }
}

#[test]
fn source_disappearance_after_copy_cleans_partial() {
    let f = Fixture::new(131_073);
    f.hook("after_copy", |source, _| {
        std::fs::remove_file(source).unwrap()
    });
    assert!(f.copy(&mut ImportSourceVerification::default()).is_err());
    assert!(!f.destination.exists());
    assert!(!f.partial().exists());
}

#[cfg(unix)]
#[test]
fn same_length_change_with_restored_mtime_and_path_replacement_are_rejected() {
    for replace in [false, true] {
        let f = Fixture::new(131_073);
        let mut sources = ImportSourceVerification::default();
        sources
            .source(&f.source)
            .unwrap()
            .digest(&f.source)
            .unwrap();
        let modified = std::fs::metadata(&f.source).unwrap().modified().unwrap();
        if replace {
            // Keep old inode alive so the replacement cannot reuse its identity.
            std::fs::rename(&f.source, f.root.path().join("old.jpg")).unwrap();
        }
        write_fixture(&f.source, 131_073, if replace { 17 } else { 91 });
        File::options()
            .write(true)
            .open(&f.source)
            .unwrap()
            .set_times(std::fs::FileTimes::new().set_modified(modified))
            .unwrap();
        assert!(f.copy(&mut sources).is_err());
        assert!(!f.destination.exists());
    }
}

#[test]
fn changed_source_between_backup_and_managed_copy_keeps_verified_backup() {
    let f = Fixture::new(131_073);
    let original = independent_digest(&f.source);
    let backup = f.root.path().join("backup.jpg");
    let mut sources = ImportSourceVerification::default();
    copy_file_verified_atomically(&mut sources, &f.source, &backup, PublishPolicy::CreateNew)
        .unwrap();
    write_fixture(&f.source, 131_073, 91);
    assert!(f.copy(&mut sources).is_err());
    assert!(!f.destination.exists());
    assert_eq!(independent_digest(&backup), original);
    // A later operation pins the new source version, with no persistent cache.
    f.copy(&mut ImportSourceVerification::default()).unwrap();
    assert_eq!(
        independent_digest(&f.destination),
        independent_digest(&f.source)
    );
}

#[test]
fn comparison_pins_source_without_extra_digest_reads() {
    let f = Fixture::new(131_073);
    std::fs::copy(&f.source, &f.destination).unwrap();
    let mut sources = ImportSourceVerification::default();
    assert!(sources.identical(&f.source, &f.destination).unwrap());
    assert_eq!(
        PROBE.with(|p| p.borrow().as_ref().unwrap().counts.source_digest_bytes),
        0
    );
    write_fixture(&f.source, 131_073, 91);
    assert!(f.copy(&mut sources).is_err());
}

#[test]
fn publication_failure_cleans_partial_and_preserves_existing_file() {
    let f = Fixture::new(131_073);
    std::fs::write(&f.destination, b"previous").unwrap();
    assert!(copy_file_verified_atomically(
        &mut ImportSourceVerification::default(),
        &f.source,
        &f.destination,
        PublishPolicy::CreateNew
    )
    .is_err());
    assert_eq!(std::fs::read(&f.destination).unwrap(), b"previous");
    assert!(!f.partial().exists());
    std::fs::remove_file(&f.destination).unwrap();
    std::fs::create_dir(&f.destination).unwrap();
    assert!(f.copy(&mut ImportSourceVerification::default()).is_err());
    assert!(f.destination.is_dir());
    assert!(!f.partial().exists());
}

#[test]
fn uncached_fallback_rehashes_source_for_each_copy() {
    let f = Fixture::new(131_073);
    let mut sources = ImportSourceVerification::default();
    sources.source(&f.source).unwrap().reuse_digest = false;
    f.copy(&mut sources).unwrap();
    f.copy(&mut sources).unwrap();
    let counts = PROBE.with(|p| p.borrow().as_ref().unwrap().counts.clone());
    assert_eq!(counts.source_digest_bytes, 2 * 131_073);
    assert_eq!(counts.destination_digest_bytes, 2 * 131_073);
    assert_eq!(
        independent_digest(&f.source),
        independent_digest(&f.destination)
    );
    let source = f.source.clone();
    f.hook("destination_digest", move |_, _| {
        write_fixture(&source, 131_073, 91);
    });
    assert!(f.copy(&mut sources).is_err());
    assert!(!f.partial().exists());
}

#[test]
fn coarse_source_timestamps_disable_digest_reuse() {
    let f = Fixture::new(1);
    let mut version = SourceVersion::from_metadata(&std::fs::metadata(&f.source).unwrap());
    #[cfg(unix)]
    {
        version.unix_identity.3 = 0;
        version.unix_identity.5 = 0;
    }
    version.modified = None;
    version.created = None;
    assert!(!version.can_reuse_digest());
}

#[test]
fn uncached_fallback_never_refreshes_a_pinned_digest_to_different_bytes() {
    let f = Fixture::new(131_073);
    let mut sources = ImportSourceVerification::default();
    sources.source(&f.source).unwrap().reuse_digest = false;
    f.copy(&mut sources).unwrap();
    let original = independent_digest(&f.destination);
    write_fixture(&f.source, 131_073, 91);
    // Simulate a filesystem fingerprint that cannot distinguish the two writes.
    // The fresh checksum must still reject the changed bytes, never refresh them.
    sources.source(&f.source).unwrap().version =
        SourceVersion::from_metadata(&std::fs::metadata(&f.source).unwrap());
    let error = f.copy(&mut sources).unwrap_err();
    assert!(error.to_string().contains("source changed"));
    assert_eq!(independent_digest(&f.destination), original);
    assert!(!f.partial().exists());
}

#[test]
fn metadata_thread_limit_is_bounded_and_preserves_default_behavior() {
    assert_eq!(import_metadata_worker_count(100, 14, Some("2")), 2);
    assert_eq!(import_metadata_worker_count(100, 4, Some("1000")), 4);
    assert_eq!(import_metadata_worker_count(1, 14, Some("2")), 1);
    assert_eq!(import_metadata_worker_count(0, 14, Some("2")), 0);
    assert_eq!(import_metadata_worker_count(100, 0, Some("2")), 1);
    for value in [None, Some("0"), Some("-1"), Some("bad")] {
        assert_eq!(import_metadata_worker_count(100, 14, value), 14);
    }
}

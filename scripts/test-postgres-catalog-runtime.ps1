param(
    [Parameter(Mandatory = $true)]
    [string]$InstallRoot
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$resolvedInstallRoot = (Resolve-Path $InstallRoot).Path
$catalogBinDir = Join-Path $resolvedInstallRoot "bin"
$postgresRuntimeDir = Join-Path $catalogBinDir "postgres"
$temporaryParent = if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) {
    [System.IO.Path]::GetTempPath()
} else {
    $env:RUNNER_TEMP
}
$proofRoot = Join-Path $temporaryParent (
    "framehouse-postgres-runtime-proof-" + [guid]::NewGuid().ToString("N")
)
$managedRoot = Join-Path $proofRoot "managed"
$externalClientRoot = Join-Path $proofRoot "external-client"

function Require-Executable {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing executable: $Path"
    }
}

function Invoke-CatalogHelper {
    param(
        [string]$Name,
        [string[]]$Arguments = @()
    )
    $path = Join-Path $catalogBinDir "$Name.exe"
    $output = & $path @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
    return $output
}

function Invoke-CatalogHelperExpectFailure {
    param(
        [string]$Name,
        [string[]]$Arguments = @()
    )
    $path = Join-Path $catalogBinDir "$Name.exe"
    $previousPreference = $PSNativeCommandUseErrorActionPreference
    $PSNativeCommandUseErrorActionPreference = $false
    try {
        $output = & $path @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $PSNativeCommandUseErrorActionPreference = $previousPreference
    }
    if ($exitCode -eq 0) {
        throw "$Name unexpectedly succeeded"
    }
    return $output
}

function Invoke-PostgresSql {
    param(
        [string]$DatabaseUrl,
        [string]$Sql
    )
    $psqlPath = Join-Path $postgresRuntimeDir "bin\psql.exe"
    & $psqlPath $DatabaseUrl "--set=ON_ERROR_STOP=1" "--command=$Sql" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "psql failed with exit code $LASTEXITCODE"
    }
}

function Assert-CatalogIntegrityFailure {
    param(
        [string]$Helper,
        [string[]]$Arguments,
        [string]$Identity
    )
    $failure = (Invoke-CatalogHelperExpectFailure $Helper $Arguments) -join "`n"
    if (-not $failure.Contains("catalog data integrity error") -or
        -not $failure.Contains($Identity)) {
        throw "$Helper did not report the corrupt record $Identity"
    }
    if ($failure.Contains("panicked at") -or $failure.Contains("thread '")) {
        throw "$Helper panicked while decoding corrupt catalog data"
    }
}

foreach ($helper in @(
    "catalog_local_db",
    "catalog_migrate",
    "catalog_migration_inventory",
    "catalog_move_library",
    "catalog_snapshot",
    "catalog_state",
    "catalog_export_jobs",
    "catalog_integrity",
    "catalog_timeline_documents",
    "catalog_carousel_documents",
    "import_catalog"
)) {
    Require-Executable (Join-Path $catalogBinDir "$helper.exe")
}
foreach ($tool in @(
    "postgres", "initdb", "pg_ctl", "pg_dump", "pg_restore", "psql"
)) {
    Require-Executable (Join-Path $postgresRuntimeDir "bin\$tool.exe")
}

$savedEnvironment = @{
    PHOTO_EDITOR_DATABASE_URL = $env:PHOTO_EDITOR_DATABASE_URL
    PHOTO_EDITOR_APP_DATA_DIR = $env:PHOTO_EDITOR_APP_DATA_DIR
    PHOTO_EDITOR_LOG_DIR = $env:PHOTO_EDITOR_LOG_DIR
    PHOTO_EDITOR_BACKUP_DIR = $env:PHOTO_EDITOR_BACKUP_DIR
    PHOTO_EDITOR_POSTGRES_BIN_DIR = $env:PHOTO_EDITOR_POSTGRES_BIN_DIR
    PHOTO_EDITOR_REAL_PG_RESTORE = $env:PHOTO_EDITOR_REAL_PG_RESTORE
}

New-Item -ItemType Directory -Force -Path $proofRoot | Out-Null
try {
    Remove-Item Env:PHOTO_EDITOR_DATABASE_URL -ErrorAction SilentlyContinue
    $env:PHOTO_EDITOR_POSTGRES_BIN_DIR = Join-Path $postgresRuntimeDir "bin"
    $env:PHOTO_EDITOR_APP_DATA_DIR = Join-Path $managedRoot "app-data"
    $env:PHOTO_EDITOR_LOG_DIR = Join-Path $managedRoot "logs"
    $env:PHOTO_EDITOR_BACKUP_DIR = Join-Path $managedRoot "backups"

    [void](Invoke-CatalogHelper "catalog_local_db" @("ensure-running"))
    $credentialStatePath = Join-Path $env:PHOTO_EDITOR_APP_DATA_DIR "postgres\state.json"
    $legacyCredentialPath = Join-Path $env:PHOTO_EDITOR_APP_DATA_DIR "postgres\.credential"
    $credentialState = Get-Content -LiteralPath $credentialStatePath -Raw |
        ConvertFrom-Json
    $originalCredential = [string]$credentialState.password
    if ([string]::IsNullOrWhiteSpace($originalCredential)) {
        throw "Managed state did not contain a recoverable credential"
    }
    Set-Content -LiteralPath $legacyCredentialPath -Value $originalCredential -NoNewline
    $credentialRecoveryBefore =
        (Invoke-CatalogHelper "catalog_snapshot") -join "`n"
    $credentialState.password = "invalid-replacement-authority"
    $credentialState | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $credentialStatePath
    [void](Invoke-CatalogHelper "catalog_local_db" @("ensure-running"))
    $recoveredCredentialState = Get-Content -LiteralPath $credentialStatePath -Raw |
        ConvertFrom-Json
    if ([string]$recoveredCredentialState.password -ne $originalCredential) {
        throw "Managed startup did not restore the proven legacy credential"
    }
    $credentialRecoveryAfter =
        (Invoke-CatalogHelper "catalog_snapshot") -join "`n"
    if ($credentialRecoveryAfter -cne $credentialRecoveryBefore) {
        throw "Credential recovery changed catalog contents"
    }
    [void](Invoke-CatalogHelper "catalog_state" @(
        "create-project", "RemovedByPreMigrationRestore"
    ))
    $preMigrationBackups = @(
        Get-ChildItem -LiteralPath $env:PHOTO_EDITOR_BACKUP_DIR -File |
            Where-Object {
                $_.Name -like "pre-migration-v*.dump"
            } |
            Sort-Object FullName
    )
    if ($preMigrationBackups.Count -ne 1) {
        throw "Managed schema initialization did not create exactly one pre-migration backup"
    }
    $pgRestorePath = Join-Path $postgresRuntimeDir "bin\pg_restore.exe"
    & $pgRestorePath --list $preMigrationBackups[0].FullName | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Pre-migration backup is not a valid PostgreSQL archive"
    }
    [void](Invoke-CatalogHelper "catalog_local_db" @("restore-latest"))
    $preMigrationRestoredJson = (Invoke-CatalogHelper "catalog_snapshot") -join "`n"
    $preMigrationRestored = $preMigrationRestoredJson | ConvertFrom-Json
    $preMigrationProjectNames = @(
        $preMigrationRestored.projects | ForEach-Object { $_.name }
    )
    if ($preMigrationProjectNames -contains "RemovedByPreMigrationRestore") {
        throw "Pre-migration restore retained a post-backup catalog mutation"
    }
    $preMigrationBackupsAfterRestore = @(
        Get-ChildItem -LiteralPath $env:PHOTO_EDITOR_BACKUP_DIR -File |
            Where-Object {
                $_.Name -like "pre-migration-v*.dump"
            } |
            Sort-Object FullName
    )
    $preMigrationPaths = @(
        $preMigrationBackups | Select-Object -ExpandProperty FullName
    )
    $preMigrationPathsAfterRestore = @(
        $preMigrationBackupsAfterRestore | Select-Object -ExpandProperty FullName
    )
    $preMigrationDifference = @(
        Compare-Object -ReferenceObject $preMigrationPaths `
            -DifferenceObject $preMigrationPathsAfterRestore
    )
    if ($preMigrationDifference.Count -ne 0) {
        throw "Managed schema startup created duplicate pre-migration backups"
    }
    [void](Invoke-CatalogHelper "catalog_state" @("create-project", "BeforeBackup"))
    $backupPath = (Invoke-CatalogHelper "catalog_local_db" @("backup") | Select-Object -Last 1)
    if (-not (Test-Path -LiteralPath $backupPath -PathType Leaf)) {
        throw "Managed backup was not created: $backupPath"
    }
    [void](Invoke-CatalogHelper "catalog_state" @("create-project", "AfterBackup"))
    [void](Invoke-CatalogHelper "catalog_local_db" @("restore-latest"))

    $snapshotJson = (Invoke-CatalogHelper "catalog_snapshot") -join "`n"
    $snapshot = $snapshotJson | ConvertFrom-Json
    $projectNames = @($snapshot.projects | ForEach-Object { $_.name })
    if ($projectNames -notcontains "BeforeBackup") {
        throw "Restore lost the pre-backup project"
    }
    if ($projectNames -contains "AfterBackup") {
        throw "Restore retained the post-backup mutation"
    }
    [void](Invoke-CatalogHelper "catalog_state" @(
        "create-project", "SurvivesCorruptRestore"
    ))
    Start-Sleep -Seconds 1
    $corruptBackupPath = Join-Path (
        $env:PHOTO_EDITOR_BACKUP_DIR
    ) "catalog-manual-corrupt.dump"
    [System.IO.File]::WriteAllText(
        $corruptBackupPath,
        "not a PostgreSQL custom archive`n",
        [System.Text.UTF8Encoding]::new($false)
    )
    $corruptRestoreFailure = (
        Invoke-CatalogHelperExpectFailure "catalog_local_db" @("restore-latest")
    ) -join "`n"
    if (-not $corruptRestoreFailure.Contains(
            "PostgreSQL backup validation failed")) {
        throw "Corrupt restore refusal did not report archive validation failure"
    }
    $postCorruptSnapshotJson = (Invoke-CatalogHelper "catalog_snapshot") -join "`n"
    $postCorruptSnapshot = $postCorruptSnapshotJson | ConvertFrom-Json
    $postCorruptProjectNames = @(
        $postCorruptSnapshot.projects | ForEach-Object { $_.name }
    )
    if ($postCorruptProjectNames -notcontains "SurvivesCorruptRestore") {
        throw "Corrupt restore changed the live catalog before validation"
    }
    Remove-Item -LiteralPath $corruptBackupPath

    $backupsBeforeInterruption = @(
        Get-ChildItem -LiteralPath $env:PHOTO_EDITOR_BACKUP_DIR -File |
            Where-Object { $_.Name -like "catalog-manual-*.dump" } |
            Select-Object -ExpandProperty FullName |
            Sort-Object
    )
    $failureBinDir = Join-Path $proofRoot "missing-postgres-bin"
    New-Item -ItemType Directory -Force -Path $failureBinDir | Out-Null
    $env:PHOTO_EDITOR_POSTGRES_BIN_DIR = $failureBinDir
    $interruptedBackupFailure = (
        Invoke-CatalogHelperExpectFailure "catalog_local_db" @("backup")
    ) -join "`n"
    if (-not $interruptedBackupFailure.Contains("pg_dump")) {
        throw "Interrupted backup did not report the unavailable pg_dump tool"
    }
    $partialArchives = @(
        Get-ChildItem -LiteralPath $env:PHOTO_EDITOR_BACKUP_DIR -File |
            Where-Object { $_.Name -like "*.partial" }
    )
    if ($partialArchives.Count -ne 0) {
        throw "Interrupted backup stranded a partial archive"
    }
    $backupsAfterInterruption = @(
        Get-ChildItem -LiteralPath $env:PHOTO_EDITOR_BACKUP_DIR -File |
            Where-Object { $_.Name -like "catalog-manual-*.dump" } |
            Select-Object -ExpandProperty FullName |
            Sort-Object
    )
    if (Compare-Object $backupsBeforeInterruption $backupsAfterInterruption) {
        throw "Interrupted backup published a false completed archive"
    }
    $env:PHOTO_EDITOR_POSTGRES_BIN_DIR = Join-Path $postgresRuntimeDir "bin"
    $postInterruptionSnapshotJson =
        (Invoke-CatalogHelper "catalog_snapshot") -join "`n"
    $postInterruptionSnapshot = $postInterruptionSnapshotJson | ConvertFrom-Json
    $postInterruptionProjectNames = @(
        $postInterruptionSnapshot.projects | ForEach-Object { $_.name }
    )
    if ($postInterruptionProjectNames -notcontains "SurvivesCorruptRestore") {
        throw "Interrupted backup changed the live catalog"
    }

    [void](Invoke-CatalogHelper "catalog_state" @(
        "create-project", "RestoreRollbackBaseline"
    ))
    Start-Sleep -Seconds 1
    $restoreRollbackCandidate = (
        Invoke-CatalogHelper "catalog_local_db" @("backup") |
            Select-Object -Last 1
    )
    if (-not (Test-Path -LiteralPath $restoreRollbackCandidate -PathType Leaf)) {
        throw "Restore rollback candidate was not created"
    }
    [void](Invoke-CatalogHelper "catalog_state" @(
        "create-project", "SurvivesFailedRestoreRollback"
    ))
    $restoreFailureBinDir = Join-Path $proofRoot "failing-restore-postgres-bin"
    New-Item -ItemType Directory -Force -Path $restoreFailureBinDir | Out-Null
    Copy-Item -LiteralPath (
        Join-Path $postgresRuntimeDir "bin\pg_dump.exe"
    ) -Destination (Join-Path $restoreFailureBinDir "pg_dump.exe")
    $restoreWrapperSource = @'
using System;
using System.Diagnostics;
using System.IO;

public static class InjectedPgRestoreFailure
{
    private static string Quote(string value)
    {
        return "\"" + value.Replace("\"", "\\\"") + "\"";
    }

    public static int Main(string[] args)
    {
        string realPgRestore = Environment.GetEnvironmentVariable(
            "PHOTO_EDITOR_REAL_PG_RESTORE");
        if (String.IsNullOrWhiteSpace(realPgRestore))
        {
            Console.Error.WriteLine("PHOTO_EDITOR_REAL_PG_RESTORE is required");
            return 43;
        }
        bool isList = Array.Exists(args, value => value == "--list");
        string archiveName = args.Length == 0
            ? String.Empty
            : Path.GetFileName(args[args.Length - 1]);
        ProcessStartInfo startInfo = new ProcessStartInfo();
        startInfo.FileName = realPgRestore;
        startInfo.Arguments = String.Join(" ", Array.ConvertAll(args, Quote));
        startInfo.UseShellExecute = false;
        Process process = Process.Start(startInfo);
        process.WaitForExit();
        int exitCode = process.ExitCode;
        process.Dispose();
        if (exitCode != 0 || isList ||
            archiveName.StartsWith("pre-restore-", StringComparison.OrdinalIgnoreCase))
        {
            return exitCode;
        }
        Console.Error.WriteLine("injected failure after candidate restore");
        return 42;
    }
}
'@
    Add-Type -TypeDefinition $restoreWrapperSource `
        -OutputAssembly (Join-Path $restoreFailureBinDir "pg_restore.exe") `
        -OutputType ConsoleApplication
    $env:PHOTO_EDITOR_REAL_PG_RESTORE = Join-Path (
        $postgresRuntimeDir
    ) "bin\pg_restore.exe"
    $env:PHOTO_EDITOR_POSTGRES_BIN_DIR = $restoreFailureBinDir
    $failedRestoreOutput = (
        Invoke-CatalogHelperExpectFailure "catalog_local_db" @("restore-latest")
    ) -join "`n"
    if (-not $failedRestoreOutput.Contains(
            "pre-restore safety backup was reapplied")) {
        throw "Failed restore did not report successful safety-backup reapplication"
    }
    $env:PHOTO_EDITOR_POSTGRES_BIN_DIR = Join-Path $postgresRuntimeDir "bin"
    Remove-Item Env:PHOTO_EDITOR_REAL_PG_RESTORE -ErrorAction SilentlyContinue
    $postFailedRestoreJson = (Invoke-CatalogHelper "catalog_snapshot") -join "`n"
    $postFailedRestore = $postFailedRestoreJson | ConvertFrom-Json
    $postFailedRestoreProjectNames = @(
        $postFailedRestore.projects | ForEach-Object { $_.name }
    )
    if ($postFailedRestoreProjectNames -notcontains "RestoreRollbackBaseline") {
        throw "Failed restore rollback lost the candidate baseline"
    }
    if ($postFailedRestoreProjectNames -notcontains "SurvivesFailedRestoreRollback") {
        throw "Failed restore rollback did not recover the pre-restore live catalog"
    }

    $managedStatusJson =
        (Invoke-CatalogHelper "catalog_local_db" @("status")) -join "`n"
    $managedStatus = $managedStatusJson | ConvertFrom-Json
    if ($managedStatus.mode -ne "local-managed" -or
        -not $managedStatus.healthy -or -not $managedStatus.initialized) {
        throw "Managed PostgreSQL status contract failed"
    }

    $driveARoot = Join-Path $proofRoot "drive-a"
    $driveBRoot = Join-Path $proofRoot "drive-b"
    $relinkRoot = Join-Path $proofRoot "relinked"
    foreach ($directory in @($driveARoot, $driveBRoot, $relinkRoot)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }
    $driveAPath = Join-Path $driveARoot "proof-a.dng"
    $driveBPath = Join-Path $driveBRoot "proof-b.dng"
    [System.IO.File]::WriteAllText($driveAPath, "proof raw a`n")
    [System.IO.File]::WriteAllText($driveBPath, "proof raw b`n")
    [void](Invoke-CatalogHelper "import_catalog" @(
        $driveARoot, "proof-drive-a", "Proof Drive A", "referenced"
    ))
    [void](Invoke-CatalogHelper "import_catalog" @(
        $driveARoot, "proof-drive-a", "Proof Drive A", "referenced"
    ))
    [void](Invoke-CatalogHelper "import_catalog" @(
        $driveBRoot, "proof-drive-b", "Proof Drive B", "referenced"
    ))
    $multiDriveSnapshotJson = (Invoke-CatalogHelper "catalog_snapshot") -join "`n"
    $multiDriveSnapshot = $multiDriveSnapshotJson | ConvertFrom-Json
    if (@($multiDriveSnapshot.assets).Count -ne 2) {
        throw "Repeat import duplicated a stable referenced asset"
    }
    if (@($multiDriveSnapshot.drives).Count -ne 2) {
        throw "Catalog did not retain both referenced drive authorities"
    }
    $driveAAsset = @($multiDriveSnapshot.assets) | Where-Object {
        $_.asset.location.drive_id -eq "proof-drive-a"
    } | Select-Object -First 1
    if ($null -eq $driveAAsset) {
        throw "Could not resolve the first drive asset identity"
    }
    $emptyProvenance =
        '{"schema":"photo-editor-edit-recipe-provenance:v1","presetApplications":[]}'
    [void](Invoke-CatalogHelper "catalog_state" @(
        "set-edit-recipe-v13",
        $driveAAsset.asset.asset_id,
        "photo-editor-edit-recipe-v13.1",
        "sha256:postgres-runtime-proof",
        $emptyProvenance
    ))
    Remove-Item -LiteralPath $driveAPath
    $missingInventoryJson =
        (Invoke-CatalogHelper "catalog_migration_inventory") -join "`n"
    $missingInventory = $missingInventoryJson | ConvertFrom-Json
    if ($missingInventory.edited_asset_count -ne 1 -or
        @($missingInventory.assets)[0].source_available) {
        throw "Missing referenced media was not reported unavailable"
    }
    $relinkedPath = Join-Path $relinkRoot "proof-a-relinked.dng"
    [System.IO.File]::WriteAllText($relinkedPath, "proof raw a recovered`n")
    [void](Invoke-CatalogHelper "catalog_state" @(
        "set-relink-path", $driveAAsset.asset.asset_id, $relinkedPath
    ))
    $relinkedInventoryJson =
        (Invoke-CatalogHelper "catalog_migration_inventory") -join "`n"
    $relinkedInventory = $relinkedInventoryJson | ConvertFrom-Json
    $relinkedAsset = @($relinkedInventory.assets)[0]
    if (-not $relinkedAsset.source_available -or
        $relinkedAsset.source_path -ne $relinkedPath) {
        throw "Migration inventory did not use the available authoritative relink path"
    }

    $managedCardRoot = Join-Path $proofRoot "managed-card"
    $managedLibraryBefore = Join-Path $proofRoot "managed-library-before"
    $managedLibraryAfter = Join-Path $proofRoot "managed-library-after"
    New-Item -ItemType Directory -Force -Path $managedCardRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $managedLibraryBefore | Out-Null
    $managedCardPath = Join-Path $managedCardRoot "managed-proof.dng"
    [System.IO.File]::WriteAllText($managedCardPath, "managed proof raw`n")
    [void](Invoke-CatalogHelper "import_catalog" @(
        $managedCardRoot,
        "proof-managed-card",
        "Managed Proof Card",
        "managed",
        "-",
        $managedLibraryBefore
    ))
    $managedBeforePath = Join-Path (
        Join-Path $managedLibraryBefore "proof-managed-card"
    ) "managed-proof.dng"
    if (-not (Test-Path -LiteralPath $managedBeforePath -PathType Leaf)) {
        throw "Managed import did not copy its original"
    }
    $libraryMoveOutput = Invoke-CatalogHelper "catalog_move_library" @(
        $managedLibraryBefore, $managedLibraryAfter
    )
    $managedAfterPath = Join-Path (
        Join-Path $managedLibraryAfter "proof-managed-card"
    ) "managed-proof.dng"
    if (-not (Test-Path -LiteralPath $managedAfterPath -PathType Leaf)) {
        throw "Library move did not publish the copied original"
    }
    if (-not (Test-Path -LiteralPath $managedBeforePath -PathType Leaf)) {
        throw "Library move removed the old original before approval"
    }
    $beforeHash = (Get-FileHash -LiteralPath $managedBeforePath -Algorithm SHA256).Hash
    $afterHash = (Get-FileHash -LiteralPath $managedAfterPath -Algorithm SHA256).Hash
    if ($beforeHash -ne $afterHash) {
        throw "Library move changed managed-original bytes"
    }
    $libraryMoveResultLine = @($libraryMoveOutput) | Where-Object {
        $_.StartsWith("FRAMEHOUSE_LIBRARY_MOVE_RESULT=")
    } | Select-Object -Last 1
    $libraryMoveResult = $libraryMoveResultLine.Substring(
        "FRAMEHOUSE_LIBRARY_MOVE_RESULT=".Length
    ) | ConvertFrom-Json
    if (-not $libraryMoveResult.oldLibraryRetained) {
        throw "Library move did not report old-library retention"
    }
    $libraryMoveBackups = @(
        Get-ChildItem -LiteralPath $env:PHOTO_EDITOR_BACKUP_DIR -File |
            Where-Object { $_.Name -like "pre-library-move-*.dump" }
    )
    if ($libraryMoveBackups.Count -eq 0) {
        throw "Library move did not create its PostgreSQL safety backup"
    }
    $managedLibrarySnapshotJson =
        (Invoke-CatalogHelper "catalog_snapshot") -join "`n"
    $managedLibrarySnapshot = $managedLibrarySnapshotJson | ConvertFrom-Json
    $relocatedManagedAsset = @($managedLibrarySnapshot.assets) | Where-Object {
        $_.asset.location.drive_id -eq "proof-managed-card"
    } | Select-Object -First 1
    $expectedManagedRoot = Join-Path $managedLibraryAfter "proof-managed-card"
    if ($relocatedManagedAsset.asset.location.root_path -ne $expectedManagedRoot) {
        throw "Catalog did not adopt the relocated managed-library root"
    }
    $managedLibraryFailed = Join-Path $proofRoot "managed-library-failed"
    Remove-Item -LiteralPath $managedAfterPath
    $failedLibraryMove = (
        Invoke-CatalogHelperExpectFailure "catalog_move_library" @(
            $managedLibraryAfter, $managedLibraryFailed
        )
    ) -join "`n"
    if (-not $failedLibraryMove.Contains("managed original is unavailable")) {
        throw "Failed library move did not identify the missing managed original"
    }
    if (-not (Test-Path -LiteralPath $managedLibraryFailed -PathType Container)) {
        throw "Failed library move did not preserve the selected empty destination"
    }
    if (@(Get-ChildItem -LiteralPath $managedLibraryFailed).Count -ne 0) {
        throw "Failed library move published partial destination content"
    }
    $strandedStaging = @(
        Get-ChildItem -LiteralPath $proofRoot -Directory |
            Where-Object {
                $_.Name -like ".managed-library-failed.framehouse-library-migration-*"
            }
    )
    if ($strandedStaging.Count -ne 0) {
        throw "Failed library move stranded its sibling staging directory"
    }
    $failedMoveSnapshotJson =
        (Invoke-CatalogHelper "catalog_snapshot") -join "`n"
    $failedMoveSnapshot = $failedMoveSnapshotJson | ConvertFrom-Json
    $failedMoveAsset = @($failedMoveSnapshot.assets) | Where-Object {
        $_.asset.location.drive_id -eq "proof-managed-card"
    } | Select-Object -First 1
    if ($failedMoveAsset.asset.location.root_path -ne $expectedManagedRoot) {
        throw "Failed library move changed catalog root authority"
    }
    Copy-Item -LiteralPath $managedBeforePath -Destination $managedAfterPath

    $externalDatabaseUrl = (
        Invoke-CatalogHelper "catalog_local_db" @("connection-url", "--ensure-running") |
            Select-Object -Last 1
    )

    $fixtureDatabase = "photo_editor_corrupt_fixture"
    $fixtureDatabaseUrl = $externalDatabaseUrl.Substring(
        0, $externalDatabaseUrl.LastIndexOf("/")
    ) + "/$fixtureDatabase"
    Invoke-PostgresSql $externalDatabaseUrl "CREATE DATABASE $fixtureDatabase"
    $env:PHOTO_EDITOR_DATABASE_URL = $fixtureDatabaseUrl
    [void](Invoke-CatalogHelper "catalog_migrate")
    Invoke-PostgresSql $fixtureDatabaseUrl @'
INSERT INTO drives (drive_id, display_name, mount_path)
VALUES ('corrupt-fixture-drive', 'Corrupt Fixture Drive', '/fixture');

INSERT INTO assets (
  asset_id, drive_id, root_path, relative_path, import_mode,
  capture_year, capture_month, capture_day,
  camera_make, camera_model, lens_model, iso, shutter_speed, aperture
) VALUES (
  'corrupt-asset', 'corrupt-fixture-drive', '/fixture', 'corrupt.dng', 'referenced',
  2024, 1, 1, NULL, NULL, NULL, 100, NULL, 4.0
);

ALTER TABLE assets DROP CONSTRAINT assets_capture_date_range_check;
UPDATE assets SET capture_year = NULL WHERE asset_id = 'corrupt-asset';
'@
    Assert-CatalogIntegrityFailure "catalog_snapshot" @() "corrupt-asset"

    Invoke-PostgresSql $fixtureDatabaseUrl @'
UPDATE assets SET capture_year = 2024, capture_month = 13
WHERE asset_id = 'corrupt-asset';
'@
    Assert-CatalogIntegrityFailure "catalog_snapshot" @() "corrupt-asset"

    Invoke-PostgresSql $fixtureDatabaseUrl @'
UPDATE assets SET capture_month = 1 WHERE asset_id = 'corrupt-asset';
ALTER TABLE assets DROP CONSTRAINT assets_import_mode_check;
UPDATE assets SET import_mode = 'unknown-mode' WHERE asset_id = 'corrupt-asset';
'@
    Assert-CatalogIntegrityFailure "catalog_snapshot" @() "corrupt-asset"

    Invoke-PostgresSql $fixtureDatabaseUrl @'
UPDATE assets SET import_mode = 'referenced' WHERE asset_id = 'corrupt-asset';
ALTER TABLE asset_review_states DROP CONSTRAINT asset_review_states_rating_range_check;
INSERT INTO asset_review_states (asset_id, rating) VALUES ('corrupt-asset', 6);
'@
    Assert-CatalogIntegrityFailure "catalog_snapshot" @() "corrupt-asset"

    Invoke-PostgresSql $fixtureDatabaseUrl @'
DELETE FROM asset_review_states WHERE asset_id = 'corrupt-asset';
ALTER TABLE asset_relinks ALTER COLUMN relinked_file_path DROP NOT NULL;
INSERT INTO asset_relinks (asset_id, relinked_file_path) VALUES ('corrupt-asset', NULL);
'@
    Assert-CatalogIntegrityFailure "catalog_snapshot" @() "corrupt-asset"

    Invoke-PostgresSql $fixtureDatabaseUrl @'
DELETE FROM asset_relinks WHERE asset_id = 'corrupt-asset';
ALTER TABLE recipe_migration_records
  DROP CONSTRAINT recipe_migration_records_profile_identities_array_check;
INSERT INTO recipe_migration_records (
  asset_id, source_recipe_version, source_renderer, target_renderer,
  profile_identities, plan_hash, evidence_hash, rollback_pointer, status
) VALUES (
  'corrupt-asset', 12, 'legacy', 'current', '{}'::jsonb,
  'plan', 'evidence', 'backup', 'candidate'
);
'@
    Assert-CatalogIntegrityFailure `
        "catalog_migration_inventory" @() "corrupt-asset"

    Invoke-PostgresSql $fixtureDatabaseUrl @'
ALTER TABLE export_jobs DROP CONSTRAINT export_jobs_progress_range_check;
INSERT INTO export_jobs (job_id, preset_name, destination_path, status, progress)
VALUES ('corrupt-export', 'JPEG', '/fixture', 'Queued', 101);

ALTER TABLE timeline_documents DROP CONSTRAINT timeline_documents_duration_range_check;
INSERT INTO timeline_documents (timeline_id, asset_id, duration_seconds)
VALUES ('corrupt-timeline', 'corrupt-timeline-asset', -1);

ALTER TABLE carousel_documents DROP CONSTRAINT carousel_documents_slide_count_range_check;
INSERT INTO carousel_documents (document_id, slide_count)
VALUES ('corrupt-carousel', -1);
'@
    $integrityJson = (Invoke-CatalogHelper "catalog_integrity") -join "`n"
    $integrity = $integrityJson | ConvertFrom-Json
    if ($integrity.scanned_record_count -ne 6) {
        throw "Catalog integrity inventory did not account for unaffected records"
    }
    foreach ($identity in @(
        "corrupt-asset", "corrupt-export", "corrupt-timeline", "corrupt-carousel"
    )) {
        if (-not $integrityJson.Contains($identity)) {
            throw "Catalog integrity inventory hid corrupt record $identity"
        }
    }
    [void](Invoke-CatalogHelperExpectFailure "catalog_integrity" @("--require-clean"))
    Assert-CatalogIntegrityFailure "catalog_export_jobs" @("list") "corrupt-export"
    Assert-CatalogIntegrityFailure `
        "catalog_timeline_documents" @("list") "corrupt-timeline"
    Assert-CatalogIntegrityFailure `
        "catalog_carousel_documents" @("list") "corrupt-carousel"

    Remove-Item Env:PHOTO_EDITOR_DATABASE_URL -ErrorAction SilentlyContinue
    $env:PHOTO_EDITOR_DATABASE_URL = $externalDatabaseUrl
    $env:PHOTO_EDITOR_APP_DATA_DIR = Join-Path $externalClientRoot "app-data"
    $env:PHOTO_EDITOR_LOG_DIR = Join-Path $externalClientRoot "logs"
    $env:PHOTO_EDITOR_BACKUP_DIR = Join-Path $externalClientRoot "backups"
    [void](Invoke-CatalogHelper "catalog_local_db" @("ensure-running"))
    $externalStatusJson =
        (Invoke-CatalogHelper "catalog_local_db" @("status")) -join "`n"
    $externalStatus = $externalStatusJson | ConvertFrom-Json
    if ($externalStatus.mode -ne "external" -or -not $externalStatus.healthy -or
        $null -ne $externalStatus.state_file -or $null -ne $externalStatus.data_dir) {
        throw "Explicit PostgreSQL URL did not preserve external authority"
    }
    if (Test-Path -LiteralPath (Join-Path $externalClientRoot "app-data\postgres")) {
        throw "External mode created an unauthorized managed cluster"
    }
    $externalBackupPath =
        (Invoke-CatalogHelper "catalog_local_db" @("backup") | Select-Object -Last 1)
    if (-not (Test-Path -LiteralPath $externalBackupPath -PathType Leaf)) {
        throw "External PostgreSQL backup was not created: $externalBackupPath"
    }
    $externalRestoreFailure = (
        Invoke-CatalogHelperExpectFailure "catalog_local_db" @("restore-latest")
    ) -join "`n"
    if (-not $externalRestoreFailure.Contains(
            "automatic restore is disabled for an external PostgreSQL server")) {
        throw "External restore refusal did not explain the administrator boundary"
    }

    Write-Host "postgres_catalog_runtime_proof=passed"
    Write-Host "postgres_catalog_credential_recovery=passed"
    Write-Host "postgres_catalog_pre_migration_rollback=passed"
    Write-Host "postgres_catalog_managed_backup_restore=passed"
    Write-Host "postgres_catalog_corrupt_restore_guard=passed"
    Write-Host "postgres_catalog_interrupted_backup_guard=passed"
    Write-Host "postgres_catalog_failed_restore_rollback=passed"
    Write-Host "postgres_catalog_media_relink_dedup=passed"
    Write-Host "postgres_catalog_managed_library_move=passed"
    Write-Host "postgres_catalog_external_authority=passed"
    Write-Host "postgres_catalog_external_restore_guard=passed"
    Write-Host "postgres_catalog_corrupt_decode_guard=passed"
} finally {
    Remove-Item Env:PHOTO_EDITOR_DATABASE_URL -ErrorAction SilentlyContinue
    $env:PHOTO_EDITOR_APP_DATA_DIR = Join-Path $managedRoot "app-data"
    $env:PHOTO_EDITOR_LOG_DIR = Join-Path $managedRoot "logs"
    $env:PHOTO_EDITOR_BACKUP_DIR = Join-Path $managedRoot "backups"
    try {
        [void](Invoke-CatalogHelper "catalog_local_db" @("stop"))
    } catch {
        Write-Warning "Temporary managed PostgreSQL stop failed: $_"
    }
    foreach ($entry in $savedEnvironment.GetEnumerator()) {
        if ($null -eq $entry.Value) {
            Remove-Item "Env:$($entry.Key)" -ErrorAction SilentlyContinue
        } else {
            Set-Item "Env:$($entry.Key)" $entry.Value
        }
    }
    if ([System.IO.Path]::GetFileName($proofRoot).StartsWith(
            "framehouse-postgres-runtime-proof-")) {
        Remove-Item -LiteralPath $proofRoot -Recurse -Force
    }
}

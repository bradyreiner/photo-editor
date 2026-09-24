param(
    [Parameter(Mandatory = $true)]
    [string]$InstallRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    # PackageName and AppId intentionally keep the private-alpha identity so an
    # installed build can upgrade in place. They are not user-visible branding.
    [string]$PackageName = "Virel.Studio",
    [string]$PackageDisplayName = "Looksy",
    [string]$Publisher = "",
    [string]$PublisherDisplayName = "",
    [string]$Description = "Looksy photo editor",
    [string]$Version = "0.1.0",
    [string]$Architecture = "x64",
    [string]$Executable = "bin\Looksy.exe",
    [string]$AppId = "VirelStudio",
    [string]$MinVersion = "10.0.19045.0",
    [string]$MaxVersionTested = "10.0.26100.0",
    [string]$BackgroundColor = "transparent",
    [string]$CertificatePath = "",
    [string]$CertificatePassword = "",
    [string]$TimestampUrl = "",
    [string]$AppInstallerUri = "",
    [string]$PackageBaseUri = ""
)

$ErrorActionPreference = "Stop"

function Get-SdkToolPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ToolName
    )

    $command = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $sdkRoot = "C:\Program Files (x86)\Windows Kits\10\bin"
    if (-not (Test-Path $sdkRoot)) {
        throw "Windows SDK bin directory not found: $sdkRoot"
    }

    $candidate = Get-ChildItem -Path $sdkRoot -Filter $ToolName -Recurse -File |
        Where-Object { $_.FullName -match "\\x64\\" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1

    if ($null -eq $candidate) {
        throw "$ToolName was not found in the Windows SDK."
    }

    return $candidate.FullName
}

function Convert-ToQuadVersion {
    param([string]$InputVersion)

    if ($null -eq $InputVersion) {
        return "0.1.0.0"
    }

    $clean = $InputVersion.Trim()
    if ($clean.StartsWith("v")) {
        $clean = $clean.Substring(1)
    }

    if ([string]::IsNullOrWhiteSpace($clean)) {
        return "0.1.0.0"
    }

    if ($clean -notmatch "^[0-9]+(\.[0-9]+){0,3}$") {
        throw "MSIX package version must contain one to four numeric dot-separated parts, got: $InputVersion"
    }

    $parts = $clean.Split(".")

    foreach ($part in $parts) {
        $value = 0
        [void][int]::TryParse($part, [ref]$value)
        if ($value -lt 0 -or $value -gt 65535) {
            throw "MSIX package version parts must be between 0 and 65535, got: $InputVersion"
        }
    }

    while ($parts.Count -lt 4) {
        $parts += "0"
    }

    return ($parts[0..3] -join ".")
}

function Join-Uri {
    param(
        [string]$Base,
        [string]$Leaf
    )

    if ([string]::IsNullOrWhiteSpace($Base)) {
        return $Leaf
    }

    return ([System.Uri]::new([System.Uri]$Base.TrimEnd("/") + "/", $Leaf)).AbsoluteUri
}

function Get-DefaultIfEmpty {
    param(
        [string]$Value,
        [string]$Fallback
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $Fallback
    }

    return $Value
}

if (-not (Test-Path $InstallRoot)) {
    throw "Install root not found: $InstallRoot"
}

$PackageName = Get-DefaultIfEmpty -Value $PackageName -Fallback "Virel.Studio"
$PackageDisplayName = Get-DefaultIfEmpty -Value $PackageDisplayName -Fallback "Looksy"
$Description = Get-DefaultIfEmpty -Value $Description -Fallback "Looksy photo editor"
$Executable = Get-DefaultIfEmpty -Value $Executable -Fallback "bin\Looksy.exe"
$AppId = Get-DefaultIfEmpty -Value $AppId -Fallback "VirelStudio"
$Architecture = Get-DefaultIfEmpty -Value $Architecture -Fallback "x64"
$MinVersion = Get-DefaultIfEmpty -Value $MinVersion -Fallback "10.0.19045.0"
$MaxVersionTested = Get-DefaultIfEmpty -Value $MaxVersionTested -Fallback "10.0.26100.0"
$BackgroundColor = Get-DefaultIfEmpty -Value $BackgroundColor -Fallback "transparent"

$resolvedInstallRoot = (Resolve-Path $InstallRoot).Path
$resolvedOutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$packageVersion = Convert-ToQuadVersion $Version
$packageFileName = "Looksy-$packageVersion-$Architecture.msix"
$appInstallerFileName = "Looksy.appinstaller"
$stagingRoot = Join-Path $resolvedOutputDir "msix-root"
$assetsDir = Join-Path $stagingRoot "Assets"
$packagePath = Join-Path $resolvedOutputDir $packageFileName
$appInstallerPath = Join-Path $resolvedOutputDir $appInstallerFileName

New-Item -ItemType Directory -Force -Path $resolvedOutputDir | Out-Null
if (Test-Path $stagingRoot) {
    Remove-Item -Recurse -Force $stagingRoot
}
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null

Copy-Item -Path (Join-Path $resolvedInstallRoot "*") -Destination $stagingRoot -Recurse -Force
New-Item -ItemType Directory -Force -Path $assetsDir | Out-Null

$sourceAssetsDir = Join-Path $PSScriptRoot "..\apps\desktop\assets\windows"
$square44Path = Join-Path $assetsDir "Square44x44Logo.png"
$square44AltPath = Join-Path $assetsDir "Square44x44Logo.targetsize-44_altform-unplated.png"
$square150Path = Join-Path $assetsDir "Square150x150Logo.png"
$storeLogoPath = Join-Path $assetsDir "StoreLogo.png"

foreach ($assetName in @(
    "Square44x44Logo.png",
    "Square44x44Logo.targetsize-44_altform-unplated.png",
    "Square150x150Logo.png",
    "StoreLogo.png"
)) {
    $sourceAsset = Join-Path $sourceAssetsDir $assetName
    if (-not (Test-Path $sourceAsset)) {
        throw "Production package asset is missing: $sourceAsset"
    }
    Copy-Item $sourceAsset (Join-Path $assetsDir $assetName) -Force
}

if ([string]::IsNullOrWhiteSpace($Publisher)) {
    throw "Publisher must match the subject of the production code-signing certificate. Set PHOTO_EDITOR_WINDOWS_PACKAGE_PUBLISHER."
}
if ([string]::IsNullOrWhiteSpace($PublisherDisplayName)) {
    throw "PublisherDisplayName must be the approved legal publisher name. Set LOOKSY_LEGAL_PUBLISHER."
}

$manifestPath = Join-Path $stagingRoot "AppxManifest.xml"
$manifestXml = @"
<?xml version="1.0" encoding="utf-8"?>
<Package
  xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
  xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
  xmlns:uap10="http://schemas.microsoft.com/appx/manifest/uap/windows10/10"
  xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
  IgnorableNamespaces="uap uap10 rescap">
  <Identity Name="$PackageName" Version="$packageVersion" Publisher="$Publisher" ProcessorArchitecture="$Architecture" />
  <Properties>
    <DisplayName>$PackageDisplayName</DisplayName>
    <PublisherDisplayName>$PublisherDisplayName</PublisherDisplayName>
    <Description>$Description</Description>
    <Logo>Assets\StoreLogo.png</Logo>
  </Properties>
  <Resources>
    <Resource Language="en-us" />
  </Resources>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Desktop" MinVersion="$MinVersion" MaxVersionTested="$MaxVersionTested" />
  </Dependencies>
  <Capabilities>
    <rescap:Capability Name="runFullTrust" />
  </Capabilities>
  <Applications>
    <Application Id="$AppId" Executable="$Executable" uap10:RuntimeBehavior="packagedClassicApp" uap10:TrustLevel="mediumIL">
      <uap:VisualElements
        DisplayName="$PackageDisplayName"
        Description="$Description"
        Square150x150Logo="Assets\Square150x150Logo.png"
        Square44x44Logo="Assets\Square44x44Logo.png"
        BackgroundColor="$BackgroundColor" />
    </Application>
  </Applications>
</Package>
"@
Set-Content -Path $manifestPath -Value $manifestXml -Encoding UTF8

$makeAppx = Get-SdkToolPath -ToolName "makeappx.exe"
& $makeAppx pack /d $stagingRoot /p $packagePath /o | Write-Host

if (-not [string]::IsNullOrWhiteSpace($CertificatePath)) {
    $signTool = Get-SdkToolPath -ToolName "signtool.exe"
    $signArgs = @("sign", "/fd", "SHA256", "/f", $CertificatePath)
    if (-not [string]::IsNullOrWhiteSpace($CertificatePassword)) {
        $signArgs += @("/p", $CertificatePassword)
    }
    if (-not [string]::IsNullOrWhiteSpace($TimestampUrl)) {
        $signArgs += @("/tr", $TimestampUrl, "/td", "SHA256")
    }
    $signArgs += $packagePath
    & $signTool @signArgs | Write-Host
}

if (-not [string]::IsNullOrWhiteSpace($AppInstallerUri) -and -not [string]::IsNullOrWhiteSpace($PackageBaseUri)) {
    $packageUri = Join-Uri -Base $PackageBaseUri -Leaf $packageFileName
    $appInstallerXml = @"
<?xml version="1.0" encoding="utf-8"?>
<AppInstaller
  xmlns="http://schemas.microsoft.com/appx/appinstaller/2021"
  Version="$packageVersion"
  Uri="$AppInstallerUri">
  <MainPackage
    Name="$PackageName"
    Publisher="$Publisher"
    Version="$packageVersion"
    ProcessorArchitecture="$Architecture"
    Uri="$packageUri" />
  <UpdateSettings>
    <OnLaunch HoursBetweenUpdateChecks="0" ShowPrompt="true" UpdateBlocksActivation="false" />
  </UpdateSettings>
</AppInstaller>
"@
    Set-Content -Path $appInstallerPath -Value $appInstallerXml -Encoding UTF8
}

Write-Host "MSIX package created at $packagePath"
if (Test-Path $appInstallerPath) {
    Write-Host "App Installer file created at $appInstallerPath"
}

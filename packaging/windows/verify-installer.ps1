[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$MsiPath,
    [Parameter(Mandatory)][string]$StageDir,
    [string]$DisplayVersion = "0.2.0-rc1",
    [string]$UpgradeCode = "{6E41AAE8-13C4-4D46-AB5B-7F04E92E9B76}"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-Sha256([string]$Path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try {
        return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace("-", "").ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $sha.Dispose()
    }
}

$msi = (Resolve-Path $MsiPath).Path
$stage = (Resolve-Path $StageDir).Path
if ((Get-Item $msi).Length -le 0) { throw "MSI is empty." }
if ([System.IO.Path]::GetFileName($msi) -notlike "*$DisplayVersion*Windows-x64.msi") {
    throw "MSI filename does not contain $DisplayVersion and Windows-x64."
}

$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.GetType().InvokeMember(
    "OpenDatabase", "InvokeMethod", $null, $installer, @($msi, 0))
function Read-MsiRows([string]$Query, [int]$Columns = 1) {
    $view = $database.GetType().InvokeMember("OpenView", "InvokeMethod", $null, $database, @($Query))
    $view.GetType().InvokeMember("Execute", "InvokeMethod", $null, $view, $null) | Out-Null
    $rows = @()
    while ($record = $view.GetType().InvokeMember("Fetch", "InvokeMethod", $null, $view, $null)) {
        $values = for ($column = 1; $column -le $Columns; ++$column) {
            $record.GetType().InvokeMember("StringData", "GetProperty", $null, $record, $column)
        }
        $rows += ,$values
    }
    return $rows
}
function Read-Property([string]$Name) {
    $escaped = $Name.Replace("'", "''")
    $view = $database.GetType().InvokeMember(
        "OpenView", "InvokeMethod", $null, $database,
        @("SELECT ``Value`` FROM ``Property`` WHERE ``Property``='$escaped'"))
    $view.GetType().InvokeMember("Execute", "InvokeMethod", $null, $view, $null) | Out-Null
    $record = $view.GetType().InvokeMember("Fetch", "InvokeMethod", $null, $view, $null)
    if (-not $record) { return $null }
    return $record.GetType().InvokeMember("StringData", "GetProperty", $null, $record, 1)
}

if ((Read-Property "ProductName") -ne "ATK Player") { throw "Incorrect ProductName." }
if ((Read-Property "ProductVersion") -ne "0.2.0") { throw "Incorrect MSI ProductVersion." }
if ((Read-Property "UpgradeCode").ToUpperInvariant() -ne $UpgradeCode.ToUpperInvariant()) {
    throw "Incorrect UpgradeCode."
}

$summary = $database.SummaryInformation(0)
$template = $summary.Property(7)
if ($template -notmatch "x64|Intel64") { throw "MSI summary is not x64: $template" }

$shortcuts = (Read-MsiRows "SELECT ``Name`` FROM ``Shortcut``") -join "`n"
if ($shortcuts -notmatch "ATK Player") { throw "Start Menu shortcut is missing." }
$registry = Read-MsiRows "SELECT ``Registry``, ``Key``, ``Value`` FROM ``Registry``" 3
$registryText = ($registry | ForEach-Object { $_ -join "|" }) -join "`n"
if ($registryText -notmatch "\.atkproj" -or $registryText -notmatch "ATKPlayer\.Project") {
    throw ".atkproj association is missing."
}

$files = (Read-MsiRows "SELECT ``FileName`` FROM ``File``") | ForEach-Object {
    ([string]$_).Split('|')[-1]
}
$requiredNames = @(
    "ATKPlayer.exe", "Qt6Core.dll", "Qt6Multimedia.dll", "qwindows.dll",
    "windowsmediaplugin.dll", "ATK-Player-MIT.txt", "Qt-LGPL-3.0.txt",
    "FFmpeg-LGPL-2.1.txt", "THIRD_PARTY_NOTICES.txt"
)
foreach ($name in $requiredNames) {
    if ($files -notcontains $name) { throw "MSI file table is missing $name" }
}
foreach ($pattern in @("avcodec-*.dll", "avformat-*.dll", "avutil-*.dll", "swresample-*.dll", "swscale-*.dll")) {
    if (-not ($files | Where-Object { $_ -like $pattern })) { throw "MSI is missing $pattern" }
}
$badFiles = $files | Where-Object {
    $_ -in @("ffmpeg.exe", "ffprobe.exe") -or $_ -like "tst_*.exe" -or
    [System.IO.Path]::GetExtension($_) -in @(".pdb", ".lib", ".obj", ".ilk") -or
    $_ -match '^(Qt6.*d|qwindowsd|.*plugind|q.*backendd|q.*styled|q(gif|ico|jpeg|svg)d|qsvgicond|qnetworklistmanagerd)\.dll$'
}
if ($badFiles) { throw "MSI includes prohibited files: $($badFiles -join ', ')" }

$stageBad = Get-ChildItem $stage -Recurse -File | Where-Object {
    $_.Name -in @("ffmpeg.exe", "ffprobe.exe") -or $_.Name -like "tst_*.exe" -or
    $_.Extension -in @(".pdb", ".lib", ".obj", ".ilk")
}
if ($stageBad) { throw "Stage includes prohibited files: $($stageBad.FullName -join ', ')" }

$hash = Get-Sha256 $msi
Write-Host "Verified x64 MSI: $msi"
Write-Host "ProductName: ATK Player"
Write-Host "ProductVersion: 0.2.0 ($DisplayVersion display release)"
Write-Host "UpgradeCode: $UpgradeCode"
Write-Host "SHA-256: $hash"

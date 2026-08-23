[CmdletBinding()]
param(
    [string]$VersionSuffix = "rc1",
    [string]$WixVersion = "4.0.6",
    [string]$DotNetSdkVersion = "8.0.419",
    [string]$PythonExecutable,
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$SkipSmokeTest,
    [string]$CertificateThumbprint,
    [string]$PfxPath,
    [string]$PfxPassword,
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "../..")).Path
$packageRoot = Join-Path $repoRoot "build/package/windows"
$buildDir = Join-Path $repoRoot "build/windows-package"
$stageDir = Join-Path $packageRoot "stage"
$toolsDir = Join-Path $packageRoot "tools"
$displayVersion = if ($VersionSuffix) { "0.2.0-$VersionSuffix" } else { "0.2.0" }
$productVersion = "0.2.0"
$msiPath = Join-Path $packageRoot "ATK-Player-$displayVersion-Windows-x64.msi"

function Assert-LastExitCode([string]$Action) {
    if ($LASTEXITCODE -ne 0) { throw "$Action failed with exit code $LASTEXITCODE" }
}

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

function Resolve-RequiredCommand([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) { throw "Required command '$Name' was not found." }
    return $command.Source
}

function Resolve-Wix {
    $installed = Get-Command wix -ErrorAction SilentlyContinue
    if ($installed) { return $installed.Source }

    $wixExe = Join-Path $toolsDir "wix/wix.exe"
    if (Test-Path $wixExe) { return $wixExe }

    New-Item -ItemType Directory -Path $toolsDir -Force | Out-Null
    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    $hasSdk = $false
    if ($dotnet) {
        $sdks = & $dotnet.Source --list-sdks 2>$null
        $hasSdk = [bool]$sdks
    }

    if (-not $hasSdk) {
        $dotnetRoot = Join-Path $toolsDir "dotnet"
        $dotnetExe = Join-Path $dotnetRoot "dotnet.exe"
        if (-not (Test-Path $dotnetExe)) {
            $installer = Join-Path $toolsDir "dotnet-install.ps1"
            Invoke-WebRequest https://dot.net/v1/dotnet-install.ps1 -OutFile $installer
            & powershell -NoProfile -ExecutionPolicy Bypass -File $installer `
                -Version $DotNetSdkVersion -InstallDir $dotnetRoot -NoPath
            Assert-LastExitCode "Developer-local .NET SDK bootstrap"
        }
        $dotnet = [pscustomobject]@{ Source = $dotnetExe }
    }

    $wixDir = Split-Path -Parent $wixExe
    New-Item -ItemType Directory -Path $wixDir -Force | Out-Null
    & $dotnet.Source tool install wix --tool-path $wixDir --version $WixVersion | Out-Host
    Assert-LastExitCode "WiX Toolset bootstrap"
    if (-not (Test-Path $wixExe)) { throw "WiX bootstrap did not create $wixExe" }
    return $wixExe
}

function Invoke-Signing([string]$Path) {
    if (-not $CertificateThumbprint -and -not $PfxPath) {
        Write-Warning "UNSIGNED: no trusted signing certificate was supplied for $Path"
        return
    }
    $signtool = Resolve-RequiredCommand "signtool"
    $arguments = @("sign", "/fd", "SHA256", "/td", "SHA256", "/tr", $TimestampUrl)
    if ($CertificateThumbprint) {
        $arguments += @("/sha1", $CertificateThumbprint)
    } else {
        $arguments += @("/f", (Resolve-Path $PfxPath).Path)
        if ($PfxPassword) { $arguments += @("/p", $PfxPassword) }
    }
    $arguments += $Path
    & $signtool @arguments
    Assert-LastExitCode "Signing $Path"
}

function Get-StableWixId([string]$Prefix, [string]$Value) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Value.ToLowerInvariant())
        $hash = [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace("-", "")
        return "$Prefix$($hash.Substring(0, 24))"
    } finally {
        $sha.Dispose()
    }
}

function Get-RelativePath([string]$BasePath, [string]$TargetPath) {
    $separator = [IO.Path]::DirectorySeparatorChar
    $baseFull = [IO.Path]::GetFullPath($BasePath).TrimEnd($separator) + $separator
    $baseUri = [Uri]::new($baseFull)
    $targetUri = [Uri]::new([IO.Path]::GetFullPath($TargetPath))
    return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($targetUri).ToString()).Replace('/', $separator)
}

function New-WixFilesFragment([string]$SourceStage, [string]$OutputPath) {
    $namespace = "http://wixtoolset.org/schemas/v4/wxs"
    $settings = [System.Xml.XmlWriterSettings]::new()
    $settings.Indent = $true
    $settings.Encoding = [Text.UTF8Encoding]::new($false)
    $writer = [System.Xml.XmlWriter]::Create($OutputPath, $settings)
    try {
        $writer.WriteStartDocument()
        $writer.WriteStartElement("Wix", $namespace)

        $directories = @{}
        foreach ($file in Get-ChildItem $SourceStage -Recurse -File) {
            $relative = Get-RelativePath $SourceStage $file.FullName
            $directory = [IO.Path]::GetDirectoryName($relative)
            while ($directory) {
                $directories[$directory] = $true
                $directory = [IO.Path]::GetDirectoryName($directory)
            }
        }
        foreach ($directory in $directories.Keys | Sort-Object { ($_ -split '[\\/]').Count }, { $_ }) {
            $parent = [IO.Path]::GetDirectoryName($directory)
            $parentId = if ($parent) { Get-StableWixId "D" $parent } else { "INSTALLFOLDER" }
            $writer.WriteStartElement("Fragment", $namespace)
            $writer.WriteStartElement("DirectoryRef", $namespace)
            $writer.WriteAttributeString("Id", $parentId)
            $writer.WriteStartElement("Directory", $namespace)
            $writer.WriteAttributeString("Id", (Get-StableWixId "D" $directory))
            $writer.WriteAttributeString("Name", [IO.Path]::GetFileName($directory))
            $writer.WriteEndElement()
            $writer.WriteEndElement()
            $writer.WriteEndElement()
        }

        $componentIds = @()
        foreach ($file in Get-ChildItem $SourceStage -Recurse -File | Sort-Object FullName) {
            $relative = Get-RelativePath $SourceStage $file.FullName
            $directory = [IO.Path]::GetDirectoryName($relative)
            $directoryId = if ($directory) { Get-StableWixId "D" $directory } else { "INSTALLFOLDER" }
            $componentId = Get-StableWixId "C" $relative
            $componentIds += $componentId
            $writer.WriteStartElement("Fragment", $namespace)
            $writer.WriteStartElement("DirectoryRef", $namespace)
            $writer.WriteAttributeString("Id", $directoryId)
            $writer.WriteStartElement("Component", $namespace)
            $writer.WriteAttributeString("Id", $componentId)
            $writer.WriteAttributeString("Guid", "*")
            $writer.WriteStartElement("File", $namespace)
            $writer.WriteAttributeString("Id", (Get-StableWixId "F" $relative))
            $writer.WriteAttributeString("Source", $file.FullName)
            $writer.WriteAttributeString("Name", $file.Name)
            $writer.WriteAttributeString("KeyPath", "yes")
            $writer.WriteEndElement()
            $writer.WriteEndElement()
            $writer.WriteEndElement()
            $writer.WriteEndElement()
        }

        $writer.WriteStartElement("Fragment", $namespace)
        $writer.WriteStartElement("ComponentGroup", $namespace)
        $writer.WriteAttributeString("Id", "ApplicationFiles")
        foreach ($componentId in $componentIds) {
            $writer.WriteStartElement("ComponentRef", $namespace)
            $writer.WriteAttributeString("Id", $componentId)
            $writer.WriteEndElement()
        }
        $writer.WriteEndElement()
        $writer.WriteEndElement()
        $writer.WriteEndElement()
        $writer.WriteEndDocument()
    } finally {
        $writer.Dispose()
    }
}

Set-Location $repoRoot
Resolve-RequiredCommand "cmake" | Out-Null
Resolve-RequiredCommand "ninja" | Out-Null
$compilerCommand = Get-Command cl -ErrorAction SilentlyContinue
$compiler = if ($compilerCommand) {
    $compilerCommand.Source
} elseif ($env:VCToolsInstallDir) {
    Join-Path $env:VCToolsInstallDir "bin/Hostx64/x64/cl.exe"
} else {
    $null
}
if (-not $compiler -or -not (Test-Path $compiler)) {
    throw "The x64 MSVC compiler was not found. Run from a VS 2022 x64 developer environment."
}
$compiler = $compiler.Replace('\', '/')
$sdkBin = if ($env:WindowsSdkVerBinPath) { Join-Path $env:WindowsSdkVerBinPath "x64" } else { $null }
$resourceCompiler = if ($sdkBin) { Join-Path $sdkBin "rc.exe" } else { $null }
$manifestTool = if ($sdkBin) { Join-Path $sdkBin "mt.exe" } else { $null }
if (-not (Test-Path $resourceCompiler) -or -not (Test-Path $manifestTool)) {
    throw "Windows SDK x64 rc.exe/mt.exe were not found. Install the VS 2022 Windows SDK component."
}
$resourceCompiler = $resourceCompiler.Replace('\', '/')
$manifestTool = $manifestTool.Replace('\', '/')
$pythonCommand = if ($PythonExecutable) {
    Get-Item (Resolve-Path $PythonExecutable)
} else {
    Get-Command python -ErrorAction SilentlyContinue
}
if (-not $pythonCommand) {
    throw "Python 3 was not found. Put python on PATH or pass -PythonExecutable."
}
$python = if ($pythonCommand.PSObject.Properties.Name -contains "Source") {
    $pythonCommand.Source
} else {
    $pythonCommand.FullName
}
& $python --version | Out-Host
Assert-LastExitCode "Python 3 prerequisite"
$python = $python.Replace('\', '/')
if ($env:PROCESSOR_ARCHITECTURE -ne "AMD64") { throw "M6 packaging requires a Windows x64 host." }
if (-not $env:QT_ROOT -or -not (Test-Path $env:QT_ROOT)) { throw "QT_ROOT must point to Qt 6.9.3." }
if (-not $env:VCPKG_ROOT -or -not (Test-Path "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake")) {
    throw "VCPKG_ROOT must point to the pinned vcpkg checkout."
}
$wix = Resolve-Wix
Write-Host "WiX: $(& $wix --version)"

if (-not $SkipBuild) {
    $configure = @(
        "-S", $repoRoot, "-B", $buildDir, "--fresh", "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DCMAKE_CXX_COMPILER=$compiler",
        "-DCMAKE_RC_COMPILER=$resourceCompiler", "-DCMAKE_MT=$manifestTool",
        "-DPython3_EXECUTABLE=$python",
        "-DCMAKE_INSTALL_BINDIR=.",
        "-DATK_BUILD_TESTS=ON", "-DATK_VERSION_SUFFIX=$VersionSuffix",
        "-DATK_QT_ROOT=$env:QT_ROOT",
        "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake",
        "-DVCPKG_TARGET_TRIPLET=x64-windows"
    )
    if ($env:ATK_VCPKG_INSTALLED_DIR) {
        $configure += "-DVCPKG_INSTALLED_DIR=$env:ATK_VCPKG_INSTALLED_DIR"
    }
    & cmake @configure
    Assert-LastExitCode "Release candidate configure"
    & cmake --build $buildDir
    Assert-LastExitCode "Release candidate build"
}

if (-not $SkipTests) {
    & ctest --test-dir $buildDir --output-on-failure
    Assert-LastExitCode "Release candidate tests"
}

$resolvedPackageRoot = [System.IO.Path]::GetFullPath($packageRoot)
$resolvedStage = [System.IO.Path]::GetFullPath($stageDir)
if (-not $resolvedStage.StartsWith($resolvedPackageRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean stage outside $resolvedPackageRoot"
}
if (Test-Path $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force }
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
& cmake --install $buildDir --prefix $stageDir
Assert-LastExitCode "Release staging install"

$stagedExe = Join-Path $stageDir "ATKPlayer.exe"
if (-not (Test-Path $stagedExe)) { throw "Staged executable is missing: $stagedExe" }
Invoke-Signing $stagedExe

$required = @(
    "ATKPlayer.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll",
    "Qt6Multimedia.dll", "Qt6Network.dll", "plugins/platforms/qwindows.dll",
    "plugins/multimedia/windowsmediaplugin.dll", "licenses/ATK-Player-MIT.txt",
    "licenses/Qt-LGPL-3.0.txt", "licenses/FFmpeg-LGPL-2.1.txt",
    "licenses/THIRD_PARTY_NOTICES.txt"
)
foreach ($relative in $required) {
    if (-not (Test-Path (Join-Path $stageDir $relative))) { throw "Staged file missing: $relative" }
}
$ffmpegPatterns = @("avcodec-*.dll", "avformat-*.dll", "avutil-*.dll", "swresample-*.dll", "swscale-*.dll")
foreach ($pattern in $ffmpegPatterns) {
    if (@(Get-ChildItem $stageDir -Filter $pattern -File).Count -ne 1) {
        throw "Expected exactly one staged FFmpeg runtime matching $pattern"
    }
}
$prohibited = Get-ChildItem $stageDir -Recurse -File | Where-Object {
    $_.Name -in @("ffmpeg.exe", "ffprobe.exe") -or
    $_.Extension -in @(".pdb", ".lib", ".obj", ".ilk") -or
    $_.Name -like "tst_*.exe" -or
    $_.Name -match '^(Qt6.*d|qwindowsd|.*plugind|q.*backendd|q.*styled|q(gif|ico|jpeg|svg)d|qsvgicond|qnetworklistmanagerd)\.dll$'
}
if ($prohibited) { throw "Prohibited staged files: $($prohibited.FullName -join ', ')" }

if (-not $SkipSmokeTest) {
    $oldPlatform = $env:QT_QPA_PLATFORM
    $oldPath = $env:PATH
    try {
        $env:QT_QPA_PLATFORM = "offscreen"
        $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
        $process = Start-Process -FilePath $stagedExe -WorkingDirectory $stageDir -PassThru
        Start-Sleep -Seconds 8
        $process.Refresh()
        if ($process.HasExited) { throw "Staged ATKPlayer.exe exited with code $($process.ExitCode)" }
        Stop-Process -Id $process.Id
        Wait-Process -Id $process.Id -ErrorAction SilentlyContinue
        Write-Host "Staged executable smoke test passed."
    } finally {
        $env:QT_QPA_PLATFORM = $oldPlatform
        $env:PATH = $oldPath
    }
}

New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
if (Test-Path $msiPath) { Remove-Item -LiteralPath $msiPath -Force }
$filesWxs = Join-Path $packageRoot "Files.wxs"
New-WixFilesFragment $stageDir $filesWxs
& $wix build -arch x64 `
    -d "StageDir=$stageDir" -d "SourceDir=$repoRoot" `
    -d "ProductVersion=$productVersion" -d "DisplayVersion=$displayVersion" `
    -o $msiPath (Join-Path $scriptDir "Package.wxs") $filesWxs
Assert-LastExitCode "WiX MSI build"
Invoke-Signing $msiPath

& (Join-Path $scriptDir "verify-installer.ps1") -MsiPath $msiPath -StageDir $stageDir `
    -DisplayVersion $displayVersion
Assert-LastExitCode "MSI verification"

$hash = Get-Sha256 $msiPath
$checksumPath = "$msiPath.sha256"
Set-Content -LiteralPath $checksumPath -Encoding ascii -NoNewline `
    -Value "$hash  $([System.IO.Path]::GetFileName($msiPath))`n"
Write-Host "MSI: $msiPath"
Write-Host "SHA-256: $hash"
Write-Host "Checksum: $checksumPath"

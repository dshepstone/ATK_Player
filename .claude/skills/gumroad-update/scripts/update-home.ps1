<#
Publishes home.html as the custom store home page (Gumroad slug "profile").

Needs the Gumroad CLI, signed in (`gumroad auth login`). Run from the working
folder holding home.html; the live page is backed up there first.

  powershell -ExecutionPolicy Bypass -File update-home.ps1            # backup, preview, push --dry-run
  powershell -ExecutionPolicy Bypass -File update-home.ps1 -Apply     # publish

Roll back with `gumroad pages push profile <backup>`, or "Remove custom page"
in the Gumroad dashboard.
#>
[CmdletBinding()]
param(
    [string]$HtmlFile = ".\home.html",
    [string]$Store = "shepstone.gumroad.com",
    [switch]$Apply
)

$ErrorActionPreference = "Stop"
$workDir = (Get-Location).Path
if (-not (Test-Path -LiteralPath $HtmlFile)) { throw "Page file not found: $HtmlFile (run get-home.ps1 first)." }
$HtmlFile = (Resolve-Path -LiteralPath $HtmlFile).Path

$found = Get-Command gumroad -ErrorAction SilentlyContinue
if (-not $found) {
    throw "Gumroad CLI not found. Install it from https://github.com/antiwork/gumroad-cli/releases, then reopen the terminal."
}
$cli = $found.Source

function Invoke-Gumroad {
    param([string[]]$Arguments)
    Write-Host "> gumroad $($Arguments -join ' ')"
    # Output goes to the screen, not into the return value, so the caller
    # gets only the exit code.
    & $cli @Arguments 2>&1 | Out-Host
    return $LASTEXITCODE
}

function Get-ImageLinks {
    param([string]$Html)
    [regex]::Matches($Html, '<img[^>]+src="([^"]+)"') | ForEach-Object { $_.Groups[1].Value }
}

# Gumroad's sandbox only loads images it hosts; anything else is a blank box.
$offHost = @(Get-ImageLinks ([IO.File]::ReadAllText($HtmlFile)) |
    Where-Object { -not $_.StartsWith("https://public-files.gumroad.com/") })
if ($offHost.Count) {
    Write-Warning "These images aren't on Gumroad and won't show on the page:"
    $offHost | ForEach-Object { Write-Host "  $_" }
    Write-Host "Run use-gumroad-images.ps1 to move them."
    if ($Apply) { throw "Not publishing until every image is on public-files.gumroad.com." }
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backup = Join-Path $workDir "gumroad-home-backup-$stamp.html"
try {
    Invoke-WebRequest -UseBasicParsing -Uri "https://$Store/landing/embed" -OutFile $backup
    Write-Host "Backed up the live home page to $backup"
} catch {
    Write-Warning "Couldn't back up the live page ($($_.Exception.Message))."
    if ($Apply) { throw "Not publishing without a backup." }
}

if (-not $Apply) {
    if ((Invoke-Gumroad @("pages", "preview", $HtmlFile)) -ne 0) { Write-Warning "Preview failed (see above)." }
    if ((Invoke-Gumroad @("pages", "push", "profile", $HtmlFile, "--dry-run")) -ne 0) { Write-Warning "Push dry run failed (see above)." }
    Write-Host ""
    Write-Host "Dry run: nothing published. Re-run with -Apply to publish $HtmlFile."
    return
}

if ((Invoke-Gumroad @("pages", "push", "profile", $HtmlFile)) -ne 0) {
    & $cli pages push --help 2>&1 | Out-Host
    throw "Push failed (see above)."
}

Start-Sleep -Seconds 5
$live = (Invoke-WebRequest -UseBasicParsing -Uri "https://$Store/landing/embed").Content
Write-Host ""
Write-Host "Published. Images on the live page:"
Get-ImageLinks $live | ForEach-Object { Write-Host "  $_" }
Write-Host "Check https://$Store"

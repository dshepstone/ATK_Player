<#
Saves the store home page's own HTML to home.html, for editing.

The store URL only serves Gumroad's iframe wrapper; the page itself is at
/landing/embed, wrapped in scripts Gumroad injects. This keeps only the part
the seller wrote: from after the <script id="gumroad-data"> block up to the
first injected data-gumroad-* bridge script.

  powershell -ExecutionPolicy Bypass -File get-home.ps1                         # live page -> .\home.html
  powershell -ExecutionPolicy Bypass -File get-home.ps1 -Source .\backup.html   # strip a saved copy
#>
[CmdletBinding()]
param(
    [string]$Store = "shepstone.gumroad.com",
    [string]$Source,
    [string]$OutFile = ".\home.html"
)

$ErrorActionPreference = "Stop"
if ($Source) {
    $raw = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $Source).Path)
} else {
    $resp = Invoke-WebRequest -UseBasicParsing -Uri "https://$Store/landing/embed"
    # Decode as UTF-8 regardless of what Windows PowerShell guessed.
    $raw = [Text.Encoding]::UTF8.GetString($resp.RawContentStream.ToArray())
}

$start = [regex]::Match($raw, '(?s)<script id="gumroad-data"[^>]*>.*?</script>')
if (-not $start.Success) { throw "Couldn't find Gumroad's data block; the page layout may have changed. Save the page source and edit it by hand." }
$from = $start.Index + $start.Length
$end = [regex]::Match($raw.Substring($from), '<script[^>]*data-gumroad-[a-z-]+-bridge')
if (-not $end.Success) { throw "Couldn't find where Gumroad's injected scripts start; edit the page source by hand." }

$page = $raw.Substring($from, $end.Index).Trim() + "`n"
$outPath = Join-Path (Get-Location).Path $OutFile
[IO.File]::WriteAllText($outPath, $page, (New-Object Text.UTF8Encoding $false))
Write-Host "Saved $($page.Length) characters to $outPath"

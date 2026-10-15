<#
Moves every image in home.html onto Gumroad's own image host.

Gumroad shows the home page in a sandbox that only loads images from
public-files.gumroad.com, so any other <img src> shows as a blank box. For each
such image this downloads it (or uses a local file path), uploads it with
`gumroad media upload`, and rewrites the link. home.html is only saved when
every image succeeded.

  powershell -ExecutionPolicy Bypass -File use-gumroad-images.ps1 [-HtmlFile .\home.html]
#>
[CmdletBinding()]
param([string]$HtmlFile = ".\home.html")

$ErrorActionPreference = "Stop"
$file = (Resolve-Path -LiteralPath $HtmlFile).Path
$html = [IO.File]::ReadAllText($file)
$gumroadHost = "https://public-files.gumroad.com/"

$sources = [regex]::Matches($html, '<img[^>]+src="([^"]+)"') |
    ForEach-Object { $_.Groups[1].Value } |
    Where-Object { -not $_.StartsWith($gumroadHost) } |
    Select-Object -Unique
if (-not $sources) { Write-Host "All images are already on Gumroad. Nothing to do."; return }

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("gumroad-images-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    foreach ($src in $sources) {
        if ($src -match '^https?://') {
            $name = [IO.Path]::GetFileName(([uri]$src).AbsolutePath)
            if (-not $name) { $name = "image.png" }
            $local = Join-Path $tmp $name
            Invoke-WebRequest -UseBasicParsing -Uri $src -OutFile $local
        } else {
            $local = (Resolve-Path -LiteralPath $src).Path
        }
        $bytes = [IO.File]::ReadAllBytes($local)
        $isPng = $bytes.Length -gt 4 -and $bytes[0] -eq 137 -and $bytes[1] -eq 80
        $isJpg = $bytes.Length -gt 3 -and $bytes[0] -eq 255 -and $bytes[1] -eq 216
        $isGif = $bytes.Length -gt 3 -and $bytes[0] -eq 71 -and $bytes[1] -eq 73
        $isWebp = $bytes.Length -gt 12 -and $bytes[8] -eq 87 -and $bytes[9] -eq 69
        if (-not ($isPng -or $isJpg -or $isGif -or $isWebp)) {
            throw "$src didn't download as an image ($($bytes.Length) bytes). Nothing was changed."
        }
        Write-Host "> gumroad media upload $local"
        $out = & gumroad media upload $local 2>&1 | Out-String
        Write-Host $out
        $url = [regex]::Match($out, 'https://public-files\.gumroad\.com/\S+').Value
        if (-not $url) { throw "No Gumroad link in the upload output for $src. Nothing was changed." }
        $html = $html.Replace($src, $url)
        Write-Host "$src -> $url"
    }
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

[IO.File]::WriteAllText($file, $html, (New-Object Text.UTF8Encoding $false))
Write-Host "Saved $file with $(@($sources).Count) image link(s) moved to Gumroad."

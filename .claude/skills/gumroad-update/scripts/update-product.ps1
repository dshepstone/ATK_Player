<#
Replaces a Gumroad product's description (default: ATK Player).

Needs $env:GUMROAD_TOKEN (Gumroad > Settings > Advanced > Applications >
Generate access token). Run from the working folder; backups are saved there.

  powershell -ExecutionPolicy Bypass -File update-product.ps1 -DescriptionFile .\desc.html          # dry run + backup
  powershell -ExecutionPolicy Bypass -File update-product.ps1 -DescriptionFile .\desc.html -Apply   # send it

Only the description is sent: files, price, tags and content stay as they are.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$DescriptionFile,
    [string]$ProductName = "ATK Player",
    [switch]$Apply
)

$ErrorActionPreference = "Stop"
$token = $env:GUMROAD_TOKEN
if (-not $token) { throw 'Set $env:GUMROAD_TOKEN first (Gumroad > Settings > Advanced > Applications).' }
if (-not (Test-Path -LiteralPath $DescriptionFile)) { throw "Description file not found: $DescriptionFile" }
$workDir = (Get-Location).Path
$api = "https://api.gumroad.com/v2"

$list = Invoke-RestMethod -Method Get -Uri "$api/products" -Body @{ access_token = $token }
if (-not $list.success) { throw "Gumroad returned an error listing products." }
$product = @($list.products | Where-Object { $_.name -like "*$ProductName*" })
if ($product.Count -ne 1) {
    $list.products | ForEach-Object { Write-Host "  $($_.id)  $($_.name)" }
    throw "Expected one product matching '$ProductName', found $($product.Count). Use -ProductName with a more exact name."
}
$product = $product[0]
Write-Host "Product: $($product.name)  (id $($product.id))  $($product.short_url)"

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backup = Join-Path $workDir "gumroad-description-backup-$stamp.html"
[IO.File]::WriteAllText($backup, [string]$product.description, (New-Object Text.UTF8Encoding $false))
Write-Host "Backed up current description to $backup"

$newDescription = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $DescriptionFile).Path)
if (-not $Apply) {
    Write-Host "Dry run. New description is $($newDescription.Length) characters. Re-run with -Apply to send it."
    return
}

try {
    # Encode the form body as UTF-8 ourselves; Windows PowerShell 5.1 would
    # otherwise mangle characters such as en dashes and arrows.
    $form = "access_token=" + [uri]::EscapeDataString($token) +
            "&description=" + [uri]::EscapeDataString($newDescription)
    $result = Invoke-RestMethod -Method Put -Uri "$api/products/$($product.id)" `
        -ContentType "application/x-www-form-urlencoded; charset=utf-8" `
        -Body ([Text.Encoding]::UTF8.GetBytes($form))
} catch {
    $status = $_.Exception.Response.StatusCode.value__
    if ($status -eq 404) {
        throw "Gumroad's API refused the edit (404). Paste the description in the Gumroad dashboard instead; nothing was changed."
    }
    throw
}
if (-not $result.success) { throw "Gumroad did not accept the update: $($result | ConvertTo-Json -Depth 4)" }

$check = Invoke-RestMethod -Method Get -Uri "$api/products/$($product.id)" -Body @{ access_token = $token }
if ($check.product.description -eq $newDescription) {
    Write-Host "Updated: $($product.short_url)"
} else {
    Write-Warning "Gumroad said success, but the description read back differs from the file (Gumroad may have cleaned up the HTML). Check $($product.short_url)"
}

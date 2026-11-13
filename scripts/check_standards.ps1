[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$Root = Split-Path -Parent $PSScriptRoot
$SrcDir = Join-Path $Root "src"
$TestsDir = Join-Path $Root "tests"
$Violations = [System.Collections.Generic.List[string]]::new()

$firstPartyFiles = @(
    Get-ChildItem -Path $SrcDir, $TestsDir -Recurse -File -Include *.ixx, *.cpp, *.h, *.hpp
)

foreach ($file in $firstPartyFiles) {
    $rel = $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
    $lines = Get-Content -Path $file.FullName -Encoding UTF8
    $lineCount = $lines.Count

    if ($file.Extension -in @('.h', '.hpp')) {
        $Violations.Add("$rel : Traditional header forbidden inside src/ or tests/")
    }
    if ($file.Extension -eq '.ixx' -and $lineCount -gt 200) {
        $Violations.Add("$rel : .ixx file has $lineCount lines (hard limit 200)")
    }
    if ($file.Extension -eq '.cpp' -and $lineCount -gt 450) {
        $Violations.Add("$rel : .cpp file has $lineCount lines (hard limit 450)")
    }

    $inExportBlock = $false
    for ($i = 0; $i -lt $lineCount; $i++) {
        $line = $lines[$i]
        $ln = $i + 1
        if ($line -match '^\s*export\s+module\b') {
            $inExportBlock = $true
        }
        if ($file.Extension -eq '.ixx' -and $inExportBlock -and $line -match '^\s*#\s*include\b') {
            $Violations.Add("${rel}:$ln : #include inside .ixx module purview forbidden")
        }
        if ($line -match '\bCP_ACP\b') {
            $Violations.Add("${rel}:$ln : CP_ACP forbidden")
        }
        if ($line -match '#\s*pragma\s+warning\s*\(\s*disable') {
            $Violations.Add("${rel}:$ln : #pragma warning(disable) forbidden")
        }
        if ($rel -match '^src/(domain|app)/' -and $line -match 'windows\.h|d3d11\.h|imgui\.h') {
            $Violations.Add("${rel}:$ln : Platform/UI header leak inside domain/app")
        }
        if ($rel -match '^src/ui/' -and $line -match '^\s*import\s+motrix\.(engine|ipc)\b') {
            $Violations.Add("${rel}:$ln : Reverse import from motrix.ui to engine/ipc")
        }
    }
}

if ($Violations.Count -gt 0) {
    Write-Host "Engineering standard violations found:" -ForegroundColor Red
    foreach ($v in $Violations) {
        Write-Host "  - $v" -ForegroundColor Red
    }
    exit 1
}

Write-Host "All engineering standard checks passed ($($firstPartyFiles.Count) files checked)." -ForegroundColor Green

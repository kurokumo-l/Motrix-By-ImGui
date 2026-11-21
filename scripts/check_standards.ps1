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
    Get-ChildItem -Path $SrcDir, $TestsDir -Recurse -File -Include *.ixx, *.cpp, *.h, *.hpp |
        Sort-Object FullName
)

$bareImGuiRegex =
    'ImGui::(BeginChild|EndChild|BeginPopup|BeginPopupModal|EndPopup|BeginTable|EndTable|BeginGroup|EndGroup|PushFont|PopFont|PushStyleColor|PopStyleColor|PushStyleVar|PopStyleVar|PushID|PopID|PushTextWrapPos|PopTextWrapPos|PushClipRect|PopClipRect)\b|->(PushClipRect|PopClipRect)\b'

$allowedWin32EncodingFiles = '^src/(core/encoding|platform/win32|engine/http_winhttp|engine/tracker_winhttp|engine/win32_util|ipc/named_pipe_ipc|ui/fonts_dwrite|main\.cpp)'

foreach ($file in $firstPartyFiles) {
    $rel = $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
    $lines = @(Get-Content -Path $file.FullName -Encoding UTF8)
    $lineCount = $lines.Count
    $isTest = ($rel -match '^tests/')

    if ($file.Extension -in @('.h', '.hpp')) {
        $Violations.Add("$rel : Traditional header forbidden inside src/ or tests/")
    }
    if ($file.Extension -eq '.ixx' -and $lineCount -gt 450) {
        $Violations.Add("$rel : .ixx file has $lineCount lines (limit 450)")
    }
    if ($file.Extension -eq '.cpp' -and $lineCount -gt 1000) {
        $Violations.Add("$rel : .cpp file has $lineCount lines (limit 1000)")
    }

    $inExportBlock = $false
    $depth = 0
    $ctrlStack = [System.Collections.Generic.Stack[int]]::new()
    $maxCtrl = 0
    $maxCtrlLine = 0

    $nsDepth = 0
    $inFunc = $false
    $funcDepth = 0
    $funcStart = 0
    $funcName = ""
    $pendingSig = ""
    $pendingStart = 0

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
        if (-not $isTest -and $rel -ne 'src/ui/scope_guard.cpp' -and $line -match $bareImGuiRegex) {
            $Violations.Add("${rel}:$ln : Bare ImGui stack call '$($Matches[0])' must use ScopeGuard RAII")
        }
        if (-not $isTest -and $line -match '\b(std::)?snprintf\b') {
            $Violations.Add("${rel}:$ln : snprintf forbidden in first-party source (use std::format/std::to_string)")
        }
        if (-not $isTest -and $rel -ne 'src/core/encoding.cpp' -and
            $line -match '\b(MultiByteToWideChar|WideCharToMultiByte)\b') {
            $Violations.Add("${rel}:$ln : Raw Win32 encoding API '$($Matches[0])' only allowed in src/core/encoding.cpp")
        }
        if (-not $isTest -and $rel -notmatch $allowedWin32EncodingFiles -and
            $line -match '\b(Utf8ToWide|WideToUtf8)\b') {
            $Violations.Add("${rel}:$ln : Win32 wide/UTF-8 conversion '$($Matches[0])' outside allowed OS boundary")
        }
        if (-not $isTest -and $line -match '\.string\s*\(\s*\)') {
            $Violations.Add("${rel}:$ln : filesystem::path::string() forbidden on Windows (use motrix::core::PathToUtf8)")
        }
        if (-not $isTest -and $line -match '\.get\s*<|->get\s*<\s*std::string\s*>') {
            $Violations.Add("${rel}:$ln : Unchecked JSON .get<T>() forbidden (use type checks / .get_ref)")
        }
        if (-not $isTest -and $line -match 'json::parse\(' -and $line -notmatch 'json::parse\(.+,\s*nullptr,\s*false') {
            $Violations.Add("${rel}:$ln : Throwing json::parse() forbidden (pass nullptr, false)")
        }
        if ($rel -match '^src/ui/' -and $line -match '^\s*static\s+(?!constexpr\b)(?!const\b)') {
            $Violations.Add("${rel}:$ln : Mutable static UI state forbidden")
        }

        if (-not $isTest) {
            # Strip string literals BEFORE // comments so URLs like "https://..." are preserved intact.
            $code = ($line -replace '"([^"\\]|\\.)*"', '""') -replace '//.*$', ''
            $trimmed = $code.Trim()
            $opens = ([regex]::Matches($code, '\{')).Count
            $closes = ([regex]::Matches($code, '\}')).Count

            if ($opens -gt 0 -and $trimmed -match '^(\}\s*else\s+|else\s+)?(if|for|while|switch)\b') {
                $ctrlStack.Push($depth + 1)
                if ($ctrlStack.Count -gt $maxCtrl) {
                    $maxCtrl = $ctrlStack.Count
                    $maxCtrlLine = $ln
                }
            }
            $depth += ($opens - $closes)
            while ($ctrlStack.Count -gt 0 -and $depth -lt $ctrlStack.Peek()) {
                [void]$ctrlStack.Pop()
            }

            if (-not $inFunc -and $trimmed -ne '' -and
                $trimmed -notmatch '^(module|import|export\s+module|#|\}|namespace\b|using\b|template\b|struct\b|class\b|enum\b|static_assert)') {
                if ($pendingSig -eq '') {
                    $pendingSig = $trimmed
                    $pendingStart = $ln
                } else {
                    $pendingSig += " " + $trimmed
                }
                if ($trimmed.EndsWith(';')) {
                    $pendingSig = ""
                } elseif ($opens -gt 0) {
                    if ($pendingSig -match '^[A-Za-z_~].*?\)[^;]*\{' -and
                        $pendingSig -notmatch '^(if|for|while|switch|else|return|try|catch)\b') {
                        $inFunc = $true
                        $funcDepth = $nsDepth + 1
                        $funcStart = $pendingStart
                        $funcName = ($pendingSig -replace '\s+', ' ')
                        if ($funcName.Length -gt 64) { $funcName = $funcName.Substring(0, 64) + "..." }
                    }
                    $pendingSig = ""
                }
            } elseif (-not $inFunc -and $trimmed.EndsWith(';')) {
                $pendingSig = ""
            }

            $nsDepth += ($opens - $closes)
            if ($inFunc -and $nsDepth -lt $funcDepth) {
                $funcLen = $ln - $funcStart + 1
                if ($funcLen -gt 160) {
                    $Violations.Add("${rel}:$funcStart : Function exceeds 160 lines ($funcLen lines): $funcName")
                }
                $inFunc = $false
                $pendingSig = ""
            }
        }
    }

    if ($maxCtrl -gt 4) {
        $Violations.Add("${rel}:$maxCtrlLine : Control-flow nesting depth $maxCtrl exceeds limit 4")
    }
}

foreach ($ps in (Get-ChildItem -Path (Join-Path $Root "scripts") -Filter *.ps1)) {
    $raw = [System.IO.File]::ReadAllText($ps.FullName)
    if (($raw -replace "`r`n", "").Contains("`n")) {
        $Violations.Add("scripts/$($ps.Name) : PowerShell script must use CRLF line endings per .editorconfig")
    }
}

$skipCf = ($env:MOTRIX_SKIP_CLANG_FORMAT -eq '1')
$cf = if (-not $skipCf) { (Get-Command clang-format -ErrorAction SilentlyContinue)?.Source } else { $null }
if (-not $skipCf -and -not $cf -and (Test-Path "C:\Program Files\LLVM\bin\clang-format.exe")) {
    $cf = "C:\Program Files\LLVM\bin\clang-format.exe"
}
if ($cf) {
    $cppFiles = @($firstPartyFiles | Where-Object { $_.Extension -in @('.ixx', '.cpp') } | ForEach-Object { $_.FullName })
    if ($cppFiles.Count -gt 0) {
        $cfErr = & $cf --dry-run --Werror @cppFiles 2>&1
        if ($LASTEXITCODE -ne 0) {
            $preview = ($cfErr | Select-Object -First 8) -join "`n"
            $Violations.Add("clang-format : One or more .ixx/.cpp files do not match .clang-format`n$preview")
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

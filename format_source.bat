@echo off
REM Format all .cpp / .h files under source\ with clang-format.
REM Requires clang-format on PATH (install LLVM and enable "Add to PATH").
REM third_party\ is skipped by its own .clang-format config (DisableFormat).
REM Style comes from the repo-root .clang-format.

setlocal
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "SRC=%ROOT%\source"

where clang-format >nul 2>nul
if errorlevel 1 (
    echo [ERROR] clang-format not found. Install LLVM and add it to PATH.
    exit /b 1
)

if not exist "%SRC%\" (
    echo [ERROR] directory not found: %SRC%
    exit /b 1
)

echo Formatting .cpp/.h under %SRC% ...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$files = Get-ChildItem -LiteralPath '%SRC%' -Recurse -File | Where-Object { $_.Extension -eq '.cpp' -or $_.Extension -eq '.h' }; $ok=0; $fail=0; foreach($f in $files){ & clang-format -i -style=file $f.FullName; if($LASTEXITCODE -eq 0){ $ok++ } else { $fail++; Write-Host ('[FAIL] ' + $f.FullName) } }; Write-Host ('Done. OK=' + $ok + ' FAIL=' + $fail + '.')"
endlocal

param([string]$ToolchainBin)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
Push-Location $project
try {
    if (!$ToolchainBin) { $ToolchainBin = Join-Path $project '.toolchain\llvm-mingw-20260922-ucrt-x86_64\bin' }
    $env:PATH = $ToolchainBin + ';' + $env:PATH
    cmake --build build/vertical-ninja -j 6
    if ($LASTEXITCODE) { throw 'Application build failed' }
    $objects = Get-ChildItem build/vertical-ninja/CMakeFiles/tinta.dir/src -Filter '*.obj' |
        Where-Object Name -NE 'main_d2d.cpp.obj' | ForEach-Object FullName
    clang++ -std=c++17 -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -Iinclude -municode -static tests/vertical_reading_tests.cpp @objects build/vertical-ninja/_deps/md4c-build/src/libmd4c.a -ld2d1 -ldwrite -lshell32 -lwindowscodecs -lurlmon -lole32 -limm32 -ld3d11 -ldwmapi -lcomdlg32 -lshlwapi -lwinspool -lwinhttp -ldbghelp -luuid -lgdi32 -o build/vertical-tests.exe
    if ($LASTEXITCODE) { throw 'Harness build failed' }
    & ./build/vertical-tests.exe tests/fixtures/vertical-chinese.md build/vertical-check
    if ($LASTEXITCODE) { throw 'Mixed Markdown fixture failed' }
    & ./build/vertical-tests.exe tests/fixtures/vertical-search-pages.md build/vertical-search-check
    if ($LASTEXITCODE) { throw 'Chinese reading fixture failed' }
} finally { Pop-Location }

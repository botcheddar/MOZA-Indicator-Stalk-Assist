param([switch]$Bootstrap)
$ErrorActionPreference='Stop'
Set-Location -LiteralPath $PSScriptRoot
$compiler = Join-Path $PSScriptRoot 'vendor\zig\zig-x86_64-windows-0.15.2\zig.exe'
if (-not (Test-Path -LiteralPath $compiler)) {
    if (-not $Bootstrap) { throw 'Portable compiler missing. Run .\Build.ps1 -Bootstrap to fetch the pinned official compiler.' }
    $package = Get-Content -LiteralPath 'vendor\zig-download.json' -Raw | ConvertFrom-Json
    Invoke-WebRequest -Uri $package.tarball -OutFile 'vendor\zig.zip'
    if ((Get-FileHash -LiteralPath 'vendor\zig.zip' -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.shasum) { throw 'Compiler checksum mismatch' }
    Expand-Archive -LiteralPath 'vendor\zig.zip' -DestinationPath 'vendor\zig' -Force
}
New-Item -ItemType Directory -Path 'build','logs','release','release\Tests' -Force | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot 'build\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $PSScriptRoot 'build\zig-local'
function Compile([string[]]$Arguments) {
    & $compiler c++ @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $($Arguments[0])" }
}
$common = @('-std=c++17','-O2','-Wall','-Wextra','-Werror')
$inputLibs = @('-ldinput8','-ldxguid','-luser32')
Compile (@('src/app.cpp') + $common + @('-municode','-Wl,--subsystem,windows') + $inputLibs + @('-lgdi32','-lshell32','-lws2_32','-ladvapi32','-o','build/MozaComfort.exe'))
Compile (@('src/plugin.cpp','src/plugin.def') + $common + @('-Ivendor/scs-sdk/include','-shared') + $inputLibs + @('-o','build/MozaComfortPlugin.dll'))
Compile (@('src/input_diagnostic.cpp') + $common + $inputLibs + @('-o','build/InputDiagnostic.exe'))
Compile (@('tests/engine_tests.cpp') + $common + @('-o','build/EngineTests.exe'))
Compile (@('tests/plugin_tests.cpp') + $common + @('-Ivendor/scs-sdk/include') + $inputLibs + @('-o','build/PluginTests.exe'))
Compile (@('tests/input_tests.cpp') + $common + @('-o','build/InputTests.exe'))
Compile (@('tests/safety_tests.cpp') + $common + @('-o','build/SafetyTests.exe'))
Compile (@('tests/background_tests.cpp') + $common + @('-lws2_32','-ladvapi32','-luser32','-o','build/BackgroundTests.exe'))
Compile (@('tests/bridge_tests.cpp') + $common + @('-lws2_32','-luser32','-o','build/BridgeTests.exe'))
Compile (@('tests/binary_tests.cpp') + $common + @('-o','build/BinaryTests.exe'))
foreach ($name in @('EngineTests','PluginTests','InputTests','SafetyTests','BackgroundTests','BridgeTests','BinaryTests')) {
    & ".\build\$name.exe" | Tee-Object -FilePath "logs\$name.txt"
    if ($LASTEXITCODE -ne 0) { throw "$name failed" }
}
$luaSource = Join-Path $PSScriptRoot 'vendor\lua\lua-5.1.5'
if (-not (Test-Path -LiteralPath "$luaSource\src\lua.c")) {
    if (-not $Bootstrap) { throw 'Lua test sources missing. Run .\Build.ps1 -Bootstrap.' }
    New-Item -ItemType Directory -Path 'vendor\lua' -Force | Out-Null
    Invoke-WebRequest -Uri 'https://www.lua.org/ftp/lua-5.1.5.tar.gz' -OutFile 'vendor\lua\lua-5.1.5.tar.gz'
    if ((Get-FileHash -LiteralPath 'vendor\lua\lua-5.1.5.tar.gz').Hash -ne '2640FC56A795F29D28EF15E13C34A47E223960B0240E8CB0A82D9B0738695333') { throw 'Lua checksum mismatch' }
    tar -xzf 'vendor\lua\lua-5.1.5.tar.gz' -C 'vendor\lua'
    if ($LASTEXITCODE -ne 0) { throw 'Lua extraction failed' }
}
$luaFiles = @(Get-ChildItem -LiteralPath "$luaSource\src" -Filter '*.c' | Where-Object Name -NotIn @('luac.c','print.c') | ForEach-Object FullName)
& $compiler cc @luaFiles '-O2' '-DLUA_USE_WINDOWS' '-o' 'build/LuaTestsRuntime.exe'
if ($LASTEXITCODE -ne 0) { throw 'Lua test runtime compilation failed' }
foreach ($suite in @('vehicle','ge')) {
    & '.\build\LuaTestsRuntime.exe' "tests/beamng/${suite}_tests.lua" | Tee-Object -FilePath "logs\BeamNG-${suite}-Tests.txt"
    if ($LASTEXITCODE -ne 0) { throw "BeamNG $suite tests failed" }
}
Copy-Item -LiteralPath 'build\MozaComfort.exe','build\MozaComfortPlugin.dll' -Destination 'release' -Force
Copy-Item -LiteralPath 'build\EngineTests.exe','build\PluginTests.exe','build\InputTests.exe','build\SafetyTests.exe' -Destination 'release\Tests' -Force
$process = Start-Process -FilePath (Join-Path $PSScriptRoot 'release\MozaComfort.exe') -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru
if (-not $process.WaitForInputIdle(5000)) { throw 'GUI did not become idle' }
$duplicate = Start-Process -FilePath (Join-Path $PSScriptRoot 'release\MozaComfort.exe') -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru
if (-not $duplicate.WaitForExit(5000)) { $duplicate.Kill(); throw 'Duplicate helper did not exit' }
if ($duplicate.ExitCode -ne 3) { throw "Single-instance check failed: $($duplicate.ExitCode)" }
'PASS second helper rejected with exit code 3' | Set-Content -LiteralPath 'logs\SingleInstanceTest.txt'
if (-not $process.WaitForExit(15000)) { $process.Kill(); throw 'GUI smoke test did not exit' }
if ($process.ExitCode -ne 0) { throw "GUI smoke test failed: $($process.ExitCode)" }
'PASS GUI starts, enumerates input, remains disabled, logs, and shuts down' | Set-Content -LiteralPath 'logs\GuiSmokeTest.txt'
Write-Output 'BUILD AND TESTS PASSED. Release is in release\. Nothing was installed into ETS2.'





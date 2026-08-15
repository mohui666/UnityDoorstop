$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$configTestExe = Join-Path $PSScriptRoot "config-util-regressions.exe"
$iatTestExe = Join-Path $PSScriptRoot "iat-hook-regressions.exe"
$compiler = (Get-Command gcc.exe -ErrorAction Stop).Source

try {
    & $compiler `
        -std=gnu11 `
        -DUNICODE `
        -D_UNICODE `
        -ffunction-sections `
        -fdata-sections `
        "-Wl,--gc-sections" `
        -I (Join-Path $repoRoot "src") `
        (Join-Path $repoRoot "tests\windows\config-util-regressions.c") `
        (Join-Path $repoRoot "tests\windows\test-crt.c") `
        (Join-Path $repoRoot "src\bootstrap.c") `
        (Join-Path $repoRoot "src\windows\config.c") `
        (Join-Path $repoRoot "src\windows\util.c") `
        (Join-Path $repoRoot "src\config\common.c") `
        (Join-Path $repoRoot "src\runtimes\globals.c") `
        -lshell32 `
        -lkernel32 `
        -o $configTestExe
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile Windows config regressions (exit $LASTEXITCODE)"
    }

    & $configTestExe
    if ($LASTEXITCODE -ne 0) {
        throw "Windows config regressions failed (case $LASTEXITCODE)"
    }

    & $compiler `
        -std=gnu11 `
        -DUNICODE `
        -D_UNICODE `
        -I (Join-Path $repoRoot "src") `
        (Join-Path $repoRoot "tests\windows\iat-hook-regressions.c") `
        -lkernel32 `
        -o $iatTestExe
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile Windows IAT regressions (exit $LASTEXITCODE)"
    }

    & $iatTestExe
    if ($LASTEXITCODE -ne 0) {
        throw "Windows IAT regressions failed (case $LASTEXITCODE)"
    }
} finally {
    foreach ($testExe in @($configTestExe, $iatTestExe)) {
        if (Test-Path -LiteralPath $testExe) {
            Remove-Item -LiteralPath $testExe -Force
        }
    }
}

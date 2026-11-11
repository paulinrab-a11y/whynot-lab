<#
.SYNOPSIS
    CI: traz para os passos seguintes do job o ambiente do compilador (o que o vcvars64.bat monta).

.DESCRIPTION
    Acha o Visual Studio 2022 pelo vswhere, roda o vcvars64.bat e grava no arquivo do GITHUB_ENV cada variável
    que mudou (PATH, INCLUDE, LIB...). Os passos seguintes enxergam o cl, o cmake e o ninja.
    Em GITHUB_OUTPUT sai "chave" (versão do MSVC + versão do SDK do Windows), que entra na chave do cache do
    build: um build guardado com outro compilador tem caminhos que não existem mais.
    Fora do GitHub Actions (sem GITHUB_ENV), só confere e mostra as versões.
#>
[CmdletBinding()]
param(
    # Faixa de versões do Visual Studio aceita. O padrão é o VS 2022 (17.x), o que o docs/como-rodar.md testa.
    [string] $Versao = '[17.0,18.0)'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Parar([string] $texto) { Write-Host "Parei: $texto" -ForegroundColor Red; exit 1 }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { Parar "não achei o vswhere em $vswhere" }

$pastaVs = & $vswhere -latest -products * -version $Versao -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (-not $pastaVs) { Parar "nenhum Visual Studio na faixa $Versao com o compilador C++ x64 (confira a imagem do runner)" }

$vcvars = Join-Path $pastaVs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { Parar "não achei $vcvars" }

# um .cmd temporário evita as regras de aspas do "cmd /c" com um caminho entre aspas no meio
$pastaTemp = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
$bat = Join-Path $pastaTemp 'wnl-ci-vcvars.cmd'
Set-Content -LiteralPath $bat -Encoding Ascii -Value "@call `"$vcvars`" >nul`r`n@if errorlevel 1 exit /b 1`r`n@set"
$linhas = & cmd.exe /d /c $bat
if ($LASTEXITCODE -ne 0) { Parar "o vcvars64.bat falhou (código $LASTEXITCODE)" }
Remove-Item -LiteralPath $bat -Force

$semBom = New-Object Text.UTF8Encoding $false
$mudaram = 0
foreach ($linha in $linhas) {
    $igual = $linha.IndexOf('=')
    if ($igual -lt 1) { continue }
    $nome = $linha.Substring(0, $igual)
    $valor = $linha.Substring($igual + 1)
    if ([Environment]::GetEnvironmentVariable($nome) -ceq $valor) { continue }
    [Environment]::SetEnvironmentVariable($nome, $valor)
    if ($env:GITHUB_ENV) { [IO.File]::AppendAllText($env:GITHUB_ENV, "$nome=$valor`n", $semBom) }
    $mudaram++
}

foreach ($programa in @('cl.exe', 'link.exe', 'cmake.exe', 'ctest.exe', 'ninja.exe')) {
    if (-not (Get-Command $programa -CommandType Application -ErrorAction SilentlyContinue)) {
        Parar "depois do vcvars64.bat, o $programa não está no PATH (a imagem do runner mudou?)"
    }
}

$sdk = "$env:WindowsSDKVersion".Trim('\')
$chave = "msvc$($env:VCToolsVersion)-sdk$sdk"
if ($env:GITHUB_OUTPUT) { [IO.File]::AppendAllText($env:GITHUB_OUTPUT, "chave=$chave`n", $semBom) }

Write-Host "Visual Studio: $pastaVs"
Write-Host "MSVC $($env:VCToolsVersion), SDK do Windows $sdk ($mudaram variáveis de ambiente passadas adiante)"
Write-Host "CMake: $((& cmake.exe --version | Select-Object -First 1))"
Write-Host "Ninja: $(& ninja.exe --version)"
Write-Host "Chave do cache: $chave"
exit 0

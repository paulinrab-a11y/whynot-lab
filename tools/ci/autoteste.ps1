<#
.SYNOPSIS
    CI: roda o "WHYNOT LAB.exe" --self-test, espera acabar e sai com o número de falhas.

.DESCRIPTION
    O app é um programa de janela: chamado direto, o PowerShell não espera por ele nem recebe o código de saída.
    Por isso o Start-Process. O código de saída do app é o número de falhas; se o app cair no meio, o código é o
    da queda e o relatório mostra até onde chegou (ele é gravado a cada linha).
#>
[CmdletBinding()]
param(
    [string] $Exe = '',
    [string] $Relatorio = '',

    # Mata o app e falha se ele passar disto (o autoteste leva de 1 a 2 minutos num notebook).
    [int] $MinutosNoMaximo = 20
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$raiz = Split-Path (Split-Path $PSScriptRoot)
if (-not $Exe) { $Exe = Join-Path $raiz 'build\whynot_lab_artefacts\Release\WHYNOT LAB.exe' }
if (-not $Relatorio) { $Relatorio = Join-Path $raiz 'build\selftest\relatorio.txt' }
$Relatorio = [IO.Path]::GetFullPath($Relatorio)

if (-not (Test-Path -LiteralPath $Exe)) { Write-Host "Parei: não achei o app em $Exe" -ForegroundColor Red; exit 1 }
New-Item -ItemType Directory -Force -Path (Split-Path $Relatorio) | Out-Null
if (Test-Path -LiteralPath $Relatorio) { Remove-Item -LiteralPath $Relatorio -Force }

$p = Start-Process -FilePath $Exe -ArgumentList @('--self-test', "`"$Relatorio`"") -PassThru
$null = $p.Handle   # sem isto o ExitCode pode vir vazio no Windows PowerShell 5.1

if (-not $p.WaitForExit($MinutosNoMaximo * 60 * 1000)) {
    $p.Kill()
    Write-Host "Parei: o autoteste não acabou em $MinutosNoMaximo min; o app foi fechado à força." -ForegroundColor Red
    if (Test-Path -LiteralPath $Relatorio) { Get-Content -LiteralPath $Relatorio -Encoding UTF8 -Tail 15 }
    exit 1
}

$codigo = $p.ExitCode
if (-not (Test-Path -LiteralPath $Relatorio)) {
    Write-Host "Parei: o app saiu com código $codigo e não gravou o relatório." -ForegroundColor Red
    exit 1
}

$oks = @(Select-String -LiteralPath $Relatorio -Pattern '^  ok: ' -Encoding UTF8).Count
$falhas = @(Select-String -LiteralPath $Relatorio -Pattern '^  FALHOU: ' -Encoding UTF8)
$acabou = @(Select-String -LiteralPath $Relatorio -Pattern '^(TODOS OS TESTES PASSARAM|\d+ TESTE\(S\) FALHARAM)' -Encoding UTF8).Count -gt 0

if ($codigo -eq 0 -and $acabou) {
    Write-Host "Autoteste: $oks ok, nenhuma falha." -ForegroundColor Green
    exit 0
}

if (-not $acabou) {
    Write-Host "O app saiu no meio do autoteste (código $codigo). As últimas linhas do relatório:" -ForegroundColor Red
    Get-Content -LiteralPath $Relatorio -Encoding UTF8 -Tail 15
}
else {
    Write-Host "Autoteste: $oks ok e $codigo falha(s):" -ForegroundColor Red
}
$falhas | ForEach-Object { Write-Host $_.Line }
if ($codigo -eq 0) { exit 1 }
exit $codigo

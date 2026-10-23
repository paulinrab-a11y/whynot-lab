<#
.SYNOPSIS
    Leva e traz o código da WHYNOT LAB entre dois PCs por um arquivo (pendrive), sem GitHub.

.DESCRIPTION
    -Exportar grava todos os branches num git bundle, um arquivo só.
    -Importar lê um bundle e mostra, branch por branch, o que mudou em relação a este PC.
    Os branches que vêm do arquivo ficam em pendrive/<branch> e não mexem em nada daqui.
    Com -Avancar, o script também avança os branches daqui que só ficaram para trás, sem merge.
    Branch que divergiu (mudou nos dois PCs) nunca é tocado: o script mostra o comando para juntar à mão.

    Mudança sem commit não vai no bundle. Commite antes de exportar.

.EXAMPLE
    .\trocar-codigo.ps1 -Exportar E:\whynot-casa.bundle

.EXAMPLE
    .\trocar-codigo.ps1 -Importar E:\whynot-casa.bundle -Avancar
#>
[CmdletBinding(DefaultParameterSetName = 'Exportar')]
param(
    [Parameter(ParameterSetName = 'Exportar', Mandatory = $true)] [string] $Exportar,
    [Parameter(ParameterSetName = 'Importar', Mandatory = $true)] [string] $Importar,
    [Parameter(ParameterSetName = 'Importar')] [switch] $Avancar,

    # Repositório. Padrão: o repositório onde este script está ou, se ele estiver fora de um, a pasta atual.
    [string] $Projeto = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Parar([string] $texto) { Write-Host "Parei: $texto" -ForegroundColor Red; exit 1 }

# git com o código de saída conferido; devolve as linhas do stdout
function Git-Exigir([string[]] $argumentos, [string] $oQue) {
    $ErrorActionPreference = 'Continue'
    $saida = & git.exe -C $Projeto @argumentos
    if ($LASTEXITCODE -ne 0) { Parar "$oQue (código $LASTEXITCODE)" }
    return $saida
}

function Git-Talvez([string[]] $argumentos) {
    $ErrorActionPreference = 'Continue'
    $saida = & git.exe -C $Projeto @argumentos 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    return $saida
}

if (-not (Get-Command git.exe -ErrorAction SilentlyContinue)) { Parar 'o git não está no PATH' }
if (-not $Projeto) {
    $raiz = Split-Path (Split-Path $PSScriptRoot)
    $Projeto = if (Test-Path (Join-Path $raiz '.git')) { $raiz } else { (Get-Location).Path }
}
$Projeto = [IO.Path]::GetFullPath($Projeto)
if (-not (Git-Talvez @('rev-parse', '--git-dir'))) { Parar "$Projeto não é um repositório git. Use -Projeto <pasta>." }
Write-Host "Repositório: $Projeto"

#==============================================================================
if ($PSCmdlet.ParameterSetName -eq 'Exportar') {
    $arquivo = [IO.Path]::GetFullPath($Exportar)
    if (Test-Path -LiteralPath $arquivo) { Parar "$arquivo já existe. Apague ou escolha outro nome: não sobrescrevo." }

    $sujos = @(Git-Exigir @('status', '--porcelain', '--untracked-files=no') 'não deu para ler o estado do repositório' | Where-Object { $_ })
    if ($sujos.Count -gt 0) {
        Write-Host "Atenção: $($sujos.Count) arquivo(s) com mudança sem commit. Eles NÃO vão no bundle:" -ForegroundColor Yellow
        $sujos | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" }
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $arquivo) | Out-Null
    $null = Git-Exigir @('bundle', 'create', $arquivo, '--branches', '--tags') 'não deu para criar o bundle'
    $null = Git-Exigir @('bundle', 'verify', '--quiet', $arquivo) 'o bundle gravado não passou na verificação'

    $branches = @(Git-Exigir @('for-each-ref', '--format=%(refname:short)  %(objectname:short)  %(committerdate:format:%d/%m %H:%M)', 'refs/heads') 'não deu para listar os branches')
    $mb = [math]::Round((Get-Item -LiteralPath $arquivo).Length / 1MB, 1)
    Write-Host "Gravado e verificado: $arquivo ($mb MB, $($branches.Count) branches)" -ForegroundColor Green
    $branches | ForEach-Object { Write-Host "  $_" }
    Write-Host 'No outro PC: tools\portatil\trocar-codigo.ps1 -Importar <este arquivo>'
    exit 0
}

#==============================================================================
$arquivo = [IO.Path]::GetFullPath($Importar)
if (-not (Test-Path -LiteralPath $arquivo)) { Parar "não achei $arquivo" }
$null = Git-Exigir @('bundle', 'verify', '--quiet', $arquivo) 'o bundle não passou na verificação (cópia corrompida, ou ele depende de commits que este PC não tem)'

# Os branches do arquivo vão para refs/remotes/pendrive/*. O "+" deixa trocar o que veio de um pendrive anterior.
$null = Git-Exigir @('fetch', '--no-tags', '--prune', $arquivo, '+refs/heads/*:refs/remotes/pendrive/*') 'não deu para ler os branches do bundle'

$atual = Git-Talvez @('symbolic-ref', '--quiet', '--short', 'HEAD')
# branches abertos em algum worktree não podem ser movidos com "git branch -f"
$abertos = @(Git-Exigir @('worktree', 'list', '--porcelain') 'não deu para listar os worktrees' |
             Where-Object { $_ -like 'branch refs/heads/*' } | ForEach-Object { $_.Substring('branch refs/heads/'.Length) })
$limpo = @(Git-Exigir @('status', '--porcelain', '--untracked-files=no') 'não deu para ler o estado do repositório' | Where-Object { $_ }).Count -eq 0

$vindos = @(Git-Exigir @('for-each-ref', '--format=%(refname:short)', 'refs/remotes/pendrive') 'não deu para listar o que veio' |
            Where-Object { $_ -and $_ -ne 'pendrive/HEAD' })
$divergiram = @()

Write-Host ''
Write-Host "Branches no arquivo: $($vindos.Count)"
foreach ($remoto in $vindos) {
    $nome = $remoto.Substring('pendrive/'.Length)
    $local = Git-Talvez @('rev-parse', '--verify', '--quiet', "refs/heads/$nome")

    if (-not $local) {
        if ($Avancar) {
            $null = Git-Exigir @('branch', $nome, $remoto) "não deu para criar o branch $nome"
            Write-Host "  novo        $nome (criado)" -ForegroundColor Green
        }
        else { Write-Host "  novo        $nome (só existe no arquivo)" -ForegroundColor Cyan }
        continue
    }

    $contagem = (Git-Exigir @('rev-list', '--left-right', '--count', "refs/heads/$nome...$remoto") "não deu para comparar $nome") -split '\s+'
    $soAqui = [int] $contagem[0]; $soLa = [int] $contagem[1]

    if ($soLa -eq 0 -and $soAqui -eq 0) { Write-Host "  igual       $nome"; continue }
    if ($soLa -eq 0) { Write-Host "  daqui       $nome ($soAqui commit(s) só neste PC)"; continue }
    if ($soAqui -gt 0) {
        Write-Host "  divergiu    $nome ($soAqui só aqui, $soLa só no arquivo)" -ForegroundColor Yellow
        $divergiram += $nome
        continue
    }

    # só ficou para trás: dá para avançar sem merge
    if (-not $Avancar) { Write-Host "  atrás       $nome ($soLa commit(s) novos no arquivo)" -ForegroundColor Cyan; continue }
    if ($nome -eq $atual) {
        if (-not $limpo) { Write-Host "  atrás       ${nome}: é o branch aberto e tem mudança sem commit, não avancei" -ForegroundColor Yellow; continue }
        $null = Git-Exigir @('merge', '--ff-only', '--quiet', $remoto) "não deu para avançar $nome"
        Write-Host "  avançado    $nome (+$soLa, branch aberto)" -ForegroundColor Green
    }
    elseif ($abertos -contains $nome) {
        Write-Host "  atrás       ${nome}: aberto em outro worktree, avance por lá com git merge --ff-only $remoto" -ForegroundColor Yellow
    }
    else {
        $null = Git-Exigir @('branch', '-f', $nome, $remoto) "não deu para avançar $nome"
        Write-Host "  avançado    $nome (+$soLa)" -ForegroundColor Green
    }
}

if ($divergiram.Count -gt 0) {
    Write-Host ''
    Write-Host 'Branches que mudaram nos dois PCs. Para juntar, com o branch aberto e sem mudança pendente:' -ForegroundColor Yellow
    $divergiram | ForEach-Object { Write-Host "  git switch $_; git merge pendrive/$_" }
}
if (-not $Avancar) {
    Write-Host ''
    Write-Host 'Nada foi mudado nos seus branches. Para avançar os que estão "atrás" e criar os "novos": rode de novo com -Avancar.'
}
exit 0

<#
.SYNOPSIS
    CI: faz o cache da pasta build/ servir de verdade, devolvendo a data dos fontes que não mudaram.

.DESCRIPTION
    O Ninja recompila o que tem a data mais nova que o objeto. Num checkout novo todo arquivo tem a data de agora,
    então a pasta build/ que veio do cache não poupava nada: recompilava tudo.

    -Guardar (depois do build): grava em build/.ci-datas.tsv, para cada arquivo do git (com os submódulos),
    o hash do conteúdo (o blob do git) e a data que o arquivo tinha neste build.

    -Restaurar (depois do checkout e de o cache voltar, antes do cmake): o arquivo cujo conteúdo é o mesmo do
    build guardado recebe de volta a data daquele build. O que mudou, ou é novo, fica com a data de agora: mais
    nova que qualquer objeto do cache, então o Ninja recompila só isso.

    Compara o conteúdo, não a data do commit: um commit feito antes do build guardado (rebase, push atrasado)
    teria data mais velha que o objeto e ficaria sem recompilar.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [ValidateSet('Guardar', 'Restaurar')] [string] $Acao,

    # Raiz do repositório. Padrão: o repositório onde este script está.
    [string] $Raiz = '',

    # Onde fica a lista. Padrão: build\.ci-datas.tsv (vai junto no cache da pasta build).
    [string] $Arquivo = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

if (-not $Raiz) { $Raiz = Split-Path (Split-Path $PSScriptRoot) }
$Raiz = [IO.Path]::GetFullPath($Raiz)
if (-not $Arquivo) { $Arquivo = Join-Path $Raiz 'build\.ci-datas.tsv' }

# "<modo> <blob> <estágio><TAB><caminho>", com os arquivos dos submódulos já baixados
function Listar-Arquivos {
    $antes = [Console]::OutputEncoding
    [Console]::OutputEncoding = [Text.Encoding]::UTF8
    try { $saida = & git.exe -C $Raiz -c core.quotepath=off ls-files --stage --recurse-submodules }
    finally { [Console]::OutputEncoding = $antes }
    if ($LASTEXITCODE -ne 0) { Write-Host "Parei: o git ls-files falhou (código $LASTEXITCODE)" -ForegroundColor Red; exit 1 }

    foreach ($linha in $saida) {
        $tab = $linha.IndexOf("`t")
        if ($tab -lt 0) { continue }
        $campos = $linha.Substring(0, $tab).Split(' ')
        # 160000 é um submódulo sem baixar; caminho entre aspas tem caractere que o git escapou: os dois ficam de fora
        if ($campos[0] -eq '160000' -or $linha[$tab + 1] -eq '"') { continue }
        [pscustomobject] @{ Blob = $campos[1]; Caminho = $linha.Substring($tab + 1) }
    }
}

if ($Acao -eq 'Guardar') {
    $linhas = New-Object System.Collections.Generic.List[string]
    foreach ($a in Listar-Arquivos) {
        $completo = Join-Path $Raiz $a.Caminho
        if (-not [IO.File]::Exists($completo)) { continue }
        $linhas.Add(('{0}{1}{2}{1}{3}' -f $a.Blob, "`t", [IO.File]::GetLastWriteTimeUtc($completo).Ticks, $a.Caminho))
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $Arquivo) | Out-Null
    [IO.File]::WriteAllLines($Arquivo, $linhas, (New-Object Text.UTF8Encoding $false))
    Write-Host "Datas de $($linhas.Count) arquivo(s) guardadas em $Arquivo"
    exit 0
}

if (-not [IO.File]::Exists($Arquivo)) {
    Write-Host "Sem $Arquivo (o cache não voltou, ou é de antes deste script): o build é do zero."
    exit 0
}

$guardados = New-Object 'System.Collections.Generic.Dictionary[string,string[]]'
foreach ($linha in [IO.File]::ReadAllLines($Arquivo, [Text.Encoding]::UTF8)) {
    $campos = $linha.Split("`t", 3)
    if ($campos.Count -eq 3) { $guardados[$campos[2]] = $campos }
}

$iguais = 0; $mudados = 0; $novos = 0
foreach ($a in Listar-Arquivos) {
    $campos = $null
    if (-not $guardados.TryGetValue($a.Caminho, [ref] $campos)) { $novos++; continue }
    if ($campos[0] -ne $a.Blob) { $mudados++; continue }
    $completo = Join-Path $Raiz $a.Caminho
    if (-not [IO.File]::Exists($completo)) { continue }
    [IO.File]::SetLastWriteTimeUtc($completo, (New-Object DateTime ([long] $campos[1]), ([DateTimeKind]::Utc)))
    $iguais++
}
Write-Host "Datas devolvidas a $iguais arquivo(s) iguais aos do build guardado; $mudados mudaram e $novos são novos (esses recompilam)."
exit 0

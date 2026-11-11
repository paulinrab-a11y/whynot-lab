<#
.SYNOPSIS
    Confere o LAME 3.100 e o ASIO SDK 2.3.4 de external/ com os hashes registrados; com -Baixar, baixa os dois antes.

.DESCRIPTION
    Sem opção: para cada pasta que existir (external\lame-3.100, external\asiosdk), compara o sha256 de cada
    arquivo com a lista tools\ci\terceiros.sha256. Arquivo diferente ou faltando: sai com código 1.
    Arquivo a mais na pasta: só avisa. Pasta que não existe: nada a conferir (o app compila sem ela).

    -Baixar (o CI usa; roda igual num PC): baixa os dois arquivos das fontes oficiais, as mesmas da seção 2b do
    docs/como-rodar.md, confere o sha256 de cada um com o registrado em docs/licencas.md ANTES de extrair, extrai
    em external\ e depois confere arquivo por arquivo. Não sobrescreve: pasta que já existe não é baixada de novo.

    Os dois hashes dos arquivos baixados estão aqui e no docs/licencas.md; o script para se um não bater com o outro.
#>
[CmdletBinding()]
param(
    # Baixa e extrai o que faltar em external\ antes de conferir.
    [switch] $Baixar,

    # Sai com código 1 se alguma das duas pastas não existir (o release exige o LAME e o ASIO no exe).
    [switch] $Exigir,

    # Raiz do repositório. Padrão: o repositório onde este script está.
    [string] $Raiz = '',

    # Onde ficam os arquivos baixados (o CI guarda esta pasta em cache).
    [string] $Downloads = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Ok([string] $texto) { Write-Host "  ok        $texto" -ForegroundColor Green }
function Aviso([string] $texto) { Write-Host "  atenção   $texto" -ForegroundColor Yellow }
function Parar([string] $texto) { Write-Host "Parei: $texto" -ForegroundColor Red; exit 1 }

if (-not $Raiz) { $Raiz = Split-Path (Split-Path $PSScriptRoot) }
$Raiz = [IO.Path]::GetFullPath($Raiz)
$external = Join-Path $Raiz 'external'
if (-not $Downloads) { $Downloads = Join-Path $(if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }) 'wnl-terceiros' }

$itens = @(
    [pscustomobject] @{
        Nome = 'LAME 3.100'; Pasta = 'lame-3.100'; Marca = 'include\lame.h'; Arquivo = 'lame-3.100.tar.gz'
        Url = 'https://sourceforge.net/projects/lame/files/lame/3.100/lame-3.100.tar.gz/download'
        Sha256 = 'ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e'
    },
    [pscustomobject] @{
        Nome = 'ASIO SDK 2.3.4'; Pasta = 'asiosdk'; Marca = 'common\iasiodrv.h'; Arquivo = 'ASIO-SDK_2.3.4_2025-10-15.zip'
        Url = 'https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip'
        Sha256 = 'd5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca'
    }
)

function Sha256([string] $caminho) { return (Get-FileHash -LiteralPath $caminho -Algorithm SHA256).Hash.ToLowerInvariant() }

# os hashes daqui têm de ser os do docs/licencas.md: é lá que fica o registro do que foi baixado e conferido
$licencas = Join-Path $Raiz 'docs\licencas.md'
if (Test-Path -LiteralPath $licencas) {
    $texto = [IO.File]::ReadAllText($licencas)
    foreach ($i in $itens) {
        if (-not $texto.Contains($i.Sha256)) { Parar "o sha256 do $($i.Nome) neste script não está no docs\licencas.md: acerte um dos dois" }
    }
}

#------------------------------------------------------------------------------
if ($Baixar) {
    Write-Host '== Baixar' -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $external, $Downloads | Out-Null

    foreach ($i in $itens) {
        $destino = Join-Path $external $i.Pasta
        if (Test-Path -LiteralPath $destino) { Ok "$($i.Nome): external\$($i.Pasta) já existe, não baixo de novo"; continue }

        $arquivo = Join-Path $Downloads $i.Arquivo
        if ((Test-Path -LiteralPath $arquivo) -and (Sha256 $arquivo) -ne $i.Sha256) { Remove-Item -LiteralPath $arquivo -Force }
        if (-not (Test-Path -LiteralPath $arquivo)) {
            & curl.exe --location --fail --silent --show-error --retry 3 --retry-delay 5 --output $arquivo $i.Url
            if ($LASTEXITCODE -ne 0) { Parar "não consegui baixar o $($i.Nome) de $($i.Url) (curl, código $LASTEXITCODE)" }
        }

        $achado = Sha256 $arquivo
        if ($achado -ne $i.Sha256) { Parar "o $($i.Arquivo) baixado tem sha256 $achado, e o registrado é $($i.Sha256). Nada foi extraído." }
        Ok "$($i.Arquivo): sha256 confere com o docs\licencas.md"

        # extrai numa pasta à parte e só depois move: uma extração pela metade não vira external\<pasta>
        $temp = Join-Path $Downloads "extraindo-$($i.Pasta)"
        if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $temp | Out-Null

        if ($i.Arquivo.EndsWith('.zip')) {
            Expand-Archive -LiteralPath $arquivo -DestinationPath $temp
        }
        else {
            # o tar.exe do Windows (o do Git, se vier antes no PATH, não entende "C:\...")
            & (Join-Path $env:SystemRoot 'System32\tar.exe') -xzf $arquivo -C $temp
            if ($LASTEXITCODE -ne 0) { Parar "o tar.exe não conseguiu extrair o $($i.Arquivo) (código $LASTEXITCODE)" }
        }

        # a pasta de dentro do arquivo (lame-3.100, ASIOSDK) é a que tem o arquivo de marca
        $raizExtraida = @(Get-ChildItem -LiteralPath $temp -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName $i.Marca) }) | Select-Object -First 1
        if (-not $raizExtraida) { Parar "o $($i.Arquivo) não tem uma pasta com $($i.Marca)" }
        Move-Item -LiteralPath $raizExtraida.FullName -Destination $destino
        Remove-Item -LiteralPath $temp -Recurse -Force
        Ok "$($i.Nome) extraído em external\$($i.Pasta)"
    }
}

#------------------------------------------------------------------------------
Write-Host '== Conferir' -ForegroundColor Cyan
$lista = Join-Path $PSScriptRoot 'terceiros.sha256'
$esperados = New-Object 'System.Collections.Generic.Dictionary[string,string]'
foreach ($linha in [IO.File]::ReadAllLines($lista, [Text.Encoding]::UTF8)) {
    # "<64 hex><2 espaços><caminho com />"
    if ($linha.Length -gt 66 -and -not $linha.StartsWith('#')) { $esperados[$linha.Substring(66)] = $linha.Substring(0, 64) }
}

$erros = 0
$faltam = 0
foreach ($i in $itens) {
    $pasta = Join-Path $external $i.Pasta
    if (-not (Test-Path -LiteralPath (Join-Path $pasta $i.Marca))) {
        $faltam++
        Aviso "$($i.Nome): sem external\$($i.Pasta)\$($i.Marca); nada a conferir"
        continue
    }

    $prefixo = "$($i.Pasta)/"
    $daPasta = @($esperados.Keys | Where-Object { $_.StartsWith($prefixo) })
    $errosAntes = $erros
    foreach ($relativo in $daPasta) {
        $arquivo = Join-Path $external ($relativo -replace '/', '\')
        if (-not (Test-Path -LiteralPath $arquivo -PathType Leaf)) { Write-Host "  falta     $relativo" -ForegroundColor Red; $erros++ }
        elseif ((Sha256 $arquivo) -ne $esperados[$relativo]) { Write-Host "  diferente $relativo" -ForegroundColor Red; $erros++ }
    }

    $aMais = @(Get-ChildItem -LiteralPath $pasta -Recurse -File -Force | ForEach-Object { $_.FullName.Substring($external.Length + 1) -replace '\\', '/' } |
               Where-Object { -not $esperados.ContainsKey($_) })
    if ($aMais.Count -gt 0) { Aviso "$($i.Nome): $($aMais.Count) arquivo(s) que não vêm no original, como $($aMais[0])" }
    if ($erros -eq $errosAntes) { Ok "$($i.Nome): $($daPasta.Count) arquivos iguais aos do original" }
}

if ($erros -gt 0) { Parar "$erros arquivo(s) de external\ não batem com tools\ci\terceiros.sha256. Apague a pasta e extraia de novo do arquivo oficial." }
if ($Exigir -and $faltam -gt 0) { Parar "faltam $faltam pasta(s) em external\ e este build exige as duas (rode com -Baixar)" }
exit 0

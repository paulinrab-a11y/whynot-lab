<#
.SYNOPSIS
    Monta a pasta do pacote portátil da WHYNOT LAB: app, código, SDKs, notas do Claude e o script de preparo.

.DESCRIPTION
    Roda no PC de origem, depois de todas as frentes estarem no main e de o app do main estar compilado.
    Antes de gravar, confere:
    - se todos os branches já estão no main;
    - se o exe é mais novo que o último commit do main.
    Com -Listar, só mostra o que entraria e o que está faltando, sem gravar nada.

    Com -Completo, o pacote leva também o Tracktion e o JUCE, o CMake e o Ninja, os plugins de teste e os projetos:
    no outro PC, só o Git e o VS Build Tools vêm da internet. -Coordenacao e -Extras acrescentam os documentos
    de coordenação e pastas de fora do git (o projeto do vídeo, por exemplo).

    Com -SoApp, o pacote é o dos produtores: só o app e um LEIA-ME de uso, sem código, SDKs, notas do Claude,
    plugins de teste nem scripts de programar. -Piano acrescenta a biblioteca do WHYNOT Piano (Salamander,
    uns 600 MB) em app\Bibliotecas, ao lado do exe, onde o app também procura: o produtor não copia nada.
    O exe precisa ter o LAME (sem ele, o MP3 fica desligado): a conferência para o pacote se ele faltar.

    Proteção de dados: nas notas do Claude, na coordenação e nas extras, os e-mails e os links do Google Drive
    são trocados por marcadores. Depois, uma varredura procura e-mail e segredo (chave, token, senha) em todo
    o pacote e no histórico do git, e para antes do zip se achar algo fora do código de terceiros.

    Não sobrescreve nada: a pasta de destino não pode existir.
    O guia está em docs\portatil.md.

.EXAMPLE
    .\montar-pacote.ps1 -Listar

.EXAMPLE
    .\montar-pacote.ps1 -Completo -Coordenacao C:\Users\<usuário>\dev -Extras C:\Users\<usuário>\dev\wnl-video -Excluir 'wnl-video\gravacoes\teste.*' -Zip

.EXAMPLE
    .\montar-pacote.ps1 -SoApp -Piano -Destino "$env:USERPROFILE\Desktop\WHYNOT-LAB-app" -Zip
#>
[CmdletBinding()]
param(
    [string] $Destino = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'WHYNOT-LAB-portatil'),

    # Repositório. Padrão: o repositório onde este script está. O app, os SDKs e as notas saem do checkout principal dele.
    [string] $Projeto = '',

    # Padrão: build\whynot_lab_artefacts\Release\WHYNOT LAB.exe do checkout principal.
    [string] $Exe = '',

    [string] $Ramo = 'main',

    # Só mostra o plano e os problemas. Não grava nada.
    [switch] $Listar,

    # Inclui Músicas\WHYNOT LAB (projetos e Biblioteca de sons).
    [switch] $ComProjetos,

    # Pacote completo: também o Tracktion e o JUCE (o outro PC não baixa), o CMake e o Ninja, os plugins de teste,
    # os favoritos do synth e os projetos.
    [switch] $Completo,

    # Pasta dos documentos de coordenação: os .md dela e da subpasta revisoes vão para coordenacao\.
    [string] $Coordenacao = '',

    # Pastas de fora do git copiadas para extras\<nome> (o projeto do vídeo, por exemplo).
    [string[]] $Extras = @(),

    # Arquivos das extras que ficam de fora, como '<nome>\caminho' (aceita * e ?). Ex.: 'wnl-video\gravacoes\teste.*'
    [string[]] $Excluir = @(),

    # Também comprime a pasta num .zip ao lado dela.
    [switch] $Zip,

    # Copia o .zip para esta pasta (a do Google Drive, por exemplo), com a data e o commit no nome. Precisa de -Zip.
    [string] $CopiarPara = '',

    # Monta mesmo com branch fora do main ou com o exe mais velho que o main. Não passa por cima da varredura de dados.
    [switch] $Forcar,

    # Arquivos já conferidos à mão (aceita * e ?, com / ou \): os achados do padrão genérico (token = "valor")
    # neles não param o pacote. Os formatos de chave conhecidos (sk-, ghp_, AKIA, chave privada...) param sempre.
    [string[]] $SegredoRevisado = @(),

    # Pacote só para usar (o dos produtores): o app e o LEIA-ME de uso. Sem código, SDKs, notas, plugins de teste nem scripts.
    [switch] $SoApp,

    # Inclui a biblioteca do WHYNOT Piano (a pasta de %APPDATA%\WHYNOT LAB\Bibliotecas com o whynot-piano.json) em app\Bibliotecas.
    [switch] $Piano
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
if ($SoApp -and $Completo) { Write-Host 'Parei: -SoApp e -Completo não vão juntos (um é o pacote de usar, o outro o de programar).' -ForegroundColor Red; exit 1 }
if ($Completo) { $ComProjetos = $true }

$script:problemas = New-Object System.Collections.Generic.List[string]
function Ok([string] $texto) { Write-Host "  ok        $texto" -ForegroundColor Green }
function Aviso([string] $texto) { Write-Host "  atenção   $texto" -ForegroundColor Yellow }
function Problema([string] $texto) { Write-Host "  problema  $texto" -ForegroundColor Red; $script:problemas.Add($texto) }
function Parar([string] $texto) { Write-Host "Parei: $texto" -ForegroundColor Red; exit 1 }

function Git-Exigir([string] $pasta, [string[]] $argumentos, [string] $oQue) {
    $ErrorActionPreference = 'Continue'
    $saida = & git.exe -C $pasta @argumentos
    if ($LASTEXITCODE -ne 0) { Parar "$oQue (código $LASTEXITCODE)" }
    return $saida
}

function Tamanho-Mb([string] $caminho) {
    if (-not (Test-Path -LiteralPath $caminho)) { return 0 }
    $item = Get-Item -Force -LiteralPath $caminho
    if (-not $item.PSIsContainer) { return [math]::Round($item.Length / 1MB, 1) }
    # pasta sem arquivo nenhum: o Measure-Object devolveria nulo, então a soma é feita à mão
    $soma = 0
    foreach ($a in Get-ChildItem -LiteralPath $caminho -Recurse -File -Force) { $soma += $a.Length }
    return [math]::Round($soma / 1MB, 1)
}

function Chave-Do-Projeto([string] $caminho) { return ($caminho -replace '[^A-Za-z0-9]', '-') }

# Só os .md da pasta de coordenação e da subpasta revisoes.
function Arquivos-Coordenacao([string] $pasta) {
    return @(Get-ChildItem -LiteralPath $pasta -Filter *.md -File) +
           @(Get-ChildItem -LiteralPath (Join-Path $pasta 'revisoes') -Filter *.md -File -ErrorAction SilentlyContinue)
}
function Tamanho-Coordenacao([string] $pasta) {
    $soma = 0
    foreach ($a in Arquivos-Coordenacao $pasta) { $soma += $a.Length }
    return [math]::Round($soma / 1MB, 1)
}

# Cópia de uma árvore grande pelo robocopy, sem os .git (pasta ou arquivo) e sem os arquivos de -Excluir.
function Copiar-Arvore([string] $origem, [string] $alvo, [string[]] $semArquivos) {
    $argumentos = @($origem, $alvo, '/E', '/XD', '.git', '/XF', '.git') + $semArquivos + @('/R:1', '/W:1', '/NFL', '/NDL', '/NJH', '/NJS', '/NP')
    $ErrorActionPreference = 'Continue'
    & robocopy.exe @argumentos | Out-Null
    if ($LASTEXITCODE -ge 8) { Parar "o robocopy não conseguiu copiar $origem (código $LASTEXITCODE)" }
}

# Arquivos de texto que passam pela limpeza e pela varredura de dados.
$extensoesDeTexto = @('.md', '.txt', '.json', '.sh', '.ps1', '.cmd', '.bat', '.js', '.mjs', '.ts', '.html', '.htm', '.css',
                      '.py', '.srt', '.xml', '.wnl', '.cfg', '.ini', '.yml', '.yaml', '.toml', '.csv', '.log', '.settings')
$padraoEmail = '[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}'
$padraoDrive = 'https?://(drive|docs)\.google\.com/[^\s)>\]"''`]*'
# O primeiro é o genérico: nome de segredo recebendo um valor literal entre aspas (uma expressão como header.substr(7)
# não conta). Ele pode ser liberado por -SegredoRevisado. Os outros são formatos de chave conhecidos e param sempre.
$padraoGenerico = '(?i)\b(api[_-]?key|secret|token|senha|password|passwd)\b["'']?\s*[:=]\s*["''][A-Za-z0-9_\-\.\/+=]{12,}["'']'
$padroesFortes = @(
    '\bsk-[A-Za-z0-9_-]{20,}', '\bghp_[A-Za-z0-9]{30,}', '\bgithub_pat_[A-Za-z0-9_]{30,}', '\bAKIA[0-9A-Z]{16}\b',
    '-----BEGIN [A-Z ]*PRIVATE KEY-----', '\bya29\.[0-9A-Za-z_-]{20,}', '\bxox[abprs]-[0-9A-Za-z-]{10,}', '(?i)authorization:\s*bearer\s+[A-Za-z0-9_\-\.=]{20,}'
)

function E-Texto([System.IO.FileInfo] $arq) { return $extensoesDeTexto -contains $arq.Extension.ToLowerInvariant() }

function E-Revisado([string] $caminho) {
    $c = $caminho -replace '\\', '/'
    foreach ($r in $SegredoRevisado) { if ($c -like ($r -replace '\\', '/')) { return $true } }
    return $false
}

# Segredos numa linha ou num texto: devolve as descrições do que achou (vazio = limpo).
function Achar-Segredos([string] $texto, [string] $onde) {
    $saida = @()
    foreach ($p in $padroesFortes) { foreach ($m in [regex]::Matches($texto, $p)) { $saida += "possível segredo em ${onde}: $($m.Value.Substring(0, [Math]::Min(24, $m.Value.Length)))..." } }
    if (-not (E-Revisado $onde)) {
        foreach ($m in [regex]::Matches($texto, $padraoGenerico)) { $saida += "possível segredo em ${onde}: $($m.Value.Substring(0, [Math]::Min(24, $m.Value.Length)))..." }
    }
    return $saida
}

# Troca e-mails e links do Drive por marcadores. Devolve quantas trocas fez.
function Limpar-Dados([string] $pasta) {
    $trocas = 0
    if (-not (Test-Path -LiteralPath $pasta)) { return 0 }
    foreach ($arq in Get-ChildItem -LiteralPath $pasta -Recurse -File -Force | Where-Object { E-Texto $_ }) {
        $bytes = [IO.File]::ReadAllBytes($arq.FullName)
        $temBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
        $texto = [IO.File]::ReadAllText($arq.FullName)
        $n = ([regex]::Matches($texto, $padraoEmail)).Count + ([regex]::Matches($texto, $padraoDrive)).Count
        if ($n -eq 0) { continue }
        $texto = [regex]::Replace($texto, $padraoDrive, '<link do Drive removido>')
        $texto = [regex]::Replace($texto, $padraoEmail, '<e-mail removido>')
        [IO.File]::WriteAllText($arq.FullName, $texto, (New-Object Text.UTF8Encoding $temBom))
        $trocas += $n
    }
    return $trocas
}

#==============================================================================
if (-not $Projeto) { $Projeto = Split-Path (Split-Path $PSScriptRoot) }
$Projeto = [IO.Path]::GetFullPath($Projeto)
$Destino = [IO.Path]::GetFullPath($Destino).TrimEnd('\')

# o primeiro worktree da lista é o checkout principal: é lá que ficam o build, o external\ e as notas do Claude
$principal = (Git-Exigir $Projeto @('worktree', 'list', '--porcelain') "$Projeto não é um repositório git" |
              Where-Object { $_ -like 'worktree *' } | Select-Object -First 1).Substring('worktree '.Length)
$principal = [IO.Path]::GetFullPath($principal)
if (-not $Exe) { $Exe = Join-Path $principal 'build\whynot_lab_artefacts\Release\WHYNOT LAB.exe' }

$memoria = Join-Path $env:USERPROFILE ".claude\projects\$(Chave-Do-Projeto $principal)\memory"
$skill = Join-Path $env:USERPROFILE '.claude\skills\whynot-agentes'
$atalhos = Join-Path $env:APPDATA 'WHYNOT LAB\atalhos.json'
$favoritos = Join-Path $env:APPDATA 'WHYNOT LAB\synth-favoritos.txt'
$musicas = Join-Path ([Environment]::GetFolderPath('MyMusic')) 'WHYNOT LAB'

Write-Host 'WHYNOT LAB: montar o pacote portátil' -ForegroundColor White
Write-Host "Checkout principal: $principal"
Write-Host "Destino: $Destino"
if ($Completo) { Write-Host 'Pacote completo: o outro PC só precisa baixar o Git e o VS Build Tools.' }
Write-Host ''

#------------------------------------------------------------------------------
Write-Host '== Conferências' -ForegroundColor Cyan
$null = Git-Exigir $principal @('rev-parse', '--verify', '--quiet', "refs/heads/$Ramo") "o branch $Ramo não existe"
$fora = @(Git-Exigir $principal @('branch', '--no-merged', $Ramo, '--format=%(refname:short)') 'não deu para listar os branches' | Where-Object { $_ })
if ($fora.Count -eq 0) { Ok "todos os branches já estão no $Ramo" }
else { Problema "$($fora.Count) branch(es) ainda fora do ${Ramo}: $($fora -join ', ')" }

$aberto = Git-Exigir $principal @('symbolic-ref', '--quiet', '--short', 'HEAD') 'o checkout principal está sem branch aberto'
if ($aberto -ne $Ramo) { Aviso "o checkout principal está no $aberto, não no ${Ramo}: confira se o exe foi compilado do $Ramo" }

$sujos = @(Git-Exigir $principal @('status', '--porcelain', '--untracked-files=no') 'não deu para ler o estado do repositório' | Where-Object { $_ })
if ($sujos.Count -gt 0) { Aviso "$($sujos.Count) arquivo(s) com mudança sem commit no checkout principal: não vão no bundle" }

if (-not (Test-Path -LiteralPath $Exe)) {
    Problema "não achei o app em $Exe. Compile o $Ramo antes"
}
else {
    $quandoCommit = [DateTimeOffset]::FromUnixTimeSeconds([long] (Git-Exigir $principal @('log', '-1', '--format=%ct', $Ramo) 'não deu para ler a data do commit')).UtcDateTime
    $quandoExe = (Get-Item -Force -LiteralPath $Exe).LastWriteTimeUtc
    if ($quandoExe -lt $quandoCommit) { Problema "o exe ($($quandoExe.ToLocalTime().ToString('dd/MM HH:mm'))) é mais velho que o último commit do $Ramo ($($quandoCommit.ToLocalTime().ToString('dd/MM HH:mm'))). Compile o $Ramo de novo" }
    else { Ok "exe compilado depois do último commit do $Ramo" }

    # Sem o LAME o app abre, mas o MP3 fica desligado: no PC do produtor isso vira "não exporta".
    $textoExe = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Exe))
    if ($textoExe.Contains('LAME3.100')) { Ok 'o exe tem o LAME (exporta MP3)' }
    else { Problema 'o exe foi compilado sem o LAME: o MP3 fica desligado. Ponha o external\lame-3.100 e compile de novo' }
    $textoExe = $null
}

if ((Test-Path $Destino) -and (Get-ChildItem -LiteralPath $Destino -Force | Select-Object -First 1)) {
    Problema "$Destino já existe e não está vazia: não sobrescrevo. Apague ou use -Destino"
}
if ($Zip -and (Test-Path -LiteralPath "$Destino.zip")) {
    Problema "$Destino.zip já existe: não sobrescrevo. Apague ou use -Destino"
}
if ($CopiarPara) {
    if (-not $Zip) { Problema '-CopiarPara precisa de -Zip' }
    elseif (-not (Test-Path -LiteralPath $CopiarPara -PathType Container)) {
        Problema "a pasta $CopiarPara não existe. Se for o Google Drive, confira se ele está aberto e com a conta certa"
    }
    else { Ok "o zip também vai para $CopiarPara" }
}

#------------------------------------------------------------------------------
# O que entra. Tipo: arquivo, pasta, arvore (robocopy sem .git), coordenacao (só .md), bundle (gerado aqui).
# Limpar: passa pela troca de e-mails e links do Drive.
$itens = New-Object System.Collections.Generic.List[object]
function Incluir([string] $noPacote, [string] $origem, [string] $descricao, [string] $tipo = '', [bool] $limpar = $false, [string[]] $sem = @()) {
    if (-not $tipo) { $tipo = if ($origem -and (Get-Item -Force -LiteralPath $origem).PSIsContainer) { 'pasta' } else { 'arquivo' } }
    $itens.Add([pscustomobject] @{ NoPacote = $noPacote; Origem = $origem; Descricao = $descricao; Tipo = $tipo; Limpar = $limpar; Sem = $sem })
}

if ($SoApp) {
    Incluir 'LEIA-ME.txt' (Join-Path $PSScriptRoot 'LEIA-ME-app.txt') 'como usar o app'
    # o pacote dos produtores é público: a licença do código (AGPL-3.0) e as dos terceiros vão junto
    Incluir 'LICENCA.txt' (Join-Path (Split-Path (Split-Path $PSScriptRoot)) 'LICENSE') 'a licença do código (AGPL-3.0)'
    Incluir 'AVISOS-DE-TERCEIROS.txt' (Join-Path $PSScriptRoot 'AVISOS-DE-TERCEIROS.txt') 'as licenças dos terceiros'
    if (Test-Path -LiteralPath $Exe) { Incluir 'app\WHYNOT LAB.exe' $Exe 'o app, roda sem instalar' 'arquivo' }
}
else {
    foreach ($arq in @('LEIA-ME.txt', 'preparar-pc.cmd', 'preparar-pc.ps1', 'trocar-codigo.ps1')) {
        Incluir $arq (Join-Path $PSScriptRoot $arq) 'instruções e scripts'
    }
    # o guia vai junto: o branch do pacote pode ainda não ter o docs\portatil.md
    Incluir 'GUIA.md' (Join-Path (Split-Path (Split-Path $PSScriptRoot)) 'docs\portatil.md') 'guia completo'
    if (Test-Path -LiteralPath $Exe) { Incluir 'app\WHYNOT LAB.exe' $Exe 'o app, roda sem instalar' 'arquivo' }
    if (Test-Path $atalhos) { Incluir 'app\config\atalhos.json' $atalhos 'os seus atalhos' }
    Incluir 'codigo\whynot-lab.bundle' '' "todos os branches do git (o $Ramo abre no clone)" 'bundle'

    foreach ($sdk in @(@{ Nome = 'lame-3.100'; Marca = 'include\lame.h'; Para = 'exportar MP3' },
                       @{ Nome = 'asiosdk'; Marca = 'common\iasiodrv.h'; Para = 'drivers ASIO' })) {
        $pasta = Join-Path $principal "external\$($sdk.Nome)"
        if (Test-Path (Join-Path $pasta $sdk.Marca)) { Incluir "externos\$($sdk.Nome)" $pasta "SDK fora do git, para $($sdk.Para)" }
        else { Aviso "sem external\$($sdk.Nome): o build de casa sai sem $($sdk.Para)" }
    }
}

# WHYNOT Piano: a biblioteca vai ao lado do exe (o app procura em <pasta do exe>\Bibliotecas), sem o _download
if ($Piano) {
    $bibliotecas = Join-Path $env:APPDATA 'WHYNOT LAB\Bibliotecas'
    $pianos = @(Get-ChildItem -LiteralPath $bibliotecas -Directory -ErrorAction SilentlyContinue |
                Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'whynot-piano.json') } | Sort-Object Name)
    if ($pianos.Count -eq 0) { Problema "-Piano, mas não achei a biblioteca (whynot-piano.json) em $bibliotecas" }
    foreach ($p in $pianos) {
        if (-not (Test-Path -LiteralPath (Join-Path $p.FullName 'CREDITOS.txt'))) { Problema "a biblioteca $($p.Name) está sem o CREDITOS.txt (a licença CC BY pede o crédito)" }
        Incluir "app\Bibliotecas\$($p.Name)" $p.FullName 'WHYNOT Piano: os samples e o crédito (CC BY 3.0)' 'arvore'
    }
}

if ($SoApp) {
    if ($Coordenacao -or $Extras.Count -gt 0) { Aviso 'no -SoApp a coordenação e as extras ficam de fora' }
    $Coordenacao = ''
    $Extras = @()
}

if ($Completo) {
    $tracktion = Join-Path $principal 'external\tracktion_engine'
    if (Test-Path (Join-Path $tracktion 'modules\juce\modules\juce_core\juce_core.h')) {
        Incluir 'externos\tracktion_engine' $tracktion 'Tracktion e JUCE no commit fixado, sem .git (casa não baixa)' 'arvore'
    }
    else { Problema "-Completo, mas o Tracktion com o JUCE não está em $tracktion" }

    $cmake = Get-Command cmake.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmake) { Incluir 'ferramentas\cmake' (Split-Path (Split-Path $cmake.Source)) 'CMake portátil' 'arvore' }
    else { Aviso 'sem o CMake no PATH: o pacote vai sem ele' }
    $ninja = Get-Command ninja.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ninja) { Incluir 'ferramentas\ninja\ninja.exe' $ninja.Source 'Ninja' 'arquivo' }
    else { Aviso 'sem o Ninja no PATH: o pacote vai sem ele' }

    $plugins = Join-Path (Split-Path (Split-Path (Split-Path $Exe))) 'test_plugins'
    if (-not (Test-Path $plugins)) { $plugins = Join-Path $principal 'build\test_plugins' }
    if (Test-Path $plugins) { Incluir 'app\test_plugins' $plugins 'os VST3 de teste (o autoteste usa)' 'arvore' }
    else { Aviso 'sem os plugins de teste: o outro PC compila os dele' }

    if (Test-Path $favoritos) { Incluir 'app\config\synth-favoritos.txt' $favoritos 'os favoritos do synth' }
}

if (-not $SoApp) {
    if (Test-Path $memoria) { Incluir 'claude\memoria' $memoria 'notas do projeto para o Claude Code (e-mails e links trocados)' 'pasta' $true }
    else { Aviso "sem notas do Claude em $memoria" }
    if (Test-Path $skill) { Incluir 'claude\skills\whynot-agentes' $skill 'skill de convivência das sessões' 'pasta' $true }
    else { Aviso "sem a skill em $skill" }
}

if ($ComProjetos) {
    if (Test-Path $musicas) { Incluir 'projetos' $musicas 'projetos e Biblioteca (Músicas\WHYNOT LAB)' 'pasta' $true }
    else { Aviso "sem projetos em $musicas" }
}

if ($Coordenacao) {
    if (Test-Path -LiteralPath $Coordenacao -PathType Container) {
        Incluir 'coordenacao' ([IO.Path]::GetFullPath($Coordenacao)) 'quadro, relatórios, planos e revisões (.md)' 'coordenacao' $true
    }
    else { Problema "a pasta de coordenação $Coordenacao não existe" }
}

foreach ($extra in $Extras) {
    if (-not (Test-Path -LiteralPath $extra -PathType Container)) { Problema "a pasta extra $extra não existe"; continue }
    $extra = [IO.Path]::GetFullPath($extra).TrimEnd('\')
    $nome = Split-Path $extra -Leaf
    # os arquivos de -Excluir viram caminhos completos para o /XF do robocopy
    $sem = @()
    foreach ($padrao in $Excluir) {
        if (-not $padrao.StartsWith("$nome\", [StringComparison]::OrdinalIgnoreCase)) { continue }
        $sem += @(Get-ChildItem -Path (Join-Path (Split-Path $extra) $padrao) -File -Force -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
    }
    if ($sem.Count -gt 0) { Aviso "de fora em extras\${nome}: $(($sem | ForEach-Object { $_.Substring($extra.Length + 1) }) -join ', ')" }
    Incluir "extras\$nome" $extra 'pasta de fora do git (e-mails e links trocados)' 'arvore' $true $sem
}

Write-Host ''
Write-Host '== O que entra no pacote' -ForegroundColor Cyan
$total = 0
foreach ($i in $itens) {
    $mb = if ($i.Tipo -eq 'bundle') { Tamanho-Mb (Join-Path $principal '.git') }
          elseif ($i.Tipo -eq 'coordenacao') { Tamanho-Coordenacao $i.Origem }
          else { Tamanho-Mb $i.Origem }
    $total += $mb
    $tam = if ($mb -lt 0.1) { '< 0.1 MB' } elseif ($i.Tipo -eq 'bundle') { "até $mb MB" } else { "$mb MB" }
    Write-Host ('  {0,-36} {1,10}   {2}' -f $i.NoPacote, $tam, $i.Descricao)
}
Write-Host ('  {0,-36} {1,10}' -f 'total (aproximado)', "$([math]::Round($total, 1)) MB")

if ($Listar) {
    Write-Host ''
    if ($script:problemas.Count -gt 0) { Write-Host "Com -Listar nada foi gravado. Antes de montar, resolva $($script:problemas.Count) problema(s) acima." -ForegroundColor Yellow; exit 1 }
    Write-Host 'Com -Listar nada foi gravado. Pode montar: rode sem -Listar.' -ForegroundColor Green
    exit 0
}
if ($script:problemas.Count -gt 0 -and -not $Forcar) { Parar "$($script:problemas.Count) problema(s) acima. Resolva, ou use -Forcar se tiver certeza." }

#------------------------------------------------------------------------------
Write-Host ''
Write-Host '== Montando' -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $Destino | Out-Null

$trocas = 0
foreach ($i in $itens) {
    $alvo = Join-Path $Destino $i.NoPacote
    New-Item -ItemType Directory -Force -Path (Split-Path $alvo) | Out-Null
    switch ($i.Tipo) {
        'bundle' {
            $null = Git-Exigir $principal @('bundle', 'create', $alvo, '--branches', '--tags') 'não deu para criar o bundle'
            $null = Git-Exigir $principal @('bundle', 'verify', '--quiet', $alvo) 'o bundle gravado não passou na verificação'
        }
        'arvore' { Copiar-Arvore $i.Origem $alvo $i.Sem }
        'coordenacao' {
            New-Item -ItemType Directory -Force -Path (Join-Path $alvo 'revisoes') | Out-Null
            Get-ChildItem -LiteralPath $i.Origem -Filter *.md -File | Copy-Item -Destination $alvo
            Get-ChildItem -LiteralPath (Join-Path $i.Origem 'revisoes') -Filter *.md -File -ErrorAction SilentlyContinue | Copy-Item -Destination (Join-Path $alvo 'revisoes')
        }
        'pasta' {
            New-Item -ItemType Directory -Force -Path $alvo | Out-Null
            Copy-Item -Path (Join-Path $i.Origem '*') -Destination $alvo -Recurse -Force
        }
        default { Copy-Item -LiteralPath $i.Origem -Destination $alvo }
    }
    if ($i.Limpar) {
        $n = Limpar-Dados $alvo
        $trocas += $n
        if ($n -gt 0) { Ok "$($i.NoPacote) ($n e-mail(s) ou link(s) trocados por marcadores)" } else { Ok $i.NoPacote }
    }
    else { Ok $i.NoPacote }
}

#------------------------------------------------------------------------------
# Varredura de dados: e-mail e segredo no pacote (menos o código de terceiros) e no histórico do git.
Write-Host ''
Write-Host '== Varredura de dados' -ForegroundColor Cyan
$terceiros = @('externos\', 'ferramentas\', 'app\test_plugins\', 'app\Bibliotecas\')
$achados = New-Object System.Collections.Generic.List[string]
$lidos = 0
foreach ($arq in Get-ChildItem -LiteralPath $Destino -Recurse -File -Force | Where-Object { E-Texto $_ }) {
    $relativo = $arq.FullName.Substring($Destino.Length + 1)
    if ($terceiros | Where-Object { $relativo.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }) { continue }
    $lidos++
    $texto = [IO.File]::ReadAllText($arq.FullName)
    foreach ($m in [regex]::Matches($texto, $padraoEmail)) { $achados.Add("e-mail em ${relativo}: $($m.Value)") }
    foreach ($m in [regex]::Matches($texto, $padraoDrive)) { $achados.Add("link do Drive em ${relativo}") }
    foreach ($s in Achar-Segredos $texto $relativo) { $achados.Add($s) }
}
if ($achados.Count -gt 0) {
    $achados | Select-Object -First 30 | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    Parar "a varredura achou $($achados.Count) dado(s) que não deviam ir no pacote. Nada foi comprimido nem copiado; a pasta $Destino ficou para conferência."
}
Ok "$lidos arquivo(s) de texto sem e-mail, link do Drive nem segredo (o código de terceiros fica de fora da varredura)"

# histórico do git: os e-mails de autor e de quem commitou vão no bundle; segredo nos diffs para o pacote
if (-not $SoApp) {
$emailsHistorico = @(Git-Exigir $principal @('log', '--branches', '--tags', '--format=%ae%n%ce') 'não deu para ler o histórico' | Where-Object { $_ } | Sort-Object -Unique)
$ErrorActionPreference = 'Continue'
$diffs = & git.exe -C $principal log --branches --tags -p --format='commit %h' 2>$null
$ErrorActionPreference = 'Stop'
$achadosHistorico = New-Object System.Collections.Generic.List[string]
$commitAtual = ''; $arquivoAtual = ''
# filtro rápido na frente: só a linha que casa com algum padrão passa pela análise detalhada
$qualquer = New-Object regex ((@($padraoGenerico) + $padroesFortes) -join '|'), ([Text.RegularExpressions.RegexOptions]::Compiled)
foreach ($linha in $diffs) {
    if ($linha.StartsWith('commit ')) { $commitAtual = $linha.Substring(7); continue }
    if ($linha.StartsWith('+++ b/')) { $arquivoAtual = $linha.Substring(6); continue }
    if (-not $qualquer.IsMatch($linha)) { continue }
    foreach ($s in Achar-Segredos $linha $arquivoAtual) { $achadosHistorico.Add("$s (commit $commitAtual)") }
}
if ($achadosHistorico.Count -gt 0) {
    $achadosHistorico | Select-Object -First 30 | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    Parar "o histórico do git tem $($achadosHistorico.Count) linha(s) com cara de segredo. Se for alarme falso conferido à mão, use -SegredoRevisado <arquivo>."
}
Ok 'histórico do git sem segredo (chave, token, senha) nos diffs'
if ($SegredoRevisado.Count -gt 0) { Aviso "conferidos à mão (o padrão genérico não para neles): $($SegredoRevisado -join ', ')" }
$mascarados = $emailsHistorico | ForEach-Object { $_ -replace '^(.).*@', '$1***@' }
Aviso "o histórico do git leva o e-mail de autor dos commits ($($mascarados -join ', ')). Ele faz parte de cada commit e não sai sem reescrever a história: guarde o pacote em lugar privado"
}
else { Ok 'sem código no pacote (-SoApp): o histórico do git e os e-mails de autor ficam de fora' }

$exeNoPacote = Join-Path $Destino 'app\WHYNOT LAB.exe'
$assunto = Git-Exigir $principal @('log', '-1', '--format=%h (%cd)', '--date=format:%d/%m/%Y %H:%M', $Ramo) 'não deu para ler o commit'
$versao = @(
    'WHYNOT LAB: pacote portátil',
    # no -SoApp (público), sem a pasta de quem montou: ela leva o usuário do Windows
    ("Montado em $(Get-Date -Format 'dd/MM/yyyy HH:mm')" + $(if ($SoApp) { '' } else { " a partir de $principal" })),
    "Código: $Ramo em $assunto"
)
if (Test-Path -LiteralPath $exeNoPacote) { $versao += "App: WHYNOT LAB.exe, $(Tamanho-Mb $exeNoPacote) MB, sha256 $((Get-FileHash -LiteralPath $exeNoPacote -Algorithm SHA256).Hash)" }
if ($Completo) { $versao += 'Completo: Tracktion e JUCE, CMake e Ninja, plugins de teste e projetos. O outro PC só baixa o Git e o VS Build Tools' }
if ($SoApp) { $versao += 'Só o app (-SoApp): o pacote dos produtores, sem código, SDKs, notas nem plugins de teste' }
if ($Piano) { $versao += 'Com o WHYNOT Piano em app\Bibliotecas (Salamander Grand Piano V3, CC BY 3.0)' }
$versao += "Proteção de dados: $trocas e-mail(s) ou link(s) trocados por marcadores; varredura sem achados em $lidos arquivo(s) de texto"
if (-not $SoApp) { $versao += 'O histórico do git leva o e-mail de autor de cada commit: guarde o pacote em lugar privado' }
if ($SegredoRevisado.Count -gt 0) { $versao += "Conferidos à mão na varredura (alarme falso do padrão genérico): $($SegredoRevisado -join ', ')" }
if ($fora.Count -gt 0) { $versao += "Branches fora do ${Ramo} (montado com -Forcar): $($fora -join ', ')" }
Set-Content -LiteralPath (Join-Path $Destino 'versao.txt') -Value $versao -Encoding UTF8
Ok 'versao.txt'

# conteudo.sha256: o preparar-pc.ps1 confere a cópia do pendrive com esta lista
$base = $Destino.Length + 1
$linhas = Get-ChildItem -LiteralPath $Destino -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash, $_.FullName.Substring($base)
}
Set-Content -LiteralPath (Join-Path $Destino 'conteudo.sha256') -Value $linhas -Encoding UTF8
Ok "conteudo.sha256 ($(@($linhas).Count) arquivos)"

if ($Zip) {
    $arquivoZip = "$Destino.zip"
    # o tar.exe do Windows comprime centenas de MB em segundos e não trava arquivo que outro processo esteja lendo;
    # o Compress-Archive fica de reserva para um Windows sem ele
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    if (Test-Path $tar) {
        $nomes = @(Get-ChildItem -LiteralPath $Destino -Force | ForEach-Object { $_.Name })
        $ErrorActionPreference = 'Continue'
        & $tar -a -cf $arquivoZip -C $Destino @nomes
        $codigoTar = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($codigoTar -ne 0) { Parar "o tar.exe não conseguiu criar $arquivoZip (código $codigoTar). A pasta $Destino está pronta; comprima de novo." }
    }
    else {
        Compress-Archive -Path (Join-Path $Destino '*') -DestinationPath $arquivoZip -ErrorAction Stop
    }
    if (-not (Test-Path -LiteralPath $arquivoZip)) { Parar "o zip $arquivoZip não foi criado. A pasta $Destino está pronta; comprima de novo." }
    Ok "$arquivoZip ($(Tamanho-Mb $arquivoZip) MB)"

    if ($CopiarPara) {
        $curto = Git-Exigir $principal @('rev-parse', '--short', $Ramo) 'não deu para ler o commit'
        $copia = Join-Path $CopiarPara ('WHYNOT-LAB-{0}_{1}_{2}.zip' -f $(if ($SoApp) { 'app' } else { 'portatil' }), (Get-Date -Format 'yyyy-MM-dd_HHmm'), $curto)
        if (Test-Path -LiteralPath $copia) { Aviso "$copia já existe: não copiei" }
        else {
            Copy-Item -LiteralPath $arquivoZip -Destination $copia
            if ((Get-Item -Force -LiteralPath $copia).Length -ne (Get-Item -Force -LiteralPath $arquivoZip).Length) {
                Parar "a cópia em $copia saiu com outro tamanho"
            }
            Ok "copiado para $copia (o Google Drive sobe em segundo plano: espere o ícone dele terminar antes de desligar o PC)"
        }
    }
}

Write-Host ''
Write-Host "Pacote pronto em $Destino ($(Tamanho-Mb $Destino) MB)." -ForegroundColor Green
if ($SoApp) { Write-Host 'No outro PC: extraia o zip inteiro e abra app\WHYNOT LAB.exe (veja o LEIA-ME.txt).' }
else { Write-Host 'No outro PC: copie a pasta e dê dois cliques em preparar-pc.cmd (ou veja o LEIA-ME.txt).' }
exit 0

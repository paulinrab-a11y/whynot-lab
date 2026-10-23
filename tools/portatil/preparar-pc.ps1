<#
.SYNOPSIS
    Prepara um PC com Windows para programar a WHYNOT LAB: ferramentas, código, dependências,
    build e autoteste.

.DESCRIPTION
    Roda da pasta do pacote (pendrive) ou de dentro do repositório (tools\portatil).
    Cada passo confere antes de agir. Se algo falhar, corrija e rode de novo: o que já está pronto é pulado.

    Sem -Instalar, o script não instala nada no Windows: só aponta o que falta.
    Ele nunca apaga nem sobrescreve arquivos que já existem.

    O guia completo está em docs\portatil.md.

.EXAMPLE
    .\preparar-pc.ps1 -SoVerificar
    Mostra o que falta e o que seria feito, sem mudar nada.

.EXAMPLE
    .\preparar-pc.ps1 -Instalar
    Instala o que falta pelo winget, clona o código do pacote em C:\dev\whynot-lab, compila e roda o autoteste.

.EXAMPLE
    .\preparar-pc.ps1 -Repositorio https://github.com/<conta>/whynot-lab.git
    Clona de um repositório em vez do bundle do pacote.
#>
[CmdletBinding()]
param(
    # Onde o código fica. Use um caminho curto: o git e o build passam do limite de caminho do Windows em pastas fundas.
    [string] $Destino = 'C:\dev\whynot-lab',

    # Pasta do pacote. Padrão: a pasta deste script, se ela tiver codigo\whynot-lab.bundle.
    [string] $Pacote = '',

    # Clona deste endereço em vez do bundle do pacote (um repositório privado no GitHub, por exemplo).
    [string] $Repositorio = '',

    # Branch que fica aberto depois do clone.
    [string] $Ramo = 'main',

    # Instala pelo winget o que faltar: Git, VS 2022 Build Tools (C++), CMake e Ninja.
    [switch] $Instalar,

    # Só confere e mostra o plano. Não instala, não baixa, não copia e não compila.
    [switch] $SoVerificar,

    [switch] $SemCompilar,

    # Não compila os quatro plugins VST3 de teste. Economiza RAM e tempo, mas o autoteste acusa uma falha,
    # a não ser que build\test_plugins já tenha os plugins (copiados de outro build).
    [switch] $SemPluginsDeTeste,

    # Pula o autoteste do app e o ctest.
    [switch] $SemAutoteste,

    # Jobs do build. 0 escolhe pela RAM: cerca de 2,5 GB por job, no mínimo 2.
    [int] $Jobs = 0,

    # Pasta do Claude Code deste usuário, onde ficam as notas do projeto e as skills.
    [string] $PastaClaude = (Join-Path $env:USERPROFILE '.claude')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$script:pendencias = New-Object System.Collections.Generic.List[string]

function Passo([string] $texto) { Write-Host ''; Write-Host "== $texto" -ForegroundColor Cyan }
function Ok([string] $texto) { Write-Host "  ok     $texto" -ForegroundColor Green }
function Info([string] $texto) { Write-Host "         $texto" }
function Faria([string] $texto) { Write-Host "  faria  $texto" -ForegroundColor DarkCyan }
function Falta([string] $texto) {
    Write-Host "  falta  $texto" -ForegroundColor Yellow
    $script:pendencias.Add($texto)
}
function Parar([string] $texto) {
    Write-Host ''
    Write-Host "Parei: $texto" -ForegroundColor Red
    exit 1
}

# Comando externo: o código de saída decide, não o que ele escreve no stderr (o git escreve progresso lá).
function Rodar([string] $programa, [string[]] $argumentos, [string] $oQue) {
    $ErrorActionPreference = 'Continue'
    & $programa @argumentos
    if ($LASTEXITCODE -ne 0) { Parar "$oQue (código $LASTEXITCODE)" }
}

function Atualizar-Path {
    $maquina = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $usuario = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = (@($maquina, $usuario) | Where-Object { $_ }) -join ';'
}

function Achar-Programa([string] $nome, [string[]] $outrosLugares) {
    $cmd = Get-Command $nome -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) { return $cmd.Source }
    foreach ($lugar in $outrosLugares) {
        $achado = Get-ChildItem -Path $lugar -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($achado) { return $achado.FullName }
    }
    return $null
}

function Achar-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $pasta = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $pasta) { return $null }
    $vcvars = Join-Path $pasta 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { return $null }
    $versao = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property catalog_productDisplayVersion
    return [pscustomobject] @{ Vcvars = $vcvars; Versao = "$versao"; Pasta = $pasta }
}

function Achar-Ferramentas {
    $vs = Achar-VisualStudio
    $pastaVs = if ($vs) { $vs.Pasta } else { 'C:\nada' }
    $winget = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet'
    $cmakeVs = Join-Path $pastaVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $ninjaVs = Join-Path $pastaVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    return [pscustomobject] @{
        Git   = Achar-Programa 'git' @("$env:ProgramFiles\Git\cmd\git.exe")
        Vs    = $vs
        CMake = Achar-Programa 'cmake' @("$env:ProgramFiles\CMake\bin\cmake.exe",
                                         "$winget\Packages\Kitware.CMake_*\cmake-*\bin\cmake.exe",
                                         $cmakeVs,
                                         (Join-Path $pastaFerramentas 'cmake\bin\cmake.exe'))
        Ninja = Achar-Programa 'ninja' @("$winget\Links\ninja.exe",
                                         "$winget\Packages\Ninja-build.Ninja_*\ninja.exe",
                                         $ninjaVs,
                                         (Join-Path $pastaFerramentas 'ninja\ninja.exe'))
    }
}

# Cópia grande pelo robocopy, sem sobrescrever nada que já exista no destino.
function Copiar-Rapido([string] $origem, [string] $destino) {
    $ErrorActionPreference = 'Continue'
    & robocopy.exe $origem $destino /E /XC /XN /XO /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { Parar "o robocopy não conseguiu copiar $origem (código $LASTEXITCODE)" }
}

# Pastas do Claude Code: o nome da pasta do projeto é o caminho com tudo que não é letra ou número trocado por "-".
function Chave-Do-Projeto([string] $caminho) { return ($caminho -replace '[^A-Za-z0-9]', '-') }

# Copia só o que ainda não existe no destino. Devolve quantos arquivos copiou e quantos já estavam lá.
function Copiar-Sem-Sobrescrever([string] $origem, [string] $destino) {
    $copiados = 0; $mantidos = 0
    $base = (Resolve-Path -LiteralPath $origem).ProviderPath.TrimEnd('\')
    foreach ($arq in Get-ChildItem -LiteralPath $base -Recurse -File -Force) {
        $relativo = $arq.FullName.Substring($base.Length + 1)
        $alvo = Join-Path $destino $relativo
        if (Test-Path -LiteralPath $alvo) { $mantidos++; continue }
        New-Item -ItemType Directory -Force -Path (Split-Path $alvo) | Out-Null
        Copy-Item -LiteralPath $arq.FullName -Destination $alvo
        $copiados++
    }
    return [pscustomobject] @{ Copiados = $copiados; Mantidos = $mantidos }
}

# Traz para esta janela o ambiente do compilador (PATH, INCLUDE, LIB) que o vcvars64.bat monta.
function Carregar-Ambiente-Vc([string] $vcvars) {
    $bat = Join-Path $env:TEMP 'wnl-vcvars.cmd'
    Set-Content -LiteralPath $bat -Encoding Ascii -Value "@call `"$vcvars`" >nul`r`n@if errorlevel 1 exit /b 1`r`n@set"
    $ErrorActionPreference = 'Continue'
    $linhas = & cmd.exe /d /c $bat
    $codigo = $LASTEXITCODE
    Remove-Item -LiteralPath $bat -ErrorAction SilentlyContinue
    if ($codigo -ne 0) { Parar "o vcvars64.bat falhou (código $codigo)" }
    foreach ($linha in $linhas) {
        $i = $linha.IndexOf('=')
        if ($i -gt 0) { [Environment]::SetEnvironmentVariable($linha.Substring(0, $i), $linha.Substring($i + 1), 'Process') }
    }
}

#==============================================================================
if (-not $Pacote -and (Test-Path (Join-Path $PSScriptRoot 'codigo\whynot-lab.bundle'))) { $Pacote = $PSScriptRoot }
# Rodando de dentro do repositório (tools\portatil), sem pacote: o destino é o próprio repositório.
$raizDoScript = Split-Path (Split-Path $PSScriptRoot)
if (-not $Pacote -and -not $PSBoundParameters.ContainsKey('Destino') -and (Test-Path (Join-Path $raizDoScript '.git'))) {
    $Destino = $raizDoScript
}
$Destino = [IO.Path]::GetFullPath($Destino).TrimEnd('\')
# onde ficam o CMake e o Ninja que vêm do pacote completo: ao lado da pasta do código (C:\dev\ferramentas)
$pastaFerramentas = Join-Path (Split-Path $Destino) 'ferramentas'
$bundle = if ($Pacote) { Join-Path $Pacote 'codigo\whynot-lab.bundle' } else { '' }

Write-Host 'WHYNOT LAB: preparar este PC' -ForegroundColor White
Write-Host "Destino do código: $Destino"
if ($Pacote) { Write-Host "Pacote: $Pacote" } else { Write-Host 'Sem pacote: só o que dá para fazer com o repositório.' }
if ($SoVerificar) { Write-Host 'Modo só verificar: nada vai ser instalado, baixado, copiado nem compilado.' -ForegroundColor DarkCyan }

#------------------------------------------------------------------------------
Passo 'Pacote'
if (-not $Pacote) {
    Info 'nenhum pacote nesta pasta (ok se o código vier de -Repositorio ou já estiver no destino)'
}
elseif (-not (Test-Path $bundle)) {
    Parar "o pacote em $Pacote não tem codigo\whynot-lab.bundle"
}
else {
    $lista = Join-Path $Pacote 'conteudo.sha256'
    if (-not (Test-Path $lista)) {
        Info 'pacote sem conteudo.sha256: não dá para conferir se a cópia veio inteira'
    }
    else {
        $erros = 0; $total = 0
        foreach ($linha in Get-Content -LiteralPath $lista -Encoding UTF8) {
            if (-not $linha.Trim()) { continue }
            $hash = $linha.Substring(0, 64); $relativo = $linha.Substring(66)
            $arq = Join-Path $Pacote $relativo
            $total++
            if (-not (Test-Path -LiteralPath $arq)) { $erros++; Write-Host "  faltando no pacote: $relativo" -ForegroundColor Red; continue }
            if ((Get-FileHash -LiteralPath $arq -Algorithm SHA256).Hash -ne $hash) { $erros++; Write-Host "  diferente do original: $relativo" -ForegroundColor Red }
        }
        if ($erros -gt 0) { Parar "$erros de $total arquivos do pacote estão faltando ou corrompidos. Copie o pacote de novo." }
        Ok "$total arquivos conferidos pelo sha256"
    }
    $versao = Join-Path $Pacote 'versao.txt'
    if (Test-Path $versao) { Get-Content -LiteralPath $versao -Encoding UTF8 | ForEach-Object { Info $_ } }
}

#------------------------------------------------------------------------------
Passo 'Sistema'
if (-not [Environment]::Is64BitOperatingSystem) { Parar 'a WHYNOT LAB precisa do Windows 64 bits' }
$so = Get-CimInstance Win32_OperatingSystem
Ok "$($so.Caption) ($($so.Version))"
$ramGb = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
if ($Jobs -le 0) { $Jobs = [math]::Max(2, [math]::Min([Environment]::ProcessorCount, [int][math]::Floor($ramGb / 2.5))) }
Ok "$ramGb GB de RAM, $([Environment]::ProcessorCount) núcleos: build com -j $Jobs"
if ($ramGb -lt 8) { Info "pouca RAM: feche o navegador durante o primeiro build. Se travar, use -Jobs $([math]::Max(1, $Jobs - 1))" }

$letra = $Destino.Substring(0, 1)
$livreGb = [math]::Round((Get-PSDrive -Name $letra).Free / 1GB, 1)
$precisaGb = 6
if (-not (Achar-VisualStudio)) { $precisaGb += 8 }
if ($livreGb -lt $precisaGb) { Falta "espaço em ${letra}: $livreGb GB livres, precisa de uns $precisaGb GB" }
else { Ok "$livreGb GB livres em ${letra}: (precisa de uns $precisaGb GB)" }

#------------------------------------------------------------------------------
Passo 'Ferramentas'
$f = Achar-Ferramentas
# Pacote completo: o CMake e o Ninja vêm do pacote para uma pasta fixa ao lado do código,
# porque o build guarda o caminho deles (o pendrive pode não estar lá no próximo build).
$ferramentasDoPacote = if ($Pacote) { Join-Path $Pacote 'ferramentas' } else { '' }
if ($ferramentasDoPacote -and (Test-Path $ferramentasDoPacote) -and (-not $f.CMake -or -not $f.Ninja)) {
    if ($SoVerificar) {
        Faria "copiar o CMake e o Ninja do pacote para $pastaFerramentas"
        if (-not $f.CMake) { $f.CMake = Join-Path $pastaFerramentas 'cmake\bin\cmake.exe' }
        if (-not $f.Ninja) { $f.Ninja = Join-Path $pastaFerramentas 'ninja\ninja.exe' }
    }
    else {
        Copiar-Rapido $ferramentasDoPacote $pastaFerramentas
        Ok "CMake e Ninja copiados do pacote para $pastaFerramentas"
        $f = Achar-Ferramentas
    }
}
$faltando = @()
if (-not $f.Git) { $faltando += 'Git' }
if (-not $f.Vs) { $faltando += 'VS2022' }
if (-not $f.CMake) { $faltando += 'CMake' }
if (-not $f.Ninja) { $faltando += 'Ninja' }

if ($faltando.Count -gt 0 -and $Instalar -and -not $SoVerificar) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Parar 'o winget não está neste Windows. Instale o "Instalador de Aplicativo" pela Microsoft Store e rode de novo.'
    }
    Info 'o winget pode pedir para você aceitar os termos de cada programa: responda S (ou Y)'
    foreach ($item in $faltando) {
        switch ($item) {
            'Git'    { Rodar 'winget' @('install', '--id', 'Git.Git', '-e', '--source', 'winget') 'a instalação do Git falhou' }
            'CMake'  { Rodar 'winget' @('install', '--id', 'Kitware.CMake', '-e', '--source', 'winget') 'a instalação do CMake falhou' }
            'Ninja'  { Rodar 'winget' @('install', '--id', 'Ninja-build.Ninja', '-e', '--source', 'winget') 'a instalação do Ninja falhou' }
            'VS2022' {
                Info 'VS 2022 Build Tools com o C++: são alguns GB e leva de 10 a 30 minutos'
                Rodar 'winget' @('install', '--id', 'Microsoft.VisualStudio.2022.BuildTools', '-e', '--source', 'winget',
                                 '--override', '--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended') 'a instalação do VS 2022 Build Tools falhou. Se o Build Tools já estava instalado sem o C++, abra o Visual Studio Installer, clique em Modificar e marque "Desenvolvimento para desktop com C++"'
            }
        }
    }
    Atualizar-Path
    $f = Achar-Ferramentas
}

if ($f.Git) { Ok "Git: $($f.Git)" } else { Falta 'Git (winget install --id Git.Git -e)' }
if ($f.Vs) {
    Ok "Visual Studio $($f.Vs.Versao) com o C++: $($f.Vs.Pasta)"
    if (-not $f.Vs.Versao.StartsWith('17.')) { Info 'a WHYNOT LAB foi testada com o VS 2022 (17.14). Outra versão deve funcionar, mas não foi testada' }
}
else {
    Falta 'VS 2022 Build Tools com "Desenvolvimento para desktop com C++". Se o Visual Studio já estiver instalado sem o C++, adicione esse workload pelo Visual Studio Installer'
}
if ($f.CMake) { Ok "CMake: $($f.CMake)" } else { Falta 'CMake (winget install --id Kitware.CMake -e)' }
if ($f.Ninja) { Ok "Ninja: $($f.Ninja)" } else { Falta 'Ninja (winget install --id Ninja-build.Ninja -e)' }

$temFerramentas = $f.Git -and $f.Vs -and $f.CMake -and $f.Ninja
if (-not $temFerramentas -and -not $SoVerificar) {
    if ($Instalar) { Parar 'ainda falta ferramenta depois do winget. Veja as mensagens acima, feche esta janela, abra outra e rode de novo.' }
    Parar 'falta ferramenta. Rode de novo com -Instalar, ou instale à mão e rode de novo.'
}

#------------------------------------------------------------------------------
Passo 'Código'
$temRepo = Test-Path (Join-Path $Destino '.git')
$veioDoPacote = $false
if ($temRepo) {
    Ok "repositório já está em $Destino (não mexo nos branches; para trazer mudanças use tools\portatil\trocar-codigo.ps1)"
}
else {
    $origem = if ($Repositorio) { $Repositorio } elseif ($bundle) { $bundle } else { '' }
    if (-not $origem) {
        Parar "não há código em $Destino e nenhum lugar para buscar. Rode do pacote ou use -Repositorio <endereço>."
    }
    if ((Test-Path $Destino) -and (Get-ChildItem -LiteralPath $Destino -Force | Select-Object -First 1)) {
        Parar "$Destino já existe e não está vazia. Escolha outra pasta com -Destino."
    }
    if ($SoVerificar) {
        Faria "clonar $origem em $Destino (branch $Ramo)"
    }
    else {
        New-Item -ItemType Directory -Force -Path (Split-Path $Destino) | Out-Null
        Rodar $f.Git @('-c', 'core.longpaths=true', 'clone', '--branch', $Ramo, $origem, $Destino) 'o clone falhou'
        Rodar $f.Git @('-C', $Destino, 'config', 'core.longpaths', 'true') 'não deu para ligar core.longpaths'
        if (-not $Repositorio) {
            # o bundle é só o meio de transporte: o nome "pacote" deixa "origin" livre para um GitHub no futuro
            Rodar $f.Git @('-C', $Destino, 'remote', 'rename', 'origin', 'pacote') 'não deu para renomear o remote'
            $veioDoPacote = $true
        }
        Ok "código em $Destino, branch $Ramo"
    }
}

# Identidade dos commits: se este PC não tem uma, usa a do último commit, como no PC de origem.
if ((Test-Path (Join-Path $Destino '.git')) -and -not $SoVerificar) {
    $ErrorActionPreference = 'Continue'
    $nome = & $f.Git -C $Destino config user.name
    $ErrorActionPreference = 'Stop'
    if (-not $nome) {
        $autor = & $f.Git -C $Destino log -1 --format=%an
        $email = & $f.Git -C $Destino log -1 --format=%ae
        Rodar $f.Git @('-C', $Destino, 'config', 'user.name', $autor) 'não deu para gravar o nome dos commits'
        Rodar $f.Git @('-C', $Destino, 'config', 'user.email', $email) 'não deu para gravar o e-mail dos commits'
        Ok "commits deste repositório saem como $autor"
    }
}

#------------------------------------------------------------------------------
Passo 'Dependências'
$tracktion = Join-Path $Destino 'external\tracktion_engine'
$marcaJuce = 'modules\juce\modules\juce_core\juce_core.h'
$juceOk = Test-Path (Join-Path $tracktion $marcaJuce)
$tracktionDoPacote = if ($Pacote) { Join-Path $Pacote 'externos\tracktion_engine' } else { '' }
if ($juceOk) {
    Ok 'Tracktion Engine e JUCE em external\tracktion_engine'
}
elseif ($tracktionDoPacote -and (Test-Path (Join-Path $tracktionDoPacote $marcaJuce))) {
    # pacote completo: os arquivos no commit fixado, sem .git. Com o submódulo sem "init", o git status fica limpo.
    if ($SoVerificar) { Faria 'copiar o Tracktion Engine e o JUCE do pacote (sem baixar nada)' }
    else {
        Info 'copiando o Tracktion Engine e o JUCE do pacote (uns 10 mil arquivos)...'
        Copiar-Rapido $tracktionDoPacote $tracktion
        Ok 'Tracktion Engine e JUCE copiados do pacote, sem baixar'
    }
}
elseif ($SoVerificar) {
    Faria 'baixar o Tracktion Engine e o JUCE do GitHub (uns 180 MB), no commit que o repositório fixa'
}
else {
    Info 'baixando o Tracktion Engine e o JUCE do GitHub (uns 180 MB)...'
    Rodar $f.Git @('-C', $Destino, 'submodule', 'update', '--init', 'external/tracktion_engine') 'não deu para baixar o Tracktion Engine'
    # o submódulo do JUCE aponta por SSH; por HTTPS não precisa de chave do GitHub
    Rodar $f.Git @('-C', $tracktion, 'config', 'submodule.modules/juce.url', 'https://github.com/juce-framework/JUCE.git') 'não deu para trocar o endereço do JUCE'
    Rodar $f.Git @('-C', $tracktion, 'submodule', 'update', '--init', '--depth', '1') 'não deu para baixar o JUCE'
    Ok 'Tracktion Engine e JUCE baixados'
}

$sdks = @(
    @{ Nome = 'lame-3.100'; Marca = 'include\lame.h';      Sem = 'o app compila sem exportar MP3' },
    @{ Nome = 'asiosdk';    Marca = 'common\iasiodrv.h';  Sem = 'o app compila só com o WASAPI, sem ASIO' }
)
foreach ($sdk in $sdks) {
    $alvo = Join-Path $Destino "external\$($sdk.Nome)"
    $noPacote = if ($Pacote) { Join-Path $Pacote "externos\$($sdk.Nome)" } else { '' }
    if (Test-Path (Join-Path $alvo $sdk.Marca)) {
        Ok "$($sdk.Nome) em external\"
    }
    elseif ($noPacote -and (Test-Path (Join-Path $noPacote $sdk.Marca))) {
        if ($SoVerificar) { Faria "copiar $($sdk.Nome) do pacote para external\" }
        else {
            $r = Copiar-Sem-Sobrescrever $noPacote $alvo
            Ok "$($sdk.Nome) copiado do pacote ($($r.Copiados) arquivos)"
        }
    }
    else {
        Info "sem $($sdk.Nome): $($sdk.Sem). Para ligar, veja a seção 2b de docs\como-rodar.md"
    }
}

#------------------------------------------------------------------------------
Passo 'Claude Code, atalhos e projetos'
$chave = Chave-Do-Projeto $Destino
$copias = @(
    @{ De = 'claude\memoria';  Para = (Join-Path $PastaClaude "projects\$chave\memory"); Nome = 'notas do projeto para o Claude' },
    @{ De = 'claude\skills';   Para = (Join-Path $PastaClaude 'skills');                 Nome = 'skills do projeto para o Claude' },
    @{ De = 'app\config';      Para = (Join-Path $env:APPDATA 'WHYNOT LAB');               Nome = 'atalhos do app' },
    @{ De = 'projetos';        Para = (Join-Path ([Environment]::GetFolderPath('MyMusic')) 'WHYNOT LAB'); Nome = 'projetos e Biblioteca' }
)
$algum = $false
foreach ($c in $copias) {
    $origem = if ($Pacote) { Join-Path $Pacote $c.De } else { '' }
    if (-not $origem -or -not (Test-Path $origem)) { continue }
    $algum = $true
    if ($SoVerificar) { Faria "copiar $($c.Nome) para $($c.Para), sem sobrescrever"; continue }
    $r = Copiar-Sem-Sobrescrever $origem $c.Para
    $extra = if ($r.Mantidos -gt 0) { ", $($r.Mantidos) já existiam e ficaram como estavam" } else { '' }
    Ok "$($c.Nome): $($r.Copiados) arquivos em $($c.Para)$extra"
}
if (-not $algum) { Info 'nada para copiar (sem pacote, ou pacote sem essas pastas)' }

#------------------------------------------------------------------------------
Passo 'Compilar'
$build = Join-Path $Destino 'build'
$exe = Join-Path $build 'whynot_lab_artefacts\Release\WHYNOT LAB.exe'
# Pacote completo: os plugins de teste vêm prontos e não precisam ser compilados (cada um recompilaria o JUCE).
$pluginsDoPacote = if ($Pacote) { Join-Path $Pacote 'app\test_plugins' } else { '' }
$pluginsNoBuild = Join-Path $build 'test_plugins'
if (-not $SemCompilar -and $pluginsDoPacote -and (Test-Path $pluginsDoPacote)) {
    if ($SoVerificar) { Faria 'usar os plugins de teste do pacote, sem compilá-los' }
    else {
        Copiar-Rapido $pluginsDoPacote $pluginsNoBuild
        Ok "plugins de teste copiados do pacote para $pluginsNoBuild"
    }
    $SemPluginsDeTeste = $true
}
$alvos = @('whynot_lab')
if (-not $SemPluginsDeTeste) { $alvos = @('whynot_test_plugins') + $alvos }

if ($SemCompilar) {
    Info 'pulado (-SemCompilar)'
}
elseif ($SoVerificar) {
    Faria "configurar o CMake em $build e compilar $($alvos -join ', ') com -j $Jobs"
    Info 'o primeiro build compila o JUCE e o Tracktion: uns 10 minutos num notebook i5, de 15 a 20 com o LAME'
}
else {
    Carregar-Ambiente-Vc $f.Vs.Vcvars
    $configurar = @('-S', $Destino, '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_MAKE_PROGRAM=$($f.Ninja)")
    if ($SemPluginsDeTeste) { $configurar += '-DWHYNOT_BUILD_TEST_PLUGINS=OFF' }
    Rodar $f.CMake $configurar 'o CMake não conseguiu configurar o projeto'
    $inicio = Get-Date
    Rodar $f.CMake (@('--build', $build, '--target') + $alvos + @('-j', "$Jobs")) 'o build falhou. Se foi falta de memória, rode de novo com -Jobs 2'
    $minutos = [math]::Round(((Get-Date) - $inicio).TotalMinutes, 1)
    Ok "compilado em $minutos min: $exe"
}

#------------------------------------------------------------------------------
Passo 'Autoteste'
if ($SemAutoteste -or $SemCompilar) {
    Info 'pulado'
}
elseif ($SoVerificar) {
    Faria "rodar `"WHYNOT LAB.exe`" --self-test e mostrar as falhas"
}
else {
    $relatorio = Join-Path $build 'selftest\relatorio.txt'
    New-Item -ItemType Directory -Force -Path (Split-Path $relatorio) | Out-Null
    $p = Start-Process -FilePath $exe -ArgumentList @('--self-test', "`"$relatorio`"") -PassThru
    $null = $p.Handle   # sem isto o ExitCode pode vir vazio no Windows PowerShell 5.1
    $p.WaitForExit()
    $falhas = $p.ExitCode
    if ($falhas -eq 0) {
        Ok "autoteste: $(@(Select-String -LiteralPath $relatorio -Pattern '^  ok: ' -Encoding UTF8).Count) ok, nenhuma falha"
    }
    else {
        Falta "autoteste com $falhas falha(s), relatório em $relatorio"
        Select-String -LiteralPath $relatorio -Pattern '^  FALHOU: ' -Encoding UTF8 | ForEach-Object { Info $_.Line.Trim() }
        if ($SemPluginsDeTeste) { Info 'a falha dos plugins de teste é esperada com -SemPluginsDeTeste' }
    }
}

#------------------------------------------------------------------------------
# Os alvos de teste (tests/) não entram no "Compilar": são pequenos, sem JUCE, e só o ctest precisa deles.
Passo 'Testes (ctest)'
if ($SemAutoteste -or $SemCompilar) {
    Info 'pulado'
}
elseif ($SoVerificar) {
    Faria 'compilar os alvos de teste e rodar o ctest --test-dir build'
}
else {
    $ctest = Join-Path (Split-Path $f.CMake) 'ctest.exe'
    $ErrorActionPreference = 'Continue'
    & $f.CMake --build $build -j "$Jobs"
    $codigoBuild = $LASTEXITCODE
    $codigoTestes = -1
    if ($codigoBuild -eq 0) {
        & $ctest --test-dir $build --output-on-failure
        $codigoTestes = $LASTEXITCODE
    }
    $ErrorActionPreference = 'Stop'
    if ($codigoBuild -ne 0) { Falta "os alvos de teste não compilaram (código $codigoBuild); veja a saída acima" }
    elseif ($codigoTestes -ne 0) { Falta "o ctest acusou falha (código $codigoTestes); veja a saída acima" }
    else { Ok 'ctest: todos os testes passaram' }
}

#------------------------------------------------------------------------------
Passo 'Resumo'
if ($script:pendencias.Count -eq 0) {
    if ($SoVerificar) { Write-Host 'Nada falta. Rode sem -SoVerificar para fazer o que está marcado com "faria".' -ForegroundColor Green }
    else {
        Write-Host 'Pronto.' -ForegroundColor Green
        Write-Host "  Código:  $Destino"
        if (Test-Path $exe) { Write-Host "  App:     $exe" }
        if ($Pacote) {
            Write-Host "  Guia:    $(Join-Path $Pacote 'GUIA.md') (ida e volta do código, Claude Code e problemas comuns)"
            Write-Host "  Para levar o que você fizer de volta: $(Join-Path $Pacote 'trocar-codigo.ps1') -Projeto $Destino -Exportar <arquivo.bundle>"
        }
        else { Write-Host '  Guia:    docs\portatil.md (ida e volta do código, Claude Code e problemas comuns)' }
    }
    exit 0
}
Write-Host "Falta resolver $($script:pendencias.Count) item(ns):" -ForegroundColor Yellow
$script:pendencias | ForEach-Object { Write-Host "  - $_" }
if (-not $Instalar -and ($script:pendencias | Where-Object { $_ -match 'winget|Build Tools' })) {
    Write-Host 'Dica: rode de novo com -Instalar para o script instalar as ferramentas pelo winget.'
}
exit 1

# Versão portátil: levar a WHYNOT LAB para outro PC

Serve para usar o app em outro PC e para continuar programando lá.
Os scripts ficam em `tools/portatil/`:

| Script | Onde roda | O que faz |
|---|---|---|
| `montar-pacote.ps1` | no PC de origem | monta a pasta do pacote: app, código (git bundle), LAME, ASIO, notas do Claude e os scripts |
| `preparar-pc.ps1` (ou `preparar-pc.cmd`) | no PC novo | confere o pacote, instala as ferramentas, clona, baixa o Tracktion e o JUCE, compila e roda o autoteste |
| `trocar-codigo.ps1` | nos dois | leva e traz os commits por um arquivo, sem GitHub |

Os três rodam no Windows PowerShell 5.1, que já vem no Windows, e no PowerShell 7. Nenhum deles apaga ou sobrescreve arquivos que já existem.

## 1. Por que o app já é portátil

O `WHYNOT LAB.exe` é um arquivo só, de uns 20 MB:
- a biblioteca do C++ vai dentro dele (`CMAKE_MSVC_RUNTIME_LIBRARY` MultiThreaded, no `CMakeLists.txt`);
- o kit, as fontes e os ícones também vão dentro, como dados binários;
- ele só usa DLLs que todo Windows 10 e 11 de 64 bits tem.

O que fica fora do exe:

| O quê | Onde | Vai no pacote? |
|---|---|---|
| Atalhos (`atalhos.json`) | `%APPDATA%\WHYNOT LAB` | sim, em `app\config` |
| Kit WHYNOT gerado | `%APPDATA%\WHYNOT LAB\Kit` | não: o app gera de novo |
| Cache de plugins, dispositivo de áudio, marcas de sessão | `%APPDATA%\WHYNOT LAB` | não: os caminhos e o hardware são deste PC |
| Projetos e Biblioteca de sons | `Músicas\WHYNOT LAB` | só com `-ComProjetos` |
| Plugins VST3 | `C:\Program Files\Common Files\VST3` | não: instale de novo no outro PC |

Cada projeto `.wnl` é uma pasta com os próprios samples, então dá para levar um projeto avulso copiando a pasta dele.

## 2. Montar o pacote (no PC de origem)

Monte só depois de todas as frentes estarem no `main` e de o `main` estar compilado no checkout principal. O script confere as duas coisas e para se faltar alguma.

Primeiro veja o plano, sem gravar nada:

```
powershell -ExecutionPolicy Bypass -File tools\portatil\montar-pacote.ps1 -Listar
```

Depois monte:

```
powershell -ExecutionPolicy Bypass -File tools\portatil\montar-pacote.ps1 -Destino E:\WHYNOT-LAB-portatil
```

| Opção | Para quê |
|---|---|
| `-Destino <pasta>` | onde montar. Padrão: `WHYNOT-LAB-portatil` na Área de Trabalho. A pasta não pode existir |
| `-ComProjetos` | inclui `Músicas\WHYNOT LAB` |
| `-Completo` | também o Tracktion e o JUCE, o CMake, o Ninja, os plugins de teste e os favoritos do synth. Liga o `-ComProjetos` (seção 2d) |
| `-Coordenacao <pasta>` | os `.md` dessa pasta e da subpasta `revisoes` (quadro, relatórios, planos) vão para `coordenacao\` |
| `-Extras <pastas>` | pastas de fora do git, copiadas para `extras\<nome>` (o projeto do vídeo, por exemplo) |
| `-Excluir <padrões>` | arquivos das extras que ficam de fora, como `'wnl-video\gravacoes\teste.*'` |
| `-Zip` | também gera um `.zip` ao lado da pasta |
| `-CopiarPara <pasta>` | copia o `.zip` para essa pasta como `WHYNOT-LAB-portatil_<data>_<commit>.zip`. Precisa de `-Zip`. Serve para a pasta do Google Drive (seção 2b) |
| `-Forcar` | monta mesmo com branch fora do `main` ou com o exe mais velho que o último commit. Fica anotado no `versao.txt` |

O pacote fica assim:

```
WHYNOT-LAB-portatil\
  LEIA-ME.txt, GUIA.md (este guia), versao.txt, conteudo.sha256
  preparar-pc.cmd, preparar-pc.ps1, trocar-codigo.ps1
  app\WHYNOT LAB.exe
  app\config\atalhos.json
  codigo\whynot-lab.bundle       todos os branches e tags
  externos\lame-3.100\           fora do git (LGPL)
  externos\asiosdk\              fora do git (GPLv3, ver docs/licencas.md)
  claude\memoria\                notas do projeto para o Claude Code
  claude\skills\whynot-agentes\
  projetos\                      só com -ComProjetos
  externos\tracktion_engine\     só com -Completo: o Tracktion e o JUCE, sem .git
  ferramentas\cmake\, ferramentas\ninja\   só com -Completo
  app\test_plugins\              só com -Completo: os VST3 de teste
  coordenacao\                   só com -Coordenacao
  extras\<nome>\                 só com -Extras
```

O `conteudo.sha256` tem o hash de cada arquivo. O `preparar-pc` confere tudo antes de começar, para pegar uma cópia de pendrive que veio pela metade.

### 2b. Google Drive

A pasta **WHYNOTLAB** do Drive guarda as versões do pacote. Neste PC, o Google Drive para computador (instalado pelo winget em 16/10/2026) mostra o Drive como uma pasta do Windows. Copiar para lá já sobe o arquivo:

```
powershell -ExecutionPolicy Bypass -File tools\portatil\montar-pacote.ps1 -Zip -CopiarPara "<pasta do Drive>\WHYNOTLAB"
```

- Confira a letra e o nome da pasta do Drive no Explorer: pode ser "Meu Drive" ou "My Drive".
- Cada versão entra com a data e o commit no nome. O script não apaga as antigas: limpe no Drive quando quiser.
- O upload acontece em segundo plano. Antes de desligar o PC, espere o ícone do Drive na bandeja terminar de sincronizar.
- O conector do Drive no Claude não serve para isso: ele só envia arquivos de texto pequenos.

### 2c. Pacote depois de uma integração (roteiro da segunda, 19/10/2026)

No fim de semana de 16 a 18/10, as frentes foram juntadas no branch `integracao-fds`. Nada foi empacotado nem copiado para o Drive: isso espera o usuário. Na segunda, depois que o usuário decidir o que vai para o pacote:

1. **Escolha o branch.** É o `main`, se o usuário levar o `integracao-fds` para lá. Se ele quiser o pacote antes do merge, é o próprio `integracao-fds`: troque `-Ramo main` por `-Ramo integracao-fds` nos comandos abaixo.
2. **Compile esse branch no checkout principal**, com a trava, e rode o autoteste (saída 0). O script recusa um exe mais velho que o último commit do branch.
3. **Confira sem gravar nada.** Na pasta `tools\portatil` do checkout principal, ou do `wnl-portatil` se a frente ainda não estiver no branch:

   ```
   powershell -ExecutionPolicy Bypass -File .\montar-pacote.ps1 -Ramo main -Destino C:\Users\<usuário>\Desktop\WHYNOT-LAB-portatil-2026-10-19 -Zip -CopiarPara "<pasta do Drive>\WHYNOTLAB" -Listar
   ```

   O resultado tem de terminar com "Pode montar". Branch fora do `-Ramo` aparece como problema: ou ele entra, ou o usuário aceita deixá-lo de fora com `-Forcar`.
4. **Monte e copie para o Drive**, com o ok do usuário: o mesmo comando, sem `-Listar`.

O `-Destino` tem de ser uma pasta nova. A de sexta (`Desktop\WHYNOT-LAB-portatil`, com o zip do `main` 3.4) continua lá, e o script não sobrescreve.

Em 16/10, por volta das 16:56, o passo 3 rodou com o `integracao-fds` ainda vazio. Ele achou a pasta do Drive e mostrou o plano (uns 94 MB). Recusou pelos dois motivos esperados naquela hora: as frentes ainda fora do `main` e o exe mais velho que o último commit.

### 2d. Pacote completo e proteção de dados

O `-Completo` leva tudo o que o outro PC precisaria baixar, menos o Git e o VS Build Tools. Esses dois vêm do winget (o VS Build Tools não pode ir num pacote, e o instalador dele baixa o resto). O comando de 20/10, com a coordenação e o vídeo:

```
powershell -ExecutionPolicy Bypass -File tools\portatil\montar-pacote.ps1 -Completo -Coordenacao C:\Users\<usuário>\dev -Extras C:\Users\<usuário>\dev\wnl-video -Excluir 'wnl-video\gravacoes\teste.*' -Destino C:\Users\<usuário>\Desktop\WHYNOT-LAB-completo-2026-10-20 -Zip
```

No outro PC, o `preparar-pc` usa o que vem no pacote:
- copia o Tracktion e o JUCE para `external\tracktion_engine` em vez de baixar. O submódulo fica sem `init`, então o `git status` continua limpo;
- copia o CMake e o Ninja para `C:\dev\ferramentas`, ao lado do código, se o PC não tiver os dois. O build guarda o caminho deles, e o pendrive pode não estar lá no próximo build;
- usa os plugins de teste prontos, sem compilar.

**Proteção de dados.** O pacote é para você mesmo, e o script cuida para nada escapar:
- **Trocados por marcadores:** os e-mails (`<e-mail removido>`) e os links do Google Drive (`<link do Drive removido>`). Isso vale para as notas do Claude, a coordenação, os projetos e as extras. Os originais neste PC não mudam.
- **Varredura:** antes de comprimir, o script procura e-mail, link do Drive e segredo (chave de API, token, senha, chave privada) em todo arquivo de texto do pacote. Ficam de fora só o código de terceiros: `externos\`, `ferramentas\` e os plugins de teste. Se achar algo, ele para e não gera o zip.
- **Histórico do git:** o script procura segredo em todos os diffs. O e-mail de autor de cada commit vai junto, porque faz parte do commit e só sairia reescrevendo a história. Por isso o pacote deve ficar em lugar privado: um pendrive seu, ou uma pasta do Drive com acesso "Restrito".
- **Fica fora do pacote:** o cache de plugins (`plugins.xml`), o `Settings.xml` do Tracktion e as marcas de sessão de `%APPDATA%\WHYNOT LAB`, o histórico do Claude e as configurações do Windows.
- **Exclusão manual:** os arquivos que você manda deixar de fora com `-Excluir`. Em 20/10 saíram as duas capturas de teste do vídeo (`teste.mp4` e `teste.png`), que pegaram a área de trabalho inteira e não só a janela do app.

### 2e. Pacote dos produtores: só o app, com o piano (`-SoApp -Piano`)

Quem só vai usar o app não precisa de código, SDKs, notas do Claude, plugins de teste nem scripts de programar. Desde a caça de bugs de 21/10:

```
powershell -ExecutionPolicy Bypass -File tools\portatil\montar-pacote.ps1 -SoApp -Piano -Destino C:\Users\<usuário>\Desktop\WHYNOT-LAB-app-<data> -Zip
```

- **`-SoApp`:** o pacote leva só `app\WHYNOT LAB.exe` e o `LEIA-ME.txt` de uso (`tools/portatil/LEIA-ME-app.txt`: extrair o zip antes de abrir, o SmartScreen, os plugins, o "Acesso controlado a pastas" e mandar a pasta inteira do projeto). Sem o bundle, a varredura do histórico do git também fica de fora. Não vai junto com `-Completo`.
- **`-Piano`:** copia a biblioteca do WHYNOT Piano (a pasta de `%APPDATA%\WHYNOT LAB\Bibliotecas` com o `whynot-piano.json`, hoje a `Salamander`, uns 590 MB) para `app\Bibliotecas`. O app procura a biblioteca em três lugares, nesta ordem: a pasta de dados, `%APPDATA%\WHYNOT LAB\Bibliotecas` e a pasta do exe. Por isso, ao lado do exe ela funciona sem copiar nada no PC do produtor. O `_download` (o .tar.xz original) fica de fora. O `CREDITOS.txt` é obrigatório: a licença CC BY 3.0 pede o crédito.
- **LAME:** o script confere se o exe tem o LAME (a marca `LAME3.100` dentro dele) e para se não tiver. Sem o LAME, o app abre, mas o MP3 fica desligado, e no PC do produtor isso vira "não exporta".
- O pacote sem o piano tem uns 22 MB; com o piano, uns 600 MB antes do zip (o WAV comprime pouco). Dá para mandar os dois: o app primeiro e o piano à parte.

## 3. No outro PC: só usar o app

1. Extraia o zip inteiro (botão direito, "Extrair tudo...") e abra `app\WHYNOT LAB.exe`. Aberto de dentro do zip, o Windows tira só o exe para uma pasta temporária, e o piano e os arquivos ao lado dele ficam para trás.
2. Na primeira vez, o SmartScreen avisa que o app não é assinado. Clique em "Mais informações" e depois em "Executar assim mesmo".
3. Instale os plugins VST3 que você usa, aperte F8 e clique em Escanear.

## 4. No outro PC: continuar programando

Dê dois cliques em `preparar-pc.cmd`. Ele roda o `preparar-pc.ps1 -Instalar` e deixa a janela aberta no fim. Os passos:

| Passo | O que faz | Tempo |
|---|---|---|
| Pacote | confere o sha256 de cada arquivo | segundos |
| Sistema | Windows 64 bits, RAM (escolhe os jobs) e espaço livre | segundos |
| Ferramentas | acha ou instala pelo winget: Git, VS 2022 Build Tools com o C++, CMake, Ninja | 10 a 30 min, se instalar |
| Código | clona o bundle em `C:\dev\whynot-lab`, no `main` | segundos |
| Dependências | baixa o Tracktion e o JUCE do GitHub (uns 180 MB) e copia o LAME e o ASIO do pacote | uns minutos |
| Claude Code, atalhos e projetos | copia as notas, a skill, os atalhos e os projetos, sem sobrescrever | segundos |
| Compilar | CMake e Ninja, com o ambiente do `vcvars64.bat` | 10 a 20 min no primeiro build (12 min neste notebook, com `-j 2`, em 20/10) |
| Autoteste | `WHYNOT LAB.exe --self-test` e mostra as falhas | 1 a 2 min |
| Testes (ctest) | compila os alvos de teste (pequenos, sem JUCE) e roda o `ctest` | uns 2 min (o do synth leva meio minuto) |

Espaço: uns 6 GB para o código e o build, mais uns 8 GB se precisar instalar o VS Build Tools.

Se algo falhar, a mensagem diz o quê. Corrija e rode de novo: cada passo pula o que já está pronto.

Para rodar pelo terminal:

```
powershell -ExecutionPolicy Bypass -File .\preparar-pc.ps1 -SoVerificar
```

| Opção | Para quê |
|---|---|
| `-SoVerificar` | mostra o que falta e o que seria feito. Não instala, não baixa, não copia e não compila |
| `-Instalar` | instala pelo winget o que faltar. Sem ela, o script só aponta |
| `-Destino <pasta>` | onde fica o código. Padrão: `C:\dev\whynot-lab`. Use um caminho curto: pastas fundas passam do limite de caminho do Windows no git e no build |
| `-Repositorio <endereço>` | clona de um repositório (GitHub, por exemplo) em vez do bundle |
| `-Jobs <n>` | jobs do build. O padrão sai da RAM: uns 2,5 GB por job, no mínimo 2 |
| `-SemPluginsDeTeste` | não compila os quatro VST3 de teste. Economiza tempo, mas o autoteste acusa uma falha no scan, a não ser que `build\test_plugins` já tenha os plugins, copiados de outro build |
| `-SemCompilar` | para antes do build; também pula o autoteste e o ctest |
| `-SemAutoteste` | pula o autoteste e o ctest |

O `preparar-pc.ps1` também roda de dentro do repositório (`tools\portatil`), sem pacote. Aí o destino é o próprio repositório, e ele serve para conferir o ambiente e compilar.

### Claude Code no outro PC

O Claude Code guarda as notas de cada projeto em `%USERPROFILE%\.claude\projects\<pasta>\memory`. O `<pasta>` é o caminho do projeto com tudo que não é letra ou número trocado por `-`: `C:\dev\whynot-lab` vira `C--dev-whynot-lab`. O `preparar-pc` copia as notas para a pasta certa do destino que você escolher.

Abra o Claude Code na pasta do código. As notas falam de caminhos deste PC (`C:\Users\...\dev\wnl-*`, a trava de build e o quadro das sessões). Lá elas só servem de contexto.

As skills de domínio (JUCE, DSP, validadores de plugin) vêm de plugins do Claude Code. Instale de novo pelo próprio Claude Code. Só a `whynot-agentes` vai no pacote.

## 5. Ida e volta do código

Mudança sem commit não viaja: commite antes de levar.

O `tools/portatil` está no `main` desde 20/10 (467d244). Com um código mais antigo, use o `trocar-codigo.ps1` da pasta do pacote, com `-Projeto <pasta do código>`.

### Pelo pendrive (sem GitHub)

No PC onde você trabalhou, na pasta do código:

```
powershell -ExecutionPolicy Bypass -File tools\portatil\trocar-codigo.ps1 -Exportar E:\whynot-casa.bundle
```

No outro PC, primeiro só para ver:

```
powershell -ExecutionPolicy Bypass -File tools\portatil\trocar-codigo.ps1 -Importar E:\whynot-casa.bundle
```

Os branches do arquivo ficam em `pendrive/<branch>`, e os seus branches não mudam. O script compara cada um:

| Estado | Quer dizer | O que fazer |
|---|---|---|
| igual | nada mudou | nada |
| daqui | só este PC tem commits novos | nada: leve no próximo pendrive |
| atrás | só o arquivo tem commits novos | rodar de novo com `-Avancar` (avança sem merge) |
| novo | o branch só existe no arquivo | `-Avancar` cria o branch |
| divergiu | mudou nos dois PCs | juntar à mão: `git switch <branch>` e `git merge pendrive/<branch>` |

O `-Avancar` não mexe em branch que divergiu nem em branch aberto em outro worktree. Também não mexe no branch aberto se ele tiver mudança sem commit.

### Pelo GitHub (repositório privado)

É o jeito mais simples de manter os dois PCs iguais, e ainda vira o backup que hoje não existe: o projeto só está neste notebook. **Ainda não foi feito.** Criar e publicar o repositório depende do ok do usuário.

Com o repositório criado:
- neste PC: `git remote add origin <endereço>` e depois `git push -u origin --all`;
- no outro PC: `preparar-pc.ps1 -Repositorio <endereço>`.

O LAME e o ASIO continuam fora do git e vêm do pacote ou da seção 2b de `docs/como-rodar.md`.

## 6. Problemas comuns

| Sintoma | Causa e saída |
|---|---|
| "a execução de scripts foi desabilitada neste sistema" | política do Windows. Use o `preparar-pc.cmd` ou `powershell -ExecutionPolicy Bypass -File ...` |
| "O Windows protegeu o computador" ao abrir o app | o exe não é assinado. Clique em "Mais informações" e depois em "Executar assim mesmo" |
| o winget não existe | instale o "Instalador de Aplicativo" pela Microsoft Store |
| o winget pede para aceitar termos | responda S. O script não aceita por você |
| o VS Build Tools já estava instalado, mas sem o C++ | Visual Studio Installer, Modificar, marque "Desenvolvimento para desktop com C++" e rode de novo |
| `'vswhere.exe' is not recognized` durante o build | aviso do `vcvars64.bat`, não atrapalha |
| o build trava ou falha por memória | `-Jobs 2` e feche o navegador. Com 8 GB de RAM a memória livre chega a zero no pico |
| erro de caminho longo no git ou no build | use um destino curto (`C:\dev\whynot-lab`). O script já liga `core.longpaths` |
| o autoteste falha em "plugins de teste" | compile sem `-SemPluginsDeTeste`, ou copie `build\test_plugins` de outro build: o teste do scan usa esses VST3 |
| o pacote acusa arquivo "diferente do original" | a cópia do pendrive veio corrompida. Copie de novo |

## 7. O que foi testado

Neste PC (Windows 11, PowerShell 5.1 e 7), em 16/10/2026:
- os três scripts passam no analisador de sintaxe dos dois PowerShell;
- o `preparar-pc.ps1 -SoVerificar` acha o Git, o VS 2022 Build Tools, o CMake e o Ninja. Também conferi que a chave da pasta de notas do Claude bate com a que o Claude Code criou para este projeto;
- a cadeia inteira rodou com um repositório de teste, nos dois PowerShell. Nada disso tocou o repositório real nem baixou nada:
  - o `montar-pacote` monta o pacote, recusa destino que já existe e recusa branch fora do `main`;
  - o `preparar-pc` usa esse pacote para clonar do bundle, copiar o LAME, o ASIO, as notas e a skill sem sobrescrever e gravar a identidade dos commits;
  - rodar o `preparar-pc` de novo não refaz nada, e um arquivo corrompido no pacote faz ele parar;
  - o `trocar-codigo` exporta, recusa sobrescrever e importa com os estados igual, daqui, atrás, novo e divergiu;
  - o `-Avancar` avança o branch aberto sem merge, cria os novos e não toca no que divergiu;
- o `montar-pacote.ps1 -Listar` no repositório real mostra o plano (uns 95 MB) e recusa. Os motivos: frentes fora do `main` e o exe mais velho que o último commit.

Em 16/10, a pedido do usuário, o zip do `main` 3.4 foi montado, extraído e conferido:
- os arquivos bateram com o sha256;
- o clone do bundle no c905ade;
- o exe abre.

Em 20/10, o build do zero pelo `preparar-pc.ps1`, no worktree `wnl-portatil` do `main` 467d244, com `-j 2`:
- o script não baixou nada, porque o Tracktion, o LAME e o ASIO vieram por junction;
- os plugins de teste foram copiados de outro build (`-SemPluginsDeTeste`);
- 12 min de compilação, sem nenhum aviso; o script inteiro levou 15 min 13 s;
- autoteste 433 ok, saída 0;
- ctest 8/8, com os testes compilados à parte (1 min 14 s). Depois disso, o passo "Testes (ctest)" entrou no script e foi validado por ele mesmo, com o build já pronto: o script inteiro levou 2 min 30 s, com autoteste 433 ok e ctest 8/8;
- a RAM livre mínima foi de 510 MB.

Em 20/10, pacote completo do `main` a1a71f1, pedido pelo usuário:
- montado com `-Completo`, `-Coordenacao` e `-Extras` (o vídeo, sem as duas capturas de teste). O resultado: 15.476 arquivos, zip de 421 MB pelo `tar.exe`, copiado para o Drive depois de a pasta ficar "Restrito";
- dados: 2 trocas nas notas do Claude e varredura sem achados em 70 arquivos de texto;
- a varredura parou na primeira tentativa por dois alarmes falsos no código da Fase 4. Um era um token lido de um cabeçalho, o outro um token inventado de teste. Por isso o padrão genérico passou a exigir valor entre aspas, e veio o `-SegredoRevisado`;
- uma conferência à parte, em todo o pacote, não achou os e-mails da conta nem o id da pasta do Drive. Os únicos links do Drive estão em documentação pública de terceiros (HarfBuzz no JUCE e o manual do CMake);
- **prova offline:** o `preparar-pc` do pacote preparou um código novo em `dev\wnl-offline`, sem junction e sem rede:
  - os 15.475 arquivos conferidos pelo sha256;
  - o clone do bundle;
  - o Tracktion e o JUCE copiados do pacote;
  - build do zero em 14,6 min (`-j 2`) e 0 avisos;
  - autoteste 444 ok e ctest 8/8;
  - `git status` limpo, com o submódulo sem `init`;
  - o script inteiro levou 24 min, junto com outros trabalhos na máquina;
- o primeiro zip, pelo `Compress-Archive`, falhou: a prova lia os mesmos arquivos ao mesmo tempo. Daí a troca pelo `tar.exe`, que comprimiu tudo em 35 s. O zip do `tar` abre com o `Expand-Archive`, a mesma biblioteca do "Extrair tudo" do Windows.

Não testado:
- copiar o CMake e o Ninja do pacote para `C:\dev\ferramentas`: este PC já tem os dois, então essa parte não rodou;
- o `preparar-pc` num PC limpo, com a instalação pelo winget e o download do Tracktion e do JUCE;
- o app num Windows 10.

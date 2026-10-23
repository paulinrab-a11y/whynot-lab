# WHYNOT LAB — Pesquisa de mercado (Fase 1)

Data da pesquisa: 15/10/2026. Objetivo: decidir o que entra no MVP de uma DAW pessoal, estilo FL Studio, para **um produtor de trap que trabalha sozinho**. Tudo aqui é sobre **comportamento público** das DAWs (manuais, changelogs, fóruns, reviews). Nenhum software foi baixado ou inspecionado por dentro.

---

## 1. Como a pesquisa foi feita e onde ela é fraca

- Quatro frentes em paralelo: (1) FL Studio e trap; (2) Ableton Live, Bitwig Studio e Fender Studio Pro (ex-Studio One); (3) Audacity, REAPER, LMMS e Zrythm; (4) temas transversais em KVR e Gearspace, mais a parte técnica de isolamento e scan de plugins.
- Fontes lidas de fato: forum.image-line.com, forum.ableton.com, fórum do KVR (inclusive os subfóruns de Bitwig, Tracktion e Studio One), manuais e release notes oficiais (Image-Line, Ableton, Bitwig, Fender, Steinberg, JUCE, Tracktion), GitHub, Sound on Sound, discourse.ardour.org, forums.steinberg.net.
- **Reddit não entrou.** O mecanismo de busca recusa o domínio, o leitor de páginas é bloqueado e o navegador embutido do app também bloqueia reddit.com por política de segurança. Não tentei contornar. A opinião de r/FL_Studio, r/trapproduction, r/ableton, r/audacity, r/WeAreTheMusicMakers e r/edmproduction é **desconhecida**, não neutra. Isso pesa mais no tema "roteamento confuso", porque iniciantes perdidos costumam postar no Reddit.
- **Gearspace, VI-Control e alguns sites de suporte devolvem 403.** Desses só vimos títulos e trechos de busca, marcados como "(só snippet)" nas tabelas.
- **As contagens são de amostra, não de prevalência.** "Nº de threads" é quantas threads distintas os pesquisadores acharam e abriram (ou viram no snippet, quando marcado). Fórum oficial tem viés para quem tem problema ou pedido. Uma thread de 120 posts conta 1, igual a uma de 0 respostas. Entre colunas pode haver sobreposição de 1–2 threads (a mesma thread do fórum da Image-Line apareceu para duas frentes).
- Citações: só paráfrase; nenhum trecho longo copiado.

---

## 2. Versões atuais (out/2026) que mudam o quadro

| Produto | Versão | Observação | Fonte |
|---|---|---|---|
| FL Studio | 2026 (2026.1.7, set/2026) | Backup de projeto no FL Cloud, Audio Logger, "Remix a song" (stems), Gopher que controla recursos | [manual, What's new](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_new.htm) |
| Ableton Live | 12.4.6 (15/09/2026) | Sem Live 13 anunciado | [release notes](https://www.ableton.com/en/release-notes/live-12/) |
| Bitwig Studio | 6.1 (26/08/2026) | 6.0 (mar/2026) trouxe alias clips, tonalidade do projeto, automação refeita | [notas 6.0](https://downloads.bitwig.com/6.0/Release-Notes-6.0.html), [6.1](https://downloads.bitwig.com/6.1/Release-Notes-6.1.html) |
| Fender Studio Pro | 8.1 (09/06/2026) | Nome novo do Studio One desde 13/01/2026; "songs" viraram "sessions", Project Page virou Mastering Page | [SOS](https://www.soundonsound.com/news/studio-one-becomes-fender-studio-pro), [SOS 8.1](https://www.soundonsound.com/news/fender-studio-pro-81-arrives) |
| JUCE | 9.0.4 (12/10/2026) | 9.0 saiu em 21/07/2026; a linha 8 ainda recebe correções. **Na Fase 2 precisa confirmar qual versão o Tracktion Engine aceita** | [releases](https://github.com/juce-framework/JUCE/releases), [KVR](https://www.kvraudio.com/news/juce-9-now-available-67802) |
| SDK VST3 | 3.8.0 (20/10/2025) | **Agora licença MIT** | [Steinberg](https://forums.steinberg.net/t/vst-3-8-0-sdk-released/1011988) |
| REAPER | 7.82 (04/10/2026) | Instalador de ~20 MB, roda portátil | [reaper.fm](https://www.reaper.fm/) |
| Audacity | 4.0.1 (30/09/2026) | Interface nova; saiu sem macros, mix-and-render e exportar múltiplos, e há usuários ficando na 3.7 | [changelog](https://www.audacityteam.org/changelog/) |
| LMMS | 1.3.0-alpha.2 (06/09/2026) | A última versão estável é a 1.2.2, de **julho de 2020** | [releases](https://github.com/LMMS/lmms/releases/tag/v1.3.0-alpha.2) |
| Zrythm | 2.0.0-alpha.6 (04/10/2026) | Reescrita de C/GTK para C++/Qt6/JUCE; projetos da v1 não abrem na v2 | [releases](https://github.com/zrythm/zrythm/releases) |

---

## 3. O que cada DAW faz bem e vale trazer

### 3.1 FL Studio — a referência de fluxo

| Recurso | Por que importa para trap | Fonte |
|---|---|---|
| **Padrões + Channel Rack**: todos os padrões compartilham os mesmos canais; um padrão vira clipe na playlist | Esboçar um beat inteiro em minutos | [manual Channel Rack](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/channelrack.htm) |
| **Step sequencer com Graph Editor por passo**: velocity, pan, pitch, release, shift e **Rep** (repetição/roll), swing por canal | Hi-hats e rolls rápidos | idem |
| **Piano roll**: ghost notes (inclusive de padrões sobrepostos, desde a 24.2), slide/portamento por nota, ferramentas Chop, Strum, Flam, Arpeggiate, Claw machine, Randomize | Escrever 808 vendo a melodia; variações de groove | [manual Piano roll](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll.htm), [menu](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_menu.htm) |
| **Note repeat** no piano roll e no step sequencer (24.1.1, jul/2024) | Pedido que liderou enquete de 417 votos em 2023 | [fórum, 24.1.1](https://forum.image-line.com/viewtopic.php?p=1956169), [enquete](https://forum.image-line.com/viewtopic.php?t=305378) |
| **Playlist** com clipes de padrão, áudio e automação em qualquer faixa | Arranjo livre | [manual Playlist](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm) |
| **Mixer**: 500 inserts (2025.1), sends, sidechain send, 10 slots de FX por insert, PDC; **Ctrl+L** liga canal→trilha | Bus de drums, sidechain do 808 | [manual Mixer](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm) |
| **Edison**: editor de áudio dentro de um slot do mixer | Chop rápido de sample | [manual Edison](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Edison.htm) |
| **Save new version** e "Revert to last backup" | Voltar a versões do beat | [manual File menu](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/menu_file.htm) |

Limitação importante descoberta: **slide e portamento do piano roll só funcionam com instrumentos nativos do FL**, não com VST. A equipe confirmou em 2017 e 2020 ([2017](https://forum.image-line.com/viewtopic.php?t=178776), [2020](https://forum.image-line.com/viewtopic.php?t=219259)). Para trap, onde o glide do 808 é assinatura, isso é uma oportunidade.

### 3.2 Ableton Live 12

| Recurso | Por que importa | Fonte |
|---|---|---|
| **Session View**: grade de clipes e cenas, gravação da performance na timeline | Montar loops antes do arranjo | [manual](https://www.ableton.com/en/live-manual/12/session-view/) |
| **Capture MIDI**: recupera o que você tocou sem ter apertado gravar | Não perder a melodia boa da jam | [manual](https://www.ableton.com/en/live-manual/12/recording-new-clips/) |
| **Warp**: encaixar áudio no BPM, com marcadores | Flip de sample | [manual](https://www.ableton.com/en/live-manual/12/audio-clips-tempo-and-warping/) |
| **Drum Rack** com choke groups e **Simpler** com fatiamento por transiente | Hat aberto/fechado; chop de loop | [Drum Rack](https://www.ableton.com/en/live-manual/12/instrument-drum-and-effect-racks/) |
| **Comping** de áudio e de MIDI em take lanes, com fades de 4 ms nas emendas | Gravar vocal/ad-lib | [manual Comping](https://www.ableton.com/en/live-manual/12/comping/) |
| **MIDI Tools**: Chop, Strum, Ornament (flam), Rhythm, Euclidean, Stacks, cientes de escala | Rolls e acordes rápidos | [manual](https://www.ableton.com/en/live-manual/12/midi-tools/) |
| **Scale Mode** por clipe (Fold/Fit to Scale) | Não errar a tonalidade | [manual](https://www.ableton.com/en/live-manual/12/editing-midi/) |
| **Undo History** navegável (12.1) | Voltar a qualquer ponto — mas **zera ao reabrir o Set** | [manual](https://www.ableton.com/en/live-manual/12/managing-files-and-sets/) |

### 3.3 Bitwig Studio 6

| Recurso | Por que importa | Fonte |
|---|---|---|
| **Hospedagem de plugins com 5 modos** (Within Bitwig, Together, By manufacturer, By plug-in, Individually), mais exceção por plugin; no crash, a interface vira aviso e há **Reload Plug-in** | Um plugin quebrado não derruba o beat | [manual](https://www.bitwig.com/userguide/latest/vst_plug-in_handling_and_options) |
| **Alias clips** (6.0): vários clipes compartilham o mesmo padrão | Editar o 808 uma vez e atualizar todas as cópias (é o comportamento natural dos padrões do FL) | [notas 6.0](https://downloads.bitwig.com/6.0/Release-Notes-6.0.html) |
| **Tonalidade do projeto** + Snap to Key no piano roll (6.0) | Tonalidade global barata e útil | idem |
| Modulators e The Grid | Poderosos, mas fora do fluxo de beat | [manual](https://bitwig.com/userguide/latest/modulator) |

### 3.4 Fender Studio Pro 8 (ex-Studio One)

| Recurso | Por que importa | Fonte |
|---|---|---|
| **Pattern editor de bateria**: lanes com comprimento e resolução próprios (polirritmia), por passo: velocity, **repeat**, delay e **probability** | A melhor referência de step sequencer além do FL | [manual Patterns](https://fenderstudiopromanual.fender.com/en/Content/Editing_Topics/Patterns.htm) |
| **Impact**: arrastar um loop de bateria com Shift fatia os golpes pelos pads | Do loop ao kit em um gesto | [manual Impact](https://fenderstudiopromanual.fender.com/en/Content/Built-In_Instruments_Topics/Impact.htm) |
| **Scratch Pad**: linha do tempo alternativa salva dentro da sessão | Testar arranjos sem bagunçar o principal | [manual](https://fenderstudiopromanual.fender.com/en/Content/Arranging_Topics/ScratchPad.htm) |
| **Arranger Track**: mover/copiar seções inteiras (todas as faixas, andamento, automação) | Reorganizar verso/refrão rápido | [manual](https://fenderstudiopromanual.fender.com/en/Content/Arranging_Topics/ArrangerTrack.htm) |
| **Safety Options** após crash: nomeia o plugin suspeito e permite abrir sem ele; blocklist com reset (só snippet) | Recuperar o projeto quando um plugin quebra | [KB PreSonus](https://support.presonus.com/hc/en-us/articles/30765230983821-Studio-One-Pro-7-Startup-and-Recovery-Options-in-Studio-One) |
| **Save New Version** com descrição e Restore Version (confirmado em versões antigas; não confirmado na 8) | Versões nomeadas do beat | [pcaudiolabs](https://pcaudiolabs.com/save-as-vs-versions-in-studio-one-4/) |

### 3.5 REAPER 7

| Recurso | Por que importa | Fonte |
|---|---|---|
| **Leveza**: ~20 MB, versão portátil, abre rápido | Meta de footprint para a WHYNOT | [sobre](https://www.reaper.fm/about.php) |
| **Ações, atalhos e macros totalmente configuráveis**, conjuntos de atalhos em camadas (v7), scripts Lua/EEL/Python | Produtor solo vive de atalho; macros de ações compostas | [sobre](https://www.reaper.fm/about.php), [Production Expert](https://www.production-expert.com/production-expert-1/reaper-7-we-take-a-look-at-new-features) |
| **"Run as" por plugin**: Native only / Separate process / Dedicated process | Isolamento opt-in, o modelo mais simples de copiar (detalhes na seção 6) | [SOS](https://www.soundonsound.com/techniques/project-recovery) |
| **Comping por lanes fixas com swipe** (v7): arrastar sobre a lane escolhe a take, A/B em um clique | Fluxo de comping mais elogiado pela imprensa | [Production Expert](https://www.production-expert.com/production-expert-1/reaper-7-we-take-a-look-at-new-features) |
| **Backups com data e hora** (até 50 por padrão) e **"Open with FX offline"** para projeto que trava ao abrir | Rede de segurança barata | [SOS recuperação](https://www.soundonsound.com/techniques/project-recovery) |
| Roteamento livre (qualquer faixa recebe de qualquer outra) | Poderoso, mas é justamente onde a curva de aprendizado pesa (seção 4) | [reaper.fm](https://www.reaper.fm/) |

O que o REAPER **não** tem: step sequencer e padrões estilo FL. Há 9 threads no fórum da Cockos pedindo isso (só snippet), o que confirma que esse fluxo é o diferencial que a WHYNOT quer.

### 3.6 Audacity 4

| Recurso | Por que importa | Fonte |
|---|---|---|
| **Edição não destrutiva por clipe**: alças no topo aparam sem perder áudio (arrastar de volta recupera); envelope de volume dentro do clipe (4.0) | Cortar sample e vocal sem destruir o arquivo | [aparar/esticar](https://www.audacityteam.org/manual/clips/trim-and-stretch), [4.0.0](https://www.audacityteam.org/changelog/4.0.0/) |
| **Separação clara entre "aplicar" (destrutivo) e efeito em tempo real**; apagar com ou sem fechar o buraco (ripple explícito, 4.0) | Usuário sempre sabe o que é reversível | [guia 3→4](https://www.audacityteam.org/manual/new-in-audacity-4/audacity-3-to-4-transition-guide) |
| **Detecção de tempo de loop importado** e esticar clipe ao andamento (3.4/3.5) | Encaixar loop de terceiros no BPM | [3.5](https://www.audacityteam.org/changelog/3.5.0/) |
| Edição espectral e remoção de ruído com modo **Residue** (ouvir o que vai ser removido) | Bom padrão de usabilidade | [manual](https://manual.audacityteam.org/man/noise_reduction.html) |
| Zoom até o nível de amostra, snap em amostra, ferramenta Split (tecla S) | Precisão de edição | [4.0.0](https://www.audacityteam.org/changelog/4.0.0/) |

Alerta vindo do Audacity: efeitos em tempo real empilhados com latência dessincronizaram a reprodução ([#3221](https://github.com/audacity/audacity/issues/3221), [#3616](https://github.com/audacity/audacity/issues/3616) ainda aberta). **Compensação de latência (PDC) é requisito desde o primeiro dia**.

### 3.7 LMMS e Zrythm: lições do que deu errado

São os dois projetos abertos mais próximos do que a WHYNOT quer ser, e os dois estagnaram.

**LMMS**
- O editor de padrões acertou o básico: uma linha de passos por instrumento, e um padrão colocado várias vezes atualiza todas as cópias ([manual](https://docs.lmms.io/user-manual/navigation/3.8.md)).
- Errou no modelo de dados: todos os padrões compartilham os mesmos instrumentos, e **não grava nem edita áudio gravado** ([Song Editor](https://docs.lmms.io/user-manual/navigation/song-editor.md)).
- Seis anos sem versão estável (1.2.2 em 2020). A alpha de 2026 avisa que o formato de projeto é de mão única e pode quebrar arquivos.
- Formatos de plugin atrasados: VST3 pedido desde 2018 (31 +1), CLAP, Qt6, PDC, todos ainda abertos.
- A issue que pede **projeto em JSON legível** tem 15 +1 ([#3981](https://github.com/LMMS/lmms/issues/3981)), o que reforça a escolha do `.wnl` em JSON.

**Zrythm**
- Uma pessoa fez a imensa maioria do trabalho (7.035 contribuições contra 242 do segundo, no espelho do GitHub).
- Apostou em GTK, que o próprio autor diz performar mal fora do Linux. Windows ficou praticamente sem suporte na v1.
- Reescreveu tudo no meio do caminho (v2 em C++/Qt6/JUCE), sem compatibilidade de projeto, e a v2 já passou do prazo prometido.

**O que isso significa para a WHYNOT**
1. Ter algo **estável e usável cedo**, em vez de muitos recursos pela metade.
2. **Não trocar de toolkit** no meio. JUCE roda nativo no Windows, que é a sua máquina.
3. Formato de projeto com **versão de esquema e migração** desde o primeiro arquivo, nunca de mão única.
4. Hospedar **VST3 desde o começo**; o LMMS mostra o custo de adiar formato de plugin.
5. Gravar e editar áudio desde o MVP, nem que seja o básico.
6. Nada de telemetria (o episódio de privacidade do Audacity em 2021 rendeu 535 comentários e um fork).

---

## 4. Reclamações e pedidos recorrentes

Contagem por fonte (threads distintas achadas; "S" = só snippet). Veredito considera volume, recência e se o problema ainda existe.

Colunas: **IL** = fórum da Image-Line (FL); **Abl** = fórum da Ableton; **KVR+** = KVR, Gearspace e outros, incluindo os subfóruns de Bitwig e Fender; **Abertos** = REAPER (fórum da Cockos, todos só snippet) · Audacity (GitHub + fórum) · LMMS (GitHub) · Zrythm (fórum + GitLab).

| Tema | IL | Abl | KVR+ | Abertos (REAPER · Audacity · LMMS · Zrythm) | Período | Veredito | Relevância trap solo |
|---|---|---|---|---|---|---|---|
| **Plugin que trava derruba a DAW** | 10 (6 pedem sandbox) | 4 | 11 (9 + 2S); Bitwig 3; Fender 2 + 2S | 7S · 5 · 23 issues · 4 | 2012–2026 | **Confirmado, forte e atual.** O FL responde que o modo bridged é o sandbox; Ableton, Fender, Audacity e LMMS não isolam; até o Bitwig teve o motor derrubado por bug de CLAP em set/2026 | **Alta** |
| **Scan de plugins lento / blacklist sem saída** | 6 (casos de 10 a 30 min, inclusive FL 2026) | 4 (1 direto) | 10 (7 + 3S); Bitwig 1 | 8S · 7 · 0 · 3 | 2011–2026 | **Confirmado.** Causas comuns: arquivos que não são plugin nas pastas, demos expirados reescaneados toda vez, falta de blacklist, blacklist que não deixa reativar, Waves. Pedido explícito no Audacity: pular e ter tempo limite por plugin ([#3802](https://github.com/audacity/audacity/issues/3802)) | **Média/alta** (depende de quantos plugins você tem) |
| **Comping de takes** | 10 (pedido mais repetido; planejado em 2022, não entregue) | 3 (pré-Live 11) | 7 (3 + 4S); Bitwig MIDI 4 | 9S (atritos das lanes, não falta) · 0 · 0 · 0 | 2011–2026 | **Confirmado no FL; resolvido no Live, Fender e REAPER 7.** No Bitwig falta comping de MIDI | **Média** se você grava vocal; baixa se só faz beat |
| **Roteamento confuso** | 8 | 3 + 1S | 13 (3 + 10S, quase todos dúvida de iniciante); Bitwig 3 | 10S (4 com "confusão" no título) · 0 · 3 · 0 | 2008–2026 | **Parcial, com causas concretas** (ver lista abaixo da tabela) | **Alta** |
| **Pastas / grupos de faixas** | 6 (falta aninhamento) | 3 (já funciona) | 9 (4 + 5S); Fender 1 | 7S · 0 · 1 · 0 | 2011–2026 | **Parcial.** Dor real no FL (falta pasta e aninhamento). No REAPER e no KVR a confusão é a pasta agir como bus sem deixar isso visível | **Média** |
| **Edição de áudio fraca** | 7 (warp markers, clip FX, fades) | 1 + 3S (antigos) | 7 (2 + 5S); Bitwig 4 (ARA) | 4S (espectral) · regressões da 4.0 · 2 · 0 | 2008–2026 | **Parcial.** A queixa real é edição cirúrgica ou em lote; a DAW basta para o resto. O LMMS sofreu por não editar áudio gravado | **Média** |
| **Versionamento / histórico** | 7 | 1 + 4S | 8 (5 + 3S) | 7S · 0 · 1 (projeto em JSON, 15 +1) · 1 | 2011–2026 | **Parcial.** Desejo antigo de "voltar a qualquer versão"; consenso cético com git, porque o arquivo de projeto muda inteiro a cada save (diffs ruidosos no REAPER) e um merge pode corromper o estado de plugin | **Alta** (barato e evita perda) |
| **Colaboração** | 5 (0–3 respostas cada) | 1 + 3S (antigos) | 5 (3 + 2S); o Splice Studio fechou em 2023 | 6S · 3 (bugs da nuvem) · 0 · 0 | 2008–2026 | **Fraco** | **Baixa** |
| **Perda de projeto por crash / autosave** | — | — | 7 (3 + 4S) | REAPER oferece backups com data e "abrir com FX offline" | 2021–2023 | **Confirmado.** O Live recupera pelo histórico de undo e pode perder estado interno de plugin | **Alta** |
| **Samples faltando ao reabrir** | FAQ oficial | — | 9 (3 + 6S) | — | 2017–2024 | **Confirmado.** Caminhos absolutos quebram quando a pasta muda | **Alta** |
| **PDC / latência** | 4 (bug com automação em 2025, 38 posts) | — | 10 (4 + 6S) | 5S · 5 · 1 · 0 | 2013–2026 | **Confirmado.** Três causas: plugin que declara latência errada; automação ou sidechain não compensados; cadeia de efeitos em tempo real dessincronizando (Audacity) | **Média/alta** |
| **UI em 4K / HiDPI** | 4 (escala de plugins) | 2 (antigos) | 8 (4 + 4S) | 5S · 18 issues · 2 (uma aberta há 10 anos, 100 coment.) · 0 | 2014–2026 | **Confirmado**, sobretudo plugins de terceiros | **Média** (tela grande de estúdio) |
| **Falta de step sequencer / padrões estilo FL** | — | 1 + 1 tangente | — | 9S · — · — · — | 2009–2025 | **Confirmado** nas DAWs que não têm (REAPER, Live) | **Alta** (é o diferencial da WHYNOT) |
| **Congelar / bounce com um clique** | 4 + 6S (workaround de 21 passos em 2025) | — | — | — | 2010s–2026 | **Confirmado no FL** | **Alta** |
| **Export de stems selecionáveis** | 6 | — | — | — | 2023–2024 | **Confirmado.** "Split mixer tracks" exporta tudo, inclusive trilhas vazias | **Alta** (entrega para cantor/engenheiro) |
| **Piano roll do Live fraco vs FL** | — | 4 (2009–2021) | blogs de 2026 (fonte fraca) | — | 2009–2021 | **Parcial**: o Live 12 cobriu boa parte | — (confirma que o piano roll do FL é o modelo) |
| **CPU** | 4 | 3 + 1S | 9 (2 + 7S) | — | 2018–2026 | Anedótico, sem medição | **Média** |
| **Gerenciamento de samples** | — | — | 5 (2 + 3S) | — | 2024–2025 | **Confirmado**, volume baixo | **Alta** (kits e one-shots são o centro do trap) |
| **Release que remove recurso / quebra projeto** | — | — | — | — · 12 queixas na 4.0 · alpha de mão única · v1→v2 incompatível | 2026 | **Confirmado** nos três projetos abertos | **Alta** como lição de processo |
| **Mac instável (FL)** | 5 | — | — | — | 2024–2025 | Confirmado | Baixa (você está no Windows) |

**De onde vem a confusão de roteamento** (o que a WHYNOT precisa resolver):
1. No FL, o Fruity Send aponta para o destino **pela posição do canal**: mexer nas rotas redireciona o envio sem aviso ([fev/2026](https://forum.image-line.com/viewtopic.php?p=2048053)).
2. No FL, um canal sem trilha de mixer vai para o Master em silêncio, e no menu a palavra "channels" quer dizer Channel Rack.
3. Montar sidechain exige vários cliques entre o mixer e a janela do plugin, e a fonte pode mudar se outro roteamento mudar ([2025](https://forum.image-line.com/viewtopic.php?t=332105)).
4. No Live, o menu de saída deixa escolher o grupo na seção errada e o resultado é silêncio ([2024](https://forum.ableton.com/viewtopic.php?p=1825262)).
5. No REAPER, a pasta cria um envio implícito que a matriz de roteamento não mostra. No LMMS, não dá para ver as conexões do mixer de relance ([#4181](https://github.com/LMMS/lmms/issues/4181)).
6. Plugins com várias saídas criam dezenas de canais no mixer (Bitwig, Live).

O padrão comum: **o roteamento existe, mas não está visível onde o usuário está olhando**, e às vezes muda sozinho.

---

## 5. Necessidades específicas de trap

| Necessidade | O que a pesquisa mostrou | Fonte |
|---|---|---|
| **808 com glide/slide** | 6 threads (2017–2025). No FL o slide só funciona com instrumento nativo; plugins de terceiros não recebem | [2017](https://forum.image-line.com/viewtopic.php?t=178776), [2025](https://forum.image-line.com/viewtopic.php?t=333059) |
| **Hi-hat rolls / note repeat** | Enquete de 417 votos (2023); FL entregou em jul/2024; Studio One tem repeat e probability por passo | [enquete](https://forum.image-line.com/viewtopic.php?t=305378), [Patterns](https://fenderstudiopromanual.fender.com/en/Content/Editing_Topics/Patterns.htm) |
| **Sidechain kick→808** | 4 threads (2022–2026), incluindo "Sidechaining Sucks" (2025); o pedido é um seletor de fonte no próprio compressor | [2025](https://forum.image-line.com/viewtopic.php?t=332105) |
| **Afinação do 808** | Só tutoriais; nenhuma thread de dor. FL 2025.1 passou a permitir ignorar a root note do sample | [2025.1](https://www.image-line.com/fl-studio-news/history/fl-studio-2025) |
| **Bounce/freeze rápido** | Sem solução de um clique no FL | [2025](https://forum.image-line.com/viewtopic.php?t=335849) |
| **Stems para cantor/engenheiro** | Escolher quais trilhas exportar, com prefixo | [2024](https://forum.image-line.com/viewtopic.php?t=324793) |
| **Tag de produtor (MP3 com tag, WAV sem)** | 1 thread; evidência fraca | [2024](https://forum.image-line.com/viewtopic.php?p=1924874) |
| **Choke de hat aberto/fechado** | Drum Rack (Live) e Impact (Fender) têm choke groups | [Drum Rack](https://www.ableton.com/en/live-manual/12/instrument-drum-and-effect-racks/) |

---

## 6. Isolamento e scan de plugins (resumo técnico)

| DAW | Como isola | No crash | Fonte |
|---|---|---|---|
| **Bitwig** | 5 modos globais, de "dentro do motor" a "um processo por instância", com lista de exceções por plugin | A interface do plugin vira aviso; Reload Plug-in / Reload All | [manual](https://www.bitwig.com/userguide/latest/vst_plug-in_handling_and_options) |
| **REAPER** | Por plugin, no FX Browser: Run as Automatic (padrão) / Separate process / Dedicated process / Native only | Separate: cai o processo compartilhado, REAPER sobrevive; Dedicated: só aquele plugin cai (semântica descrita por blog de comunidade de 2012, não pela Cockos) | [guia oficial 6.66–6.70](https://dlx.reaper.fm/userguide/REAPER%20main%20changes%20666%20to%20670.pdf), [reaper.blog](https://reaper.blog/2012/02/run-plugin-as-dedicated-process/) |
| **FL Studio** | "Make bridged" por plugin; custo declarado de ~1–2% de CPU por plugin | Protege o FL, mas o próprio bridge gera queixas | [manual Wrapper](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/wrapper.htm) |
| **Ableton** | Sem sandbox | Plugin que crasha no **scan** é apontado na reabertura; segundo crash o marca indisponível | [manual](https://www.ableton.com/en/live-manual/12/using-plug-ins/) |
| **Cubase / Fender** | Sem sandbox | Blocklist com botão de reativar | [Cubase](https://archive.steinberg.help/cubase_pro/v12/en/cubase_nuendo/topics/installing_and_managing_plugins/installing_and_managing_plugins_plug-in_manager_reactivating_from_blacklist_t.html) |
| **Ardour** (contraponto) | Recusa sandbox de propósito: troca de contexto entre processos não escala em sessão grande de baixa latência | — | [ardour.org](https://ardour.org/plugins-in-process.html) |

**O que JUCE e Tracktion já dão de graça:**
- JUCE: `PluginDirectoryScanner` com *dead man's pedal* (arquivo que registra qual plugin estava sendo escaneado quando o processo morreu e o põe na blacklist), `KnownPluginList` com blacklist e `CustomScanner`, e o exemplo oficial AudioPluginHost com scan em processo filho ([docs](https://docs.juce.com/master/classjuce_1_1PluginDirectoryScanner.html), [HostStartup.cpp](https://github.com/juce-framework/JUCE/blob/master/extras/AudioPluginHost/Source/HostStartup.cpp)).
- Tracktion Engine: scan em processo filho pronto (`PluginScanHelpers`), **desligado por padrão** (`EngineBehaviour::canScanPluginsOutOfProcess()` devolve `false`). Não traz blacklist persistente nem timeout: um plugin que trava sem crashar (diálogo de licença) segura o scanner ([PluginScanHelpers](https://github.com/Tracktion/tracktion_engine/blob/develop/modules/tracktion_engine/plugins/tracktion_PluginScanHelpers.h), [EngineBehaviour](https://github.com/Tracktion/tracktion_engine/blob/develop/modules/tracktion_engine/utilities/tracktion_EngineBehaviour.h)).
- Nenhum dos dois oferece **execução** isolada de plugin: isso seria trabalho nosso (fase seguinte ao MVP).

**Formatos:**
- **VST3**: hospedagem nativa no JUCE; SDK agora MIT.
- **VST2**: a Steinberg encerrou o SDK em 2018 e, em 2020, disse que não licencia VST2 nem para host novo ([fórum Steinberg](https://forums.steinberg.net/t/can-i-create-vst-2-support-for-host-application-vst-3-license/202012)). O JUCE exige o SDK VST2 para hospedar VST2 e não o inclui. Cabeçalhos feitos por engenharia reversa (VeSTige) receberam DMCA em 2018. **Detalhe completo das opções vai em `docs/licencas.md` (Fase 2).**
- **CLAP**: MIT, adotado por Bitwig, FL, REAPER e Fender. **O JUCE não hospeda CLAP** e o clap-juce-extensions só serve para criar plugins, não hospedar ([README](https://github.com/free-audio/clap-juce-extensions)). Hospedar CLAP exige escrever um formato próprio.
- **LV2 no Windows**: o JUCE hospeda, mas quase não há plugins LV2 para Windows; o Surge XT, por exemplo, só oferece LV2 no Linux ([Surge](https://surge-synthesizer.github.io/)).
- **AU**: só macOS.

---

## 7. Tabela final: o que entra no MVP

Esforço estimado já contando com o que JUCE + Tracktion Engine entregam prontos. **P** = até uns dias, **M** = 1–2 semanas, **G** = mais de 2 semanas.

| # | Ideia | Quem pede / de onde vem | Esforço | Decisão |
|---|---|---|---|---|
| 1 | Channel rack com step sequencer de 16 passos, velocity e **roll por passo** | FL (fluxo base); enquete de 417 votos por note repeat | M | **MVP** |
| 2 | Piano roll: desenhar, mover, redimensionar, apagar, velocity, snap, pintar, ghost notes | FL; usuários do Live pedem o piano roll do FL | M | **MVP** |
| 3 | **Tonalidade do projeto** com destaque de escala no piano roll | Bitwig 6, Live 12, FL | P | **MVP** |
| 4 | **Slide/glide por nota que funcione no sampler nativo** (808) | Trap; limitação do FL | P–M | **MVP** (só o sampler nativo) |
| 5 | Glide para plugins de terceiros, gerando pitch bend/portamento MIDI | Limitação do FL que ninguém resolveu | M | Depois (v1.1) |
| 6 | Playlist com padrões e clipes de áudio | FL | M | **MVP** |
| 7 | Mixer com volume, pan, mute/solo, medidores, 8 inserts | Pedido do projeto | M | **MVP** |
| 8 | **Roteamento à prova de confusão**: cada canal novo ganha trilha própria no mixer por padrão; rota guardada por ID fixo (nunca por posição); etiqueta "saída →" visível em todo canal e trilha; sidechain escolhido no próprio slot do efeito | FL (Fruity Send por posição, canais indo ao Master em silêncio, sidechain em vários cliques); Live (dropdown de saída) | M | **MVP** |
| 9 | **Scan de plugins em processo separado + blacklist persistente + timeout + cache por data do arquivo** | Todas as DAWs; Tracktion já traz metade | M | **MVP** |
| 10 | Ao reabrir após crash: mostrar qual plugin estava ativo e oferecer **abrir o projeto com plugins desligados** | Ableton (pedido), Fender (Safety Options), REAPER ("Open with FX offline") | P | **MVP** |
| 11 | Execução isolada por plugin, opt-in ("rodar em processo separado") | Bitwig/REAPER/FL como modelo; pedido forte no Live e no Fender | G | Depois (fase pós-MVP, documentada na arquitetura) |
| 12 | **Autosave frequente com data e hora + versões numeradas do `.wnl`** ("Salvar nova versão", lista para restaurar) | KVR, FL; REAPER e Studio One como modelo | P–M | **MVP** |
| 12b | **Formato `.wnl` com versão de esquema e migração**, chaves em ordem fixa e estado binário de plugin em arquivo separado, para o diff entre versões fazer sentido | LMMS (pedido de projeto em JSON, 15 +1; alpha que quebra projetos), Zrythm (v1→v2 incompatível), REAPER (diffs ruidosos no git) | P | **MVP** (decisão de arquitetura) |
| 12c | Compensação de latência de plugin (PDC) funcionando desde o primeiro dia, incluindo automação e sidechain | FL, Audacity, REAPER | P (o Tracktion Engine faz; nós testamos) | **MVP** |
| 13 | **Caminhos de sample relativos + "coletar arquivos no projeto"** + busca automática de samples faltando | KVR (9 threads) | P–M | **MVP** |
| 14 | Exportar WAV e MP3 do master; **stems escolhendo as trilhas**, com prefixo | Pedido do projeto; FL (6 threads) | P–M | **MVP** |
| 15 | Exportar MIDI | Pedido do projeto | P | **MVP** |
| 16 | Freeze/bounce de canal com um clique | FL (workaround de 21 passos) | P (o Tracktion já tem `Track::setFrozen`, confirmado na Fase 2) | Depois (v1.1) |
| 17 | Edição de áudio básica e **não destrutiva** no clipe: aparar pelas bordas (recuperável), cortar, fade, ganho, reverter | Todas; Audacity como modelo; LMMS como contraexemplo | M | **MVP** (só o básico) |
| 17b | Gravação básica de áudio e MIDI | Transporte pedido no projeto; o LMMS sofreu por não gravar | M | **MVP** |
| 18 | Warp / encaixe de sample no BPM, fatiar por transiente | Live, Fender (Impact) | G | Depois |
| 19 | Choke groups (hat aberto/fechado) e probabilidade por passo | Live, Fender | P | Depois (v1.1) |
| 20 | Capture MIDI (guarda o que você tocou sem gravar) | Live; usuários de Bitwig sentem falta | P | Depois (v1.1) |
| 21 | Comping de takes de áudio | FL (pedido mais repetido) | M–G | Depois: só vale se você grava vocal |
| 22 | Pastas / grupos aninhados de faixas | FL | M | Depois |
| 23 | Scratch Pad / arranjo alternativo | Fender | M | Depois |
| 24 | Navegador de arquivos e de plugins com busca, favoritos e categoria; prévia de sample | Pedido do projeto; gerenciamento de samples (KVR) | M | **MVP** |
| 25 | UI escalável para tela grande (zoom global da interface) | HiDPI em todas as DAWs | P | **MVP** |
| 26 | Atalhos configuráveis com preset parecido com o FL | Pedido do projeto | P–M | **MVP** |
| 27 | Toda ação da interface como comando (base do MCP) | Pedido do projeto | M | **MVP** (arquitetura); servidor MCP na Fase 4 |
| 28 | VST3 | — | P (JUCE) | **MVP** |
| 29 | CLAP | Adoção crescente, mas quase todo plugin CLAP também existe em VST3 | G (formato próprio) | Depois |
| 30 | Separação de stems por IA | FL, Live, Fender | G | Depois (se você trabalhar muito com sample) |
| 31 | Tag de produtor automática no MP3 | 1 thread | P | Depois |
| 32 | Macros: encadear comandos e dar um atalho a eles | REAPER | P (sai quase de graça da camada de comandos) | Depois (v1.1) |
| 33 | Edição espectral e remoção de ruído | Audacity, REAPER 7.53 | G | Depois (use um plugin) |

### Cortes (não fazem sentido para um produtor de trap sozinho, pelo menos agora)

- **Colaboração em tempo real**: pouca demanda, o Splice Studio fechou, e você trabalha sozinho. Exportar stems e um pacote com samples resolve.
- **Session View / clip launcher completo**: o fluxo de padrões do FL já cobre o "montar loop antes do arranjo"; seria uma segunda forma de tocar o projeto, com custo alto.
- **Modulators e The Grid (Bitwig)**: poderosos, fora do fluxo de beat.
- **Página de masterização**: use um plugin no master.
- **VST2**: sem caminho legal limpo; detalhes na Fase 2.
- **LV2**: quase nenhum plugin útil no Windows.
- **AU**: você está no Windows; fica para quando houver build de Mac.
- **Undo persistente entre sessões**: ninguém pediu; as versões numeradas resolvem o mesmo problema com muito menos risco.
- **Subprojetos (REAPER)**: renderizar a cada save custa mais do que ajuda para quem trabalha sozinho.

### Leitura crítica do tamanho do MVP

Mesmo depois dos cortes, sobram cerca de 25 itens marcados como MVP. Isso não cabe numa entrega só sem virar "tudo pela metade", que é exatamente o caminho que travou o LMMS e o Zrythm. Na Fase 2 vou propor fatiar a Fase 3 em entregas que tocam som desde a primeira:
1. **Núcleo tocável**: transporte, channel rack com sampler nativo, mixer simples, salvar/abrir `.wnl`, exportar WAV.
2. **Composição**: piano roll, playlist com padrões e áudio, tonalidade, glide do 808.
3. **Plugins**: scan isolado, blacklist, VST3 instrumento e efeito, inserts, estado salvo no projeto.
4. **Acabamento**: MP3, MIDI, stems, navegador com favoritos, atalhos estilo FL, autosave com versões, recuperação após crash.

---

## 8. Lacunas

- Reddit: nada lido (bloqueado no ambiente). Se quiser, cole links de threads que considera representativas e eu incluo.
- Gearspace, VI-Control e fórum da Cockos (REAPER): só snippets, sem data verificada; o fórum da Cockos tem verificação anti-bot que não contornamos.
- Manual atual do REAPER (PDF de mais de 10 MB) não foi lido; o comportamento do "Run as" vem do guia oficial antigo (6.66–6.70), da Sound on Sound e de um blog de 2012.
- Audacity: comping, pastas, roteamento e versionamento local ficaram sem busca completa ("0" = não encontrado, não "não existe").
- Segurança: a página de issues do GitLab do Zrythm continha um texto dirigido a agentes de IA pedindo para ler arquivos e usar ferramentas. Foi tratado como dado e ignorado; a pesquisa seguiu pela API pública.
- Números de CPU/RAM dos modos de isolamento do Bitwig: não existem publicamente.
- Compatibilidade do Tracktion Engine com JUCE 9: a verificar na Fase 2.
- "Save New Version" no Fender Studio Pro 8: não confirmado.

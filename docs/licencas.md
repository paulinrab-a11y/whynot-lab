# WHYNOT LAB — Licenças (Fase 2)

Levantamento de 15/10/2026, feito nos arquivos de licença oficiais de cada projeto. Isto **não é parecer jurídico**; é o que os textos dizem e a leitura técnica que fiz deles. Se um dia a WHYNOT for vendida ou distribuída fechada, vale consultar um advogado.

## 1. Resumo para decidir

- **Para uso pessoal, no seu estúdio, sem distribuir o programa: todas as peças abaixo podem ser usadas de graça.** As licenças GPL/AGPL só cobram algo quando você entrega o programa (ou o acesso a ele pela rede) a outra pessoa.
- **Se um dia você publicar o código ou der o executável para alguém**, a WHYNOT inteira precisa sair como **AGPL-3.0-or-later**, com o código-fonte junto. Motivo: o JUCE (AGPLv3) e o Tracktion Engine (GPLv3+) só aceitam ser combinados nesse regime.
- **Se um dia quiser vender fechado**, são precisas duas licenças comerciais separadas: a do JUCE e a do Tracktion Engine. A do ASIO também, se o build tiver ASIO (ou se usa a opção GPLv3 e o produto fica aberto).
- **VST2 fica fora**: não há caminho limpo para um host novo (seção 4).

## 2. Componentes

| Componente | Versão que vamos usar | Licença | Papel na WHYNOT | Uso pessoal | Se distribuir | Fonte |
|---|---|---|---|---|---|---|
| **JUCE** | **8.0.13** (é a que o Tracktion Engine fixa como submódulo; o JUCE 9.0.4 já existe, mas o Tracktion não o adota ainda) | AGPLv3 **ou** EULA comercial do JUCE 8 | UI, áudio, MIDI, formatos de plugin, threads | Livre pela AGPLv3 | Código aberto sob AGPLv3, ou licença comercial (Starter grátis até US$ 20 mil de receita; Indie e Pro pagos) | [LICENSE.md no commit fixado](https://github.com/juce-framework/JUCE/blob/37c894f83d379179b2070d437ccd0f1cd9af9576/LICENSE.md), [juce.com/get-juce](https://juce.com/get-juce/) |
| **Tracktion Engine** | branch `develop` (commit `38d5a27`, 14/10/2026) | GPLv3 ou posterior **ou** comercial | Motor: transporte, clipes, plugins, PDC, gravação, render | Livre | GPLv3+ com fonte, ou licença comercial da Tracktion (separada da do JUCE) | [LICENSE.md](https://github.com/Tracktion/tracktion_engine/blob/develop/LICENSE.md), [README](https://github.com/Tracktion/tracktion_engine/blob/develop/README.md) |
| Terceiros embutidos no Tracktion | — | rpmalloc (domínio público), moodycamel ConcurrentQueue (BSD/BSL), choc (ISC), crill (BSL-1.0), expected (CC0), libsamplerate (BSD-2), MPMCQueue (MIT), magic_enum (MIT), farbot (MIT), doctest (MIT), **signalsmith-stretch (MIT)** | Filas lock-free, reamostragem, time-stretch | Livre | Manter os avisos de copyright | [README do Tracktion](https://github.com/Tracktion/tracktion_engine/blob/develop/README.md) |
| Terceiros embutidos no JUCE | — | Listados no SBOM oficial | — | Livre | Manter os avisos | [JUCE.spdx.json](https://github.com/juce-framework/JUCE/blob/master/JUCE.spdx.json) |
| **SDK VST3** | 3.8.x (vem dentro do JUCE) | **MIT** desde a 3.8.0 (out/2025) | Hospedar plugins VST3 | Livre | Manter o aviso MIT | [LICENSE.txt](https://github.com/steinbergmedia/vst3sdk/blob/master/LICENSE.txt), [anúncio](https://forums.steinberg.net/t/vst-3-8-0-sdk-released/1011988) |
| **SDK ASIO** | 2.3.x | Dupla: proprietária **ou GPLv3** (desde out/2025) | Driver de baixa latência no Windows | Livre pela GPLv3 | Pela GPLv3: produto aberto; fechado: acordo proprietário com a Steinberg | [press release Steinberg](https://ocl-steinberg-live.steinberg.net/_storage/asset/819253/storage/master/Press%20Release%20-%202025-10-29%20-%20VST%203.8%20-%20EN.pdf), [LibreArts](https://librearts.org/2025/11/steinberg-relicenses-vst3-and-asio/), [heise](https://heise.de/-10963451) |
| **CLAP** | — | MIT | Formato de plugin (fase posterior) | Livre | Manter o aviso | [LICENSE](https://github.com/free-audio/clap/blob/main/LICENSE) |
| **LAME** (encoder de MP3) | 3.100 | LGPL | Exportar MP3 | Livre | Compatível com AGPL; publicar mudanças feitas no LAME; creditar | [license.txt](https://lame.sourceforge.io/license.txt) |
| Patentes do MP3 | — | Programa de licenciamento encerrado pela Fraunhofer/Technicolor em 23/04/2017 | — | Sem royalties | Sem royalties | [MacRumors](https://macrumors.com/2017/05/15/mp3-format-terminated), [Digital Music News](https://www.digitalmusicnews.com/2017/05/12/the-death-of-mp3/) |
| **SDK MCP (TypeScript)** | linha 1.x estável | MIT, em transição para Apache-2.0 | Servidor MCP (processo separado) | Livre | Manter os avisos | [LICENSE](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/LICENSE) |
| **PyFLP** | — | GPLv3 | Fase 5: ler os seus `.flp` | Livre | Roda como ferramenta separada; GPLv3 é compatível de qualquer forma | [LICENSE](https://github.com/demberto/PyFLP/blob/master/LICENSE) |

### O que precisa do seu ok antes de baixar

| Item | Quando | De onde |
|---|---|---|
| JUCE + Tracktion Engine (código-fonte) | Agora, na prova de conceito da Fase 2 | GitHub oficial: `Tracktion/tracktion_engine` (traz o JUCE junto como submódulo) |
| SDK ASIO | Fase 3, entrega 4: **baixado com ok** (abaixo) | Site oficial da Steinberg |
| LAME 3.100 (fonte) | Fase 3, entrega 4: **baixado com ok** (abaixo) | lame.sourceforge.io |
| pluginval, validador VST3, clap-validator | Quando formos validar | GitHub oficial de Tracktion, Steinberg e free-audio |
| Plugins de teste | Fase 3, entrega 3 | Surge XT e Dexed: GitHub oficial (sem conta). Vital: o site exige conta, então **você** baixa. Valhalla Supermassive: site oficial (conferir na hora se pede conta) |

Usados na entrega 3.3, com o seu ok, sem instalar nada no Windows (tudo em `build\terceiros`, fora do git):

| Item | Versão | Origem | Licença | Uso |
|---|---|---|---|---|
| Surge XT (zip portátil) | 1.3.4 | github.com/surge-synthesizer/releases-xt, md5 conferido com o `md5sum.txt` da release | GPL-3.0 | Só teste: não vai junto com a WHYNOT |
| pluginval | 1.0.4 | github.com/Tracktion/pluginval | GPL-3.0 | Ferramenta de validação: não vai junto com a WHYNOT |
| Plugins de teste próprios | — | `tests/plugins` (feitos com JUCE e o SDK VST3 que vem nele) | Do projeto | Só para o autoteste |

Usados na entrega 3.4, com o seu ok. Ficam em `external/` (fora do git, ver `.gitignore`) e entram no build quando existem:

| Item | Versão | Origem | sha256 do arquivo baixado | Licença | Como entra |
|---|---|---|---|---|---|
| LAME (código-fonte) | 3.100 | `sourceforge.net/projects/lame/files/lame/3.100/lame-3.100.tar.gz` (o link oficial do lame.sourceforge.io) | `ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e` | LGPL | Biblioteca estática `whynot_lame`, compilada sem mudanças no código do LAME. Exportar MP3 |
| ASIO SDK | 2.3.4 (2025-10-15) | `download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip` | `d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca` | Dupla: proprietária ou GPLv3 (o `LICENSE.txt` do zip confirma); usamos pela **GPLv3** | Só o cabeçalho `common/iasiodrv.h`, que o JUCE inclui com `JUCE_ASIO=1` |

O que isso pede da WHYNOT:
- **LAME (LGPL)**: dizer que o app usa o LAME e onde está o código dele. Se um dia mudarmos o código do LAME, publicar essa mudança. Como é estático, quem recebe o app precisa poder trocar o LAME. Isso fica resolvido com o código da WHYNOT aberto (a AGPL recomendada na seção 5).
- **ASIO pela GPLv3**: quem distribuir o build com ASIO tem de distribuir o código aberto junto, o que fecha com a AGPL recomendada (a AGPLv3 e a GPLv3 combinam pela seção 13 das duas). Um build fechado precisaria do acordo proprietário com a Steinberg.
- Enquanto o app só rodar na sua máquina, nada disso gera obrigação: as licenças pedem algo só na hora de distribuir.
- **Sem os SDKs** o app compila igual, só sem MP3 (o diálogo explica) ou só com WASAPI.

Usado no WHYNOT Piano (onda 3 dos pedidos de 21/10), baixado com o seu ok em 21/10. Fica fora do git, na pasta de dados (`%APPDATA%\WHYNOT LAB\Bibliotecas\Salamander`, 592 MB):

| Item | Versão | Origem | sha256 do arquivo baixado | Licença | Como entra |
|---|---|---|---|---|---|
| Salamander Grand Piano (Yamaha C5, gravado por Alexander Holm) | V3, 44,1 kHz / 16 bits (`SalamanderGrandPianoV3+20161209_44khz16bit.tar.xz`, 394 MB) | `freepats.zenvoid.org/Piano/acoustic-grand-piano.html` | `58750eb1366761e187f71ddb9b932355ea894d28ec4331e74ab8acb44c819936` | CC BY 3.0 | `tools/piano/salamander_para_whynot.py` lê o `.sfz`, escolhe 8 das 16 camadas de força, deixa de fora o ruído do pedal e grava o `whynot-piano.json` e o `CREDITOS.txt`. O app lê o começo de cada WAV para a memória e o resto do disco |

O que a CC BY 3.0 pede: dar o crédito ao autor, apontar a licença e dizer o que foi mudado. O crédito está na janela "Sobre a WHYNOT LAB" (menu Arquivo), no `CREDITOS.txt` da biblioteca e no `piano.status`. Pode ir junto com um pacote da WHYNOT, inclusive fechado, desde que o crédito vá junto.

**Esticar o áudio no tempo** (pedidos de 21/10): usa o **signalsmith-stretch** (Geraint Luff / Signalsmith Audio, MIT), que já vem dentro do Tracktion em `modules/tracktion_engine/3rd_party/signalsmith-stretch`. Nada foi baixado. Entra no build pela definição `TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH=1` do `CMakeLists.txt`. A MIT pede só que o aviso de copyright e o texto da licença (o `LICENSE.txt` dessa pasta) vão junto com quem distribuir o app.

## 3. Marca, fontes e ícones da interface

| Item | Origem | Licença / uso |
|---|---|---|
| Logo WHYNOT (`assets/brand/`) | GUIDE WHYNOT, fornecido pelo dono do projeto; extraída em vetor do PDF | Marca de propriedade da WHYNOT. Fica fora da licença de código: quem redistribuir o código não ganha direito de usar a marca |
| **Montserrat** Regular, Medium, SemiBold e Bold (`assets/fonts/montserrat/`) | Repositório oficial da designer: [JulietaUla/Montserrat](https://github.com/JulietaUla/Montserrat) | SIL Open Font License 1.1 (arquivo `OFL.txt` junto). Pode ser embutida e redistribuída com o app |
| **Horizon** (tipografia principal do guia) | Ainda não adicionada | Fonte com licença própria: será embutida só a partir do arquivo licenciado que você fornecer. Não extraímos fontes de dentro do PDF do guia |
| Ícones de transporte (tocar, parar, metrônomo, loop) | Desenhados no código (`TransportBar.cpp`) | Próprios |
| Kit WHYNOT (bateria, 808 e Bell) | Sintetizado pelo próprio app (`KitGenerator.cpp`) | Próprio; nenhum sample de terceiros |

Nada de arte, ícone, skin, fonte ou sample extraído do FL Studio ou de outra DAW.

## 4. VST2: situação e opções legais

**Os fatos:**
- A Steinberg encerrou o SDK VST2 em outubro de 2018 e o retirou do SDK do VST3 ([fórum Steinberg](https://forums.steinberg.net/t/vst-2-sdk-discontinued/201774)).
- Em 2020, a equipe da Steinberg respondeu, após consultar advogados, que não é possível licenciar VST2 nem para host em desenvolvimento novo ([fórum Steinberg](https://forums.steinberg.net/t/can-i-create-vst-2-support-for-host-application-vst-3-license/202012)).
- O JUCE só hospeda VST2 com os arquivos do SDK VST2 (flag `JUCE_PLUGINHOST_VST`), e não os inclui desde a versão 5.4.0 ([BREAKING_CHANGES](https://github.com/juce-framework/JUCE/blob/master/BREAKING_CHANGES.md)).
- Os cabeçalhos feitos por engenharia reversa (VeSTige e similares) receberam pedido de remoção (DMCA) em 2018. Há relato, de segunda mão, de que a Steinberg os considera ilegais ([fórum JUCE](https://forum.juce.com/t/using-bona-fida-reversed-engineered-headers-to-distribute-vst2-like-plugins/32795)).
- A licença MIT do VST3 não diz nada sobre VST2.

**Opções, da mais recomendada para a menos:**
1. **Só VST3 (recomendado).** Quase todos os plugins atuais saem em VST3, inclusive todos os gratuitos que vamos testar.
2. **Plugin "ponte" comercial.** Um plugin VST3 de terceiros que carrega VST2 dentro dele, como o Blue Cat's PatchWork, que mistura VST2, VST3 e AU na mesma sessão ([KVR](https://kvraudio.com/news/blue-cat-audio-adds-vst3-hosting-to-blue-cats-patchwork-and-mb-7-mixer-33497), [Thomann](https://www.thomann.co.uk/blue_cat_audio_blue_cats_patchwork.htm)). A licença VST2 é problema do fabricante dele, não nosso. Custa dinheiro e acrescenta uma camada, mas resolve um plugin antigo que você não queira largar.
3. **Comprar ou trocar pela versão VST3** do plugin, quando existir.
4. **Não fazer**: baixar o SDK VST2 de fonte não oficial ou usar cabeçalhos de engenharia reversa. Viola a regra clean-room e a posição declarada da Steinberg.

**Licença VST2 assinada antes de 2018:** só seria uma opção se você tivesse um acordo VST2 assinado antes de outubro de 2018, e você não tem.

## 5. Licença da própria WHYNOT LAB

Recomendação: **AGPL-3.0-or-later**, a única combinação que respeita JUCE (AGPLv3) e Tracktion (GPLv3+) ao mesmo tempo. Enquanto o repositório for privado e o programa só rodar na sua máquina, isso não gera nenhuma obrigação. Não criei o arquivo `LICENSE` ainda: publicar ou não o código é decisão sua.

**Proibido ao longo do projeto** (regra clean-room): copiar ou extrair qualquer código, DLL, skin, ícone, sample ou preset do FL Studio ou de outra DAW comercial; usar material de versão crackeada; usar plugins crackeados em testes.

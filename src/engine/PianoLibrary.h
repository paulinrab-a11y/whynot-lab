#pragma once

#include <JuceHeader.h>

#include "../audio/PianoVoice.h"

#include <memory>
#include <thread>
#include <vector>

namespace whynot::piano
{

/** Quadros de cada sample que ficam na memória (0,37 s a 44,1 kHz). O resto o streamer lê do disco. */
constexpr int headFrames = 16384;

/** Um piano carregado (o WHYNOT Piano, pedido do usuário de 21/10): as zonas, com o começo de cada
    sample em 16 bits, e os arquivos mapeados na memória para o streamer ler o resto. Imutável depois
    de carregado e compartilhado entre os canais e os Edits (o da tela e o da exportação).

    A biblioteca é uma pasta com whynot-piano.json (feito por tools/piano/salamander_para_whynot.py)
    e os WAVs. Ela fica em <dados>/Bibliotecas/<nome>/; procuramos na pasta de dados do app, em
    %APPDATA%\WHYNOT LAB e ao lado do executável. */
struct Library
{
    audio::PianoSet set;
    juce::File folder;
    juce::String name, credit, license;
    std::vector<std::unique_ptr<juce::MemoryMappedAudioFormatReader>> readers;   // um por zona
    int skippedZones = 0;   // zonas do manifesto que ficaram de fora (arquivo ausente, nome inválido)

    /** Lê `num` quadros da zona a partir de `from` em dest (2 canais; mono sai duplicado). Sem trava:
        a leitura de um arquivo mapeado não muda o leitor. Thread do streamer ou do render offline. */
    bool read (int zone, std::int64_t from, int num, juce::AudioBuffer<float>& dest) const;

    /** Memória dos começos dos samples, em bytes. */
    std::int64_t headBytes() const noexcept;
};

/** A pasta de dados do app (AppController). Procuramos nela antes de %APPDATA%\WHYNOT LAB. */
void setDataFolder (const juce::File&);

/** Para os testes: procura só nesta pasta de biblioteca. File() volta à busca normal. */
void setLibraryOverride (const juce::File& libraryFolder);

/** A pasta da biblioteca que load() usaria, ou File() se não há nenhuma. */
juce::File findLibraryFolder();

/** O piano carregado (guardado por pasta: a segunda chamada é instantânea). A primeira lê o começo
    de cada sample (~26 MB, cerca de 1 s). Thread de mensagem ou de trabalho, nunca a de áudio.
    Devolve nullptr com `error` preenchido se não achou ou não conseguiu ler. */
std::shared_ptr<const Library> load (juce::String& error);

/** Carrega a biblioteca numa thread de fundo, se ela existir. O app com janela cria um ao abrir:
    a carga leva ~2 s, e assim o primeiro canal de piano não trava a tela. O destrutor espera
    a carga acabar. Chamar setDataFolder() antes. */
class Prefetch
{
public:
    Prefetch();
    ~Prefetch();

private:
    std::thread worker;
    JUCE_DECLARE_NON_COPYABLE (Prefetch)
};

} // namespace whynot::piano

#pragma once

#include <JuceHeader.h>

#include "../common/Text.h"

#include <functional>
#include <optional>
#include <vector>

namespace whynot
{

/** Os plugins que a WHYNOT conhece: o que o scan achou, o que foi bloqueado e por quê, e as pastas.

    O scan abre cada arquivo novo ou alterado num processo separado (o próprio app com
    --scan-plugin), com tempo limite. Plugin que trava ou derruba esse processo vai para a lista
    de bloqueados, com o motivo, e o app segue de pé. O resultado fica em cache (plugins.xml):
    arquivo que não mudou não é aberto de novo.

    Métodos com trava interna: o scan roda numa thread de trabalho enquanto a interface lê.
    Nunca na thread de áudio.
*/
class PluginCatalog
{
public:
    struct Blocked
    {
        juce::String file, name, reason;
        juce::Time when;
    };

    struct ScanReport
    {
        int candidates = 0;   // arquivos .vst3 encontrados nas pastas
        int scanned = 0;      // abertos agora num processo separado
        int reused = 0;       // vindos do cache (não mudaram)
        int found = 0;        // plugins conhecidos depois do scan
        juce::StringArray newlyBlocked;   // "arquivo: motivo"
        bool aborted = false;
    };

    using Progress = std::function<void (int done, int total, const juce::String& current)>;

    explicit PluginCatalog (const juce::File& cacheFile);

    void load();
    bool save() const;

    juce::Array<juce::PluginDescription> getTypes() const;
    std::vector<Blocked> getBlocked() const;
    bool isBlocked (const juce::String& file) const;
    juce::String blockReason (const juce::String& file) const;
    void block (const juce::String& file, const juce::String& name, const juce::String& reason);
    bool unblock (const juce::String& file);

    juce::StringArray getFolders() const;
    void setFolders (const juce::StringArray&);
    static juce::StringArray defaultFolders();

    /** Varre as pastas. Bloqueia quem chamar: use uma thread de trabalho (ou o autoteste). */
    ScanReport scan (int timeoutMs, const Progress& = {}, const std::function<bool()>& shouldAbort = {});

    /** O Tracktion procura os plugins nesta lista ao criar as instâncias. */
    void fillKnownList (juce::KnownPluginList&) const;

    /** Pelo id (PluginDescription::createIdentifierString) ou pelo nome, sem diferenciar maiúsculas. */
    std::optional<juce::PluginDescription> find (const juce::String& idOrName) const;

    /** Muda a cada scan ou bloqueio: o projetor tenta de novo os plugins que não abriram. */
    int getVersion() const noexcept   { return version.load(); }

private:
    struct FileEntry
    {
        juce::String file;
        juce::int64 stamp = 0;
        juce::Array<juce::PluginDescription> types;
    };

    static juce::int64 stampOf (const juce::File&);
    static juce::String scanInChildProcess (const juce::String& file, int timeoutMs, juce::Array<juce::PluginDescription>& found,
                                           const std::function<bool()>& shouldAbort);

    const juce::File cacheFile;
    mutable juce::CriticalSection lock;
    std::vector<FileEntry> files;
    std::vector<Blocked> blocked;
    juce::StringArray folders;
    std::atomic<int> version { 0 };
};

/** Modo filho do scan (whynot-lab --scan-plugin <arquivo> <saida.xml>): abre um arquivo VST3 e
    escreve o que achou. Código de saída 0 = ok; 3 = exceção dentro do plugin; 4 = não gravou. */
int runPluginScanChild (const juce::String& pluginFile, const juce::File& output);

} // namespace whynot

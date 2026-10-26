#include "CommandRegistry.h"
#include "../common/Text.h"
#include "../engine/SoundInfo.h"

// O navegador de sons com BPM e tom (bloco 4b, 23/10), para a IA: o que se sabe de um som e a busca nas pastas do
// navegador pelo tom e pelo BPM do projeto. A leitura do áudio roda na thread do SoundInfoCache; o browser.find não
// espera por ela (devolve o que já se sabe e quantos ainda faltam), o browser.info espera um som só.

namespace whynot
{

namespace
{
    using params::schema;
    using soundinfo::BpmFilter;
    using soundinfo::KeyFilter;
    using soundinfo::KindFilter;

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }

    CommandResult noCache()   { return CommandResult::fail ("Este app não tem a leitura dos sons (BPM e tom)."_t); }

    /** As palavras do nome: todas precisam estar no nome do arquivo (sem diferença de caixa). */
    bool nameHas (const juce::File& f, const juce::StringArray& words)
    {
        const auto name = f.getFileNameWithoutExtension();

        for (auto& w : words)
            if (! name.containsIgnoreCase (w))
                return false;

        return true;
    }
}

void registerBrowserCommands (CommandRegistry& r)
{
    r.add ({ "browser.info", "BPM e tom de um som"_t,
             schema ({ { "path", "string", true, "Arquivo de áudio (WAV, AIFF, FLAC, OGG, MP3)"_t },
                       { "wait", "boolean", false, "true (padrão) = espera a leitura do áudio (até 20 s); false = só o que já se sabe"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* cache = c.app.soundInfo();

                 if (cache == nullptr)
                     return noCache();

                 const juce::File file (params::string (a, "path"));

                 if (! file.existsAsFile())
                     return CommandResult::fail ("Arquivo não encontrado: "_t + file.getFullPathName());

                 SoundInfo info;

                 if (params::boolean (a, "wait", true))
                 {
                     info = cache->waitFor (file, 20000);
                 }
                 else
                 {
                     cache->request (file);
                     info = cache->get (file);
                 }

                 return CommandResult::success (info.toVar (file));
             } });

    r.add ({ "browser.find", "Achar sons pelo tom e pelo BPM"_t,
             schema ({ { "text", "string", false, "Palavras que precisam estar no nome (todas)"_t },
                       { "key", "string", false, "any (padrão), project (o tom do projeto e o relativo: as mesmas notas; a nota de um one-shot, "
                                                 "dentro da escala) ou compatible (também os vizinhos, uma quinta acima e abaixo)"_t },
                       { "bpm", "string", false, "any (padrão) ou project (a até 4% do BPM do projeto, da metade ou do dobro)"_t },
                       { "kind", "string", false, "any (padrão), loop ou oneshot"_t },
                       { "folder", "string", false, "Só nesta pasta (e nas de dentro); padrão = as pastas do navegador de sons"_t },
                       { "limit", "integer", false, "Quantos sons devolver (padrão 50, até 200)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* cache = c.app.soundInfo();

                 if (cache == nullptr)
                     return noCache();

                 const auto keyText = params::string (a, "key", "any");
                 const auto bpmText = params::string (a, "bpm", "any");
                 const auto kindText = params::string (a, "kind", "any");

                 if (keyText != "any" && keyText != "project" && keyText != "compatible")
                     return CommandResult::fail ("key é any, project ou compatible."_t);

                 if (bpmText != "any" && bpmText != "project")
                     return CommandResult::fail ("bpm é any ou project."_t);

                 if (kindText != "any" && kindText != "loop" && kindText != "oneshot")
                     return CommandResult::fail ("kind é any, loop ou oneshot."_t);

                 const auto keyFilter = keyText == "project" ? KeyFilter::project : keyText == "compatible" ? KeyFilter::compatible : KeyFilter::any;
                 const auto bpmFilter = bpmText == "project" ? BpmFilter::project : BpmFilter::any;
                 const auto kindFilter = kindText == "loop" ? KindFilter::loops : kindText == "oneshot" ? KindFilter::oneShots : KindFilter::any;

                 juce::Array<juce::File> folders;

                 if (const auto folder = params::string (a, "folder"); folder.isNotEmpty())
                 {
                     if (! juce::File (folder).isDirectory())
                         return CommandResult::fail ("Pasta não encontrada: "_t + folder);

                     folders.add (juce::File (folder));
                 }
                 else
                 {
                     folders = c.app.soundFolders();
                 }

                 const auto& p = c.model.get();
                 const auto degrees = Project::scaleDegrees (p.scale);   // dórico, frígio...; na cromática, todo tom serve
                 const int limit = juce::jlimit (1, 200, (int) params::number (a, "limit", 50));
                 const auto words = juce::StringArray::fromTokens (params::string (a, "text").trim(), true);

                 juce::Array<juce::var> sounds;
                 int scanned = 0, matched = 0, waiting = 0;
                 bool cut = false;
                 constexpr int maxScanned = 20000;
                 const double deadline = juce::Time::getMillisecondCounterHiRes() + 3000.0;   // a tela não para mais que isso
                 std::vector<juce::File> wanted;

                 for (auto& folder : folders)
                 {
                     // sem seguir atalhos de pasta (uma junção que volta para cima daria voltas)
                     for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3",
                                                                             juce::File::findFiles, juce::File::FollowSymlinks::no))
                     {
                         if (++scanned > maxScanned || juce::Time::getMillisecondCounterHiRes() > deadline)
                         {
                             cut = true;
                             break;
                         }

                         const auto f = entry.getFile();

                         if (entry.isHidden() || ! nameHas (f, words))
                             continue;

                         // sem filtro de som, o nome basta; com filtro, só o que já foi lido (o resto entra na fila)
                         auto info = cache->get (f);
                         const bool needsAudio = keyFilter != KeyFilter::any || bpmFilter != BpmFilter::any || kindFilter != KindFilter::any;

                         if (needsAudio && ! info.analysed)
                         {
                             if ((int) wanted.size() < SoundInfoCache::maxQueue)
                                 wanted.push_back (f);

                             ++waiting;
                             continue;
                         }

                         if (! soundinfo::keyFits (info, p.keyRoot, degrees, keyFilter) || ! soundinfo::bpmFits (info.bpm, p.bpm, bpmFilter)
                             || ! soundinfo::kindFits (info, kindFilter))
                             continue;

                         if (++matched <= limit)
                             sounds.add (info.toVar (f));
                     }

                     if (cut)
                         break;
                 }

                 cache->requestMany (wanted);   // na ordem das pastas; o que não coube entra na próxima chamada

                 juce::String note;

                 if (waiting > 0)
                     note << juce::String (waiting) << " sons ainda sem leitura do áudio: eles entraram na fila; "
                          << "chame de novo daqui a pouco para ver se combinam."_t;

                 if (cut)
                     note << (note.isEmpty() ? "" : " ") << "A busca parou em "_t << juce::String (juce::jmin (scanned, maxScanned))
                          << " arquivos (o limite de tempo ou de quantidade): use folder ou text para procurar num lugar menor."_t;

                 return CommandResult::success (object ({ { "sounds", sounds }, { "matched", matched }, { "pending", waiting },
                                                          { "scanned", juce::jmin (scanned, maxScanned) }, { "cut", cut },
                                                          { "note", note } }));
             } });
}

} // namespace whynot

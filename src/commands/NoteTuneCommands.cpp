// Afinação nota a nota (frente C de 27/10): os comandos da voz de um bloco de áudio da playlist vista como notas.
// - audio.analyzeNotes: as notas que a análise acha e as que estão no projeto (não muda nada);
// - audio.setNoteTune: troca todas as notas de uma vez (a tela manda assim: cada gesto, um passo de desfazer);
// - audio.editNotes: move, centraliza, achata o vibrato, mexe na formante e na transição de uma ou mais notas;
// - audio.splitNote e audio.mergeNotes: cortar e juntar notas;
// - audio.tuneToScale: tudo para a escala do projeto num clique;
// - audio.clearNoteTune: volta ao original.
// As notas ficam no canal de áudio do bloco (Channel::tunedNotes). Antes de mudar o modelo, o comando escreve o arquivo
// refeito ao lado do original (notetune::render), então o Projector já toca a versão nova. Dono: Afinação nota a nota.

#include "CommandRegistry.h"
#include "../record/NoteTuneFiles.h"
#include "../ui/NoteNames.h"

namespace whynot
{

namespace
{
    using params::schema;

    struct Target
    {
        Channel* channel = nullptr;
        juce::File file;
    };

    CommandResult findTarget (CommandContext& c, const juce::var& a, Target& t)
    {
        auto& p = c.model.data();
        const auto* item = p.findItem (params::string (a, "item"));

        if (item == nullptr || item->kind != "audio")
            return CommandResult::fail ("Bloco de áudio não encontrado."_t);

        if (item->reversed)
            return CommandResult::fail ("O bloco está invertido: desinverta antes de afinar nota a nota."_t);

        t.channel = p.findChannel (item->ref);

        if (t.channel == nullptr || ! t.channel->isAudio())
            return CommandResult::fail ("Canal do clipe não encontrado."_t);

        t.file = c.model.resolveSample (t.channel->samplePath);

        if (! t.file.existsAsFile())
            return CommandResult::fail ("Arquivo de áudio não encontrado: "_t + t.file.getFullPathName());

        return CommandResult::success();
    }

    /** As notas do canal; sem nenhuma ainda, as da análise (sem edição). */
    CommandResult currentNotes (const Target& t, std::vector<TunedNote>& notes)
    {
        notes = t.channel->tunedNotes;

        if (! notes.empty())
            return CommandResult::success();

        juce::String error;
        const auto analysis = notetune::analyse (t.file, error);

        if (analysis == nullptr)
            return CommandResult::fail (error);

        notes = dsp::notesFromAnalysis (*analysis);
        notetune::normalise (notes);

        if (notes.empty())
            return CommandResult::fail ("Não achei notas neste áudio (sem voz cantada, ou baixo demais)."_t);

        return CommandResult::success();
    }

    juce::var noteToVar (const TunedNote& n, int index)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("index", index);
        o->setProperty ("start", n.start);
        o->setProperty ("end", n.end);
        o->setProperty ("sourceMidi", n.sourceMidi);
        o->setProperty ("targetMidi", std::round (n.targetMidi() * 100.0f) / 100.0f);
        o->setProperty ("shift", n.shift);
        o->setProperty ("center", juce::roundToInt (n.center * 100.0f));
        o->setProperty ("flatten", juce::roundToInt (n.flatten * 100.0f));
        o->setProperty ("formant", n.formant);
        o->setProperty ("transitionMs", juce::roundToInt (n.transitionMs));
        o->setProperty ("edited", n.edited());
        return juce::var (o);
    }

    juce::var notesToVar (const std::vector<TunedNote>& notes)
    {
        juce::Array<juce::var> list;

        for (size_t i = 0; i < notes.size(); ++i)
            list.add (noteToVar (notes[i], (int) i));

        return list;
    }

    /** Escreve o arquivo refeito e só então troca as notas do canal. */
    CommandResult commit (const Target& t, std::vector<TunedNote> notes, bool formantFollows)
    {
        notetune::normalise (notes);
        juce::String path = t.file.getFullPathName();
        dsp::NoteRenderStats stats;

        if (notetune::anyEdited (notes))
        {
            const auto target = notetune::renderedFileFor (t.file, notes, formantFollows);

            if (! target.existsAsFile())
                if (const auto r = notetune::render (t.file, notes, formantFollows, target, &stats); r.failed())
                    return CommandResult::fail (r.getErrorMessage());

            path = target.getFullPathName();
        }

        t.channel->tunedNotes = notes;
        t.channel->tuneFormantFollows = formantFollows;

        auto o = new juce::DynamicObject();
        o->setProperty ("notes", notesToVar (notes));
        o->setProperty ("edited", (int) std::count_if (notes.begin(), notes.end(), [] (const TunedNote& n) { return n.edited(); }));
        o->setProperty ("formantFollows", formantFollows);
        o->setProperty ("path", path);
        o->setProperty ("regions", stats.regions);
        return CommandResult::success (juce::var (o));
    }

    /** Os índices pedidos em "notes" (todas, sem o parâmetro); falha se algum não existe. */
    CommandResult indicesOf (const juce::var& a, int count, std::vector<int>& out)
    {
        if (auto* list = a["notes"].getArray())
        {
            for (auto& v : *list)
            {
                const bool number = v.isInt() || v.isInt64() || v.isDouble();
                const int i = number ? (int) juce::jlimit (-1.0, 1.0e6, (double) v) : -1;

                if (! number || i < 0 || i >= count)
                    return CommandResult::fail ("Nota fora da lista: "_t + v.toString() + " (há " + juce::String (count) + ")");

                out.push_back (i);
            }
        }
        else
        {
            for (int i = 0; i < count; ++i)
                out.push_back (i);
        }

        return CommandResult::success();
    }

    /** "Lá menor", "Dó maior"...: o tom do projeto em português, para a mensagem. */
    juce::String scaleTextPt (const Project& p)
    {
        const auto& s = p.scale;
        const juce::String name = s == "major" ? "maior"_t : s == "minor" ? "menor"_t : s == "harmonicMinor" ? "menor harmônica"_t
                                : s == "phrygian" ? "frígio"_t : s == "dorian" ? "dórico"_t : s == "pentatonicMinor" ? "pentatônica menor"_t
                                : "cromática"_t;
        return noteNamePt (p.keyRoot, false) + " " + name;
    }

    bool finiteParam (const juce::var& a, const char* key)
    {
        return ! params::has (a, key) || std::isfinite (params::number (a, key, 0.0));
    }
}

void registerNoteTuneCommands (CommandRegistry& r)
{
    r.add ({ "audio.analyzeNotes", "Ver as notas da voz"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 Target t;

                 if (auto found = findTarget (c, a, t); ! found.ok)
                     return found;

                 juce::String error;
                 const auto analysis = notetune::analyse (t.file, error);

                 if (analysis == nullptr)
                     return CommandResult::fail (error);

                 juce::Array<juce::var> found;

                 for (auto& s : analysis->notes)
                 {
                     auto o = new juce::DynamicObject();
                     o->setProperty ("start", std::round (s.start * 1.0e4) / 1.0e4);
                     o->setProperty ("end", std::round (s.end * 1.0e4) / 1.0e4);
                     o->setProperty ("midi", std::round (s.midi * 100.0f) / 100.0f);
                     o->setProperty ("driftCents", juce::roundToInt (s.driftCents));
                     o->setProperty ("vibratoCents", juce::roundToInt (s.vibratoCents));
                     found.add (juce::var (o));
                 }

                 auto o = new juce::DynamicObject();
                 o->setProperty ("analysis", found);
                 o->setProperty ("notes", notesToVar (t.channel->tunedNotes));
                 o->setProperty ("formantFollows", t.channel->tuneFormantFollows);
                 o->setProperty ("seconds", analysis->curve.seconds());
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "audio.setNoteTune", "Afinar nota a nota"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio"_t },
                       { "notes", "array", true, "Todas as notas: {start, end (segundos do arquivo), sourceMidi, shift (semitons), center e "
                                                 "flatten (0 a 1), formant (semitons), transitionMs}"_t },
                       { "formantFollows", "boolean", false, "true = o timbre anda junto com o tom (como fita); false (padrão) = fica"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 Target t;

                 if (auto found = findTarget (c, a, t); ! found.ok)
                     return found;

                 auto* list = a["notes"].getArray();

                 if (list == nullptr)
                     return CommandResult::fail ("Diga as notas (notes)."_t);

                 std::vector<TunedNote> notes;

                 for (auto& v : *list)
                 {
                     TunedNote n;
                     auto num = [&v] (const char* key, double fallback)
                     {
                         const auto& x = v[key];
                         return (x.isDouble() || x.isInt() || x.isInt64()) ? (double) x : fallback;
                     };
                     n.start = num ("start", 0.0);
                     n.end = num ("end", 0.0);
                     n.sourceMidi = (float) num ("sourceMidi", 60.0);
                     n.shift = (float) num ("shift", 0.0);
                     n.center = (float) num ("center", 0.0);
                     n.flatten = (float) num ("flatten", 0.0);
                     n.formant = (float) num ("formant", 0.0);
                     n.transitionMs = (float) num ("transitionMs", n.transitionMs);

                     if (! (n.end > n.start))
                         return CommandResult::fail ("Cada nota precisa de end depois de start."_t);

                     notes.push_back (n);
                 }

                 return commit (t, std::move (notes), params::boolean (a, "formantFollows", t.channel->tuneFormantFollows));
             } });

    r.add ({ "audio.editNotes", "Editar notas da voz"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio"_t },
                       { "notes", "array", false, "Índices das notas (de audio.analyzeNotes); sem ele, todas"_t },
                       { "to", "number", false, "Leva a nota para esta nota MIDI (69 = Lá 440 Hz; inteira cai no semitom)"_t },
                       { "semitones", "number", false, "Move as notas tantos semitons (soma ao que já foi movido)"_t },
                       { "center", "number", false, "Centralizar, de 0 a 100%: puxa para o semitom e tira o desvio lento"_t },
                       { "flatten", "number", false, "Achatar o vibrato, de 0 a 100%"_t },
                       { "formant", "number", false, "Formante, de -12 a +12 semitons (o timbre, à parte do tom)"_t },
                       { "transitionMs", "number", false, "Transição de entrada da nota, de 0 a 300 ms"_t },
                       { "formantFollows", "boolean", false, "true = o timbre anda junto com o tom; false = fica"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 Target t;

                 if (auto found = findTarget (c, a, t); ! found.ok)
                     return found;

                 for (auto* key : { "to", "semitones", "center", "flatten", "formant", "transitionMs" })
                     if (! finiteParam (a, key))
                         return CommandResult::fail ("Valor inválido em "_t + key + ".");

                 std::vector<TunedNote> notes;

                 if (auto got = currentNotes (t, notes); ! got.ok)
                     return got;

                 std::vector<int> which;

                 if (auto got = indicesOf (a, (int) notes.size(), which); ! got.ok)
                     return got;

                 for (int i : which)
                 {
                     auto& n = notes[(size_t) i];

                     if (params::has (a, "to"))
                         notetune::moveTo (n, (float) params::number (a, "to", 60.0));

                     if (params::has (a, "semitones"))
                         n.shift += (float) params::number (a, "semitones", 0.0);

                     if (params::has (a, "center"))
                         n.center = (float) (params::number (a, "center", 0.0) / 100.0);

                     if (params::has (a, "flatten"))
                         n.flatten = (float) (params::number (a, "flatten", 0.0) / 100.0);

                     if (params::has (a, "formant"))
                         n.formant = (float) params::number (a, "formant", 0.0);

                     if (params::has (a, "transitionMs"))
                         n.transitionMs = (float) params::number (a, "transitionMs", 40.0);
                 }

                 return commit (t, std::move (notes), params::boolean (a, "formantFollows", t.channel->tuneFormantFollows));
             } });

    r.add ({ "audio.splitNote", "Cortar nota da voz"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio"_t },
                       { "index", "number", true, "A nota (de audio.analyzeNotes)"_t },
                       { "at", "number", true, "Onde cortar, em segundos do arquivo (cada pedaço com 30 ms ou mais)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 Target t;

                 if (auto found = findTarget (c, a, t); ! found.ok)
                     return found;

                 std::vector<TunedNote> notes;

                 if (auto got = currentNotes (t, notes); ! got.ok)
                     return got;

                 if (! notetune::split (notes, (int) juce::jlimit (-1.0, 1.0e6, params::number (a, "index", -1)), params::number (a, "at", -1.0)))
                     return CommandResult::fail ("Não deu para cortar: a nota não existe ou o corte fica a menos de 30 ms de uma ponta."_t);

                 return commit (t, std::move (notes), t.channel->tuneFormantFollows);
             } });

    r.add ({ "audio.mergeNotes", "Juntar notas da voz"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio"_t },
                       { "index", "number", true, "A primeira das duas notas (junta com a seguinte)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 Target t;

                 if (auto found = findTarget (c, a, t); ! found.ok)
                     return found;

                 std::vector<TunedNote> notes;

                 if (auto got = currentNotes (t, notes); ! got.ok)
                     return got;

                 if (! notetune::merge (notes, (int) juce::jlimit (-1.0, 1.0e6, params::number (a, "index", -1))))
                     return CommandResult::fail ("Não deu para juntar: diga uma nota que tenha outra depois dela."_t);

                 return commit (t, std::move (notes), t.channel->tuneFormantFollows);
             } });

    r.add ({ "audio.tuneToScale", "Afinar a voz na escala"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio"_t },
                       { "strength", "number", false, "Força, de 0 a 100% (padrão 100: cada nota no semitom da escala)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 Target t;

                 if (auto found = findTarget (c, a, t); ! found.ok)
                     return found;

                 const double strength = params::number (a, "strength", 100.0);

                 if (! std::isfinite (strength))
                     return CommandResult::fail ("Força inválida."_t);

                 std::vector<TunedNote> notes;

                 if (auto got = currentNotes (t, notes); ! got.ok)
                     return got;

                 const auto& p = c.model.get();
                 notetune::snapToScale (notes, [&p] (int key) { return p.inScale (key); }, (float) juce::jlimit (0.0, 1.0, strength / 100.0));
                 auto result = commit (t, std::move (notes), t.channel->tuneFormantFollows);

                 if (result.ok)
                     result.value.getDynamicObject()->setProperty ("scale", scaleTextPt (p));

                 return result;
             } });

    r.add ({ "audio.clearNoteTune", "Voltar a voz ao original"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 Target t;

                 if (auto found = findTarget (c, a, t); ! found.ok)
                     return found;

                 return commit (t, {}, false);
             } });
}

} // namespace whynot

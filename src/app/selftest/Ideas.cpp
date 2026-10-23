// Autoteste do WHYNOT Ideias: gerar acordes, melodia, 808 e bateria no projeto, seguir acordes de outro
// canal, detectar acordes, entender pedido em português, nível de cada parte, salvar e reabrir. Dono: Ideias.
// Começa de um projeto novo (kit WHYNOT), sem depender das outras áreas.

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"

namespace whynot::selftest
{

void runIdeasSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto& workFolder = ctx.work;

    rep.section ("Ideias: gerar no projeto");
    {
        app.run ("project.new");
        app.run ("project.setKey", args ({ { "root", "A" }, { "scale", "minor" } }));

        juce::String bell, bass808;

        for (auto& ch : model.get().channels)
        {
            if (ch.name == "Bell") bell = ch.id;
            if (ch.name == "808")  bass808 = ch.id;
        }

        rep.check (bell.isNotEmpty() && bass808.isNotEmpty(), "o kit tem Bell e 808 para receber as ideias");

        const auto empty = wnl::toJson (model.get());
        const auto chords = app.run ("ideas.generate", args ({ { "part", "chords" }, { "channel", bell }, { "genre", "trap" },
                                                               { "seed", 42 }, { "bars", 4 } }));
        rep.check (chords.ok && (int) chords.value["notes"] >= 8,
                   "acordes de trap no Bell: "_t + chords.value["chordNames"].toString() + (chords.ok ? juce::String() : chords.error));
        rep.check (model.get().currentPatternPtr()->lengthSteps == 64, "o padrão cresce para 4 compassos"_t);
        rep.check (chords.value["sound"].size() >= 1 && chords.value["sound"][0].toString() == "pad",
                   "acordes de trap sugerem som de pad do synth ("_t + juce::JSON::toString (chords.value["sound"], true) + ")");
        rep.check (app.run ("edit.undo").ok && wnl::toJson (model.get()) == empty, "desfazer apaga a ideia num passo só"_t);
        app.run ("edit.redo");

        const auto melody = app.run ("ideas.generate", args ({ { "part", "melody" }, { "channel", bell }, { "genre", "trap" },
                                                               { "seed", 42 }, { "replace", false } }));
        rep.check (melody.ok && melody.value["progression"] == chords.value["progression"],
                   "melodia da mesma ideia segue a mesma progressão ("_t + melody.value["progression"].toString() + ")");

        const auto varied = app.run ("ideas.generate", args ({ { "part", "melody" }, { "channel", bell }, { "genre", "trap" },
                                                               { "seed", 42 }, { "variation", 1 }, { "replace", false } }));
        rep.check (varied.ok && varied.value["progression"] == chords.value["progression"], "Variar mantém a progressão"_t);
        app.run ("edit.undo");

        const auto drill808 = app.run ("ideas.generate", args ({ { "part", "808" }, { "channel", bass808 }, { "genre", "drill" }, { "seed", 7 } }));
        rep.check (drill808.ok && (bool) drill808.value["mono"] == model.get().findChannel (bass808)->mono,
                   "808 de drill: com slide, o canal fica em mono para deslizar"_t);

        const auto drums = app.run ("ideas.generate", args ({ { "part", "drums" }, { "genre", "trap" }, { "seed", 42 } }));
        const auto lanes = drums.value["lanes"];
        const auto kickId = lanes["kick"].toString();
        rep.check (drums.ok && kickId.isNotEmpty() && lanes["snare"].toString().isNotEmpty() && lanes["hat"].toString().isNotEmpty()
                       && lanes["openhat"].toString().isNotEmpty(),
                   "bateria acha Kick, Clap, Hat e Open hat pelo nome");
        rep.check (drums.ok && model.get().currentPatternPtr()->steps.at (kickId)[0].on, "bumbo no primeiro passo");

        const auto follow = app.run ("ideas.generate", args ({ { "part", "bass" }, { "channel", bass808 }, { "genre", "pop" },
                                                               { "follow", bell }, { "seed", 3 } }));
        rep.check (follow.ok && follow.value["chordNames"].toString().isNotEmpty(),
                   "baixo segue os acordes que o Bell toca: "_t + follow.value["chordNames"].toString() + (follow.ok ? juce::String() : follow.error));

        const auto detected = app.run ("ideas.detectChords", args ({ { "channel", bell } }));
        rep.check (detected.ok && detected.value["chordNames"].toString().isNotEmpty(),
                   "acordes detectados no Bell: "_t + detected.value["chordNames"].toString() + " (tom: "_t + detected.value["key"].toString() + ")");

        const auto parsed = app.run ("ideas.parse", args ({ { "text", "trap triste em Fá menor, 4 compassos"_t } }));
        rep.check (parsed.ok && parsed.value["genre"] == "trap" && (int) parsed.value["root"] == 5 && parsed.value["mood"] == "sad",
                   "entende \"trap triste em Fá menor, 4 compassos\""_t);

        const auto bigSeed = app.run ("ideas.generate", args ({ { "part", "chords" }, { "channel", bell }, { "genre", "trap" },
                                                               { "seed", 3000000000.0 } }));
        rep.check (bigSeed.ok && (int) bigSeed.value["seed"] == 2147483647, "semente acima de 2^31 vira 2^31-1 e volta igual"_t);

        // Caixa e percussão pedidas no mesmo canal (funk tem as duas): somam, a caixa não some.
        const auto clap = channelId (model.get(), "Clap");
        const auto both = app.run ("ideas.generate", args ({ { "part", "drums" }, { "genre", "funk" }, { "seed", 5 }, { "bars", 1 },
                                                             { "channels", args ({ { "snare", clap }, { "perc", clap } }) } }));
        const auto& clapSteps = model.get().currentPatternPtr()->steps.at (clap);
        int onInBar = 0;

        for (size_t i = 0; i < 16 && i < clapSteps.size(); ++i)
            onInBar += clapSteps[i].on ? 1 : 0;

        // A caixa sozinha liga só os passos 4 e 12; com a percussão somada o compasso tem pelo menos 4.
        rep.check (both.ok && clapSteps.size() >= 16 && clapSteps[4].on && clapSteps[12].on && onInBar >= 4,
                   "caixa e percussão no mesmo canal somam ("_t + juce::String (onInBar) + " passos no compasso; a caixa sozinha teria 2)");

        rep.check (! app.run ("ideas.generate", args ({ { "part", "melody" }, { "channel", bell }, { "genre", "forro" } })).ok,
                   "gênero desconhecido é recusado"_t);
        rep.check (! app.run ("ideas.generate", args ({ { "part", "melody" } })).ok, "melodia sem canal é recusada"_t);
        rep.check (! app.run ("ideas.generate", args ({ { "part", "chords" }, { "channel", bell }, { "progression", "i XX" } })).ok,
                   "progressão inválida é recusada"_t);

        // Cada canal sozinho: nenhuma parte gerada pode clipar por conta própria.
        {
            juce::StringArray ids, names, peaks;

            for (auto& ch : model.get().channels)
            {
                ids.add (ch.id);
                names.add (ch.name);
            }

            float loudest = -100.0f;

            for (int i = 0; i < ids.size(); ++i)
            {
                for (int k = 0; k < ids.size(); ++k)
                    app.run ("channel.setMute", args ({ { "channel", ids[k] }, { "on", k != i } }));

                const auto wavOne = workFolder.getChildFile ("ideias_" + juce::String (i) + ".wav");
                app.run ("project.exportWav", args ({ { "path", wavOne.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
                const float pk = peakDb (readWav (wavOne));
                loudest = juce::jmax (loudest, pk);
                peaks.add (names[i] + " " + juce::String (pk, 1));
            }

            for (auto& id : ids)
                app.run ("channel.setMute", args ({ { "channel", id }, { "on", false } }));

            rep.check (loudest < -1.0f, "cada canal sozinho abaixo de -1 dBFS: "_t + peaks.joinIntoString (", "));
        }

        const auto wavIdeas = workFolder.getChildFile ("ideias.wav");
        app.run ("project.exportWav", args ({ { "path", wavIdeas.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        const auto ideasWav = readWav (wavIdeas);
        rep.check (ideasWav.valid && peakDb (ideasWav) > -30.0f, "a ideia inteira toca (pico sem mixagem: "_t
                                                                    + juce::String (peakDb (ideasWav), 1) + " dBFS)");

        // O kit padrão já pica em -1,2 dBFS só com a batida: acordes e melodia por cima pedem mixagem.
        // Com o estilo trap (limitador no master), a ideia inteira sai limpa.
        rep.check (app.run ("mixer.applyStyle", args ({ { "style", "trap" } })).ok, "estilo de mixagem trap na ideia");
        const auto wavIdeasMix = workFolder.getChildFile ("ideias_mixado.wav");
        app.run ("project.exportWav", args ({ { "path", wavIdeasMix.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        const float mixedPeak = peakDb (readWav (wavIdeasMix));
        rep.check (mixedPeak < -0.9f && mixedPeak > -12.0f, "com o estilo trap a ideia inteira não clipa: pico "_t
                                                            + juce::String (mixedPeak, 1) + " dBFS");
        app.run ("edit.undo");

        const auto ideasFile = workFolder.getChildFile ("Demo Ideias").getChildFile ("Demo Ideias.wnl");
        rep.check (app.run ("project.save", args ({ { "path", ideasFile.getFullPathName() } })).ok, "salva o projeto com as ideias");
        const auto ideasJson = wnl::toJson (model.get());   // depois de salvar: os samples já com caminho relativo
        app.run ("project.new");
        rep.check (app.run ("project.open", args ({ { "path", ideasFile.getFullPathName() } })).ok && wnl::toJson (model.get()) == ideasJson,
                   "reabre com as mesmas notas e passos");
    }
}

} // namespace whynot::selftest

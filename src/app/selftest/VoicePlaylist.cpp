// Autoteste da voz na playlist (frente A de 27/10): editar o áudio gravado pelo microfone na playlist.
// - fatia 1: o corte (Cortar, Picotar e o corte dos takes) deixa os fades nas pontas de fora, e o som no ponto
//   do corte continua igual ao do clipe inteiro (WAV medido contra o export sem corte);
// - fatia 2: cortar na agulha (playlist.cutAt, Ctrl+E), apagar o trecho puxando o resto (playlist.removeSpace,
//   Ctrl+Delete) e abrir espaço (playlist.insertSpace, Ctrl+Insert), com o desfazer e o som medido;
// - fatia 3: a curva de cada fade (medida no WAV), a alça arrastada para cima e o crossfade automático;
// - fatia 4: o tom do bloco (Hz medido) e o reverso sem canal novo;
// - fatia 5: normalizar o grupo e tirar o silêncio e a respiração (prévia, apagar, abaixar e a ferramenta na tela).
// Capturas: voz-playlist-curvas.png e voz-playlist-silencio.png.
// Dono: Voz na playlist.

#include "SelfTestKit.h"
#include "../../record/Takes.h"
#include "../../model/WnlFormat.h"
#include "../../record/ReversedAudio.h"
#include "../../ui/Playlist.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    constexpr double rate = 48000.0;

    bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
    {
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                .withSampleRate (rate)
                                                .withNumChannels (b.getNumChannels())
                                                .withBitsPerSample (32)
                                                .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    /** `seconds` de seno a 440 Hz com pico 0,25 (a voz de mentira: o corte no meio dele não pode afundar). */
    juce::File sineFile (const juce::File& folder, const juce::String& name, double seconds)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, (float) (0.25 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / rate)));

        const auto f = folder.getChildFile (name);
        writeWav (f, b);
        return f;
    }

    /** Seno a 440 Hz que cresce de 0,05 a 0,25 em `seconds`: o pico num ponto diz de que segundo do arquivo ele veio. */
    juce::File rampFile (const juce::File& folder, const juce::String& name, double seconds)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            b.setSample (0, i, (float) ((0.05 + 0.2 * t / seconds) * std::sin (juce::MathConstants<double>::twoPi * 440.0 * t)));
        }

        const auto f = folder.getChildFile (name);
        writeWav (f, b);
        return f;
    }

    juce::String num (double v, int places = 3)   { return juce::String (v, places).replaceCharacter ('.', ','); }

    bool near (float a, float b, float tolerance = 0.02f)   { return std::abs (a - b) <= tolerance * juce::jmax (1.0e-6f, std::abs (b)); }

    bool savePng (juce::Component& c, const juce::File& file)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        file.deleteFile();

        if (juce::FileOutputStream out (file); out.openedOk())
        {
            juce::PNGImageFormat png;
            return png.writeImageToStream (image, out) && image.getWidth() > 0;
        }

        return false;
    }

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p, juce::Point<float> down)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                 &c, &c, now, down, now, 1, p != down };
    }

    /** Uma voz de mentira com pausas: tom 0–0,5 s, silêncio até 1 s, respiração (ruído a uns -32 dBFS) até 1,4 s,
        tom até 2 s e silêncio até 2,5 s. */
    juce::File breathFile (const juce::File& folder)
    {
        juce::AudioBuffer<float> b (1, (int) (2.5 * rate));
        juce::Random random (27);

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            const bool tone = t < 0.5 || (t >= 1.4 && t < 2.0);
            const float breath = t >= 1.0 && t < 1.4 ? 0.044f * (random.nextFloat() * 2.0f - 1.0f) : 0.0f;   // RMS ~0,025
            b.setSample (0, i, tone ? (float) (0.25 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * t)) : breath);
        }

        const auto f = folder.getChildFile ("voz-com-pausas.wav");
        writeWav (f, b);
        return f;
    }

    /** Fatias 3 a 5: as curvas dos fades e o crossfade, o tom e o reverso por bloco, normalizar o grupo e o silêncio. */
    void runVoiceClipChecks (SelfTestContext& ctx, const juce::File& work)
    {
        auto& rep = ctx.rep;
        auto& app = ctx.app;
        auto& model = ctx.model;
        static PlaylistItem missing;
        auto find = [&] (const juce::String& id) { const auto* f = model.get().findItem (id); return f != nullptr ? f : &missing; };
        auto render = [&] (const juce::String& name)
        {
            const auto f = work.getChildFile (name);
            app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "what", "song" } }));
            return readWav (f);
        };
        auto fresh = [&]
        {
            app.run ("project.new");
            app.run ("transport.setBpm", args ({ { "bpm", 120.0 } }));
            app.run ("transport.setSongMode", args ({ { "on", true } }));
        };
        auto place = [&] (const juce::File& f, int track, int start)
        {
            return app.run ("playlist.addItem", args ({ { "kind", "audio" }, { "path", f.getFullPathName() }, { "track", track }, { "start", start } }))
                       .value["item"].toString();
        };

        //======================================================================
        rep.section ("Voz na playlist: curvas dos fades e crossfade automático"_t);
        fresh();
        const auto sine = sineFile (work, "voz-curvas.wav", 2.0);
        const auto clip = place (sine, 1, 0);
        const auto plain = render ("curva-sem-fade.wav");
        const float body = juce::jmax (1.0e-6f, peakIn (plain, 0.6, 0.8));

        {
            // o fade de entrada de 1 s: no primeiro quarto (alpha 0,25) cada curva dá um ganho diferente
            const std::pair<const char*, float> expected[] = { { "linear", 0.25f }, { "convex", 0.3827f }, { "concave", 0.0761f }, { "s", 0.1528f } };
            juce::String seen;
            bool all = true;

            for (auto& [curve, gain] : expected)
            {
                app.run ("audio.setFades", args ({ { "item", clip }, { "in", 1.0 }, { "out", 0.0 }, { "inCurve", curve } }));
                const float at = peakIn (render (juce::String ("curva-") + curve + ".wav"), 0.245, 0.255) / body;
                all = all && std::abs (at - gain) < 0.12f * gain + 0.01f && juce::String (fadeCurveName (find (clip)->fadeInCurve)) == juce::String (curve);
                seen << curve << " " << num (at, 3) << " (" << num (gain, 3) << ")  ";
            }

            rep.check (all, "no WAV, o fade de entrada a 1/4 do caminho segue a curva: "_t + seen.trim());

            app.run ("audio.setFades", args ({ { "item", clip }, { "in", 0.0 }, { "out", 1.0 }, { "outCurve", "convex" } }));
            const float convexOut = peakIn (render ("curva-saida-rapida.wav"), 1.495, 1.505) / body;
            app.run ("audio.setFades", args ({ { "item", clip }, { "outCurve", "concave" } }));
            const float concaveOut = peakIn (render ("curva-saida-lenta.wav"), 1.495, 1.505) / body;
            rep.check (std::abs (convexOut - 0.7071f) < 0.06f && std::abs (concaveOut - 0.2929f) < 0.05f,
                       "o fade de saída no meio: rápida "_t + num (convexOut) + " (0,707) e lenta " + num (concaveOut) + " (0,293)");
            rep.check (! app.run ("audio.setFades", args ({ { "item", clip }, { "inCurve", "torta" } })).ok, "curva desconhecida é recusada"_t);
        }

        {
            // o .wnl: os campos novos são opcionais e iguais nos dois escritores
            app.run ("audio.setFades", args ({ { "item", clip }, { "in", 0.3 }, { "inCurve", "convex" }, { "out", 0.4 }, { "outCurve", "s" } }));
            app.run ("audio.setPitch", args ({ { "item", clip }, { "semitones", 2.5 } }));
            const auto json = wnl::toJson (model.get());
            Project back;
            const bool read = wnl::fromJson (json, back).wasOk();
            const auto* again = back.findItem (clip);
            rep.check (read && json == wnl::toJsonReference (model.get()) && json.contains ("\"fadeInCurve\": \"convex\"") && json.contains ("\"pitch\": 2.5")
                           && again != nullptr && again->fadeInCurve == fadeCurveConvex && again->fadeOutCurve == fadeCurveS && again->pitch == 2.5f,
                       "o .wnl guarda a curva dos fades e o tom do bloco (toJson = toJsonReference, e volta igual)"_t);
            app.run ("audio.setFades", args ({ { "item", clip }, { "in", 0.0 }, { "out", 0.0 }, { "inCurve", "linear" }, { "outCurve", "linear" } }));
            app.run ("audio.setPitch", args ({ { "item", clip }, { "semitones", 0 } }));
            rep.check (! wnl::toJson (model.get()).contains ("Curve") && ! wnl::toJson (model.get()).contains ("\"pitch\""),
                       "um bloco sem curva nem tom sai no .wnl como antes"_t);
        }

        {
            // dois áudios sobrepostos na mesma raia: A de 0 a 2 s, B de 1,5 s a 3,5 s (o mesmo seno, na mesma fase)
            const auto b = place (sine, 1, 2880);
            const auto xa = playlistedit::autoCrossfade (model.get(), *find (clip));
            const auto xb = playlistedit::autoCrossfade (model.get(), *find (b));
            rep.check (std::abs (xa.out - 0.5) < 1.0e-9 && xa.in == 0.0 && std::abs (xb.in - 0.5) < 1.0e-9 && xb.out == 0.0,
                       "a sobra de 0,5 s vira crossfade: o de cima desce, o de baixo sobe"_t);
            const auto cross = render ("crossfade.wav");
            const float start = peakIn (cross, 1.500, 1.505) / body, middle = peakIn (cross, 1.745, 1.755) / body,
                        end = peakIn (cross, 1.995, 2.0) / body, after = peakIn (cross, 2.2, 2.3) / body;
            rep.check (start < 1.12f && std::abs (middle - 1.414f) < 0.1f && end < 1.12f && near (after, 1.0f, 0.03f),
                       "no WAV, o crossfade não soma dobrado: "_t + num (start) + " no começo da sobra, " + num (middle) + " no meio (mesma potência, 1,414), "
                           + num (end) + " no fim e " + num (after) + " depois (sem crossfade daria 2)");

            app.run ("playlist.updateItem", args ({ { "item", b }, { "track", 2 } }));
            rep.check (playlistedit::autoCrossfade (model.get(), *find (clip)).out == 0.0, "em raias diferentes, não há crossfade"_t);
            app.run ("playlist.updateItem", args ({ { "item", b }, { "track", 1 }, { "start", 960 }, { "length", 960 } }));
            rep.check (playlistedit::autoCrossfade (model.get(), *find (clip)).out == 0.0 && playlistedit::autoCrossfade (model.get(), *find (b)).in == 0.0,
                       "um bloco inteiro dentro do outro é camada, sem crossfade"_t);
            app.run ("playlist.removeItem", args ({ { "item", b } }));
        }

        {
            // a tela: arrastar a alça do fade para cima deixa a curva rápida; a captura mostra curvas e crossfade
            theme::LookAndFeel laf;
            juce::Component host;
            host.setLookAndFeel (&laf);
            host.setSize (1200, 340);
            Playlist list (app);
            host.addAndMakeVisible (list);
            list.setBounds (host.getLocalBounds());
            list.refresh (model.get());
            PlaylistGrid* grid = nullptr;

            for (auto* c : list.getChildren())
                if (auto* g = dynamic_cast<PlaylistGrid*> (c))
                    grid = g;

            if (grid != nullptr)
            {
                const float left = 151.0f, top = 31.0f, px = grid->getPxPerTick();
                const juce::Point<float> from { left + 3.0f, top + 4.0f }, to { left + (float) secondsToTicks (0.5, 120.0) * px, top - 40.0f };
                grid->mouseDown (mouseAt (*grid, from, from));
                grid->mouseDrag (mouseAt (*grid, (from + to) * 0.5f, from));
                grid->mouseDrag (mouseAt (*grid, to, from));
                grid->mouseUp (mouseAt (*grid, to, from));
                rep.check (find (clip)->fadeIn > 0.4 && find (clip)->fadeInCurve == fadeCurveConvex,
                           "arrastar a alça do fade para o lado e para cima: fade de "_t + num (find (clip)->fadeIn) + " s com a curva rápida");

                app.run ("audio.setFades", args ({ { "item", clip }, { "out", 0.6 }, { "outCurve", "concave" } }));
                app.run ("audio.setPitch", args ({ { "item", clip }, { "semitones", 2 } }));
                place (sine, 2, 0);
                const auto later = place (sine, 2, 2880);
                list.refresh (model.get());
                juce::MessageManager::getInstance()->runDispatchLoopUntil (500);   // a forma de onda carrega em segundo plano
                grid->mouseMove (mouseAt (*grid, { left + 200.0f, top + 20.0f }, { left + 200.0f, top + 20.0f }));
                rep.check (savePng (list, ctx.work.getChildFile ("voz-playlist-curvas.png")),
                           "captura: fade rápido e lento, o tom no cabeçalho e o crossfade em destaque na raia 2"_t);
                app.run ("playlist.removeItem", args ({ { "item", later } }));
            }

            host.removeChildComponent (&list);
            host.setLookAndFeel (nullptr);
        }

        //======================================================================
        rep.section ("Voz na playlist: tom e reverso por bloco, sem canal novo"_t);
        fresh();
        const auto voice = sineFile (work, "voz-tom.wav", 2.0);
        const auto v = place (voice, 1, 0);
        const auto channelsBefore = model.get().channels.size();

        {
            const auto up = app.run ("audio.setPitch", args ({ { "item", v }, { "semitones", 12 } }));
            const double hzUp = measureHz (render ("tom-mais-12.wav"), 0.5, 1.5);
            app.run ("audio.setPitch", args ({ { "item", v }, { "semitones", -5 } }));
            const double hzDown = measureHz (render ("tom-menos-5.wav"), 0.5, 1.5);
            const double expectedDown = 440.0 * std::pow (2.0, -5.0 / 12.0);
            rep.check (up.ok && std::abs (hzUp - 880.0) < 880.0 * 0.01 && std::abs (hzDown - expectedDown) < expectedDown * 0.01,
                       "no WAV, +12 semitons toca "_t + juce::String (hzUp, 1) + " Hz (880) e -5 toca " + juce::String (hzDown, 1) + " Hz ("
                           + juce::String (expectedDown, 1) + ")");
            const auto tone = render ("tom-tempo.wav");
            rep.check (model.get().channels.size() == channelsBefore && peakIn (tone, 1.8, 1.95) > 0.01f && peakIn (tone, 2.05, 2.5) < 1.0e-3f,
                       "o tom não cria canal novo nem muda o tempo (o som acaba em 2 s, como o arquivo)"_t);
            rep.check (! app.run ("audio.setPitch", args ({ { "item", v }, { "semitones", 30 } })).ok, "tom fora de -24 a +24 é recusado"_t);
            app.run ("audio.setPitch", args ({ { "item", v }, { "semitones", 0 } }));
        }

        {
            // o reverso: a voz que cresce, invertida, começa alta e acaba baixa (sozinha na música, para medir)
            app.run ("playlist.removeItem", args ({ { "item", v } }));
            const auto ramp = rampFile (work, "voz-sobe.wav", 4.0);
            const auto r = place (ramp, 2, 0);
            const auto beforeReverse = render ("antes-de-inverter.wav");
            const auto rev = app.run ("audio.reverse", args ({ { "item", r } }));
            const auto backwards = render ("invertido-no-bloco.wav");
            rep.check (rev.ok && find (r)->reversed && model.get().channels.size() == channelsBefore + 1 && find (r)->ref == model.get().channels.back().id,
                       "inverter vira o bloco (reversed) sem canal novo: o bloco continua no canal dele"_t);
            rep.check (near (peakIn (backwards, 0.1, 0.2), peakIn (beforeReverse, 3.8, 3.9), 0.03f) && near (peakIn (backwards, 3.8, 3.9), peakIn (beforeReverse, 0.1, 0.2), 0.03f),
                       "no WAV, invertido o começo soa como o fim do arquivo ("_t + num (peakIn (backwards, 0.1, 0.2), 4) + ") e o fim como o começo ("
                           + num (peakIn (backwards, 3.8, 3.9), 4) + ")");

            // cortar o invertido: o pedaço da direita continua no arquivo invertido
            const auto split = app.run ("playlist.splitItem", args ({ { "item", r }, { "at", 3840 } }));
            const auto cut = render ("invertido-cortado.wav");
            rep.check (split.ok && find (split.value["right"].toString())->reversed && maxDiff (backwards, cut) < 1.0e-3f,
                       "cortar o bloco invertido não muda o som (diferença "_t + juce::String (maxDiff (backwards, cut)) + ")");
            app.run ("edit.undo");

            const auto twice = app.run ("audio.reverse", args ({ { "item", r } }));
            rep.check (twice.ok && ! find (r)->reversed && std::abs (find (r)->offsetSeconds) < 1.0e-6 && maxDiff (beforeReverse, render ("desinvertido.wav")) < 1.0e-3f,
                       "inverter de novo volta ao original, no mesmo trecho e com o mesmo som"_t);
            app.run ("edit.undo");
            rep.check (app.run ("edit.undo").ok && ! find (r)->reversed, "Ctrl+Z desfaz o inverter"_t);

            // o arquivo invertido sumiu (o projeto foi copiado sem ele): o playedFile, que o motor usa, escreve de novo
            const auto copy = work.getChildFile ("voz-copiada.wav");
            ramp.copyFileTo (copy);
            const bool absent = ! reversed::fileFor (copy).existsAsFile();
            const auto rewritten = reversed::playedFile (copy, true);
            const auto direct = readWav (rewritten), original = readWav (copy);
            const int n = original.buffer.getNumSamples();
            rep.check (absent && rewritten == reversed::fileFor (copy) && direct.valid && direct.buffer.getNumSamples() == n && n > 10
                           && direct.buffer.getSample (0, 0) == original.buffer.getSample (0, n - 1)
                           && direct.buffer.getSample (0, n / 3) == original.buffer.getSample (0, n - 1 - n / 3),
                       "sem o arquivo invertido, ele é escrito de novo a partir do original, amostra por amostra"_t);
            rep.check (reversed::playedFile (copy, false) == copy, "um bloco não invertido toca o original"_t);
        }

        //======================================================================
        rep.section ("Voz na playlist: normalizar o grupo e tirar silêncio e respiração"_t);
        fresh();

        {
            // duas vozes: uma com pico 0,25 e outra com 0,1
            juce::AudioBuffer<float> quiet (1, (int) (1.0 * rate));

            for (int i = 0; i < quiet.getNumSamples(); ++i)
                quiet.setSample (0, i, (float) (0.1 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / rate)));

            const auto quietFile = work.getChildFile ("voz-baixa.wav");
            writeWav (quietFile, quiet);
            const auto loud = place (sineFile (work, "voz-alta.wav", 1.0), 1, 0);
            const auto soft = place (quietFile, 2, 0);
            const juce::Array<juce::var> both { juce::var (loud), juce::var (soft) };
            const double loudGain = -1.0 - 20.0 * std::log10 (0.25), softGain = -1.0 - 20.0 * std::log10 (0.1);

            const auto together = app.run ("audio.normalize", args ({ { "items", both } }));
            rep.check (together.ok && std::abs (find (loud)->gainDb - loudGain) < 0.11 && std::abs (find (soft)->gainDb - loudGain) < 0.11,
                       "normalizar os dois juntos: o mesmo ganho ("_t + juce::String (find (soft)->gainDb, 1) + " dB), o mais alto em -1 dB e o equilíbrio fica");
            app.run ("edit.undo");
            const auto each = app.run ("audio.normalize", args ({ { "items", both }, { "together", false } }));
            rep.check (each.ok && std::abs (find (loud)->gainDb - loudGain) < 0.11 && std::abs (find (soft)->gainDb - softGain) < 0.11,
                       "com together false, cada um no seu pico ("_t + juce::String (find (loud)->gainDb, 1) + " e " + juce::String (find (soft)->gainDb, 1) + " dB)");
            app.run ("edit.undo");
        }

        fresh();
        const auto pauses = place (breathFile (work), 1, 0);
        const auto withPauses = render ("pausas-original.wav");

        {
            const auto silent = app.run ("audio.findSilence", args ({ { "item", pauses } }));
            const auto breathing = app.run ("audio.findSilence", args ({ { "item", pauses }, { "thresholdDb", -28.0 } }));
            auto regions = [] (const CommandResult& r) { return r.value["regions"].size(); };
            const double s1 = silent.value["seconds"], s2 = breathing.value["seconds"];
            const auto& first = silent.value["regions"][0];
            rep.check (silent.ok && regions (silent) == 2 && std::abs ((int) first["from"] - (int) secondsToTicks (0.54, 120.0)) <= 20
                           && std::abs ((int) first["to"] - (int) secondsToTicks (0.96, 120.0)) <= 20 && (int) silent.value["regions"][1]["to"] == 4800,
                       "a prévia em -45 dB acha os dois silêncios (0,54–0,96 s e o fim), sem a respiração ("_t + num (s1, 2) + " s)");
            rep.check (breathing.ok && regions (breathing) == 2 && std::abs (s2 - s1 - 0.4) < 0.05 && model.get().playlistItems.size() == 1,
                       "em -28 dB a respiração entra junto ("_t + num (s2, 2) + " s), e a prévia não mexe no projeto"_t);

            const auto removed = app.run ("audio.removeSilence", args ({ { "item", pauses } }));
            const auto cleaned = render ("pausas-sem-silencio.wav");
            const auto* firstPiece = find (pauses);
            rep.check (removed.ok && (int) removed.value["removed"] == 2 && model.get().playlistItems.size() == 2
                           && std::abs (firstPiece->fadeOut - 0.005) < 1.0e-9 && peakIn (cleaned, 0.6, 0.9) < 1.0e-4f
                           && near (peakIn (cleaned, 1.5, 1.9), peakIn (withPauses, 1.5, 1.9), 0.01f) && peakIn (cleaned, 1.05, 1.35) > 0.005f,
                       "tirar em -45 dB: sobram 2 pedaços (5 ms de anti-clique nas bordas), o silêncio sai e a voz e a respiração ficam iguais"_t);
            app.run ("edit.undo");
            rep.check (model.get().playlistItems.size() == 1 && find (pauses)->fadeOut == 0.0, "Ctrl+Z volta o clipe inteiro"_t);

            const auto lowered = app.run ("audio.removeSilence", args ({ { "item", pauses }, { "thresholdDb", -28.0 }, { "reduceDb", -15.0 } }));
            const auto ducked = render ("pausas-respiracao-baixa.wav");
            const float drop = 20.0f * std::log10 (juce::jmax (1.0e-6f, peakIn (ducked, 1.1, 1.3)) / juce::jmax (1.0e-6f, peakIn (withPauses, 1.1, 1.3)));
            rep.check (lowered.ok && (int) lowered.value["lowered"] == 2 && std::abs (drop + 15.0f) < 0.5f && near (peakIn (ducked, 1.5, 1.9), peakIn (withPauses, 1.5, 1.9), 0.01f),
                       "abaixar em vez de apagar: a respiração fica "_t + juce::String (drop, 1) + " dB mais baixa e a voz igual");
            app.run ("edit.undo");
            rep.check (! app.run ("audio.removeSilence", args ({ { "item", pauses }, { "minMs", 10000 } })).ok
                           && model.get().playlistItems.size() == 1 && find (pauses)->length == 4800,
                       "sem trecho quieto do tamanho pedido, é recusado sem mexer no clipe"_t);
            rep.check (! app.run ("audio.removeSilence", args ({ { "item", pauses }, { "thresholdDb", -6.0 } })).ok && model.get().playlistItems.size() == 1,
                       "um limiar que pega o clipe inteiro é recusado (não apaga a voz toda)"_t);
        }

        {
            // a ferramenta na tela, sem janela: o limiar muda a prévia em vermelho; Tirar aplica
            theme::LookAndFeel laf;
            juce::Component host;
            host.setLookAndFeel (&laf);
            host.setSize (1200, 300);
            Playlist list (app);
            host.addAndMakeVisible (list);
            list.setBounds (0, 0, 1200, 300);
            list.refresh (model.get());
            PlaylistGrid* grid = nullptr;

            for (auto* c : list.getChildren())
                if (auto* g = dynamic_cast<PlaylistGrid*> (c))
                    grid = g;

            if (grid != nullptr)
            {
                auto tool = std::make_unique<SilenceTool> (app, *grid, pauses);
                host.addAndMakeVisible (*tool);
                tool->setTopLeftPosition (700, 100);
                const int quietOnly = tool->regionCount();
                const double quietSeconds = tool->previewSeconds();
                tool->setThreshold (-28.0);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
                rep.check (quietOnly == 2 && tool->regionCount() == 2 && tool->previewSeconds() > quietSeconds + 0.3
                               && savePng (host, ctx.work.getChildFile ("voz-playlist-silencio.png")),
                           "captura: a ferramenta de silêncio com a prévia em vermelho no clipe"_t);
                juce::String message;
                const bool applied = tool->apply (message);
                rep.check (applied && model.get().playlistItems.size() == 2, "Tirar na ferramenta: "_t + message);
                app.run ("edit.undo");
                host.removeChildComponent (tool.get());
            }

            host.removeChildComponent (&list);
            host.setLookAndFeel (nullptr);
        }
    }
}

void runVoicePlaylistSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto work = ctx.work.getChildFile ("voz-playlist");
    work.createDirectory();

    rep.section ("Voz na playlist: o corte não afunda o som"_t);

    // 120 BPM: 1 s = 1920 ticks. O clipe tem 2 s e fades de 0,4 s nas duas pontas.
    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 120.0 } }));
    app.run ("transport.setSongMode", args ({ { "on", true } }));
    const auto voice = sineFile (work, "voz.wav", 2.0);
    const auto added = app.run ("playlist.addItem", args ({ { "kind", "audio" }, { "path", voice.getFullPathName() }, { "track", 1 }, { "start", 0 } }));
    const auto clipId = added.value["item"].toString();
    static PlaylistItem missing;
    auto find = [&] (const juce::String& id) { const auto* f = model.get().findItem (id); return f != nullptr ? f : &missing; };
    auto render = [&] (const juce::String& name)
    {
        const auto f = work.getChildFile (name);
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "what", "song" } }));
        return readWav (f);
    };

    rep.check (added.ok && app.run ("audio.setFades", args ({ { "item", clipId }, { "in", 0.4 }, { "out", 0.4 } })).ok,
               "clipe de 2 s com fades de 0,4 s nas pontas"_t);
    const auto whole = render ("inteiro.wav");
    const float body = juce::jmax (1.0e-6f, peakIn (whole, 0.6, 0.8));

    {
        // a ferramenta Cortar no meio do clipe
        const auto split = app.run ("playlist.splitItem", args ({ { "item", clipId }, { "at", 1920 } }));
        const auto rightId = split.value["right"].toString();
        const auto* l = find (clipId);
        const auto* r = find (rightId);
        rep.check (split.ok && std::abs (l->fadeIn - 0.4) < 1.0e-9 && l->fadeOut == 0.0 && r->fadeIn == 0.0 && std::abs (r->fadeOut - 0.4) < 1.0e-9
                       && std::abs (r->offsetSeconds - 1.0) < 1.0e-6,
                   "Cortar: o pedaço da esquerda fica com o fade de entrada e o da direita com o de saída; no corte, nenhum ("_t
                       + num (l->fadeOut) + " e " + num (r->fadeIn) + " s)");

        const auto cut = render ("cortado.wav");
        const float atCut = peakIn (cut, 0.98, 1.02), diff = maxDiff (whole, cut);
        rep.check (std::abs (atCut - body) < 0.01f * body && diff < 1.0e-3f,
                   "no WAV, o ponto do corte soa igual ao clipe inteiro: pico "_t + num (atCut, 4) + " contra " + num (body, 4)
                       + " (diferença máxima " + juce::String (diff) + "; antes da correção afundava para 0,003)"_t);
        rep.check (app.run ("edit.undo").ok && model.get().findItem (rightId) == nullptr && std::abs (find (clipId)->fadeOut - 0.4) < 1.0e-9,
                   "Ctrl+Z volta o clipe inteiro com os dois fades"_t);
    }

    {
        // o picotar a cada tempo (0,5 s): só a primeira fatia tem fade de entrada, só a última o de saída
        const auto chop = app.run ("playlist.chopItem", args ({ { "item", clipId }, { "mode", "beat" } }));
        int inner = 0, pieces = 0;
        bool ends = false;

        if (auto* ids = chop.value["items"].getArray())
        {
            pieces = ids->size();

            for (int i = 0; i < pieces; ++i)
            {
                const auto* it = find (ids->getReference (i).toString());
                const bool first = i == 0, last = i == pieces - 1;
                inner += (! first && it->fadeIn != 0.0) || (! last && it->fadeOut != 0.0) ? 1 : 0;

                if (last)
                    ends = std::abs (find (ids->getReference (0).toString())->fadeIn - 0.4) < 1.0e-9 && std::abs (it->fadeOut - 0.4) < 1.0e-9;
            }
        }

        const auto chopped = render ("picotado.wav");
        const float diff = maxDiff (whole, chopped);
        rep.check (chop.ok && pieces == 4 && inner == 0 && ends && diff < 1.0e-3f,
                   "Picotar a cada tempo: "_t + juce::String (pieces) + " fatias, os fades só nas pontas de fora, e o WAV igual ao inteiro (diferença "
                       + juce::String (diff) + ")");
        app.run ("edit.undo");
    }

    {
        // o corte dos takes (a ferramenta Cortar em cima de um grupo) usa o mesmo corte
        auto copy = model.get();
        const auto rightId = takes::split (copy, clipId, 1920);
        const auto* l = copy.findItem (clipId);
        const auto* r = copy.findItem (rightId);
        rep.check (l != nullptr && r != nullptr && l->fadeOut == 0.0 && r->fadeIn == 0.0 && std::abs (r->fadeOut - 0.4) < 1.0e-9,
                   "takes: cortar um take também deixa o ponto do corte sem fade"_t);
    }

    {
        // um fade maior que o pedaço não passa dele
        app.run ("audio.setFades", args ({ { "item", clipId }, { "in", 1.5 }, { "out", 0.2 } }));
        const auto split = app.run ("playlist.splitItem", args ({ { "item", clipId }, { "at", 960 } }));
        const auto* l = find (clipId);
        rep.check (split.ok && std::abs (l->fadeIn - 0.5) < 1.0e-9 && l->fadeOut == 0.0,
                   "um fade de entrada de 1,5 s, cortado em 0,5 s, fica com 0,5 s (o tamanho do pedaço)"_t);
        app.run ("edit.undo");
        app.run ("edit.undo");
    }

    //==========================================================================
    rep.section ("Voz na playlist: cortar na agulha, apagar o trecho e abrir espaço"_t);

    // A voz (4 s, raia 1) cresce de 0,05 a 0,25; outra voz curta na raia 2 em 3 s; um marcador em 3,5 s.
    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 120.0 } }));
    app.run ("transport.setSongMode", args ({ { "on", true } }));
    const auto ramp = rampFile (work, "voz-crescendo.wav", 4.0);
    const auto other = sineFile (work, "resposta.wav", 1.0);
    const auto a = app.run ("playlist.addItem", args ({ { "kind", "audio" }, { "path", ramp.getFullPathName() }, { "track", 1 }, { "start", 0 } }));
    const auto b = app.run ("playlist.addItem", args ({ { "kind", "audio" }, { "path", other.getFullPathName() }, { "track", 2 }, { "start", 5760 } }));
    const auto aId = a.value["item"].toString(), bId = b.value["item"].toString();
    const auto marker = app.run ("playlist.addMarker", args ({ { "at", 6720 }, { "name", "Refrão"_t } }));
    const auto plain = render ("trecho-original.wav");
    auto sourcePeak = [&] (double fileSeconds) { return peakIn (plain, fileSeconds, fileSeconds + 0.1); };   // a voz no segundo do arquivo
    auto itemsOnTrack = [&] (int index)
    {
        std::vector<PlaylistItem> out;
        const auto& p = model.get();

        for (auto& i : p.playlistItems)
            if (i.track == p.playlistTracks[(size_t) index].id)
                out.push_back (i);

        std::sort (out.begin(), out.end(), [] (const PlaylistItem& x, const PlaylistItem& y) { return x.start < y.start; });
        return out;
    };

    rep.check (a.ok && b.ok && marker.ok && model.get().playlistItems.size() == 2, "duas vozes na playlist e um marcador"_t);

    {
        // cortar na agulha: só o que passa por ela; com a lista, só aqueles blocos
        const auto cut = app.run ("playlist.cutAt", args ({ { "at", 1920 } }));
        const auto lane = itemsOnTrack (0);
        rep.check (cut.ok && lane.size() == 2 && lane[1].start == 1920 && std::abs (lane[1].offsetSeconds - 1.0) < 1.0e-6
                       && itemsOnTrack (1).size() == 1,
                   "playlist.cutAt em 1 s corta só a voz que passa ali (a da raia 2 começa em 3 s)"_t);
        app.run ("edit.undo");

        const auto only = app.run ("playlist.cutAt", args ({ { "at", 6000 }, { "items", juce::Array<juce::var> { juce::var (bId) } } }));
        rep.check (only.ok && itemsOnTrack (0).size() == 1 && itemsOnTrack (1).size() == 2, "com a lista de blocos, corta só eles"_t);
        app.run ("edit.undo");
        rep.check (! app.run ("playlist.cutAt", args ({ { "at", 9000 } })).ok, "cortar onde não há bloco é recusado"_t);
    }

    {
        // Ctrl+Delete: apagar de 1 s a 2 s e puxar o resto
        const auto removed = app.run ("playlist.removeSpace", args ({ { "from", 1920 }, { "to", 3840 } }));
        const auto lane = itemsOnTrack (0), lane2 = itemsOnTrack (1);
        const auto& markers = model.get().markers;
        rep.check (removed.ok && lane.size() == 2 && lane[0].end() == 1920 && lane[1].start == 1920 && std::abs (lane[1].offsetSeconds - 2.0) < 1.0e-6
                       && lane2.size() == 1 && lane2[0].start == 3840 && markers.size() == 1 && markers[0].t == 4800,
                   "removeSpace de 1 s a 2 s: a voz fica 0–1 s e 2–4 s do arquivo colados, a da raia 2 vai de 3 s para 2 s e o marcador de 3,5 s para 2,5 s"_t);
        const auto rippled = render ("trecho-apagado.wav");
        const float before = peakIn (rippled, 0.8, 0.9), after = peakIn (rippled, 1.2, 1.3);
        rep.check (near (before, sourcePeak (0.8)) && near (after, sourcePeak (2.2)),
                   "no WAV, logo depois do ponto apagado toca o segundo 2,2 do arquivo ("_t + num (after, 4) + " contra " + num (sourcePeak (2.2), 4)
                       + ") e antes dele o 0,8 (" + num (before, 4) + ")");
        rep.check (app.run ("edit.undo").ok && itemsOnTrack (0).size() == 1 && itemsOnTrack (0)[0].length == 7680
                       && itemsOnTrack (1)[0].start == 5760 && model.get().markers[0].t == 6720,
                   "Ctrl+Z volta o trecho, a outra voz e o marcador num passo só"_t);
        rep.check (! app.run ("playlist.removeSpace", args ({ { "from", 3840 }, { "to", 1920 } })).ok, "trecho ao contrário é recusado"_t);
    }

    {
        // Ctrl+Insert: abrir 1 s de espaço em 1 s
        const auto opened = app.run ("playlist.insertSpace", args ({ { "at", 1920 }, { "length", 1920 } }));
        const auto lane = itemsOnTrack (0), lane2 = itemsOnTrack (1);
        rep.check (opened.ok && lane.size() == 2 && lane[0].end() == 1920 && lane[1].start == 3840 && std::abs (lane[1].offsetSeconds - 1.0) < 1.0e-6
                       && lane2[0].start == 7680 && model.get().markers[0].t == 8640,
                   "insertSpace de 1 s em 1 s: a voz é cortada ali e o resto anda 1 s (a outra voz e o marcador também)"_t);
        const auto spaced = render ("espaco-aberto.wav");
        const float gap = peakIn (spaced, 1.05, 1.95), after = peakIn (spaced, 2.2, 2.3);
        rep.check (gap < 1.0e-4f && near (after, sourcePeak (1.2)),
                   "no WAV, o espaço fica em silêncio ("_t + juce::String (gap) + ") e depois dele toca o segundo 1,2 do arquivo (" + num (after, 4) + ")");
        app.run ("edit.undo");
        rep.check (! app.run ("playlist.insertSpace", args ({ { "at", 0 }, { "length", maxTimelineTicks } })).ok,
                   "um espaço que empurraria a voz para fora da linha do tempo é recusado"_t);
    }

    {
        // os atalhos na tela, sem janela: Ctrl+E na agulha, Ctrl+Delete e Ctrl+Insert na seleção de tempo
        juce::Component parent;
        PlaylistGrid grid (app);
        parent.setBounds (0, 0, 1200, 600);
        parent.addAndMakeVisible (grid);
        grid.setBounds (parent.getLocalBounds());
        grid.refresh (model.get());
        const auto ctrl = juce::ModifierKeys::commandModifier;

        app.run ("transport.setPosition", args ({ { "at", 960 } }));
        rep.check (grid.keyPressed (juce::KeyPress ('e', ctrl, 0)) && itemsOnTrack (0).size() == 2 && itemsOnTrack (0)[1].start == 960,
                   "Ctrl+E corta na agulha (0,5 s)"_t);
        app.run ("edit.undo");

        grid.setTimeSelection ({ 1920, 3840 });
        rep.check (grid.keyPressed (juce::KeyPress ('e', ctrl, 0)) && itemsOnTrack (0).size() == 3, "com um trecho selecionado, Ctrl+E corta nas duas bordas"_t);
        rep.check (app.run ("edit.undo").ok && itemsOnTrack (0).size() == 1, "e o Ctrl+Z desfaz os dois cortes de uma vez"_t);

        grid.selectAll();   // com blocos selecionados, o pedaço novo da primeira borda também é cortado na segunda (achado da revisão)
        rep.check (grid.keyPressed (juce::KeyPress ('e', ctrl, 0)) && itemsOnTrack (0).size() == 3 && itemsOnTrack (1).size() == 1,
                   "com os blocos selecionados e um trecho, Ctrl+E corta nas duas bordas da voz (3 pedaços)"_t);
        app.run ("edit.undo");
        grid.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        grid.setTimeSelection ({ 1920, 3840 });

        rep.check (grid.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey, ctrl, 0)) && itemsOnTrack (1)[0].start == 3840
                       && grid.getTimeSelection().isEmpty(),
                   "Ctrl+Delete apaga o trecho selecionado e puxa o resto"_t);
        app.run ("edit.undo");

        grid.setTimeSelection ({ 1920, 3840 });
        rep.check (grid.keyPressed (juce::KeyPress (juce::KeyPress::insertKey, ctrl, 0)) && itemsOnTrack (1)[0].start == 7680,
                   "Ctrl+Insert abre um espaço do tamanho do trecho"_t);
        app.run ("edit.undo");
        grid.setTimeSelection ({});
        app.run ("transport.setPosition", args ({ { "at", 0 } }));
    }

    runVoiceClipChecks (ctx, work);
}

} // namespace whynot::selftest

#include "MidiExport.h"
#include "Projector.h"
#include "../model/NoteCurves.h"

namespace whynot
{

namespace
{
    /** O glide do 808 (nota com slide num canal mono) vira pitch bend (26/10): a nota de onde o slide sai segue até o
        fim da última que desliza dela, e o pitch wheel faz o caminho entre as teclas no tempo do glide, como o synth e
        o sampler tocam. As curvas desenhadas nas notas do grupo somam por cima. Até 24 semitons de distância da
        primeira nota; mais longe, a nota fica separada (legato, como antes). As notas vêm do Projector::notesFor, em
        ordem e com o legato do mono. */
    std::vector<Note> glidesAsBend (const std::vector<Note>& notes, double glideTicks)
    {
        std::vector<Note> out;

        for (size_t i = 0; i < notes.size();)
        {
            const auto& head = notes[i];
            size_t j = i + 1;

            while (j < notes.size() && notes[j].slide && notes[j].start > notes[j - 1].start && notes[j].start < notes[j - 1].end()
                   && std::abs (notes[j].key - head.key) <= (int) curves::maxBendSemis)
                ++j;

            if (j == i + 1)
            {
                out.push_back (head);
                ++i;
                continue;
            }

            int end = head.end();

            for (size_t k = i; k < j; ++k)
                end = std::max (end, notes[k].end());

            auto segmentAt = [&] (int t)
            {
                size_t k = i;

                while (k + 1 < j && notes[k + 1].start - head.start <= t)
                    ++k;

                return k;
            };

            auto pitchAt = [&] (int t)
            {
                const auto k = segmentAt (t);
                const int s = notes[k].start - head.start;
                float base = (float) (notes[k].key - head.key);

                if (k > i && glideTicks > 0.0)
                {
                    const float from = (float) (notes[k - 1].key - head.key);
                    base = from + (base - from) * (float) std::min (1.0, (t - s) / glideTicks);
                }

                return base + curves::valueAt (notes[k].pitch, t - s);
            };

            auto modAt = [&] (int t)
            {
                const auto k = segmentAt (t);
                return curves::valueAt (notes[k].mod, t - (notes[k].start - head.start));
            };

            // os cantos (começo de cada nota, um tick antes, o fim do glide, os pontos desenhados) e uma amostra a
            // cada stepTicks; o simplify deixa só os cantos de novo
            const int length = end - head.start;
            std::set<int> times;

            for (int t = 0; t < length; t += curves::stepTicks)
                times.insert (t);

            for (size_t k = i; k < j; ++k)
            {
                const int s = notes[k].start - head.start;
                times.insert (s);
                times.insert (std::max (0, s - 1));
                times.insert (std::min (length - 1, s + (int) std::ceil (glideTicks)));

                for (auto* pts : { &notes[k].pitch, &notes[k].mod })
                    for (auto& pt : *pts)
                        if (s + pt.t < length)
                            times.insert (s + pt.t);
            }

            std::vector<CurvePoint> pitch, mod;

            for (int t : times)
            {
                pitch.push_back ({ t, pitchAt (t) });
                mod.push_back ({ t, modAt (t) });
            }

            // Sem o curves::tidy: ele limita os pontos a 16 compassos e a 256, e uma linha de 808 com slide em toda nota
            // passa disso. Aqui a nota só vira eventos do arquivo.
            Note merged = head;
            merged.length = length;
            merged.slide = false;
            merged.pitch = curves::simplify (pitch, 0.005f);
            merged.mod = curves::simplify (mod, 0.002f);

            for (auto* pts : { &merged.pitch, &merged.mod })
                if (std::all_of (pts->begin(), pts->end(), [] (const CurvePoint& p) { return p.v == 0.0f; }))
                    pts->clear();

            out.push_back (std::move (merged));
            i = j;
        }

        return out;
    }

    /** O alcance do pitch bend no começo da trilha (RPN 0), para quem lê o MIDI dobrar o mesmo tanto. */
    void addBendRange (juce::MidiMessageSequence& seq, int semitones)
    {
        seq.addEvent (juce::MidiMessage::controllerEvent (1, 101, 0), 0.0);
        seq.addEvent (juce::MidiMessage::controllerEvent (1, 100, 0), 0.0);
        seq.addEvent (juce::MidiMessage::controllerEvent (1, 6, semitones), 0.0);
        seq.addEvent (juce::MidiMessage::controllerEvent (1, 38, 0), 0.0);
        seq.addEvent (juce::MidiMessage::controllerEvent (1, 101, 127), 0.0);   // fecha o RPN (o "nulo")
        seq.addEvent (juce::MidiMessage::controllerEvent (1, 100, 127), 0.0);
    }
}

juce::Result writeMidiFile (const Project& p, bool song, int loops, const juce::File& out)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote (ticksPerBeat);

    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / p.bpm)), 0.0);
    conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (p.timeSigNum, p.timeSigDen), 0.0);
    conductor.addEvent (juce::MidiMessage::textMetaEvent (3, p.name), 0.0);   // nome da sequência
    file.addTrack (conductor);

    int written = 0;

    for (auto& c : p.channels)
    {
        if (c.isAudio())
            continue;

        // (início no projeto, notas relativas a ele): igual ao que o projetor monta para o motor
        std::vector<std::pair<int, std::vector<Note>>> blocks;

        if (song)
        {
            for (auto& item : p.playlistItems)
                if (item.kind == "pattern" && p.itemSounds (item))
                    if (auto* pat = p.findPattern (item.ref))
                        blocks.push_back ({ item.start, Projector::notesFor (p, *pat, c, item.length, item.patternOffset, Projector::usesChance (*pat, c.id) ? item.start - item.patternOffset : 0) });
        }
        else if (auto* pat = p.currentPatternPtr())
        {
            blocks.push_back ({ 0, Projector::notesFor (p, *pat, c, pat->lengthTicks() * juce::jmax (1, loops)) });
        }

        // o glide do 808 sai como pitch bend, não como notas sobrepostas (26/10)
        if (c.mono)
            for (auto& block : blocks)
                block.second = glidesAsBend (block.second, secondsToTicks (c.glideMs / 1000.0, p.bpm));

        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, c.name), 0.0);   // nome da faixa

        // O alcance do pitch bend da trilha: o menor número inteiro de semitons em que cabe a maior curva (2 = o
        // padrão do MIDI, que não precisa de RPN).
        float maxBend = 0.0f;

        for (auto& block : blocks)
            maxBend = std::max (maxBend, curves::maxBend (block.second));

        const int range = juce::jlimit (2, 24, (int) std::ceil (maxBend - 0.001f));

        if (range != 2)
            addBendRange (seq, range);

        for (auto& [start, notes] : blocks)
        {
            // as curvas: as mesmas contas do projetor (o que toca é o que sai). Antes das notas: no mesmo tick, o
            // pitch wheel chega antes do note-on, e o synth que suaviza o bend não começa a nota no bend antigo.
            for (auto& e : curves::controlEvents (notes, curves::Kind::pitch, (float) range))
                seq.addEvent (juce::MidiMessage::pitchWheel (1, e.value), start + e.t);

            for (auto& e : curves::controlEvents (notes, curves::Kind::mod))
                seq.addEvent (juce::MidiMessage::controllerEvent (1, 1, e.value), start + e.t);

            // Na mesma tecla, a nota não pode passar do começo da próxima: o note-off atrasado (legato do
            // slide) cortaria a nota nova no programa que lê o MIDI.
            std::map<int, std::vector<std::pair<int, const Note*>>> byKey;

            for (auto& n : notes)
                byKey[n.key].push_back ({ n.start, &n });

            for (auto& [key, list] : byKey)
            {
                std::stable_sort (list.begin(), list.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });

                for (size_t i = 0; i < list.size(); ++i)
                {
                    const auto& n = *list[i].second;

                    if (i + 1 < list.size() && list[i + 1].first <= n.start)
                        continue;   // duas notas na mesma tecla e no mesmo tick (passo + piano roll): fica a última

                    int length = juce::jmax (1, n.length);

                    if (i + 1 < list.size())
                        length = juce::jmax (1, juce::jmin (length, list[i + 1].first - n.start));

                    const double on = start + n.start;
                    seq.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) juce::jlimit (1, 127, n.velocity)), on);
                    seq.addEvent (juce::MidiMessage::noteOff (1, key), on + length);
                }
            }
        }

        if (seq.getNumEvents() <= 1)
            continue;   // canal sem nenhuma nota

        seq.updateMatchedPairs();
        file.addTrack (seq);
        ++written;
    }

    if (written == 0)
        return juce::Result::fail ("Não há notas para exportar em MIDI."_t);

    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream stream (out);

    if (! stream.openedOk() || ! file.writeTo (stream, 1))
        return juce::Result::fail ("Não consegui gravar o MIDI em "_t + out.getFullPathName());

    return juce::Result::ok();
}

} // namespace whynot

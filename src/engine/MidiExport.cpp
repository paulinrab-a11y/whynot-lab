#include "MidiExport.h"
#include "Projector.h"

namespace whynot
{

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
                        blocks.push_back ({ item.start, Projector::notesFor (p, *pat, c, item.length, item.patternOffset) });
        }
        else if (auto* pat = p.currentPatternPtr())
        {
            blocks.push_back ({ 0, Projector::notesFor (p, *pat, c, pat->lengthTicks() * juce::jmax (1, loops)) });
        }

        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, c.name), 0.0);   // nome da faixa

        for (auto& [start, notes] : blocks)
        {
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

#pragma once

#include "../model/Project.h"

#include <deque>

/** Capture MIDI (bloco 3, 23/10; o "Capture" do Ableton, um dos recursos mais elogiados): o app guarda as últimas
    notas tocadas ao vivo (o teclado do computador e o controlador), gravando ou não, e o comando
    record.captureMidi traz a última frase para o padrão. Nada se perde quando a ideia boa sai sem o REC.
    Só a thread de mensagem mexe aqui. */
namespace whynot::capture
{

struct Event
{
    double ms = 0.0;             // relógio em milissegundos (juce::Time::getMillisecondCounterHiRes)
    int key = 60, velocity = 100;
    bool on = true;
    double patternTick = -1.0;   // tocando o padrão (PAT): onde a nota caiu nele; -1 = parado ou na música
};

class Buffer
{
public:
    static constexpr size_t capacity = 2048;
    static constexpr double phraseGapMs = 2500.0;   // uma pausa maior que esta entre duas notas separa as frases

    void add (const Event& e)
    {
        events.push_back (e);

        if (events.size() > capacity)
            events.pop_front();
    }

    void clear() noexcept         { events.clear(); }
    bool isEmpty() const noexcept { return events.empty(); }
    size_t size() const noexcept  { return events.size(); }

    /** A última frase (as notas depois da última pausa de mais de 2,5 s), em notas do padrão.
        - Tocada com o padrão rodando: cada nota fica onde caiu nele (`inTime` = true).
        - Parado: a primeira nota vai para o começo, e as outras seguem no BPM do projeto.
        A próxima mensagem da mesma tecla fecha a nota; uma nota ainda presa termina em `nowMs`. */
    std::vector<Note> phrase (double bpm, double nowMs, bool& inTime) const
    {
        std::vector<Note> out;
        inTime = false;
        size_t first = events.size();
        double lastOn = -1.0;

        for (size_t i = 0; i < events.size(); ++i)
        {
            if (! events[i].on)
                continue;

            if (first == events.size() || events[i].ms - lastOn > phraseGapMs)
                first = i;

            lastOn = events[i].ms;
        }

        if (first == events.size())
            return out;

        inTime = true;

        for (size_t i = first; i < events.size(); ++i)
            if (events[i].on && events[i].patternTick < 0.0)
                inTime = false;

        const double msPerTick = 60000.0 / juce::jmax (1.0, bpm) / ticksPerBeat;
        const double startMs = events[first].ms;

        for (size_t i = first; i < events.size(); ++i)
        {
            const auto& e = events[i];

            if (! e.on)
                continue;

            double offMs = juce::jmax (e.ms, nowMs);

            for (size_t j = i + 1; j < events.size(); ++j)
                if (events[j].key == e.key)
                {
                    offMs = events[j].ms;
                    break;
                }

            Note n;
            n.key = juce::jlimit (0, 127, e.key);
            n.velocity = juce::jlimit (1, 127, e.velocity);
            n.start = juce::jmax (0, juce::roundToInt (inTime ? e.patternTick : (e.ms - startMs) / msPerTick));
            n.length = juce::jmax (1, juce::roundToInt ((offMs - e.ms) / msPerTick));
            out.push_back (n);
        }

        std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        return out;
    }

private:
    std::deque<Event> events;
};

} // namespace whynot::capture

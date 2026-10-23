#pragma once

#include <JuceHeader.h>

#include <vector>

namespace whynot::synthui
{

/** Estrela de cinco pontas centrada em `area` (favoritos). */
juce::Path starPath (juce::Rectangle<float> area);

/** Gráficos da janela do WHYNOT Synth. Desenham a partir dos valores do timbre (na ordem de
    synth::allParams), sem tocar no motor: o que aparece é o que vai soar. */
class Graph : public juce::Component
{
public:
    void paint (juce::Graphics&) override;

protected:
    juce::Rectangle<float> plotArea() const   { return getLocalBounds().toFloat().reduced (8.0f, 6.0f).withTrimmedBottom (14.0f); }
    virtual void paintGraph (juce::Graphics&, juce::Rectangle<float> area) = 0;

    juce::String caption;   // legenda embaixo, à esquerda
    juce::String note;      // legenda embaixo, à direita
    bool dim = false;       // desligado: tudo em cinza
};

/** Um ciclo da camada escolhida (ou do sub, layer = -1). */
class WaveView : public Graph
{
public:
    void show (const std::vector<float>& values, int layer);

private:
    void paintGraph (juce::Graphics&, juce::Rectangle<float>) override;
    std::vector<float> points;
};

/** Resposta do filtro no corte de repouso e até onde o envelope leva. */
class FilterView : public Graph
{
public:
    void show (const std::vector<float>& values);

private:
    void paintGraph (juce::Graphics&, juce::Rectangle<float>) override;
    std::vector<float> rest, opened;   // dB em 128 pontos de 20 Hz a 20 kHz
};

/** Forma do envelope (ataque, decaimento, sustentação e soltura) com a curva real do motor. */
class EnvView : public Graph
{
public:
    void show (const std::vector<float>& values, int envelope);

private:
    void paintGraph (juce::Graphics&, juce::Rectangle<float>) override;
    std::vector<float> points;
    float releaseStart = 0.7f;   // fração do eixo onde a nota é solta
};

/** Dois ciclos do LFO. */
class LfoView : public Graph
{
public:
    void show (const std::vector<float>& values, int lfo);

private:
    void paintGraph (juce::Graphics&, juce::Rectangle<float>) override;
    std::vector<float> points;
};

} // namespace whynot::synthui

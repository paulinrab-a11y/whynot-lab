#include "FxVisuals.h"
#include "FxWidgets.h"
#include "../Theme.h"
#include "../../dsp/Color.h"
#include "../../dsp/Console.h"
#include "../../dsp/Dynamics.h"
#include "../../dsp/EqProcessor.h"
#include "../../dsp/Master.h"
#include "../../dsp/Multiband.h"
#include "../../dsp/Space.h"
#include "../../dsp/Tune.h"
#include "../../dsp/Vintage.h"

namespace whynot
{

float FxVisual::value (const char* id) const
{
    const int i = indexOf (id);
    return i >= 0 && i < (int) values.size() ? values[(size_t) i] : 0.0f;
}

namespace
{
    using namespace theme;

    constexpr double displayRate = 48000.0;
    constexpr double minHz = 20.0, maxHz = 20000.0;

    float xForHz (double hz, juce::Rectangle<float> r)
    {
        return r.getX() + r.getWidth() * (float) (std::log (hz / minHz) / std::log (maxHz / minHz));
    }

    double hzForX (float x, juce::Rectangle<float> r)
    {
        return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth())));
    }

    juce::String hzLabel (double hz)
    {
        return hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String ((int) hz);
    }

    /** Fundo de gráfico: cartão rebaixado com grade de frequências e linhas de dB. */
    void drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> r, float rangeDb, float stepDb)
    {
        g.setColour (colour::background.withAlpha (0.55f));
        g.fillRoundedRectangle (r, metric::radius);

        g.setFont (captionFont());

        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = xForHz (hz, r);
            g.setColour (colour::line);
            g.drawVerticalLine ((int) x, r.getY() + 4.0f, r.getBottom() - 4.0f);
            g.setColour (colour::textFaint);
            g.drawText (hzLabel (hz), juce::Rectangle<float> (x + 3.0f, r.getBottom() - 16.0f, 40.0f, 14.0f), juce::Justification::centredLeft);
        }

        for (float db = -rangeDb; db <= rangeDb + 0.01f; db += stepDb)
        {
            const float y = juce::jmap (db, rangeDb, -rangeDb, r.getY() + 8.0f, r.getBottom() - 8.0f);
            g.setColour (db == 0.0f ? colour::raisedHigh : colour::line);
            g.drawHorizontalLine ((int) y, r.getX() + 4.0f, r.getRight() - 4.0f);

            if (db != 0.0f)
            {
                g.setColour (colour::textFaint);
                g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (r.getX() + 6.0f, y - 7.0f, 30.0f, 14.0f),
                            juce::Justification::centredLeft);
            }
        }
    }

    //==========================================================================
    /** Analisador de espectro a partir da derivação do processador (FFT de 4096, Hann). */
    class Spectrum
    {
    public:
        static constexpr int order = 12, size = 1 << order;

        Spectrum() : fft (order), window ((size_t) size, juce::dsp::WindowingFunction<float>::hann, false)
        {
            smoothed.fill (-120.0f);
        }

        void update (const dsp::SpectrumTap& tap)
        {
            const auto written = tap.read (data.data(), size);

            if (written == lastWritten)
            {
                // Sem áudio novo: o desenho cai devagar até sumir.
                for (auto& v : smoothed)
                    v = juce::jmax (-120.0f, v - 1.5f);

                return;
            }

            lastWritten = written;
            std::fill (data.begin() + size, data.end(), 0.0f);
            window.multiplyWithWindowingTable (data.data(), (size_t) size);
            fft.performFrequencyOnlyForwardTransform (data.data(), true);

            for (int i = 0; i < size / 2; ++i)
            {
                // Seno de amplitude 1 com Hann dá N/4 no pico: normaliza para 0 dB.
                const float db = juce::Decibels::gainToDecibels (data[(size_t) i] / (size * 0.25f), -120.0f);
                auto& s = smoothed[(size_t) i];
                s = db > s ? db : s + (db - s) * 0.25f;
            }
        }

        /** Caminho do espectro dentro de r, com dB de -90 a 0 mapeados na altura. */
        juce::Path path (juce::Rectangle<float> r) const
        {
            juce::Path p;
            p.startNewSubPath (r.getX(), r.getBottom());

            for (float x = r.getX(); x <= r.getRight(); x += 2.0f)
            {
                const double f0 = hzForX (x, r), f1 = hzForX (x + 2.0f, r);
                const int b0 = juce::jlimit (1, size / 2 - 1, (int) (f0 * size / displayRate));
                const int b1 = juce::jlimit (b0, size / 2 - 1, (int) (f1 * size / displayRate));
                float db = -120.0f;

                for (int b = b0; b <= b1; ++b)
                    db = juce::jmax (db, smoothed[(size_t) b]);

                // inclinação de +4,5 dB/oitava: a mixagem "plana" fica plana na tela
                db += 4.5f * (float) std::log2 (juce::jmax (f0, 20.0) / 1000.0);
                p.lineTo (x, juce::jmap (juce::jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, r.getBottom(), r.getY()));
            }

            p.lineTo (r.getRight(), r.getBottom());
            p.closeSubPath();
            return p;
        }

    private:
        juce::dsp::FFT fft;
        juce::dsp::WindowingFunction<float> window;
        std::array<float, size * 2> data {};
        std::array<float, size / 2> smoothed {};
        std::uint32_t lastWritten = 0;
    };

    //==========================================================================
    class EqVisual : public FxVisual
    {
    public:
        explicit EqVisual (WhynotFxPlugin& p) : FxVisual (p)
        {
            setRepaintsOnMouseActivity (true);
        }

        void tick() override
        {
            if (auto* eq = dynamic_cast<dsp::EqProcessor*> (&plugin.getHost().processor()))
                spectrum.update (eq->spectrum());

            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = graphArea();
            drawFrequencyGrid (g, r, 18.0f, 6.0f);

            g.setColour (colour::textFaint.withAlpha (0.35f));
            g.fillPath (spectrum.path (r.reduced (2.0f)));

            if (values.size() < (size_t) dsp::EqProcessor::count)
                return;

            juce::Path curve;

            for (float x = r.getX(); x <= r.getRight(); x += 1.5f)
            {
                const float db = (float) dsp::EqProcessor::responseDb (values.data(), hzForX (x, r), displayRate);
                const float y = yForDb (db, r);
                x == r.getX() ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
            }

            g.setColour (colour::text);
            g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            for (int n = 0; n < numNodes; ++n)
            {
                const auto pos = nodePosition (n, r);
                const bool active = nodeActive (n);
                const bool hot = n == dragging || (dragging < 0 && n == hover);
                const float size = hot ? 14.0f : 11.0f;

                g.setColour (hot ? colour::signal : (active ? colour::text : colour::textFaint));
                g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (pos));
                g.setColour (colour::panel);
                g.setFont (font (9.0f, Weight::bold));
                g.drawText (nodeLabel (n), juce::Rectangle<float> (size, size).withCentre (pos), juce::Justification::centred);
            }

            if (const int n = dragging >= 0 ? dragging : hover; n >= 0)
            {
                g.setColour (colour::text);
                g.setFont (numberFont (type::small));
                g.drawText (describeNode (n), r.reduced (12.0f, 8.0f), juce::Justification::topRight);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int n = nodeAt (e.position);

            if (n != hover)
            {
                hover = n;
                setMouseCursor (n >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override   { hover = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = nodeAt (e.position);

            if (dragging >= 0 && onGestureStart)
                onGestureStart();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging < 0)
                return;

            const auto r = graphArea();
            const auto& nd = nodes[(size_t) dragging];
            send (nd.freq, (float) hzForX (e.position.x, r));

            if (nd.gain != nullptr)
                send (nd.gain, juce::jlimit (-18.0f, 18.0f, dbForY (e.position.y, r)));
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging >= 0 && onGestureEnd)
                onGestureEnd();

            dragging = -1;
            repaint();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int n = nodeAt (e.position);

            if (n < 0)
                return;

            // Duplo clique: liga/desliga o corte, ou zera o ganho da faixa.
            if (nodes[(size_t) n].on != nullptr)
                send (nodes[(size_t) n].on, value (nodes[(size_t) n].on) >= 0.5f ? 0.0f : 1.0f);
            else if (nodes[(size_t) n].gain != nullptr)
                send (nodes[(size_t) n].gain, 0.0f);
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            const int n = nodeAt (e.position);

            if (n >= 0 && nodes[(size_t) n].q != nullptr)
                send (nodes[(size_t) n].q, juce::jlimit (0.1f, 18.0f, value (nodes[(size_t) n].q) * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f)));
        }

    private:
        struct Node { const char* freq; const char* gain; const char* q; const char* on; const char* label; const char* name; };

        static constexpr int numNodes = 7;
        const std::array<Node, numNodes> nodes { {
            { "hpFreq",   nullptr,    nullptr, "hpOn", "C", "Corte grave" },
            { "lowFreq",  "lowGain",  nullptr, nullptr, "G", "Grave" },
            { "b1Freq",   "b1Gain",   "b1Q",   nullptr, "1", "Sino 1" },
            { "b2Freq",   "b2Gain",   "b2Q",   nullptr, "2", "Sino 2" },
            { "b3Freq",   "b3Gain",   "b3Q",   nullptr, "3", "Sino 3" },
            { "highFreq", "highGain", nullptr, nullptr, "A", "Agudo" },
            { "lpFreq",   nullptr,    nullptr, "lpOn", "C", "Corte agudo" },
        } };

        juce::Rectangle<float> graphArea() const   { return getLocalBounds().toFloat().reduced (2.0f); }

        static float yForDb (float db, juce::Rectangle<float> r)
        {
            return juce::jmap (juce::jlimit (-24.0f, 24.0f, db), 18.0f, -18.0f, r.getY() + 8.0f, r.getBottom() - 8.0f);
        }

        static float dbForY (float y, juce::Rectangle<float> r)
        {
            return juce::jmap (y, r.getY() + 8.0f, r.getBottom() - 8.0f, 18.0f, -18.0f);
        }

        bool nodeActive (int n) const
        {
            const auto& nd = nodes[(size_t) n];
            return nd.on != nullptr ? value (nd.on) >= 0.5f : value (nd.gain) != 0.0f;
        }

        juce::Point<float> nodePosition (int n, juce::Rectangle<float> r) const
        {
            const auto& nd = nodes[(size_t) n];
            const float y = nd.gain != nullptr ? yForDb (value (nd.gain), r) : yForDb (0.0f, r);
            return { xForHz (juce::jlimit (minHz, maxHz, (double) value (nd.freq)), r), y };
        }

        int nodeAt (juce::Point<float> p) const
        {
            const auto r = graphArea();

            for (int n = numNodes; --n >= 0;)
                if (nodePosition (n, r).getDistanceFrom (p) < 10.0f)
                    return n;

            return -1;
        }

        juce::String nodeLabel (int n) const   { return nodes[(size_t) n].label; }

        juce::String describeNode (int n) const
        {
            const auto& nd = nodes[(size_t) n];
            const auto& info = plugin.getInfo();
            auto text = fxui::utf8 (nd.name) + ": " + fxui::formatValue (info.params[(size_t) indexOf (nd.freq)], value (nd.freq));

            if (nd.gain != nullptr)
                text << "  " << fxui::formatValue (info.params[(size_t) indexOf (nd.gain)], value (nd.gain));

            if (nd.q != nullptr)
                text << "  " << fxui::formatValue (info.params[(size_t) indexOf (nd.q)], value (nd.q));

            if (nd.on != nullptr && value (nd.on) < 0.5f)
                text << "  (desligado: duplo clique liga)";

            return text;
        }

        void send (const char* id, float v)
        {
            const int i = indexOf (id);

            if (i >= 0 && onParam)
                onParam (i, plugin.getInfo().params[(size_t) i].clamp (v));
        }

        Spectrum spectrum;
        int hover = -1, dragging = -1;
    };

    //==========================================================================
    class CompVisual : public FxVisual
    {
    public:
        explicit CompVisual (WhynotFxPlugin& p) : FxVisual (p)   { addAndMakeVisible (reduction); }

        void tick() override
        {
            const auto& proc = plugin.getHost().processor();
            reduction.setReduction (plugin.getHost().isBypassed() ? 0.0f : proc.meter (dsp::CompressorProcessor::gainReduction));
            const float in = proc.meter (dsp::CompressorProcessor::inputLevel);

            if (std::abs (in - inputDb) > 0.2f)
            {
                inputDb = in;
                repaint();
            }
        }

        void resized() override   { reduction.setBounds (getLocalBounds().removeFromRight (40).reduced (0, 6)); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            r.removeFromRight (48.0f);
            auto graph = r.removeFromLeft (r.getHeight()).reduced (2.0f);

            g.setColour (colour::background.withAlpha (0.55f));
            g.fillRoundedRectangle (graph, metric::radius);

            auto map = [&graph] (float inDb, float outDb)
            {
                return juce::Point<float> (juce::jmap (inDb, -60.0f, 0.0f, graph.getX() + 6.0f, graph.getRight() - 6.0f),
                                           juce::jmap (juce::jlimit (-60.0f, 0.0f, outDb), -60.0f, 0.0f, graph.getBottom() - 6.0f, graph.getY() + 6.0f));
            };

            g.setColour (colour::line);
            g.drawLine ({ map (-60.0f, -60.0f), map (0.0f, 0.0f) }, 1.0f);

            float thr = value ("threshold"), ratio = juce::jmax (1.0f, value ("ratio")), knee = value ("knee"), overshoot = 1.0f;
            const auto& fxType = plugin.getFxType();

            if (fxType == "opto")
            {
                const bool limit = value ("mode") >= 0.5f;
                thr = -0.4f * value ("reduction");
                ratio = limit ? 10.0f : 3.0f;
                knee = limit ? 6.0f : 10.0f;
            }
            else if (fxType == "fet")
            {
                static constexpr float ratios[] = { 4.0f, 8.0f, 12.0f, 20.0f, 20.0f };
                static constexpr float knees[] = { 6.0f, 4.0f, 3.0f, 2.0f, 0.0f };
                const int choice = juce::jlimit (0, 4, (int) value ("ratio"));
                ratio = ratios[choice];
                knee = knees[choice];
                overshoot = choice == 4 ? 1.3f : 1.0f;
            }

            juce::Path curve;

            for (float in = -60.0f; in <= 0.01f; in += 0.5f)
            {
                const auto pt = map (in, in + overshoot * dsp::compressorGainDb (in, thr, ratio, knee));
                in == -60.0f ? curve.startNewSubPath (pt) : curve.lineTo (pt);
            }

            g.setColour (colour::text);
            g.strokePath (curve, juce::PathStrokeType (2.0f));

            g.setColour (colour::textFaint);
            const auto t = map (thr, thr);
            g.drawVerticalLine ((int) t.x, graph.getY() + 6.0f, graph.getBottom() - 6.0f);

            if (inputDb > -60.0f)
            {
                g.setColour (colour::signal);
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (map (inputDb, inputDb + overshoot * dsp::compressorGainDb (inputDb, thr, ratio, knee))));
            }

            auto text = r.reduced (16.0f, 4.0f);
            g.setFont (captionFont());
            g.setColour (colour::textFaint);
            g.drawText ("ENTRADA x SAÍDA"_t, text.removeFromTop (16.0f), juce::Justification::topLeft);
            g.setFont (font (type::body));
            g.setColour (colour::textDim);
            const auto thrText = juce::String (thr, 1).replaceCharacter ('.', ',') + " dB";
            const auto how = fxType == "opto" ? "Soltura em dois estágios: metade sai rápido e o resto devagar, conforme o canto.\n"_t
                           : fxType == "fet" && overshoot > 1.0f ? "Todas as razões juntas: passa do limiar e o som esmaga.\n"_t
                           : juce::String();
            g.drawFittedText ("Acima do limiar ("_t + thrText + "), cada "
                                  + juce::String (ratio, 1).replaceCharacter ('.', ',') + " dB de entrada viram 1 dB.\n" + how
                                  + "O ponto laranja é o nível agora; a barra à direita, quanto está reduzindo."_t,
                              text.toNearestInt(), juce::Justification::topLeft, 5);
        }

    private:
        ReductionMeter reduction;
        float inputDb = -100.0f;
    };

    //==========================================================================
    class SatVisual : public FxVisual
    {
    public:
        explicit SatVisual (WhynotFxPlugin& p) : FxVisual (p) {}

        void tick() override
        {
            const float c = plugin.getHost().processor().meter (dsp::SaturatorProcessor::compensation);

            if (std::abs (c - compensation) > 0.1f)
            {
                compensation = c;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            auto graph = r.removeFromLeft (r.getHeight()).reduced (2.0f);

            g.setColour (colour::background.withAlpha (0.55f));
            g.fillRoundedRectangle (graph, metric::radius);
            g.setColour (colour::line);
            g.drawLine (graph.getX() + 6.0f, graph.getCentreY(), graph.getRight() - 6.0f, graph.getCentreY());
            g.drawLine (graph.getCentreX(), graph.getY() + 6.0f, graph.getCentreX(), graph.getBottom() - 6.0f);

            const auto mode = (dsp::SaturationMode) (int) value ("mode");
            const float drive = juce::Decibels::decibelsToGain (value ("drive"));
            const float mix = value ("mix") * 0.01f;
            juce::Path curve;

            for (int i = 0; i <= 120; ++i)
            {
                const float x = -1.0f + 2.0f * (float) i / 120.0f;
                const float y = juce::jlimit (-1.2f, 1.2f, x + mix * (dsp::saturate (mode, x * drive) - x));
                const juce::Point<float> pt (juce::jmap (x, -1.0f, 1.0f, graph.getX() + 6.0f, graph.getRight() - 6.0f),
                                             juce::jmap (y, -1.2f, 1.2f, graph.getBottom() - 6.0f, graph.getY() + 6.0f));
                i == 0 ? curve.startNewSubPath (pt) : curve.lineTo (pt);
            }

            g.setColour (colour::text);
            g.strokePath (curve, juce::PathStrokeType (2.0f));

            auto text = r.reduced (16.0f, 4.0f);
            g.setFont (captionFont());
            g.setColour (colour::textFaint);
            g.drawText ("CURVA DE SATURAÇÃO"_t, text.removeFromTop (16.0f), juce::Justification::topLeft);
            g.setFont (font (type::body));
            g.setColour (colour::textDim);

            juce::String note = "Quanto mais a curva entorta, mais harmônicos."_t;

            if (value ("protect") >= 20.0f)
                note << "\nAbaixo de " << fxui::formatValue (plugin.getInfo().params[(size_t) indexOf ("protect")], value ("protect"))
                     << " o som passa limpo.";

            if (value ("autoGain") >= 0.5f)
                note << "\nCompensação de volume: "_t << juce::String (compensation, 1).replaceCharacter ('.', ',') << " dB.";

            g.drawFittedText (note, text.toNearestInt(), juce::Justification::topLeft, 5);
        }

    private:
        float compensation = 0.0f;
    };

    //==========================================================================
    /** Limitador e medidor: leituras de LUFS, histórico de curto prazo e alvo do gênero. */
    class LoudnessVisual : public FxVisual
    {
    public:
        explicit LoudnessVisual (WhynotFxPlugin& p) : FxVisual (p)
        {
            limiter = dynamic_cast<dsp::LimiterProcessor*> (&plugin.getHost().processor());
            meterProc = dynamic_cast<dsp::MeterProcessor*> (&plugin.getHost().processor());
            history.fill (dsp::LoudnessMeter::silence);

            resetButton.setTooltip ("Zera o integrado e o pico real (comece a medir do início da música)."_t);
            resetButton.onClick = [this] { if (auto* m = meter()) m->requestReset(); };
            addAndMakeVisible (resetButton);

            if (limiter != nullptr)
            {
                matchButton.setTooltip ("Ajusta o ganho para o integrado chegar no alvo. Toque a música inteira antes."_t);
                matchButton.onClick = [this] { matchTarget(); };
                addAndMakeVisible (matchButton);
                addAndMakeVisible (reduction);
            }
            else
            {
                addAndMakeVisible (correlation);
            }
        }

        void tick() override
        {
            auto* m = meter();

            if (m == nullptr)
                return;

            if (++frame % 3 == 0)
            {
                history[(size_t) historyPos] = m->shortTerm();
                historyPos = (historyPos + 1) % (int) history.size();
            }

            if (limiter != nullptr)
                reduction.setReduction (plugin.getHost().isBypassed() ? 0.0f : limiter->meter (dsp::LimiterProcessor::gainReduction));
            else
                correlation.setValue (m->correlation());

            repaint();
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (2);
            auto side = r.removeFromRight (limiter != nullptr ? 48 : 0);
            reduction.setBounds (side.reduced (0, 6));

            auto buttons = getLocalBounds().reduced (2).removeFromBottom (30).removeFromLeft (300);
            resetButton.setBounds (buttons.removeFromLeft (90).reduced (0, 2));
            buttons.removeFromLeft (8);
            matchButton.setBounds (buttons.removeFromLeft (130).reduced (0, 2));

            if (limiter == nullptr)
                correlation.setBounds (getLocalBounds().reduced (2).removeFromBottom (30).removeFromRight (260));
        }

        void paint (juce::Graphics& g) override
        {
            auto* m = meter();

            if (m == nullptr)
                return;

            auto r = getLocalBounds().toFloat().reduced (2.0f);
            r.removeFromBottom (36.0f);

            if (limiter != nullptr)
                r.removeFromRight (56.0f);

            auto readouts = r.removeFromLeft (200.0f);
            const float target = value ("target");

            auto reading = [&g] (juce::Rectangle<float> area, const juce::String& caption, float v, const juce::String& unit, float size, bool warn)
            {
                g.setFont (captionFont());
                g.setColour (colour::textFaint);
                g.drawText (caption, area.removeFromTop (14.0f), juce::Justification::topLeft);
                g.setFont (numberFont (size));
                g.setColour (warn ? colour::meterHigh : colour::text);
                g.drawText (v <= -99.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) : juce::String (v, 1).replaceCharacter ('.', ',') + " " + unit,
                            area, juce::Justification::topLeft);
            };

            reading (readouts.removeFromTop (54.0f), "CURTO PRAZO", m->shortTerm(), "LUFS", 30.0f, false);
            auto row = readouts.removeFromTop (44.0f);
            reading (row.removeFromLeft (100.0f), "INTEGRADO", m->integrated(), "", 18.0f, false);
            reading (row, "MOMENTÂNEO"_t, m->momentary(), "", 18.0f, false);
            row = readouts.removeFromTop (44.0f);
            reading (row.removeFromLeft (100.0f), "PICO REAL", m->truePeak(), "", 18.0f, m->truePeak() > -0.5f);
            reading (row, "ALVO", target, "", 18.0f, false);

            // Histórico de 30 s do curto prazo, com a faixa do alvo (±1 LU).
            auto graph = r.reduced (12.0f, 0.0f);
            g.setColour (colour::background.withAlpha (0.55f));
            g.fillRoundedRectangle (graph, metric::radius);

            auto yFor = [&graph] (float lufs) { return juce::jmap (juce::jlimit (-30.0f, 0.0f, lufs), -30.0f, 0.0f, graph.getBottom() - 6.0f, graph.getY() + 6.0f); };

            g.setColour (colour::signalTint);
            g.fillRect (juce::Rectangle<float> (graph.getX() + 2.0f, yFor (target + 1.0f), graph.getWidth() - 4.0f, yFor (target - 1.0f) - yFor (target + 1.0f)));
            g.setColour (colour::signal.withAlpha (0.7f));
            g.drawHorizontalLine ((int) yFor (target), graph.getX() + 2.0f, graph.getRight() - 2.0f);

            g.setFont (captionFont());

            for (float l : { -24.0f, -18.0f, -12.0f, -6.0f })
            {
                g.setColour (colour::textFaint);
                g.drawText (juce::String ((int) l), juce::Rectangle<float> (graph.getX() + 6.0f, yFor (l) - 7.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
            }

            juce::Path p;
            bool started = false;
            const int n = (int) history.size();

            for (int i = 0; i < n; ++i)
            {
                const float v = history[(size_t) ((historyPos + i) % n)];

                if (v <= -99.0f)
                {
                    started = false;
                    continue;
                }

                const float x = graph.getX() + graph.getWidth() * (float) i / (float) (n - 1);
                started ? p.lineTo (x, yFor (v)) : p.startNewSubPath (x, yFor (v));
                started = true;
            }

            g.setColour (colour::text);
            g.strokePath (p, juce::PathStrokeType (1.6f));
        }

    private:
        dsp::LoudnessMeter* meter() const
        {
            return limiter != nullptr ? &limiter->loudness() : (meterProc != nullptr ? &meterProc->loudness() : nullptr);
        }

        void matchTarget()
        {
            auto* m = meter();
            const int gainIndex = indexOf ("gain");

            if (m == nullptr || gainIndex < 0 || m->integrated() <= -99.0f || ! onParam)
                return;

            const float newGain = juce::jlimit (0.0f, 24.0f, value ("gain") + value ("target") - m->integrated());

            if (onGestureStart) onGestureStart();
            onParam (gainIndex, newGain);
            if (onGestureEnd) onGestureEnd();

            m->requestReset();   // mede de novo com o ganho novo
        }

        dsp::LimiterProcessor* limiter = nullptr;
        dsp::MeterProcessor* meterProc = nullptr;
        std::array<float, 300> history {};   // 30 s a 10 Hz
        int historyPos = 0, frame = 0;
        juce::TextButton resetButton { "Zerar" }, matchButton { "Igualar alvo" };
        ReductionMeter reduction;
        CorrelationMeter correlation;
    };

    //==========================================================================
    /** De-esser, transiente, estéreo, filtro, delay e reverb: um medidor ou um desenho simples. */
    /** Afinação: a nota que está saindo, onde a voz está em relação a ela e quanto a correção puxa. */
    class TuneVisual : public FxVisual
    {
    public:
        explicit TuneVisual (WhynotFxPlugin& p) : FxVisual (p) {}

        void tick() override
        {
            const auto& proc = plugin.getHost().processor();
            const float detected = proc.meter (dsp::TuneProcessor::detectedNote);
            const float target = proc.meter (dsp::TuneProcessor::targetNote);
            const float corr = proc.meter (dsp::TuneProcessor::correctionCents);
            const bool on = proc.meter (dsp::TuneProcessor::voicedMeter) > 0.5f;

            if (on != voiced || std::abs (detected - detectedNote) > 0.01f || std::abs (target - targetNote) > 0.01f
                || std::abs (corr - correction) > 0.5f)
            {
                voiced = on;
                detectedNote = detected;
                targetNote = target;
                correction = corr;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            static const char* names[] = { "Dó", "Dó#", "Ré", "Ré#", "Mi", "Fá", "Fá#", "Sol", "Sol#", "Lá", "Lá#", "Si" };
            auto r = getLocalBounds().toFloat().reduced (16.0f, 8.0f);

            // Nota de saída, grande, à esquerda
            auto noteArea = r.removeFromLeft (120.0f);
            g.setFont (captionFont());
            g.setColour (colour::textFaint);
            g.drawText ("NOTA", noteArea.removeFromTop (16.0f), juce::Justification::centredLeft);

            const int note = (int) std::lround (targetNote);
            const auto noteText = voiced && note >= 0
                                      ? juce::String::fromUTF8 (names[note % 12]) + " " + juce::String (note / 12 - 1)
                                      : juce::String ("-");
            g.setFont (displayFont (30.0f));
            g.setColour (voiced ? colour::text : colour::textFaint);
            g.drawText (noteText, noteArea.removeFromTop (40.0f), juce::Justification::centredLeft);

            // Régua de -50 a +50 cents: onde a voz está (bolinha) e a correção agindo (trecho laranja)
            r.removeFromLeft (16.0f);
            g.setFont (captionFont());
            g.setColour (colour::textFaint);
            g.drawText (voiced ? "DESVIO DA VOZ E CORREÇÃO: "_t + juce::String (juce::roundToInt (correction)) + " CENTS"
                               : "SEM VOZ AGORA"_t,
                        r.removeFromTop (16.0f), juce::Justification::centredLeft);

            const auto bar = r.withSizeKeepingCentre (r.getWidth(), 12.0f).withY (r.getY() + 14.0f);
            g.setColour (colour::raised);
            g.fillRoundedRectangle (bar, 6.0f);
            const float cx = bar.getCentreX();
            g.setColour (colour::line.brighter (0.4f));
            g.fillRect (juce::Rectangle<float> (cx - 0.5f, bar.getY() - 4.0f, 1.0f, bar.getHeight() + 8.0f));

            if (voiced && detectedNote >= 0.0f)
            {
                auto toX = [&bar] (float cents) { return bar.getX() + bar.getWidth() * (0.5f + juce::jlimit (-50.0f, 50.0f, cents) / 100.0f); };
                const float off = (detectedNote - targetNote) * 100.0f;
                const float xVoice = toX (off), xOut = toX (off + correction);

                g.setColour (colour::signal);   // a correção agindo agora, como a redução de um compressor
                g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (xVoice, xOut), bar.getY(),
                                                                std::abs (xOut - xVoice), bar.getHeight()), 6.0f);
                g.setColour (colour::text);
                g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ xVoice, bar.getCentreY() }));
            }

            g.setFont (font (type::small));
            g.setColour (colour::textFaint);
            auto legend = r.withY (bar.getBottom() + 6.0f).withHeight (16.0f);
            g.drawText ("-50", legend, juce::Justification::centredLeft);
            g.drawText ("+50", legend, juce::Justification::centredRight);
        }

    private:
        float detectedNote = -1.0f, targetNote = -1.0f, correction = 0.0f;
        bool voiced = false;
    };

    class SimpleVisual : public FxVisual
    {
    public:
        explicit SimpleVisual (WhynotFxPlugin& p) : FxVisual (p)
        {
            if (plugin.getFxType() == "stereo")
                addAndMakeVisible (correlation);
        }

        void resized() override   { correlation.setBounds (getLocalBounds().reduced (16, 0).removeFromBottom (36)); }

        void tick() override
        {
            const auto& proc = plugin.getHost().processor();
            const auto& type = plugin.getFxType();

            if (type == "stereo")
                correlation.setValue (proc.meter (dsp::StereoProcessor::correlation));
            else if (type == "deesser")
                setLevel (proc.meter (dsp::DeEsserProcessor::reduction));
            else if (type == "transient")
                setLevel (proc.meter (dsp::TransientProcessor::gainChange));
        }

        void paint (juce::Graphics& g) override
        {
            const auto& type = plugin.getFxType();
            auto r = getLocalBounds().toFloat().reduced (2.0f);

            if (type == "deesser" || type == "transient")
                paintBar (g, r, type == "deesser");
            else if (type == "filter")
                paintFilter (g, r);
            else if (type == "delay")
                paintDelay (g, r);
            else if (type == "reverb")
                paintReverb (g, r);
            else if (type == "stereo")
                paintStereo (g, r);
        }

    private:
        void setLevel (float v)
        {
            if (std::abs (v - level) > 0.05f)
            {
                level = v;
                repaint();
            }
        }

        void paintBar (juce::Graphics& g, juce::Rectangle<float> r, bool reductionOnly)
        {
            auto caption = r.removeFromTop (18.0f).reduced (16.0f, 0.0f);
            g.setFont (captionFont());
            g.setColour (colour::textFaint);
            g.drawText (reductionOnly ? "REDUÇÃO DOS \"S\""_t : "MUDANÇA DE GANHO AGORA"_t, caption, juce::Justification::centredLeft);

            auto bar = r.reduced (16.0f, 0.0f).withSizeKeepingCentre (r.getWidth() - 32.0f, 12.0f);
            g.setColour (colour::raised);
            g.fillRoundedRectangle (bar, 6.0f);

            const float range = reductionOnly ? 24.0f : 18.0f;
            const float zero = reductionOnly ? bar.getX() : bar.getCentreX();
            const float span = reductionOnly ? bar.getWidth() : bar.getWidth() * 0.5f;
            const float x = zero + span * juce::jlimit (-1.0f, 1.0f, level / range);
            g.setColour (level > 0.05f ? colour::signal : colour::textDim);
            g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (zero, x), bar.getY(), std::abs (x - zero), bar.getHeight()), 6.0f);

            g.setColour (colour::text);
            g.setFont (numberFont (type::body));
            g.drawText ((level > 0.05f && ! reductionOnly ? "+" : "") + juce::String (level, 1).replaceCharacter ('.', ',') + " dB",
                        r.removeFromBottom (24.0f).reduced (16.0f, 0.0f), juce::Justification::centredRight);
        }

        void paintFilter (juce::Graphics& g, juce::Rectangle<float> r)
        {
            drawFrequencyGrid (g, r, 24.0f, 12.0f);
            const int mode = (int) value ("mode");
            const auto type = mode == 1 ? dsp::FilterType::highpass : mode == 2 ? dsp::FilterType::bandpass
                            : mode == 3 ? dsp::FilterType::notch : dsp::FilterType::lowpass;
            const int sections = value ("slope") >= 0.5f ? 2 : 1;
            const double q = 0.5 * std::pow (24.0, value ("resonance") * 0.01);
            juce::Path curve;

            for (float x = r.getX(); x <= r.getRight(); x += 1.5f)
            {
                double db = 0.0;

                for (int s = 0; s < sections; ++s)
                    db += dsp::SvfCoeffs::make (type, value ("cutoff"), (sections == 2 && s == 0) ? 0.5412 : q, 0.0, displayRate)
                              .magnitudeDb (hzForX (x, r), displayRate);

                const float y = juce::jmap ((float) juce::jlimit (-30.0, 30.0, db), 24.0f, -24.0f, r.getY() + 8.0f, r.getBottom() - 8.0f);
                x == r.getX() ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
            }

            g.setColour (colour::text);
            g.strokePath (curve, juce::PathStrokeType (2.0f));
        }

        void paintDelay (juce::Graphics& g, juce::Rectangle<float> r)
        {
            const double ms = dsp::DelayProcessor::delayMs (values.data());
            const float feedback = value ("feedback") * 0.01f;
            auto text = r.removeFromLeft (200.0f).reduced (16.0f, 4.0f);

            g.setFont (captionFont());
            g.setColour (colour::textFaint);
            g.drawText ("CADA REPETIÇÃO"_t, text.removeFromTop (16.0f), juce::Justification::topLeft);
            g.setFont (numberFont (28.0f));
            g.setColour (colour::text);
            g.drawText ((ms < 100.0 ? juce::String (ms, 1).replaceCharacter ('.', ',') : juce::String (juce::roundToInt (ms))) + " ms",
                        text.removeFromTop (36.0f), juce::Justification::topLeft);

            // As repetições como barras que caem com a realimentação.
            auto lane = r.reduced (8.0f, 10.0f);
            g.setColour (colour::background.withAlpha (0.55f));
            g.fillRoundedRectangle (lane, metric::radius);
            const float step = lane.getWidth() / 9.0f;
            float echo = 1.0f;

            for (int i = 0; i < 8 && echo > 0.01f; ++i)
            {
                const float h = (lane.getHeight() - 12.0f) * echo;
                g.setColour (i == 0 ? colour::text : colour::textDim.withAlpha (0.4f + 0.6f * echo));
                g.fillRoundedRectangle (juce::Rectangle<float> (lane.getX() + step * (i + 0.5f), lane.getBottom() - 6.0f - h, 6.0f, h), 3.0f);
                echo = i == 0 ? feedback : echo * feedback;
            }
        }

        void paintReverb (juce::Graphics& g, juce::Rectangle<float> r)
        {
            g.setColour (colour::background.withAlpha (0.55f));
            g.fillRoundedRectangle (r, metric::radius);

            const float predelay = value ("predelay") * 0.001f, decay = juce::jmax (0.2f, value ("decay"));
            const float span = juce::jmax (2.0f, predelay + decay * 1.1f);
            auto area = r.reduced (12.0f, 10.0f);
            juce::Path env;
            env.startNewSubPath (area.getX(), area.getBottom());
            env.lineTo (area.getX() + area.getWidth() * predelay / span, area.getBottom());

            for (int i = 0; i <= 80; ++i)
            {
                const float t = predelay + decay * 1.1f * (float) i / 80.0f;
                const float levelDb = -60.0f * (t - predelay) / decay;
                env.lineTo (area.getX() + area.getWidth() * t / span, juce::jmap (juce::jmax (-60.0f, levelDb), -60.0f, 0.0f, area.getBottom(), area.getY()));
            }

            env.lineTo (area.getRight(), area.getBottom());
            env.closeSubPath();
            g.setColour (colour::textDim.withAlpha (0.35f));
            g.fillPath (env);
            g.setColour (colour::text);
            g.strokePath (env, juce::PathStrokeType (1.5f));

            g.setFont (captionFont());
            g.setColour (colour::textFaint);
            g.drawText ("CAUDA: " + juce::String (decay, 1).replaceCharacter ('.', ',') + " s ATÉ -60 dB"_t, area, juce::Justification::topRight);
        }

        void paintStereo (juce::Graphics& g, juce::Rectangle<float> r)
        {
            auto text = r.reduced (16.0f, 4.0f).removeFromTop (r.getHeight() - 44.0f);
            g.setFont (font (type::body));
            g.setColour (colour::textDim);
            g.drawFittedText ("Correlação perto de +1: soa igual em mono. Abaixo de 0: partes somem quando o celular "_t
                              "ou a caixa de som tocam em mono.", text.toNearestInt(), juce::Justification::topLeft, 3);
        }

        CorrelationMeter correlation;
        float level = 0.0f;
    };

    //==========================================================================
    // Efeitos de voz e beat (21/10, noite).
    // O fundo dos gráficos novos é `raised`: escurecer com background.withAlpha clareia no tema Claro
    // (aviso do Design). No merge com o extra-design, `fillGraph` passa a usar o token `shade`.

    void fillGraph (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (colour::raised);
        g.fillRoundedRectangle (r, metric::radius);
    }

    /** A grade de frequências e dB dos gráficos novos (a mesma do EQ, com o fundo de `fillGraph`). */
    void drawGraphGrid (juce::Graphics& g, juce::Rectangle<float> r, float rangeDb, float stepDb)
    {
        fillGraph (g, r);
        g.setFont (captionFont());

        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = xForHz (hz, r);
            g.setColour (colour::line);
            g.drawVerticalLine ((int) x, r.getY() + 4.0f, r.getBottom() - 4.0f);
            g.setColour (colour::textFaint);
            g.drawText (hzLabel (hz), juce::Rectangle<float> (x + 3.0f, r.getBottom() - 16.0f, 40.0f, 14.0f), juce::Justification::centredLeft);
        }

        for (float db = -rangeDb; db <= rangeDb + 0.01f; db += stepDb)
        {
            const float y = juce::jmap (db, rangeDb, -rangeDb, r.getY() + 8.0f, r.getBottom() - 8.0f);
            g.setColour (db == 0.0f ? colour::raisedHigh : colour::line);
            g.drawHorizontalLine ((int) y, r.getX() + 4.0f, r.getRight() - 4.0f);
        }
    }

    /** Curva de resposta (dB por frequência) dentro de r, na escala ±rangeDb. */
    void strokeResponse (juce::Graphics& g, juce::Rectangle<float> r, float rangeDb, const std::function<double (double)>& dbAt)
    {
        juce::Path curve;

        for (float x = r.getX(); x <= r.getRight(); x += 1.5f)
        {
            const float db = juce::jlimit (-rangeDb - 2.0f, rangeDb + 2.0f, (float) dbAt (hzForX (x, r)));
            const float y = juce::jmap (db, rangeDb, -rangeDb, r.getY() + 8.0f, r.getBottom() - 8.0f);
            x == r.getX() ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
        }

        g.setColour (colour::text);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::String dbText (float db, bool sign = false)
    {
        return (sign && db > 0.05f ? "+" : "") + juce::String (db, 1).replaceCharacter ('.', ',') + " dB";
    }

    /** Barra horizontal com legenda e valor: de 0 a `range` (redução) ou de -range a +range (ajuste). */
    void paintLevelBar (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& caption, float level, float range,
                        bool bipolar, const juce::String& valueText, const juce::String& note)
    {
        auto head = r.removeFromTop (18.0f).reduced (16.0f, 0.0f);
        g.setFont (captionFont());
        g.setColour (colour::textFaint);
        g.drawText (caption, head, juce::Justification::centredLeft);
        g.setColour (colour::text);
        g.setFont (numberFont (type::body));
        g.drawText (valueText, head, juce::Justification::centredRight);

        auto bar = r.removeFromTop (22.0f).reduced (16.0f, 5.0f);
        g.setColour (colour::raised);
        g.fillRoundedRectangle (bar, 6.0f);

        const float zero = bipolar ? bar.getCentreX() : bar.getX();
        const float span = bipolar ? bar.getWidth() * 0.5f : bar.getWidth();
        const float x = zero + span * juce::jlimit (bipolar ? -1.0f : 0.0f, 1.0f, level / juce::jmax (0.1f, range));
        g.setColour (std::abs (level) > 0.05f ? colour::signal : colour::textDim);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (zero, x), bar.getY(), std::abs (x - zero), bar.getHeight()), 6.0f);

        if (bipolar)
        {
            g.setColour (colour::line.brighter (0.4f));
            g.fillRect (juce::Rectangle<float> (zero - 0.5f, bar.getY() - 3.0f, 1.0f, bar.getHeight() + 6.0f));
        }

        g.setFont (font (type::small));
        g.setColour (colour::textDim);
        g.drawFittedText (note, r.reduced (16.0f, 2.0f).toNearestInt(), juce::Justification::topLeft, 2);
    }

    /** Gate, nivelador, limpa-voz, Grave+ e dobrador: o que o efeito está fazendo agora, numa barra. */
    class VoiceToolVisual : public FxVisual
    {
    public:
        explicit VoiceToolVisual (WhynotFxPlugin& p) : FxVisual (p)
        {
            if (plugin.getFxType() == "doubler")
                addAndMakeVisible (correlation);
        }

        void resized() override   { correlation.setBounds (getLocalBounds().reduced (16, 0).removeFromBottom (36)); }

        void tick() override
        {
            const auto& proc = plugin.getHost().processor();
            const bool off = plugin.getHost().isBypassed();
            std::array<float, 4> now { proc.meter (0), proc.meter (1), proc.meter (2), proc.meter (3) };

            if (plugin.getFxType() == "doubler")
            {
                correlation.setValue (now[0]);
                return;
            }

            if (off)
                now = { 0.0f, now[1], 0.0f, now[3] };

            bool changed = false;

            for (size_t i = 0; i < now.size(); ++i)
                changed |= std::abs (now[i] - meters[i]) > 0.1f;

            if (changed)
            {
                meters = now;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const auto& fxType = plugin.getFxType();
            auto r = getLocalBounds().toFloat().reduced (2.0f, 6.0f);

            if (fxType == "gate")
            {
                const bool open = meters[2] > 0.5f;
                const auto level = meters[1] > -100.0f ? "entrada em "_t + dbText (meters[1]) : juce::String ("sem sinal"_t);
                paintLevelBar (g, r, "REDUÇÃO AGORA"_t, meters[0], juce::jmax (1.0f, value ("range")), false, dbText (-meters[0]),
                               (open ? "Aberto: o som passa ("_t : "Fechado ("_t) + level + ", limiar " + dbText (value ("threshold")) + ").");
            }
            else if (fxType == "leveler")
            {
                const bool voice = meters[2] > 0.5f;
                paintLevelBar (g, r, "AJUSTE DO NIVELADOR"_t, meters[0], juce::jmax (1.0f, value ("range")), true, dbText (meters[0], true),
                               voice ? "Voz agora em "_t + dbText (meters[1]) + " (alvo " + dbText (value ("target")) + ")."
                                     : "Pausa: o ganho fica parado e não sobe o ruído."_t);
            }
            else if (fxType == "denoise")
            {
                paintLevelBar (g, r, "RUÍDO TIRADO AGORA"_t, meters[0], juce::jmax (1.0f, value ("reduction")), false, dbText (-meters[0]),
                               "Ruído de fundo estimado: "_t + dbText (meters[1]) + ". Ele é aprendido sozinho nas pausas (1 a 2 s)."_t);
            }
            else if (fxType == "bassplus")
            {
                const float level = juce::jmax (0.0f, meters[0] + 60.0f);
                paintLevelBar (g, r, "HARMÔNICOS AGORA"_t, level, 60.0f, false, meters[0] > -100.0f ? dbText (meters[0]) : juce::String ("-"),
                               "Abaixo de "_t + fxui::formatValue (plugin.getInfo().params[(size_t) indexOf ("freq")], value ("freq"))
                                   + " o grave vira 2º e 3º harmônicos: confira num fone pequeno ou no celular."_t);
            }
            else if (fxType == "voicepitch")
            {
                const float st = value ("semitones"), fm = value ("formant");
                auto stText = [] (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1).replaceCharacter ('.', ',') + " st"; };
                auto head = r.removeFromTop (18.0f).reduced (16.0f, 0.0f);
                auto big = r.removeFromTop (34.0f).reduced (16.0f, 0.0f);
                const auto half = head.getWidth() / 2.0f;

                g.setFont (captionFont());
                g.setColour (colour::textFaint);
                g.drawText ("TOM"_t, head.withWidth (half), juce::Justification::centredLeft);
                g.drawText ("FORMANTE"_t, head.withTrimmedLeft (half), juce::Justification::centredLeft);
                g.setFont (numberFont (24.0f));
                g.setColour (colour::text);
                g.drawText (stText (st), big.withWidth (half), juce::Justification::centredLeft);
                g.drawText (stText (fm), big.withTrimmedLeft (half), juce::Justification::centredLeft);

                const float hz = meters[0];
                g.setFont (font (type::small));
                g.setColour (colour::textDim);
                g.drawFittedText (meters[1] > 0.5f && hz > 0.0f
                                      ? "Voz agora: "_t + juce::String (juce::roundToInt (hz)) + " Hz, sai em "
                                            + juce::String (juce::roundToInt (hz * std::exp2 (st / 12.0f))) + " Hz. A formante muda o tamanho da voz sem mudar a nota."_t
                                      : "Sem voz agora. Consoantes e respiração passam sem mudar de tom."_t,
                                  r.reduced (16.0f, 2.0f).toNearestInt(), juce::Justification::topLeft, 2);
            }
            else if (fxType == "harmony")
            {
                static const char* names[] = { "Dó", "Dó#", "Ré", "Ré#", "Mi", "Fá", "Fá#", "Sol", "Sol#", "Lá", "Lá#", "Si" };
                auto noteText = [] (float midi)
                {
                    const int n = juce::roundToInt (midi);
                    return n < 0 ? juce::String ("-") : juce::String::fromUTF8 (names[n % 12]) + " " + juce::String (n / 12 - 1);
                };

                auto head = r.removeFromTop (18.0f).reduced (16.0f, 0.0f);
                g.setFont (captionFont());
                g.setColour (colour::textFaint);
                g.drawText ("NOTA CANTADA E AS VOZES NOVAS"_t, head, juce::Justification::centredLeft);

                const bool voice = meters[1] > 0.5f && meters[0] >= 0.0f;
                auto big = r.removeFromTop (34.0f).reduced (16.0f, 0.0f);
                g.setFont (displayFont (22.0f));
                g.setColour (voice ? colour::text : colour::textFaint);
                g.drawText (voice ? noteText (meters[0]) + juce::String (juce::CharPointer_UTF8 ("  \xe2\x86\x92  ")) + noteText (meters[2])
                                        + (meters[3] >= 0.0f ? "  +  " + noteText (meters[3]) : juce::String())
                                  : juce::String ("Sem voz agora"_t),
                            big, juce::Justification::centredLeft);

                g.setFont (font (type::small));
                g.setColour (colour::textDim);
                g.drawFittedText ("As vozes novas vão para as notas da escala; consoantes e respiração ficam só na voz original."_t,
                                  r.reduced (16.0f, 2.0f).toNearestInt(), juce::Justification::topLeft, 2);
            }
            else if (fxType == "doubler")
            {
                auto text = r.reduced (14.0f, 0.0f).removeFromTop (r.getHeight() - 40.0f);
                g.setFont (font (type::body));
                g.setColour (colour::textDim);
                g.drawFittedText ("Cópias atrasadas e levemente desafinadas, abertas nos lados. Correlação perto de +1: soa igual "_t
                                  "em mono; abaixo de 0, a dobra some no celular.", text.toNearestInt(), juce::Justification::topLeft, 3);
            }
        }

    private:
        std::array<float, 4> meters {};
        CorrelationMeter correlation;
    };

    /** Multibanda: as quatro faixas na régua de frequências, com o ganho e a redução de cada uma. */
    class MultibandVisual : public FxVisual
    {
    public:
        explicit MultibandVisual (WhynotFxPlugin& p) : FxVisual (p) {}

        void tick() override
        {
            const auto& proc = plugin.getHost().processor();
            bool changed = false;

            for (int b = 0; b < 4; ++b)
            {
                const float v = plugin.getHost().isBypassed() ? 0.0f : proc.meter (dsp::MultibandProcessor::reduction1 + b);
                changed |= std::abs (v - reduction[(size_t) b]) > 0.1f;
                reduction[(size_t) b] = v;
            }

            if (changed)
                repaint();
        }

        void paint (juce::Graphics& g) override
        {
            static const char* names[] = { "GRAVE", "MÉDIO-GRAVE", "MÉDIO-AGUDO", "AGUDO" };
            const auto r = getLocalBounds().toFloat().reduced (2.0f);
            constexpr float rangeDb = 18.0f;
            drawGraphGrid (g, r, rangeDb, 6.0f);

            if (values.size() < (size_t) dsp::MultibandProcessor::count)
                return;

            const auto split = dsp::MultibandProcessor::crossovers (values.data(), displayRate);
            const float edges[5] = { r.getX(), xForHz (split[0], r), xForHz (split[1], r), xForHz (split[2], r), r.getRight() };
            auto yFor = [&r, rangeDb] (float db) { return juce::jmap (juce::jlimit (-rangeDb, rangeDb, db), rangeDb, -rangeDb, r.getY() + 8.0f, r.getBottom() - 8.0f); };
            const int solo = (int) value ("solo");

            for (int b = 0; b < 4; ++b)
            {
                const auto ub = (size_t) b;
                const float x0 = edges[b] + 2.0f, x1 = edges[b + 1] - 2.0f;
                const float gain = values[(size_t) (dsp::MultibandProcessor::b1Gain + 3 * b)];
                const float yGain = yFor (gain), yNow = yFor (gain - reduction[ub]);
                const bool heard = solo == 0 || solo == b + 1;

                // O que a compressão tira agora, abaixo da linha do ganho da faixa.
                if (reduction[ub] > 0.05f)
                {
                    g.setColour (colour::signal.withAlpha (heard ? 0.55f : 0.2f));
                    g.fillRect (juce::Rectangle<float> (x0, yGain, x1 - x0, yNow - yGain));
                }

                g.setColour (heard ? colour::text : colour::textFaint);
                g.fillRect (juce::Rectangle<float> (x0, yNow - 1.0f, x1 - x0, 2.0f));

                g.setFont (captionFont());
                g.setColour (heard ? colour::textDim : colour::textFaint);
                g.drawFittedText (juce::String::fromUTF8 (names[b]), juce::Rectangle<float> (x0, r.getY() + 4.0f, x1 - x0, 14.0f).toNearestInt(),
                                  juce::Justification::centred, 1);

                if (reduction[ub] > 0.05f)
                {
                    g.setFont (numberFont (type::small));
                    g.setColour (colour::text);
                    g.drawFittedText (dbText (-reduction[ub]), juce::Rectangle<float> (x0, r.getY() + 18.0f, x1 - x0, 14.0f).toNearestInt(),
                                      juce::Justification::centred, 1);
                }
            }

            g.setColour (colour::textDim);

            for (int i = 1; i <= 3; ++i)
                g.drawVerticalLine ((int) edges[i], r.getY() + 4.0f, r.getBottom() - 4.0f);
        }

    private:
        std::array<float, 4> reduction {};
    };

    /** Console e EQ vintage: a curva do EQ; no console, com a redução do gate e do compressor ao lado. */
    class StripVisual : public FxVisual
    {
    public:
        explicit StripVisual (WhynotFxPlugin& p) : FxVisual (p), gateMeter (30.0f), compMeter (24.0f)
        {
            if (isConsole())
            {
                addAndMakeVisible (gateMeter);
                addAndMakeVisible (compMeter);
            }
        }

        void resized() override
        {
            if (! isConsole())
                return;

            auto r = getLocalBounds().removeFromRight (92).reduced (6, 18);
            gateMeter.setBounds (r.removeFromLeft (36));
            r.removeFromLeft (8);
            compMeter.setBounds (r.removeFromLeft (36));
        }

        void tick() override
        {
            if (! isConsole())
                return;

            const auto& proc = plugin.getHost().processor();
            const bool off = plugin.getHost().isBypassed();
            gateMeter.setReduction (off ? 0.0f : proc.meter (dsp::ConsoleProcessor::gateReduction));
            compMeter.setReduction (off ? 0.0f : proc.meter (dsp::ConsoleProcessor::compReduction));
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);

            if (isConsole())
            {
                auto labels = r.removeFromRight (92.0f);
                g.setFont (captionFont());
                g.setColour (colour::textFaint);
                g.drawText ("GATE", labels.withHeight (16.0f).withWidth (48.0f), juce::Justification::centred);
                g.drawText ("COMP", labels.withHeight (16.0f).withTrimmedLeft (44.0f), juce::Justification::centred);
                r.removeFromRight (6.0f);
            }

            constexpr float rangeDb = 18.0f;
            drawGraphGrid (g, r, rangeDb, 6.0f);
            const auto* v = values.data();

            if (isConsole() && values.size() >= (size_t) dsp::ConsoleProcessor::count)
                strokeResponse (g, r, rangeDb, [v] (double f) { return dsp::ConsoleProcessor::responseDb (v, f, displayRate); });
            else if (! isConsole() && values.size() >= (size_t) dsp::VintageEqProcessor::count)
                strokeResponse (g, r, rangeDb, [v] (double f) { return dsp::VintageEqProcessor::responseDb (v, f, displayRate); });
        }

    private:
        bool isConsole() const   { return plugin.getFxType() == "console"; }

        ReductionMeter gateMeter, compMeter;
    };
}

//==============================================================================
int FxVisual::heightFor (const juce::String& type)
{
    if (type == "eq")                               return 230;
    if (type == "tune")                             return 96;
    if (type == "limiter" || type == "meter")       return 196;
    if (type == "comp" || type == "sat")            return 150;
    if (type == "filter" || type == "reverb" || type == "delay")   return 120;
    if (type == "deesser" || type == "transient" || type == "stereo")   return 90;
    if (type == "opto" || type == "fet")                                return 150;
    if (type == "multiband" || type == "console" || type == "vintageEq") return 150;
    if (type == "gate" || type == "leveler" || type == "denoise" || type == "bassplus" || type == "doubler"
        || type == "voicepitch" || type == "harmony")                   return 90;
    return 0;
}

std::unique_ptr<FxVisual> FxVisual::create (WhynotFxPlugin& p)
{
    const auto& type = p.getFxType();

    if (type == "eq")                            return std::make_unique<EqVisual> (p);
    if (type == "tune")                          return std::make_unique<TuneVisual> (p);
    if (type == "comp" || type == "opto" || type == "fet")   return std::make_unique<CompVisual> (p);
    if (type == "multiband")                     return std::make_unique<MultibandVisual> (p);
    if (type == "console" || type == "vintageEq") return std::make_unique<StripVisual> (p);
    if (type == "gate" || type == "leveler" || type == "denoise" || type == "bassplus" || type == "doubler"
        || type == "voicepitch" || type == "harmony")
        return std::make_unique<VoiceToolVisual> (p);
    if (type == "sat")                           return std::make_unique<SatVisual> (p);
    if (type == "limiter" || type == "meter")    return std::make_unique<LoudnessVisual> (p);
    if (heightFor (type) > 0)                    return std::make_unique<SimpleVisual> (p);
    return {};
}

} // namespace whynot

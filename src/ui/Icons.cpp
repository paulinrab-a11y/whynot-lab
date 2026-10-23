#include "Icons.h"

namespace whynot::icons
{

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;

    /** Um ícone na grade de 24: `stroke` é contornado com o traço padrão, `fill` é preenchido. */
    struct Glyph
    {
        juce::Path stroke, fill;
        float strokeWidth = 2.0f;
    };

    void line (juce::Path& p, float x1, float y1, float x2, float y2)
    {
        p.startNewSubPath (x1, y1);
        p.lineTo (x2, y2);
    }

    void polyline (juce::Path& p, std::initializer_list<juce::Point<float>> points, bool close = false)
    {
        bool first = true;

        for (auto pt : points)
        {
            if (first) p.startNewSubPath (pt); else p.lineTo (pt);
            first = false;
        }

        if (close)
            p.closeSubPath();
    }

    /** Estrela de quatro pontas com os lados curvos para dentro (o brilho do Ideias). */
    void sparkleStar (juce::Path& p, float cx, float cy, float r)
    {
        const float k = r * 0.18f;
        p.startNewSubPath (cx, cy - r);
        p.quadraticTo (cx + k, cy - k, cx + r, cy);
        p.quadraticTo (cx + k, cy + k, cx, cy + r);
        p.quadraticTo (cx - k, cy + k, cx - r, cy);
        p.quadraticTo (cx - k, cy - k, cx, cy - r);
        p.closeSubPath();
    }

    juce::Point<float> polar (float cx, float cy, float radius, float angle)
    {
        return { cx + radius * std::sin (angle), cy - radius * std::cos (angle) };
    }

    Glyph build (Icon icon)
    {
        Glyph g;
        auto& s = g.stroke;
        auto& f = g.fill;

        switch (icon)
        {
            case Icon::none:
                break;

            //==========================================================================
            case Icon::play:
            {
                juce::Path t;
                polyline (t, { { 7.5f, 4.5f }, { 19.5f, 12.0f }, { 7.5f, 19.5f } }, true);
                f = t.createPathWithRoundedCorners (2.5f);
                break;
            }

            case Icon::pause:
                f.addRoundedRectangle (6.0f, 5.0f, 4.0f, 14.0f, 1.2f);
                f.addRoundedRectangle (14.0f, 5.0f, 4.0f, 14.0f, 1.2f);
                break;

            case Icon::stop:
                f.addRoundedRectangle (6.0f, 6.0f, 12.0f, 12.0f, 2.2f);
                break;

            case Icon::loop:
                // dois trechos com seta fechando um ciclo
                s.startNewSubPath (4.5f, 13.0f);
                s.lineTo (4.5f, 11.0f);
                s.quadraticTo (4.5f, 7.0f, 8.5f, 7.0f);
                s.lineTo (19.0f, 7.0f);
                polyline (s, { { 16.0f, 4.0f }, { 19.0f, 7.0f }, { 16.0f, 10.0f } });
                s.startNewSubPath (19.5f, 11.0f);
                s.lineTo (19.5f, 13.0f);
                s.quadraticTo (19.5f, 17.0f, 15.5f, 17.0f);
                s.lineTo (5.0f, 17.0f);
                polyline (s, { { 8.0f, 14.0f }, { 5.0f, 17.0f }, { 8.0f, 20.0f } });
                break;

            case Icon::metronome:
            {
                juce::Path body;
                polyline (body, { { 9.5f, 3.5f }, { 14.5f, 3.5f }, { 18.5f, 20.5f }, { 5.5f, 20.5f } }, true);
                s = body.createPathWithRoundedCorners (1.5f);
                line (s, 12.0f, 16.5f, 17.0f, 7.5f);   // pêndulo
                break;
            }

            case Icon::keyboard:
                // um teclado de piano deitado: três teclas brancas e duas pretas
                s.addRoundedRectangle (2.5f, 5.5f, 19.0f, 13.0f, 2.0f);
                line (s, 8.8f, 12.5f, 8.8f, 18.5f);
                line (s, 15.2f, 12.5f, 15.2f, 18.5f);
                f.addRoundedRectangle (6.6f, 5.5f, 4.4f, 7.5f, 1.0f);
                f.addRoundedRectangle (13.0f, 5.5f, 4.4f, 7.5f, 1.0f);
                break;

            //==========================================================================
            case Icon::undo:
            case Icon::redo:
                s.startNewSubPath (4.5f, 9.5f);
                s.lineTo (14.5f, 9.5f);
                s.addCentredArc (14.5f, 14.25f, 4.75f, 4.75f, 0.0f, 0.0f, pi, false);
                s.lineTo (9.5f, 19.0f);
                polyline (s, { { 8.5f, 5.5f }, { 4.5f, 9.5f }, { 8.5f, 13.5f } });

                if (icon == Icon::redo)
                    s.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f).translated (24.0f, 0.0f));
                break;

            case Icon::save:
            {
                juce::Path body;
                polyline (body, { { 4.0f, 4.0f }, { 16.5f, 4.0f }, { 20.0f, 7.5f }, { 20.0f, 20.0f }, { 4.0f, 20.0f } }, true);
                s = body.createPathWithRoundedCorners (2.0f);
                polyline (s, { { 8.0f, 4.0f }, { 8.0f, 8.5f }, { 15.0f, 8.5f }, { 15.0f, 4.0f } });
                polyline (s, { { 7.5f, 20.0f }, { 7.5f, 13.5f }, { 16.5f, 13.5f }, { 16.5f, 20.0f } });
                break;
            }

            //==========================================================================
            case Icon::playlist:
                // blocos arranjados em raias
                f.addRoundedRectangle (3.0f, 4.5f, 9.0f, 4.0f, 1.2f);
                f.addRoundedRectangle (14.0f, 4.5f, 7.0f, 4.0f, 1.2f);
                f.addRoundedRectangle (7.0f, 10.0f, 11.0f, 4.0f, 1.2f);
                f.addRoundedRectangle (3.0f, 15.5f, 6.0f, 4.0f, 1.2f);
                f.addRoundedRectangle (11.0f, 15.5f, 10.0f, 4.0f, 1.2f);
                break;

            case Icon::channelRack:
            {
                // grade de passos: ligados cheios, desligados só no contorno
                g.strokeWidth = 1.4f;
                const bool lit[2][4] = { { true, false, true, false }, { false, true, false, true } };

                for (int row = 0; row < 2; ++row)
                {
                    for (int col = 0; col < 4; ++col)
                    {
                        const juce::Rectangle<float> cell (1.5f + (float) col * 5.5f, 6.5f + (float) row * 6.5f, 4.5f, 4.5f);

                        if (lit[row][col])
                            f.addRoundedRectangle (cell, 1.2f);
                        else
                            s.addRoundedRectangle (cell.reduced (0.7f), 0.8f);
                    }
                }
                break;
            }

            case Icon::pianoRoll:
                // teclado à esquerda, notas à direita
                s.addRoundedRectangle (3.0f, 3.5f, 6.0f, 17.0f, 1.5f);
                line (s, 3.0f, 9.2f, 9.0f, 9.2f);
                line (s, 3.0f, 14.8f, 9.0f, 14.8f);
                f.addRoundedRectangle (11.5f, 5.0f, 6.5f, 3.2f, 1.2f);
                f.addRoundedRectangle (14.0f, 10.4f, 7.5f, 3.2f, 1.2f);
                f.addRoundedRectangle (11.5f, 15.8f, 5.0f, 3.2f, 1.2f);
                break;

            case Icon::mixer:
                line (s, 6.0f, 4.0f, 6.0f, 20.0f);
                line (s, 12.0f, 4.0f, 12.0f, 20.0f);
                line (s, 18.0f, 4.0f, 18.0f, 20.0f);
                f.addRoundedRectangle (3.0f, 12.0f, 6.0f, 4.0f, 1.2f);
                f.addRoundedRectangle (9.0f, 6.5f, 6.0f, 4.0f, 1.2f);
                f.addRoundedRectangle (15.0f, 14.0f, 6.0f, 4.0f, 1.2f);
                break;

            case Icon::settings:
            {
                // engrenagem de 8 dentes
                juce::Path gear;
                const float c = 12.0f, outer = 9.5f, inner = 7.2f, step = 2.0f * pi / 8.0f;

                for (int i = 0; i < 8; ++i)
                {
                    const float a = (float) i * step;
                    const auto p0 = polar (c, c, inner, a - 0.32f);
                    if (i == 0) gear.startNewSubPath (p0); else gear.lineTo (p0);
                    gear.lineTo (polar (c, c, outer, a - 0.16f));
                    gear.lineTo (polar (c, c, outer, a + 0.16f));
                    gear.lineTo (polar (c, c, inner, a + 0.32f));
                }

                gear.closeSubPath();
                s = gear.createPathWithRoundedCorners (0.8f);
                s.addEllipse (9.0f, 9.0f, 6.0f, 6.0f);
                break;
            }

            case Icon::projects:
                // disco: a borda, o selo no meio e um sulco
                s.addEllipse (3.0f, 3.0f, 18.0f, 18.0f);
                s.addEllipse (9.5f, 9.5f, 5.0f, 5.0f);
                s.addCentredArc (12.0f, 12.0f, 5.75f, 5.75f, 0.0f, 0.35f * pi, 0.9f * pi, true);
                break;

            //==========================================================================
            case Icon::pencil:
            {
                juce::Path body;
                polyline (body, { { 4.0f, 20.0f }, { 5.0f, 15.5f }, { 15.5f, 5.0f }, { 19.0f, 8.5f }, { 8.5f, 19.0f } }, true);
                s = body.createPathWithRoundedCorners (1.0f);
                line (s, 13.0f, 7.5f, 16.5f, 11.0f);
                break;
            }

            case Icon::brush:
            {
                line (s, 19.5f, 4.5f, 12.0f, 12.0f);   // cabo
                f.startNewSubPath (10.0f, 12.5f);      // cerdas
                f.cubicTo (12.5f, 12.5f, 13.5f, 14.5f, 12.8f, 16.2f);
                f.cubicTo (11.8f, 18.8f, 8.5f, 20.5f, 4.0f, 20.0f);
                f.cubicTo (5.5f, 18.5f, 5.5f, 17.0f, 6.0f, 15.5f);
                f.cubicTo (6.5f, 13.8f, 8.2f, 12.5f, 10.0f, 12.5f);
                f.closeSubPath();
                break;
            }

            case Icon::select:
            {
                // caixa tracejada + cursor
                juce::Path box;
                box.addRoundedRectangle (3.5f, 3.5f, 13.0f, 13.0f, 1.5f);
                const float dashes[] = { 2.6f, 2.2f };
                juce::PathStrokeType (1.8f).createDashedStroke (f, box, dashes, 2);

                juce::Path cursor;
                polyline (cursor, { { 12.5f, 12.5f }, { 21.0f, 15.5f }, { 17.2f, 17.2f }, { 15.5f, 21.0f } }, true);
                f.addPath (cursor.createPathWithRoundedCorners (0.6f));
                break;
            }

            case Icon::magnet:
                s.startNewSubPath (5.0f, 4.0f);
                s.lineTo (5.0f, 12.0f);
                s.addCentredArc (12.0f, 12.0f, 7.0f, 7.0f, 0.0f, 1.5f * pi, 0.5f * pi, false);
                s.lineTo (19.0f, 4.0f);
                s.lineTo (14.5f, 4.0f);
                s.lineTo (14.5f, 12.0f);
                s.addCentredArc (12.0f, 12.0f, 2.5f, 2.5f, 0.0f, 0.5f * pi, 1.5f * pi, false);
                s.lineTo (9.5f, 4.0f);
                s.closeSubPath();
                line (s, 5.0f, 8.0f, 9.5f, 8.0f);
                line (s, 14.5f, 8.0f, 19.0f, 8.0f);
                break;

            case Icon::slide:
                // nota de baixo deslizando até a de cima (glide do 808)
                f.addRoundedRectangle (2.5f, 15.5f, 6.5f, 3.5f, 1.2f);
                f.addRoundedRectangle (15.0f, 5.0f, 6.5f, 3.5f, 1.2f);
                s.startNewSubPath (9.0f, 17.25f);
                s.cubicTo (13.0f, 17.25f, 11.0f, 6.75f, 15.0f, 6.75f);
                break;

            case Icon::timer:
                s.addEllipse (5.0f, 6.5f, 14.0f, 14.0f);
                line (s, 9.5f, 2.5f, 14.5f, 2.5f);
                line (s, 12.0f, 2.5f, 12.0f, 6.5f);
                line (s, 12.0f, 13.5f, 14.5f, 10.5f);
                break;

            case Icon::note:
            {
                juce::Path head;
                head.addEllipse (-3.3f, -2.5f, 6.6f, 5.0f);
                head.applyTransform (juce::AffineTransform::rotation (-0.45f).translated (8.5f, 17.0f));
                f = head;
                line (s, 11.4f, 16.0f, 11.4f, 3.5f);
                s.startNewSubPath (11.4f, 3.5f);
                s.quadraticTo (17.0f, 5.0f, 16.5f, 10.0f);
                break;
            }

            case Icon::pattern:
                s.addRoundedRectangle (4.0f, 4.0f, 7.0f, 7.0f, 1.5f);
                s.addRoundedRectangle (13.0f, 4.0f, 7.0f, 7.0f, 1.5f);
                s.addRoundedRectangle (4.0f, 13.0f, 7.0f, 7.0f, 1.5f);
                f.addRoundedRectangle (12.0f, 12.0f, 9.0f, 9.0f, 2.2f);
                break;

            case Icon::sparkle:
                // ideia: uma estrela grande contornada e uma pequena cheia
                sparkleStar (s, 10.0f, 13.5f, 8.0f);
                sparkleStar (f, 18.5f, 5.5f, 3.4f);
                break;

            case Icon::scissors:
                // tesoura: os dois anéis embaixo e as lâminas cruzadas
                s.addEllipse (3.5f, 14.0f, 6.5f, 6.5f);
                s.addEllipse (14.0f, 14.0f, 6.5f, 6.5f);
                line (s, 8.6f, 15.0f, 19.5f, 3.5f);
                line (s, 15.4f, 15.0f, 4.5f, 3.5f);
                break;

            case Icon::eraser:
            {
                // borracha inclinada, com a ponta separada, sobre a linha do chão
                juce::Path body;
                polyline (body, { { 3.5f, 14.0f }, { 12.5f, 5.0f }, { 20.0f, 12.5f }, { 11.0f, 21.5f }, { 7.0f, 21.5f } }, true);
                s = body.createPathWithRoundedCorners (1.2f);
                line (s, 8.0f, 9.5f, 15.5f, 17.0f);
                line (s, 13.0f, 21.5f, 20.5f, 21.5f);
                break;
            }

            case Icon::slip:
                // o conteúdo anda dentro da janela: a caixa e as duas setas
                s.addRoundedRectangle (3.5f, 6.5f, 17.0f, 11.0f, 2.0f);
                line (s, 7.5f, 12.0f, 16.5f, 12.0f);
                polyline (s, { { 9.8f, 9.7f }, { 7.5f, 12.0f }, { 9.8f, 14.3f } });
                polyline (s, { { 14.2f, 9.7f }, { 16.5f, 12.0f }, { 14.2f, 14.3f } });
                break;

            //==========================================================================
            case Icon::plugin:
                // plugue: dois pinos, corpo e cabo
                line (s, 9.5f, 3.5f, 9.5f, 8.0f);
                line (s, 14.5f, 3.5f, 14.5f, 8.0f);
                s.addRoundedRectangle (6.0f, 8.0f, 12.0f, 7.0f, 2.5f);
                line (s, 12.0f, 15.0f, 12.0f, 20.5f);
                break;

            case Icon::search:
                s.addEllipse (4.5f, 4.5f, 12.0f, 12.0f);
                line (s, 15.0f, 15.0f, 20.0f, 20.0f);
                break;

            case Icon::folder:
            {
                juce::Path p;
                polyline (p, { { 3.5f, 18.5f }, { 3.5f, 5.5f }, { 9.0f, 5.5f }, { 11.0f, 8.0f }, { 20.5f, 8.0f }, { 20.5f, 18.5f } }, true);
                s = p.createPathWithRoundedCorners (1.5f);
                break;
            }

            case Icon::wave:
                // arquivo de áudio: barras de forma de onda
                line (s, 4.0f, 10.5f, 4.0f, 13.5f);
                line (s, 8.0f, 7.0f, 8.0f, 17.0f);
                line (s, 12.0f, 3.5f, 12.0f, 20.5f);
                line (s, 16.0f, 8.0f, 16.0f, 16.0f);
                line (s, 20.0f, 10.5f, 20.0f, 13.5f);
                break;

            //==========================================================================
            case Icon::star:
            case Icon::starFilled:
            {
                // estrela de 5 pontas com as quinas arredondadas: contorno = não favorito, cheia = favorito
                juce::Path star;

                for (int k = 0; k < 10; ++k)
                {
                    const auto pt = polar (12.0f, 12.8f, k % 2 == 0 ? 9.0f : 4.2f, (float) k * pi / 5.0f);
                    if (k == 0) star.startNewSubPath (pt); else star.lineTo (pt);
                }

                star.closeSubPath();
                (icon == Icon::starFilled ? f : s) = star.createPathWithRoundedCorners (1.2f);
                break;
            }

            case Icon::dice:
                // variar: dado com cinco pontos
                s.addRoundedRectangle (4.0f, 4.0f, 16.0f, 16.0f, 3.5f);
                for (auto [px, py] : { std::pair (8.6f, 8.6f), std::pair (15.4f, 8.6f), std::pair (12.0f, 12.0f),
                                       std::pair (8.6f, 15.4f), std::pair (15.4f, 15.4f) })
                    f.addEllipse (px - 1.5f, py - 1.5f, 3.0f, 3.0f);
                break;

            //==========================================================================
            case Icon::record:
                // gravar e armar: a bolinha cheia
                f.addEllipse (5.5f, 5.5f, 13.0f, 13.0f);
                break;

            case Icon::snowflake:
            {
                // congelar: três hastes com um V na ponta de cada uma
                for (int k = 0; k < 6; ++k)
                {
                    const float a = (float) k * pi / 3.0f;
                    const auto tip = polar (12.0f, 12.0f, 9.0f, a);
                    line (s, 12.0f, 12.0f, tip.x, tip.y);
                    const auto mid = polar (12.0f, 12.0f, 5.6f, a);
                    const auto left = polar (mid.x, mid.y, 3.0f, a - 0.75f);
                    const auto right = polar (mid.x, mid.y, 3.0f, a + 0.75f);
                    polyline (s, { left, mid, right });
                }
                break;
            }

            case Icon::headphones:
                // fone: o arco da tiara e as duas conchas
                s.addCentredArc (12.0f, 13.0f, 7.0f, 7.5f, 0.0f, -pi * 0.5f, pi * 0.5f, true);
                f.addRoundedRectangle (3.0f, 12.5f, 4.5f, 7.5f, 1.6f);
                f.addRoundedRectangle (16.5f, 12.5f, 4.5f, 7.5f, 1.6f);
                break;

            case Icon::automation:
                // automação: a curva com os pontos
                polyline (s, { { 3.5f, 17.5f }, { 9.0f, 8.0f }, { 15.0f, 14.0f }, { 20.5f, 5.5f } });
                for (auto [px, py] : { std::pair (3.5f, 17.5f), std::pair (9.0f, 8.0f), std::pair (15.0f, 14.0f), std::pair (20.5f, 5.5f) })
                    f.addEllipse (px - 2.0f, py - 2.0f, 4.0f, 4.0f);
                break;

            //==========================================================================
            case Icon::plus:
                line (s, 12.0f, 5.0f, 12.0f, 19.0f);
                line (s, 5.0f, 12.0f, 19.0f, 12.0f);
                break;

            case Icon::chevronDown:
                polyline (s, { { 7.0f, 9.5f }, { 12.0f, 14.5f }, { 17.0f, 9.5f } });
                break;

            case Icon::chevronLeft:
                polyline (s, { { 14.5f, 7.0f }, { 9.5f, 12.0f }, { 14.5f, 17.0f } });
                break;

            case Icon::chevronRight:
                polyline (s, { { 9.5f, 7.0f }, { 14.5f, 12.0f }, { 9.5f, 17.0f } });
                break;

            case Icon::check:
                polyline (s, { { 5.0f, 12.5f }, { 10.0f, 17.5f }, { 19.0f, 7.0f } });
                break;

            case Icon::close:
                // fechar: o × de dois riscos
                line (s, 6.5f, 6.5f, 17.5f, 17.5f);
                line (s, 17.5f, 6.5f, 6.5f, 17.5f);
                break;

            case Icon::route:
                line (s, 3.5f, 12.0f, 15.0f, 12.0f);
                polyline (s, { { 10.5f, 7.5f }, { 15.0f, 12.0f }, { 10.5f, 16.5f } });
                line (s, 20.0f, 5.0f, 20.0f, 19.0f);
                break;

            case Icon::speaker:
            {
                juce::Path body;
                polyline (body, { { 3.5f, 9.0f }, { 7.5f, 9.0f }, { 12.0f, 5.0f }, { 12.0f, 19.0f }, { 7.5f, 15.0f }, { 3.5f, 15.0f } }, true);
                s = body.createPathWithRoundedCorners (1.0f);
                s.addCentredArc (12.0f, 12.0f, 4.0f, 4.0f, 0.0f, pi * 0.25f, pi * 0.75f, true);
                s.addCentredArc (12.0f, 12.0f, 8.0f, 8.0f, 0.0f, pi * 0.22f, pi * 0.78f, true);
                break;
            }

            case Icon::warning:
            {
                juce::Path t;
                polyline (t, { { 12.0f, 3.5f }, { 21.0f, 19.5f }, { 3.0f, 19.5f } }, true);
                s = t.createPathWithRoundedCorners (2.0f);
                line (s, 12.0f, 9.5f, 12.0f, 13.5f);
                f.addEllipse (10.9f, 15.4f, 2.2f, 2.2f);
                break;
            }
        }

        return g;
    }

    const Glyph& glyph (Icon icon)
    {
        static const auto all = []
        {
            std::array<Glyph, (size_t) Icon::warning + 1> a;
            for (size_t i = 0; i < a.size(); ++i)
                a[i] = build ((Icon) i);
            return a;
        }();

        return all[(size_t) icon];
    }
}

void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    if (icon == Icon::none)
        return;

    const auto& gl = glyph (icon);
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const float scale = size / 24.0f;
    const auto t = juce::AffineTransform::scale (scale).translated (area.getCentreX() - size * 0.5f,
                                                                     area.getCentreY() - size * 0.5f);
    g.setColour (colour);

    if (! gl.fill.isEmpty())
        g.fillPath (gl.fill, t);

    if (! gl.stroke.isEmpty())
    {
        auto p = gl.stroke;
        p.applyTransform (t);
        g.strokePath (p, juce::PathStrokeType (juce::jmax (1.1f, gl.strokeWidth * scale),
                                               juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

} // namespace whynot::icons

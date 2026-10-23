// Testes das contas da gravação (sem JUCE): compensação de latência, onde o take cai, punch e notas.

#include "../../src/record/BeatTempo.h"
#include "../../src/record/RecordingGuide.h"
#include "../../src/record/TakeMath.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <tuple>

using namespace whynot::record;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }

int main()
{
    const Tempo tempo { 140.0, 960 };
    const double rate = 48000.0;
    const double tick = tempo.secondsPerTick();

    std::printf ("Relógio e latência\n");
    {
        const ClockSync sync { 1000.0, 2.0, rate };
        CHECK (near (sync.timelineAt (1000.0), 2.0, 1e-12) && near (sync.timelineAt (49000.0), 3.0, 1e-12),
               "o relógio da placa anda junto com a linha do tempo");

        Latency l { 128, 352, 0, 0.0 };
        CHECK (near (l.audioSeconds (rate) * rate, 480.0, 1e-9), "áudio: entrada + saída = 480 amostras");
        CHECK (near (l.midiSeconds (rate) * rate, 352.0, 1e-9), "MIDI: só a saída (352 amostras)");
        l.adjustMs = 5.0;
        l.graphSamples = 64;
        CHECK (near (l.audioSeconds (rate) * rate, 480.0 + 64.0 + 240.0, 1e-9), "o grafo e o ajuste manual somam no áudio");
        CHECK (near (l.midiSeconds (rate) * rate, 352.0 + 64.0, 1e-9), "o grafo soma no MIDI; o ajuste manual não");
    }

    std::printf ("Onde o take de áudio cai\n");
    {
        // arquivo começa 10 ms antes do zero (latência), 4 s de áudio, gravado do zero em diante
        const auto p = placeAudio (-0.010, 4.0, 0.0, noLimit, tempo, 240);
        CHECK (p.valid && p.startTick == 0, "o clipe começa no tick 0");
        CHECK (near (p.offsetSeconds, 0.010, 1e-12), "os 10 ms da latência ficam fora (offset 10 ms)");
        CHECK (p.lengthTicks == (int) std::floor (3.990 / tick + 1e-6), "acaba no último tick com áudio (%d ticks)", p.lengthTicks);

        // começo no meio de um tick: o clipe começa no próximo tick e o offset leva a fração
        const double fileStart = 1.0 + 0.3 * tick;
        const auto q = placeAudio (fileStart, 2.0, 0.0, noLimit, tempo, 240);
        const int expectTick = (int) std::ceil (fileStart / tick);
        CHECK (q.valid && q.startTick == expectTick, "início no tick %d (o primeiro com áudio)", q.startTick);
        CHECK (near (q.startTick * tick - q.offsetSeconds, fileStart, 1e-12), "posição exata: tick menos offset = a primeira amostra");

        // punch: só entre o compasso 2 e o 3
        const auto r = placeAudio (-0.010, 8.0, tempo.toSeconds (3840), tempo.toSeconds (7680), tempo, 240);
        CHECK (r.valid && r.startTick == 3840 && r.lengthTicks == 3840, "punch corta no compasso 2 e no 3 (ticks 3840 a 7680)");
        CHECK (near (r.offsetSeconds, tempo.toSeconds (3840) + 0.010, 1e-12), "o offset pula a parte antes do punch in");

        CHECK (! placeAudio (-0.010, 0.05, 0.0, noLimit, tempo, 240).valid, "take menor que um passo é descartado");
        CHECK (! placeAudio (0.0, 1.0, 5.0, 6.0, tempo, 240).valid, "take todo fora do punch é descartado");
    }

    std::printf ("Notas do take MIDI\n");
    {
        PatternTarget pat;
        pat.patternTicks = 3840;   // 1 compasso
        pat.loopTicks = 3840;

        std::vector<NoteEvent> ev {
            { tempo.toSeconds (0),    60, 100, true },  { tempo.toSeconds (480),  60, 0, false },
            { tempo.toSeconds (960),  63, 90,  true },  { tempo.toSeconds (1200), 63, 0, false },
            { tempo.toSeconds (3840 + 1920), 67, 80, true },   // segunda volta do loop, sem "desliga"
        };

        const auto notes = notesForPattern (ev, tempo.toSeconds (3840 + 2400), -noLimit, noLimit, tempo, pat);
        CHECK (notes.size() == 3, "3 notas (%d)", (int) notes.size());

        if (notes.size() == 3)
        {
            const RecordedNote c0 { 0, 480, 60, 100 }, ds { 960, 240, 63, 90 };
            CHECK (notes[0] == c0, "Dó no tick 0, 480 de duração");
            CHECK (notes[1] == ds, "Ré# no tick 960, 240 de duração");
            CHECK (notes[2].start == 1920 && notes[2].length == 480 && notes[2].key == 67,
                   "na volta do loop cai no tick 1920; sem desligar, vai até o fim da gravação");
        }

        // punch: só as que começam entre os ticks 900 e 3000
        const auto punched = notesForPattern (ev, tempo.toSeconds (6000), tempo.toSeconds (900), tempo.toSeconds (3000), tempo, pat);
        CHECK (punched.size() == 1 && punched[0].start == 960, "punch deixa só a nota do meio");

        // nota que passa do fim do padrão é cortada no fim
        PatternTarget noLoop;
        noLoop.patternTicks = 3840;
        const auto longNote = notesForPattern ({ { tempo.toSeconds (3600), 48, 100, true }, { tempo.toSeconds (4200), 48, 0, false } },
                                               tempo.toSeconds (5000), -noLimit, noLimit, tempo, noLoop);
        CHECK (longNote.size() == 1 && longNote[0].start == 3600 && longNote[0].length == 240, "nota no fim do padrão vai só até o fim");

        // modo SONG: conta a partir do bloco do padrão
        PatternTarget song;
        song.patternTicks = 3840;
        song.blocks = { { 7680, 7680 } };   // o padrão toca do compasso 3 ao 5
        const auto inSong = notesForPattern ({ { tempo.toSeconds (7680 + 3840 + 240), 60, 100, true }, { tempo.toSeconds (7680 + 3840 + 480), 60, 0, false } },
                                             tempo.toSeconds (20000), -noLimit, noLimit, tempo, song);
        CHECK (inSong.size() == 1 && inSong[0].start == 240, "SONG: na segunda repetição do bloco cai no tick 240");

        // a mesma tecla apertada duas vezes antes de soltar
        const auto twice = notesForPattern ({ { 0.0, 60, 100, true }, { tempo.toSeconds (240), 60, 100, true },
                                              { tempo.toSeconds (480), 60, 0, false }, { tempo.toSeconds (720), 60, 0, false } },
                                            1.0, -noLimit, noLimit, tempo, noLoop);
        CHECK (twice.size() == 2 && twice[0].length == 480 && twice[1].start == 240 && twice[1].length == 480,
               "tecla repetida: cada \"desliga\" fecha a mais antiga");
    }

    std::printf ("Posição do bloco, qualquer que seja a ordem dos callbacks\n");
    {
        // Simula a placa: o motor soma o bloco quando o callback dele roda; o da gravação lê o relógio.
        auto run = [] (bool engineFirst, int64_t startClock, bool restartAtStart)
        {
            BlockClock clock;
            int64_t engine = startClock, trueStart = startClock;
            bool ok = true;

            if (restartAtStart)
                clock.restart();

            for (int block = 0; block < 50; ++block)
            {
                const int n = block % 3 == 0 ? 256 : 128;   // buffers de tamanho variável também

                if (engineFirst) engine += n;
                ok = ok && clock.blockStart (engine, n) == trueStart;
                if (! engineFirst) engine += n;

                trueStart += n;
            }

            return ok;
        };

        CHECK (run (true, 0, true), "Tracktion antes: o bloco começa no relógio menos o tamanho dele");
        CHECK (run (false, 0, true), "Tracktion depois: o bloco começa no relógio (não fica um bloco adiantado)");
        CHECK (run (true, 4800000, false), "entrou com a placa já rodando (o Tracktion está na frente): certo também");

        // A placa reinicia (troca de taxa ou de buffer): os dois relógios zeram e a ordem é medida de novo.
        BlockClock clock;
        clock.blockStart (512, 256);   // começou com o Tracktion na frente
        clock.restart();
        CHECK (clock.blockStart (0, 256) == 0 && ! clock.isEngineFirst() && clock.blockStart (256, 256) == 256,
               "depois de reiniciar com a ordem trocada, segue certo");
    }

    std::printf ("Buracos no take\n");
    {
        const int64_t maxFill = 96000;   // 2 s a 48 kHz
        CHECK (stitch (1000, 1000, 256, 0, maxFill).action == Stitch::Action::write, "no tempo: escreve");

        const auto small = stitch (1000, 1100, 256, 0, maxFill);
        CHECK (small.action == Stitch::Action::fillThenWrite && small.silence == 100, "falha de buffer: 100 amostras de silêncio");

        const auto disk = stitch (1000, 1000 + 480000, 256, 480000, maxFill);
        CHECK (disk.action == Stitch::Action::fillThenWrite && disk.silence == 480000,
               "disco parado 10 s (a fila perdeu 480 mil amostras): preenche tudo, o resto segue no tempo");

        CHECK (stitch (1000, 1000 + 480000, 256, 0, maxFill).action == Stitch::Action::stop,
               "pulo de 10 s sem perda na fila (a placa recomeçou): o take para ali, não segue adiantado");
        CHECK (stitch (480000, 0, 256, 0, maxFill).action == Stitch::Action::stop, "relógio voltou a 0 (a placa reiniciou): para");

        const auto repeat = stitch (1000, 900, 256, 0, maxFill);
        CHECK (repeat.action == Stitch::Action::skipThenWrite && repeat.skip == 100, "pedaço repetido em parte: descarta as 100 primeiras");
    }

    std::printf ("Onde o play e o take começam (gravar voz)\n");
    {
        const double bar = tempo.toSeconds (3840);

        const auto needle = planStart (tempo.toSeconds (7680), 0.0, 0.0, noLimit, 1, bar);
        CHECK (near (needle.recordFrom, tempo.toSeconds (7680), 1e-12) && near (needle.playFrom, tempo.toSeconds (3840), 1e-12)
                   && near (needle.countIn, bar, 1e-12) && needle.passEnd == noLimit,
               "da agulha no compasso 3, com 1 compasso de contagem: o play começa no compasso 2");

        const auto fromZero = planStart (0.0, 0.0, 0.0, noLimit, 2, bar);
        CHECK (near (fromZero.playFrom, -2.0 * bar, 1e-12) && fromZero.recordFrom == 0.0,
               "da agulha no zero, a contagem de 2 compassos começa antes do zero");

        const auto selection = planStart (tempo.toSeconds (7680), tempo.toSeconds (15360), tempo.toSeconds (23040), tempo.toSeconds (23040), 0, bar);
        CHECK (near (selection.recordFrom, tempo.toSeconds (15360), 1e-12) && near (selection.passEnd, tempo.toSeconds (23040), 1e-12)
                   && near (selection.playFrom, selection.recordFrom, 1e-12),
               "com a seleção de tempo, começa nela e o take vale até a volta do loop");

        CHECK (planStart (-1.0, 0.0, 0.0, noLimit, 0, bar).recordFrom == 0.0, "agulha negativa vira o zero");
        CHECK (near (planStart (0.0, 0.0, 0.0, noLimit, 5, bar).countIn, 2.0 * bar, 1e-12), "contagem passa de 2 compassos: fica em 2");
        CHECK (planStart (tempo.toSeconds (9000), 0.0, 0.0, tempo.toSeconds (7680), 0, bar).passEnd == noLimit,
               "a agulha depois da volta do loop: o take não tem fim de passada");

        CHECK (patternBlockTicks (7497, 3840) == 7680 && patternBlockTicks (3840, 3840) == 3840 && patternBlockTicks (0, 3840) == 3840,
               "o padrão que vai junto do take cobre o fim dele em voltas inteiras (7497 -> 7680, 3840 -> 3840, 0 -> 3840)");
    }

    std::printf ("Voltas do loop (takes)\n");
    {
        // loop do compasso 5 ao 7 (2 compassos), arquivo começando 10 ms antes do compasso 5 e com 3 voltas e meia
        const double loopStart = tempo.toSeconds (15360), loopEnd = tempo.toSeconds (23040), loop = loopEnd - loopStart;
        const auto passes = loopPasses (loopStart - 0.010, 3.5 * loop + 0.010, loopStart, loopEnd, loopStart, noLimit, tempo, 960);
        CHECK (passes.size() == 4, "3 voltas inteiras e meia viram 4 takes (%d)", (int) passes.size());

        bool full = passes.size() == 4;

        for (int k = 0; k < 3 && full; ++k)
            full = passes[(size_t) k].startTick == 15360 && passes[(size_t) k].lengthTicks == 7680
                   && near (passes[(size_t) k].offsetSeconds, 0.010 + k * loop, 1e-9);

        CHECK (full, "cada volta inteira começa no compasso 5, dura 2 compassos, e o corte anda uma volta por take");
        CHECK (passes.size() == 4 && passes[3].startTick == 15360 && passes[3].lengthTicks == 3840 && near (passes[3].offsetSeconds, 0.010 + 3 * loop, 1e-9),
               "a última, pela metade, vai do compasso 5 até onde parou (1 compasso)");

        const auto tiny = loopPasses (loopStart - 0.010, 2.0 * loop + 0.010 + tempo.toSeconds (100), loopStart, loopEnd, loopStart, noLimit, tempo, 960);
        CHECK (tiny.size() == 2, "a volta que mal começou (100 ticks) fica de fora (%d takes)", (int) tiny.size());

        // R apertado no meio da volta (no compasso 6): a primeira volta começa ali
        const auto midway = loopPasses (tempo.toSeconds (19200), loop, loopStart, loopEnd, -noLimit, noLimit, tempo, 960);
        CHECK (midway.size() == 2 && midway[0].startTick == 19200 && midway[0].lengthTicks == 3840 && midway[1].startTick == 15360,
               "gravando do meio da volta: o primeiro take vai do compasso 6 ao 7, o segundo começa no 5");
    }

    std::printf ("O andamento do beat (o beat em um gesto)\n");
    {
        using namespace whynot::beat;
        CHECK (bpmFromName ("Trap 140bpm.wav") == 140.0 && bpmFromName ("beat_92_BPM_Fm.mp3") == 92.0
                   && bpmFromName ("FUNK BPM 130 (prod).wav") == 130.0,
               "pelo nome: 140bpm, 92_BPM e BPM 130");
        CHECK (bpmFromName ("Voz_take03.wav") == 0.0 && bpmFromName ("beat 2024 Am.wav") == 0.0 && bpmFromName ("x 1400bpm") == 0.0,
               "sem bpm no nome, ou com 4 dígitos, nada");

        // beats sintéticos: bumbo, caixa e chimbal, 40 s a 44,1 kHz
        auto makeBeat = [] (double bpm, bool halfTime, int hats)
        {
            const double r = 44100.0, beatSeconds = 60.0 / bpm;
            std::vector<float> x ((size_t) (40.0 * r), 0.0f);
            std::mt19937 rng (7);
            std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

            auto hit = [&] (double t, double hz, double decay, float amp, bool isNoise)
            {
                const int at = (int) (t * r);

                for (int i = 0; i < (int) (decay * 5 * r) && at + i < (int) x.size(); ++i)
                    x[(size_t) (at + i)] += (float) (amp * std::exp (-i / (decay * r)) * (isNoise ? noise (rng) : std::sin (6.283185307 * hz * i / r)));
            };

            for (int b = 0; b * beatSeconds < 40.0; ++b)
            {
                if (b % 4 == 0 || (! halfTime && b % 4 == 2) || (halfTime && b % 8 == 5))
                    hit (b * beatSeconds, 55.0, 0.12, 0.8f, false);

                if ((! halfTime && b % 2 == 1) || (halfTime && b % 4 == 2))
                    hit (b * beatSeconds, 0.0, 0.06, 0.5f, true);

                for (int h = 0; h < hats; ++h)
                    hit ((b + (double) h / hats) * beatSeconds, 0.0, 0.01, 0.15f, true);
            }

            return x;
        };

        for (auto [bpm, half, hats] : { std::tuple { 140.0, true, 4 }, std::tuple { 92.0, false, 2 },
                                        std::tuple { 130.0, false, 4 }, std::tuple { 87.5, false, 2 } })
        {
            const auto x = makeBeat (bpm, half, hats);
            const double got = bpmFromAudio (x.data(), (int) x.size(), 44100.0);
            CHECK (std::abs (got - bpm) < 0.3, "beat sintético a %.1f BPM: %.2f pela análise", bpm, got);
        }

        std::vector<float> hiss ((size_t) (20 * 44100));
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> d (-0.5f, 0.5f);

        for (auto& v : hiss)
            v = d (rng);

        CHECK (bpmFromAudio (hiss.data(), (int) hiss.size(), 44100.0) == 0.0 && bpmFromAudio (hiss.data(), 5 * 44100, 44100.0) == 0.0,
               "ruído, ou menos de 8 s: sem andamento");
    }

    std::printf ("O assistente de placas e microfones\n");
    {
        using namespace whynot::guide;
        const auto interfaceAsio = recognise ("ASIO", "Focusrite USB ASIO");
        CHECK (interfaceAsio.kind == Kind::usbInterface && interfaceAsio.directMonitor && interfaceAsio.phantom,
               "a interface pelo driver ASIO dela: com 48V e monitoração direta");
        CHECK (recognise ("ASIO", "Scarlett Solo USB").inputs == 1 && recognise ("Windows Audio", "Entrada de linha (Scarlett 2i2 4th Gen)").inputs == 2,
               "uma e duas entradas pelos nomes dos modelos");
        CHECK (recognise ("ASIO", "UMC ASIO Driver").kind == Kind::usbInterface && ! recognise ("Windows Audio", "Microfone (UMC22)").directMonitor,
               "a outra marca pelo driver; o modelo menor sem monitoração direta");
        CHECK (surelyBuiltIn ("Microfone (Realtek(R) Audio)") && surelyBuiltIn ("Conjunto de microfones (Tecnologia Intel Smart Sound para microfones digitais)")
                   && ! surelyBuiltIn ("Microfone (USB Audio CODEC)") && ! surelyBuiltIn ("Microfone (2- USB Audio Device)"),
               "a escuta só cala pela placa do computador quando o nome não deixa dúvida (fatia 8)");
        CHECK (recognise ("Windows Audio", "Microfone (AT2020USB+)").kind == Kind::usbMic && recognise ("Windows Audio", "Microphone (FIFINE K669B)").kind == Kind::usbMic,
               "microfones USB aparecem como a placa");
        CHECK (recognise ("ASIO", "ASIO4ALL v2").kind == Kind::asioWrapper && recognise ("Windows Audio", "Microfone (Realtek(R) Audio)").kind == Kind::builtIn
                   && recognise ("ASIO", "Placa misteriosa X").kind == Kind::unknown,
               "o ASIO genérico, a placa do computador e a desconhecida");

        const auto condenser = advise (interfaceAsio, Mic::condenserXlr, true);
        CHECK (condenser.needsPhantom && condenser.preferAsio && condenser.mono && condenser.input == 1 && condenser.warnDirectMonitor,
               "condensador na interface: 48V, ASIO, entrada 1 mono, e o aviso de eco com as duas monitorações");
        CHECK (! advise (interfaceAsio, Mic::condenserXlr, false).warnDirectMonitor && advise (interfaceAsio, Mic::dynamicXlr, false).moreGain
                   && ! advise (recognise ("Windows Audio", "Microfone (AT2020USB+)"), Mic::usb, false).preferAsio,
               "sem a monitoração do app, sem aviso; dinâmico pede ganho; microfone USB fica no Windows Audio");
        CHECK (judge (-60.0) == Level::silent && judge (-24.0) == Level::low && judge (-14.0) == Level::good && judge (-6.0) == Level::hot
                   && judge (-0.5) == Level::clip,
               "o medidor com alvo: silêncio, baixo, no alvo (-18 a -10), alto e estourado");
    }

    std::printf (failures == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d TESTE(S) FALHARAM\n", failures);
    return failures;
}

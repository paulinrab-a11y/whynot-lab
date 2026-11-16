// Tags automáticas do navegador de sons (frente tags-sons, 16/11): o tipo, o BPM e o tom pelo nome do arquivo e
// pela pasta (src/library/SoundTagRules.h), sem JUCE. A tabela abaixo são nomes como os kits de trap e funk
// trazem, com os falsos positivos que a regra precisa evitar.

#include "../../src/library/SoundTagRules.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace whynot::soundtags;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

/** "kick,loop" → os bits; a ordem não importa. */
static Mask maskOf (const std::string& ids)
{
    Mask m = 0;
    std::size_t from = 0;

    while (from < ids.size())
    {
        auto to = ids.find (',', from);

        if (to == std::string::npos)
            to = ids.size();

        const int type = typeFromId (ids.substr (from, to - from));

        if (type < 0)
            std::printf ("  (tipo desconhecido na tabela do teste: %s)\n", ids.substr (from, to - from).c_str());
        else
            m |= Mask (1) << type;

        from = to + 1;
    }

    return m;
}

static std::string idsOf (Mask m)
{
    std::string out;

    for (int i = 0; i < numTypes; ++i)
        if ((m & (Mask (1) << i)) != 0)
            out += (out.empty() ? "" : ",") + std::string (typeId ((Type) i));

    return out;
}

struct Case
{
    const char* path;     // com as pastas, como no disco
    const char* types;    // ids separados por vírgula ("" = nenhum)
    int bpm;
    const char* key;      // "Am", "F#", "Cmaj" ou ""
};

static const Case cases[] = {
    // --- one-shots de bateria, com as siglas dos kits
    { "Kit/Kick 01.wav", "kick", 0, "" },
    { "Kit/KICK_HARD.wav", "kick", 0, "" },
    { "Kit/kik_trap_03.wav", "kick", 0, "" },
    { "Kit/BD_Punchy.wav", "kick", 0, "" },
    { "Kit/Bass Drum 2.wav", "kick", 0, "" },
    { "Kit/BassDrum_Deep.wav", "kick", 0, "" },
    { "Kit/Bumbo Seco.wav", "kick", 0, "" },
    { "Kit/Snare Crack.wav", "snare", 0, "" },
    { "Kit/SN_Tight_02.wav", "snare", 0, "" },
    { "Kit/snr-dry.wav", "snare", 0, "" },
    { "Kit/Caixa Funk 3.wav", "snare", 0, "" },
    { "Kit/Clap Wide.wav", "clap", 0, "" },
    { "Kit/CLP_04.wav", "clap", 0, "" },
    { "Kit/Palma Baile.wav", "clap", 0, "" },
    { "Kit/Rim 02.wav", "rim", 0, "" },
    { "Kit/RimShot_A.wav", "rim", 0, "" },
    { "Kit/HH_Closed_01.wav", "hat", 0, "" },
    { "Kit/hh 12.wav", "hat", 0, "" },
    { "Kit/CH_Tick.wav", "hat", 0, "" },
    { "Kit/Hi-Hat Crisp.wav", "hat", 0, "" },
    { "Kit/HiHat07.wav", "hat", 0, "" },
    { "Kit/Closed Hat 3.wav", "hat", 0, "" },
    { "Kit/Chimbal Fechado.wav", "hat", 0, "" },
    { "Kit/OH_Long.wav", "openhat", 0, "" },
    { "Kit/Open Hat 01.wav", "openhat", 0, "" },
    { "Kit/OpenHat_Sizzle.wav", "openhat", 0, "" },
    { "Kit/Hat Open 2.wav", "openhat", 0, "" },
    { "Kit/Open Hi Hat 5.wav", "openhat", 0, "" },
    { "Kit/OHat 03.wav", "openhat", 0, "" },
    { "Kit/Chimbal Aberto.wav", "openhat", 0, "" },
    { "Kit/Perc Wood 01.wav", "perc", 0, "" },
    { "Kit/PRC_09.wav", "perc", 0, "" },
    { "Kit/Percussão Lata.wav", "perc", 0, "" },
    { "Kit/Cowbell 808.wav", "perc", 0, "" },
    { "Kit/Shaker Loop 120bpm.wav", "perc,loop", 120, "" },
    { "Kit/Floor Tom 1.wav", "tom", 0, "" },
    { "Kit/Tom 03.wav", "tom", 0, "" },
    { "Kit/Crash 01.wav", "cymbal", 0, "" },
    { "Kit/Ride Bell.wav", "cymbal,bell", 0, "" },
    { "Kit/Prato Ataque.wav", "cymbal", 0, "" },

    // --- 808 e baixo
    { "Kit/808 Long C.wav", "808", 0, "" },
    { "Kit/808_Distorted_F#.wav", "808", 0, "F#" },
    { "Kit/Dark_808-G#.wav", "808", 0, "G#" },
    { "Kit/808 Bass Sub.wav", "808", 0, "" },
    { "Kit/808s/Zay 02.wav", "808", 0, "" },
    { "Kit/Sub Bass Clean.wav", "bass", 0, "" },
    { "Kit/Reese Bass Dm.wav", "bass", 0, "Dm" },
    { "Kit/Baixo Grave 2.wav", "bass", 0, "" },
    { "Loops/bassline_loop_90bpm_Em.wav", "bass,loop", 90, "Em" },

    // --- efeitos e voz
    { "Kit/FX Riser 01.wav", "fx", 0, "" },
    { "Kit/sfx_impact.wav", "fx", 0, "" },
    { "Kit/Vinyl Noise.wav", "fx", 0, "" },
    { "Kit/Sirene Baile.wav", "fx", 0, "" },
    { "Kit/Vox Chop 03.wav", "vocal", 0, "" },
    { "Kit/Vocal Hey Short.wav", "vocal", 0, "" },
    { "Kit/Adlib Skrt.wav", "vocal", 0, "" },
    { "Kit/Voz Grito MC.wav", "vocal", 0, "" },

    // --- loops e melódicos, com BPM e tom
    { "Loops/Drum Loop 140bpm.wav", "loop", 140, "" },
    { "Loops/Hat Loop 150 BPM.wav", "hat,loop", 150, "" },
    { "Loops/Trap Melody 140bpm Am.wav", "melody,loop", 140, "Am" },
    { "Loops/Dark_Piano_Loop_F#m_128.wav", "keys,loop", 128, "F#m" },
    { "Loops/GUITAR_LOOP_AM_120.wav", "guitar,loop", 120, "Am" },
    { "Loops/dark_guitar_am_140.wav", "guitar,loop", 140, "Am" },
    { "Loops/Melodia Triste Cmin 95bpm.wav", "melody,loop", 95, "Cm" },
    { "Loops/Flute Gmaj 130.wav", "flute,loop", 130, "Gmaj" },
    { "Misc/Pad Lá menor.wav", "pad", 0, "Am" },
    { "Loops/Sino Dó# maior 100bpm.wav", "bell,loop", 100, "C#maj" },
    { "Loops/Chord Progression Bbm 140.wav", "chord,loop", 140, "A#m" },
    { "Misc/Pluck_A#_01.wav", "pluck", 0, "A#" },
    { "Loops/Bell F minor 142 bpm.wav", "bell,loop", 142, "Fm" },
    { "Loops/Lead C sharp minor.wav", "lead,loop", 0, "C#m" },
    { "Loops/Strings Ebmaj 80bpm.wav", "strings,loop", 80, "D#maj" },
    { "Loops/bpm_92_keys_Dm.wav", "keys,loop", 92, "Dm" },
    { "Loops/Melody _140_ Gm.wav", "melody,loop", 140, "Gm" },
    { "Loops/Violão Funk 130.wav", "guitar,loop", 130, "" },
    { "Loops/Loop Tom Am 120.wav", "loop", 120, "Am" },
    { "Loops/Synth Arp 128bpm C#m.wav", "melody,loop", 128, "C#m" },

    // --- a pasta fala quando o nome não diz
    { "Trap Kit/Kicks/01.wav", "kick", 0, "" },
    { "Trap Kit/Snares/Crack A.wav", "snare", 0, "" },
    { "Trap Kit/Hi Hats/tick 3.wav", "hat", 0, "" },
    { "Trap Kit/Open Hats/sizzle.wav", "openhat", 0, "" },
    { "Trap Kit/FX/Whip.wav", "fx", 0, "" },
    { "Trap Kit/Loops/Midnight 140 Am.wav", "loop", 140, "Am" },
    { "Trap Kit/Drum Loops/Hats 150.wav", "hat,loop", 150, "" },
    { "Trap Kit/Kicks/Snare 02.wav", "snare", 0, "" },
    { "Trap Kit/Percs/Kit 2/07.wav", "perc", 0, "" },

    // --- os falsos positivos
    { "Misc/Chat Noir.wav", "", 0, "" },                    // "chat" não é hat
    { "Misc/That Sound.wav", "", 0, "" },                   // nem "that"
    { "Kit/Snap 04.wav", "snap", 0, "" },                   // "snap" é estalo, não snare
    { "Kit/Finger Snaps.wav", "snap", 0, "" },
    { "Misc/Kickstart My Day.wav", "", 0, "" },             // palavra inteira: "kickstart" não é kick
    { "Misc/Bassoon Note.wav", "", 0, "" },                 // nem "bassoon" é baixo
    { "Misc/Tomorrow.wav", "", 0, "" },                     // nem "tomorrow" é tom
    { "Misc/Clapton Lick.wav", "", 0, "" },
    { "Misc/Percent.wav", "", 0, "" },
    { "Kit/Kick 808.wav", "kick", 0, "" },                  // 808 com a peça: a bateria eletrônica, e 808 não é BPM
    { "Kit/808 Snare 2.wav", "snare", 0, "" },
    { "Kit/TR808 Clap.wav", "clap", 0, "" },
    { "Kit/TR-808.wav", "", 0, "" },
    { "Kit/Kick 100.wav", "kick", 0, "" },                  // número de um one-shot não é BPM
    { "Kit/Snare 128.wav", "snare", 0, "" },
    { "Kit/808 120.wav", "808", 0, "" },
    { "Kit/Clap v2 96k.wav", "clap", 0, "" },               // versão e taxa de amostragem
    { "Misc/Vintage 77.wav", "", 0, "" },                   // número solto num nome que não diz nada
    { "Loops/Melody 001.wav", "melody,loop", 0, "" },       // zero na frente: é o número do som (o loop vem da pasta)
    { "Misc/Melody Vol 90 Take 120.wav", "melody", 0, "" },
    { "Loops/Loop 8 bars 140bpm.wav", "loop", 140, "" },
    { "Loops/Amen Break 170bpm.wav", "loop", 170, "" },     // "Amen" não é Lá menor
    { "Misc/Ambient Pad.wav", "pad", 0, "" },
    { "Misc/Emotional Keys.wav", "keys", 0, "" },
    { "Misc/FM Bass 01.wav", "bass", 0, "" },               // FM é a síntese
    { "Misc/GM PIANO 120BPM.wav", "keys,loop", 120, "" },   // GM é o General MIDI
    { "Misc/Piano Take A.wav", "keys", 0, "" },             // a letra sozinha é o take
    { "Misc/E Piano 2.wav", "keys", 0, "" },
    { "Misc/Beat do Menor 150bpm.wav", "loop", 150, "" },   // "do menor" é o MC, não Dó menor
    { "Misc/Loop a maior de todas.wav", "loop", 0, "" },    // português corrente
    { "Misc/i am the loop.wav", "loop", 0, "" },            // "am" minúsculo sem BPM não é tom
    { "Misc/Lead -6dB.wav", "lead", 0, "" },                // dB não é Ré bemol
    { "Kit/Vox Oh Yeah.wav", "vocal", 0, "" },              // o "oh" do vocal não é open hat
    { "Kit/Sub Drop.wav", "bass,fx", 0, "" },               // duas fracas empatam: fica com as duas
    { "Kit/Kick Sub Heavy.wav", "kick", 0, "" },            // a forte vence a fraca
    { "Kit/Night Bell Loop.wav", "bell,loop", 0, "" },
};

static void testTable()
{
    std::printf ("Nomes de arquivo -> tags (%zu nomes)\n", std::size (cases));
    int wrong = 0;

    for (auto& c : cases)
    {
        const auto tags = tagsForPath (c.path);
        const auto key = keyName (tags.keyRoot, tags.mode);
        const bool ok = tags.types == maskOf (c.types) && tags.bpm == c.bpm && key == c.key;

        if (! ok)
        {
            ++wrong;
            ++failures;
            std::printf ("  FALHOU: %s -> [%s] %d %s (esperado [%s] %d %s)\n", c.path, idsOf (tags.types).c_str(), tags.bpm, key.c_str(),
                         c.types, c.bpm, c.key);
        }
    }

    CHECK (wrong == 0, "os %zu nomes da tabela dão as tags esperadas (%d errados)", std::size (cases), wrong);
    static_assert (std::size (cases) >= 80, "a tabela precisa de pelo menos 80 nomes");
}

static void testDetails()
{
    std::printf ("Detalhes das regras\n");

    CHECK (tagsForPath ("Loops/bassline_loop.wav").main == Type::loop && tagsForPath ("Loops/bassline_loop.wav").has (Type::bass),
           "\"bassline_loop\" é loop de baixo: o principal é loop, e também é baixo");
    CHECK (tagsForPath ("Misc/Trap Melody 140bpm Am.wav").main == Type::melody, "com o BPM escrito e sem a palavra loop, o principal é a melodia");
    CHECK (tagsForPath ("K/Kick 01.wav").main == Type::kick && tagsForPath ("K/abc.wav").main == Type::count, "o principal de um one-shot; nenhum sem nome");
    CHECK (tagsForPath ("Loops/x 140bpm.wav").bpmExplicit && ! tagsForPath ("Loops/Melody 140 Am.wav").bpmExplicit,
           "o BPM com \"bpm\" escrito é marcado como explícito; o número solto, não");
    CHECK (tagsForPath ("C:\\Sons\\Kicks\\a.wav").has (Type::kick) && tagsForPath ("/home/x/Kicks/a.flac").has (Type::kick),
           "as duas barras de pasta valem");
    CHECK (tagsForPath ("Kicks/Sub/Deep/x.wav").types == bit (Type::bass) && tagsForPath ("Kicks/A/B/x.wav").types == 0,
           "só as duas pastas de cima contam, a mais perto primeiro");
    CHECK (tagsForPath ("x.wav").types == 0 && tagsForPath ("").types == 0, "sem pasta e vazio não quebram");

    CHECK (bpmFromName ("Loop 140bpm") == 140 && bpmFromName ("BPM_92 keys") == 92 && bpmFromName ("x 140 BPM") == 140
               && bpmFromName ("x_140-bpm") == 140, "as formas do BPM escrito");
    CHECK (bpmFromName ("808 bpm") == 0 && bpmFromName ("Loop 20bpm") == 0 && bpmFromName ("Kick 808") == 0, "808 e 20 não são BPM");

    auto key = [] (const char* name) { const auto k = keyFromName (name); return keyName (k.first, k.second); };
    CHECK (key ("Am") == "Am" && key ("F#m") == "F#m" && key ("Cmin") == "Cm" && key ("Gmaj") == "Gmaj" && key ("A#") == "A#",
           "as formas do pedido: Am, F#m, Cmin, Gmaj, A#");
    CHECK (key ("Loop Em Lá Menor") == "Am", "a forma por extenso vence a curta: \"Em Lá Menor\" é Lá menor");
    CHECK (key ("Loop Bb") == "A#" && key ("loop f#") == "F#" && key ("loop bb") == "" && key ("LOOP DB") == "",
           "só a nota: com o acidente e a maiúscula (ou o #)");
    CHECK (key ("Am7 chord") == "Am" && key ("Cmaj7") == "Cmaj", "os números das pontas saem (Am7, Cmaj7)");
    CHECK (key ("Si bemol maior") == "A#maj" && key ("Sol# menor") == "G#m" && key ("Dó menor") == "Cm", "o nome da nota em português");
    CHECK (key ("PIANO FM 120") == "" && key ("LOOP EM 120") == "Em" && key ("Piano AM 120") == "",
           "\"AM\" maiúsculo só num nome todo em maiúsculas; FM não é tom");
    CHECK (key ("f minor loop") == "Fm" && key ("e menor que") == "", "letra minúscula: só com o modo em inglês");

    CHECK (typeFromId ("808") == (int) Type::s808 && typeFromId ("OpenHat") == (int) Type::openHat && typeFromId ("nada") == -1,
           "o tipo pelo id, sem diferença de caixa");
    CHECK (typesFromName ("Hats") == bit (Type::hat) && typesFromName ("808s") == bit (Type::s808)
               && typesFromName ("Claps & Snaps") == (bit (Type::clap) | bit (Type::snap)), "o plural simples vale");
}

static void testFilter()
{
    std::printf ("Filtro\n");

    CHECK (bpmNear (140, 140) && bpmNear (144, 140) && ! bpmNear (150, 140) && bpmNear (70, 140) && bpmNear (280, 140) && ! bpmNear (0, 140),
           "BPM perto: 4%% para cada lado, a metade e o dobro");
    CHECK (keyMatches (9, Mode::minor, 9, false) && keyMatches (0, Mode::major, 9, false) && ! keyMatches (9, Mode::major, 9, false)
               && ! keyMatches (4, Mode::minor, 9, false), "tom: o mesmo e o relativo (Lá menor e Dó maior); Lá maior e Mi menor não");
    CHECK (keyMatches (5, Mode::note, 9, false) && ! keyMatches (6, Mode::note, 9, false) && ! keyMatches (-1, Mode::none, 9, false),
           "só a nota: dentro da escala (Fá em Lá menor sim, Fá# não); sem tom não combina");

    const auto kick = tagsForPath ("K/Kick 01.wav");
    const auto loop = tagsForPath ("L/Hat Loop 140bpm Am.wav");
    Filter f;
    CHECK (! f.any() && passes (kick, f), "sem filtro, tudo passa");
    f.types = bit (Type::kick) | bit (Type::hat);
    CHECK (passes (kick, f) && passes (loop, f), "OU dentro do grupo de tipos (kick ou hat)");
    f.bpm = 140;
    CHECK (! passes (kick, f) && passes (loop, f), "E com o BPM");
    f.keyRoot = 0;
    f.keyMajor = true;
    CHECK (passes (loop, f), "E com o tom (Lá menor cabe em Dó maior)");
    f.keyRoot = 2;
    CHECK (! passes (loop, f), "outro tom: não passa");

    // a correção do usuário vence a regra
    const auto fixed = withOverride (kick, true, bit (Type::snare));
    CHECK (fixed.types == bit (Type::snare) && fixed.main == Type::snare && ! fixed.has (Type::kick), "a correção do usuário troca os tipos automáticos");
    CHECK (withOverride (loop, true, 0).types == 0 && withOverride (loop, true, 0).bpm == 140, "corrigir para \"nenhum tipo\" também vale; o BPM fica");
    CHECK (withOverride (kick, false, bit (Type::snare)).types == kick.types, "sem correção, a regra");
}

static void testSpeed()
{
    std::printf ("Velocidade\n");

    std::vector<std::string> paths;

    for (int i = 0; i < 50000; ++i)
        paths.push_back ("C:\\Users\\x\\Music\\WHYNOT LAB\\Biblioteca\\Trap Kit Vol " + std::to_string (i % 40) + "\\"
                         + cases[(std::size_t) i % std::size (cases)].path);

    const auto start = std::chrono::steady_clock::now();
    Mask all = 0;

    for (auto& p : paths)
        all |= tagsForPath (p).types;

    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    CHECK (all != 0 && ms < 2000.0, "50.000 caminhos em %.0f ms (%.1f us por som), fora da thread de mensagem", ms, ms * 1000.0 / 50000.0);
}

int main()
{
    testTable();
    testDetails();
    testFilter();
    testSpeed();

    std::printf ("\n%s (%d falhas)\n", failures == 0 ? "TODOS OS TESTES PASSARAM" : "FALHOU", failures);
    return failures == 0 ? 0 : 1;
}

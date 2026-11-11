// Testes da parte pura dos parâmetros dos comandos (src/commands/Params.h), sem JUCE: a conversão de número
// que satura e recusa o não finito, e a conferência de um valor contra a regra do schema.

#include "../../src/commands/Params.h"

#include <chrono>
#include <cstdio>
#include <limits>

using namespace whynot::params;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static const double notANumber = std::numeric_limits<double>::quiet_NaN();
static const double inf = std::numeric_limits<double>::infinity();

static void testConversion()
{
    std::printf ("Conversao de numero\n");

    CHECK (toInt (0.0) == 0 && toInt (12.9) == 12 && toInt (-12.9) == -12, "trunca para zero, como o (int)");
    CHECK (toInt (notANumber, 7) == 7 && toInt (inf, 7) == 7 && toInt (-inf, -3) == -3, "NaN e infinito viram o fallback");
    CHECK (toInt (1e300) == std::numeric_limits<int>::max() && toInt (-1e300) == std::numeric_limits<int>::min(),
           "1e300 satura nas pontas do int");
    CHECK (toInt (2147483647.0) == 2147483647 && toInt (2147483648.0) == 2147483647
               && toInt (-2147483648.0) == std::numeric_limits<int>::min() && toInt (-2147483649.0) == std::numeric_limits<int>::min(),
           "as pontas exatas do int");
    CHECK (toInt (2147483647.9) == 2147483647, "2^31 - 0,1 nao estoura");

    CHECK (saturated (0.25, 9.0) == 0.25 && saturated (-1e9, 9.0) == -1e9, "saturated nao mexe no que cabe");
    CHECK (saturated (notANumber, 9.0) == 9.0 && saturated (inf, 9.0) == 9.0, "saturated: nao finito = fallback");
    CHECK (saturated (notANumber, notANumber) == 0.0 && saturated (inf, -inf) == 0.0, "saturated: fallback nao finito = 0");
    CHECK (saturated (3e9, 0.0) == 2147483647.0 && saturated (-3e9, 0.0) == -2147483648.0, "saturated: faixa do int");
    CHECK ((int) saturated (1e300, 0.0) == std::numeric_limits<int>::max(), "(int) do saturated e definido");
    CHECK ((float) saturated (1e300, 0.0) > 2.0e9f, "(float) do saturated e finito");

    CHECK (saturatedWide (3e9, 0.0) == 3e9 && saturatedWide (4294967295.0, 0.0) == 4294967295.0, "saturatedWide deixa passar 32 bits sem sinal");
    CHECK (saturatedWide (1e300, 0.0) == int64Highest && saturatedWide (-1e300, 0.0) == int64Lowest, "saturatedWide: faixa do int64");
    CHECK ((std::int64_t) saturatedWide (1e300, 0.0) > 0 && (std::int64_t) saturatedWide (-1e300, 0.0) < 0, "(int64) do saturatedWide e definido");
    CHECK (saturatedWide (notANumber, 5.0) == 5.0, "saturatedWide: nao finito = fallback");

    CHECK (toInt64 (3e9) == 3000000000LL && toInt64 (-0.9) == 0, "toInt64 trunca");
    CHECK (toInt64 (notANumber, 4) == 4 && toInt64 (inf, 4) == 4, "toInt64: nao finito = fallback");
    CHECK (toInt64 (1e300) == 9223372036854774784LL && toInt64 (-1e300) == std::numeric_limits<std::int64_t>::min(), "toInt64 satura");
    CHECK (toInt64 (9223372036854775808.0) == 9223372036854774784LL, "2^63 exato nao estoura");
}

static Value num (double v)             { return { Kind::number, v, {} }; }
static Value integer (double v)         { return { Kind::integer, v, {} }; }
static Value boolean (bool b)           { return { Kind::boolean, b ? 1.0 : 0.0, {} }; }
static Value text (std::string_view t, double asNumber = 0.0)   { return { Kind::text, asNumber, t }; }
static Value list()                     { return { Kind::list, 0.0, {} }; }
static Value object()                   { return { Kind::object, 0.0, {} }; }

static Rule rule (const char* type)     { Rule r; r.type = typeFromName (type); return r; }

static void testCheck()
{
    std::printf ("Valor contra a regra do schema\n");

    CHECK (typeFromName ("string") == Type::text && typeFromName ("array") == Type::list && typeFromName ("integer") == Type::integer
               && typeFromName ("number") == Type::number && typeFromName ("boolean") == Type::boolean && typeFromName ("object") == Type::object
               && typeFromName ("") == Type::any && typeFromName ("null") == Type::any, "nomes de tipo do JSON Schema");

    for (const char* t : { "number", "integer" })
    {
        CHECK (check (rule (t), num (0.5)) == Problem::none && check (rule (t), integer (3)) == Problem::none, "%s aceita numero", t);
        CHECK (check (rule (t), num (notANumber)) == Problem::notFinite && check (rule (t), num (inf)) == Problem::notFinite
                   && check (rule (t), num (-inf)) == Problem::notFinite, "%s recusa NaN e infinito", t);
        CHECK (check (rule (t), num (1e300)) == Problem::none, "%s deixa 1e300 passar (finito: o leitor satura)", t);
        CHECK (check (rule (t), text ("48 dB/oit", 48.0)) == Problem::none, "%s aceita o rotulo de uma lista", t);
        CHECK (check (rule (t), text ("nan", notANumber)) == Problem::notFinite && check (rule (t), text ("1e999", inf)) == Problem::notFinite,
               "%s recusa \"nan\" e \"1e999\" escritos como texto", t);
        CHECK (check (rule (t), boolean (true)) == Problem::none, "%s aceita sim/nao (vira 1 ou 0)", t);
        CHECK (check (rule (t), list()) == Problem::wrongType && check (rule (t), object()) == Problem::wrongType
                   && check (rule (t), Value {}) == Problem::wrongType, "%s recusa lista, objeto e o que nao e JSON", t);
    }

    CHECK (check (rule ("string"), text ("x")) == Problem::none && check (rule ("string"), integer (1)) == Problem::none
               && check (rule ("string"), boolean (false)) == Problem::none, "string aceita valor simples (a raia 1 da playlist)");
    CHECK (check (rule ("string"), list()) == Problem::wrongType && check (rule ("string"), object()) == Problem::wrongType, "string recusa lista e objeto");
    CHECK (check (rule ("string"), num (notANumber)) == Problem::notFinite, "numero nao finito e recusado ate em campo de texto");
    CHECK (check (rule ("string"), text ("nan", notANumber)) == Problem::none, "o texto \"nan\" num campo de texto e so um texto");

    CHECK (check (rule ("boolean"), boolean (true)) == Problem::none && check (rule ("boolean"), integer (1)) == Problem::none
               && check (rule ("boolean"), text ("true")) == Problem::none, "boolean aceita valor simples");
    CHECK (check (rule ("boolean"), list()) == Problem::wrongType, "boolean recusa lista");

    CHECK (check (rule ("array"), list()) == Problem::none && check (rule ("array"), text ("a,b")) == Problem::wrongType
               && check (rule ("array"), object()) == Problem::wrongType && check (rule ("array"), integer (1)) == Problem::wrongType, "array so aceita lista");
    CHECK (check (rule ("object"), object()) == Problem::none && check (rule ("object"), list()) == Problem::wrongType
               && check (rule ("object"), text ("{}")) == Problem::wrongType, "object so aceita objeto");

    CHECK (check (rule (""), list()) == Problem::none && check (rule (""), text ("x")) == Problem::none && check (rule (""), num (notANumber)) == Problem::notFinite,
           "sem type: so o numero nao finito e recusado");

    auto ranged = rule ("number");
    ranged.hasMinimum = true;  ranged.minimum = 0.0;
    ranged.hasMaximum = true;  ranged.maximum = 1.0;
    CHECK (check (ranged, num (0.0)) == Problem::none && check (ranged, num (1.0)) == Problem::none && check (ranged, num (0.5)) == Problem::none, "minimum e maximum incluem as pontas");
    CHECK (check (ranged, num (-0.001)) == Problem::belowMinimum && check (ranged, num (1.001)) == Problem::aboveMaximum, "fora da faixa declarada");
    CHECK (check (ranged, integer (2)) == Problem::aboveMaximum, "a faixa vale para inteiro");
    CHECK (check (ranged, text ("5", 5.0)) == Problem::none, "a faixa nao vale para texto (rotulo)");

    auto onlyMin = rule ("integer");
    onlyMin.hasMinimum = true;  onlyMin.minimum = 1.0;
    CHECK (check (onlyMin, integer (0)) == Problem::belowMinimum && check (onlyMin, num (1e300)) == Problem::none, "so minimum");

    auto mode = rule ("string");
    mode.allowedText = { "pattern", "song", "cora\xC3\xA7\xC3\xA3o" };
    CHECK (check (mode, text ("song")) == Problem::none && check (mode, text ("cora\xC3\xA7\xC3\xA3o")) == Problem::none, "enum de texto (com acento)");
    CHECK (check (mode, text ("Song")) == Problem::notAllowed && check (mode, text ("")) == Problem::notAllowed
               && check (mode, integer (1)) == Problem::notAllowed, "fora do enum");

    auto depth = rule ("integer");
    depth.allowedNumbers = { 16.0, 24.0, 32.0 };
    CHECK (check (depth, integer (24)) == Problem::none && check (depth, num (24.0)) == Problem::none, "enum de numero");
    CHECK (check (depth, integer (20)) == Problem::notAllowed && check (depth, text ("24", 24.0)) == Problem::notAllowed, "numero fora do enum; texto nao casa com numero");
    CHECK (check (depth, list()) == Problem::wrongType, "o tipo vem antes do enum");
}

// O custo por valor conferido: o run() chama isto para cada parâmetro, a cada passo de um arraste.
static void bench()
{
    std::printf ("Custo\n");

    auto ranged = rule ("number");
    ranged.hasMinimum = true;  ranged.minimum = -100.0;
    ranged.hasMaximum = true;  ranged.maximum = 6.0;
    auto mode = rule ("string");
    mode.allowedText = { "ticks", "steps", "beats", "bars" };
    const Rule rules[] = { rule ("string"), ranged, rule ("boolean"), mode, rule ("integer"), rule ("array") };
    const Value values[] = { text ("ch_kick"), num (-6.5), boolean (true), text ("beats"), integer (12), list() };

    const int rounds = 2000000;
    int problems = 0;
    volatile double sink = 0.0;
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < rounds; ++i)
        for (int k = 0; k < 6; ++k)
        {
            Value v = values[k];
            v.number += (double) (i & 1);
            problems += check (rules[k], v) == Problem::none ? 0 : 1;
            sink = sink + (double) toInt (v.number * 1.5);
        }

    const double ns = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - start).count() / (rounds * 6.0);
    std::printf ("  %.1f ns por valor (conferir + converter), %d problemas em %d\n", ns, problems, rounds * 6);
    CHECK (ns < 200.0, "menos de 200 ns por valor");
}

int main()
{
    testConversion();
    testCheck();
    bench();

    std::printf ("\n%s: %d falha(s)\n", failures == 0 ? "TUDO OK" : "FALHOU", failures);
    return failures;
}

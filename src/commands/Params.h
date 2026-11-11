#pragma once

// A parte pura (sem JUCE) da leitura de parâmetros dos comandos: a conversão de número que não cai em
// comportamento indefinido e a conferência de um valor contra a regra do schema. O CommandRegistry traduz
// o juce::var para estes tipos e monta a mensagem; aqui só há conta, para o teste rodar com o cl
// (tests/commands/test_params.cpp).

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace whynot::params
{

//==============================================================================
// Conversão de número. Converter NaN, infinito ou um double fora da faixa para inteiro é comportamento
// indefinido em C++ (no x64 costuma dar INT_MIN): estas saturam e trocam o não finito pelo fallback.

constexpr double intLowest = -2147483648.0;
constexpr double intHighest = 2147483647.0;

// O maior double que ainda cabe num int64 é 2^63 - 1024; 2^63 já estoura.
constexpr double int64Lowest = -9223372036854775808.0;
constexpr double int64Highest = 9223372036854774784.0;

/** O número se for finito; senão o fallback (e 0 se o fallback também não for finito). */
inline double finiteOr (double v, double fallback) noexcept
{
    if (std::isfinite (v))
        return v;

    return std::isfinite (fallback) ? fallback : 0.0;
}

/** Finito e dentro da faixa do int: o (int) e o (float) de quem recebe são sempre definidos. */
inline double saturated (double v, double fallback) noexcept
{
    const double f = finiteOr (v, fallback);
    return f < intLowest ? intLowest : (f > intHighest ? intHighest : f);
}

/** Finito e dentro da faixa do int64 (sementes, que passam de 32 bits). */
inline double saturatedWide (double v, double fallback) noexcept
{
    const double f = finiteOr (v, fallback);
    return f < int64Lowest ? int64Lowest : (f > int64Highest ? int64Highest : f);
}

/** double → int: trunca para zero, satura nas pontas, não finito = fallback. */
inline int toInt (double v, int fallback = 0) noexcept
{
    return (int) saturated (v, (double) fallback);
}

/** double → int64: trunca para zero, satura nas pontas, não finito = fallback. */
inline std::int64_t toInt64 (double v, std::int64_t fallback = 0) noexcept
{
    if (! std::isfinite (v))
        return fallback;

    return (std::int64_t) (v < int64Lowest ? int64Lowest : (v > int64Highest ? int64Highest : v));
}

//==============================================================================
// Conferência de um valor contra a regra de um campo do schema (type, enum, minimum, maximum).

/** O type do schema. `any` = type ausente ou desconhecido: não confere o tipo. */
enum class Type { any, boolean, integer, number, text, list, object };

/** O que veio. `other` = o que não é JSON (binário, método). */
enum class Kind { boolean, integer, number, text, list, object, other };

inline Type typeFromName (std::string_view name) noexcept
{
    if (name == "boolean")  return Type::boolean;
    if (name == "integer")  return Type::integer;
    if (name == "number")   return Type::number;
    if (name == "string")   return Type::text;
    if (name == "array")    return Type::list;
    if (name == "object")   return Type::object;
    return Type::any;
}

struct Rule
{
    Type type = Type::any;
    bool required = false;
    bool hasMinimum = false, hasMaximum = false;
    double minimum = 0.0, maximum = 0.0;
    std::vector<std::string> allowedText;    // enum: os textos aceitos (UTF-8)
    std::vector<double> allowedNumbers;      // enum: os números aceitos

    bool hasEnum() const noexcept   { return ! allowedText.empty() || ! allowedNumbers.empty(); }
};

struct Value
{
    Kind kind = Kind::other;
    double number = 0.0;      // boolean (0 ou 1), integer e number: o valor; text: o texto lido como número
    std::string_view text;    // text: o texto, em UTF-8
};

enum class Problem { none, wrongType, notFinite, belowMinimum, aboveMaximum, notAllowed };

/** Confere um valor presente. A falta de um campo obrigatório é de quem chama (aqui não há valor).

    O tipo é conferido como os leitores (params::number, string e boolean) leem, e não ao pé da letra do
    JSON Schema: valores simples (texto, número, sim/não) entram num campo simples de outro tipo, porque a
    interface e o autoteste mandam a raia da playlist como número num campo "string" e o rótulo de uma lista
    ("48 dB/oit") num campo "number". O que nenhum leitor aceita é recusado: lista ou objeto num campo
    simples, valor simples num campo "array" ou "object", e número que não é finito em qualquer campo.
    Um "integer" com casas decimais passa (os comandos truncam). */
inline Problem check (const Rule& rule, const Value& v) noexcept
{
    const bool numeric = v.kind == Kind::integer || v.kind == Kind::number;
    const bool scalar = numeric || v.kind == Kind::boolean || v.kind == Kind::text;

    if (numeric && ! std::isfinite (v.number))
        return Problem::notFinite;

    switch (rule.type)
    {
        case Type::any:
            break;

        case Type::boolean:
        case Type::text:
            if (! scalar)
                return Problem::wrongType;
            break;

        case Type::integer:
        case Type::number:
            if (! scalar)
                return Problem::wrongType;

            // "nan", "inf" ou "1e999" escritos como texto
            if (v.kind == Kind::text && ! std::isfinite (v.number))
                return Problem::notFinite;
            break;

        case Type::list:
            if (v.kind != Kind::list)
                return Problem::wrongType;
            break;

        case Type::object:
            if (v.kind != Kind::object)
                return Problem::wrongType;
            break;
    }

    if (rule.hasEnum())
    {
        bool found = false;

        if (v.kind == Kind::text)
            for (const auto& t : rule.allowedText)
                found = found || v.text == t;

        if (numeric)
            for (const double n : rule.allowedNumbers)
                found = found || v.number == n;

        if (! found)
            return Problem::notAllowed;
    }

    if (numeric)
    {
        if (rule.hasMinimum && v.number < rule.minimum)
            return Problem::belowMinimum;

        if (rule.hasMaximum && v.number > rule.maximum)
            return Problem::aboveMaximum;
    }

    return Problem::none;
}

} // namespace whynot::params

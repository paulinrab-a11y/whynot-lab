#pragma once

// Recuperação de um take que ficou sem fechar (o app caiu gravando). Sem JUCE: só bytes e contas,
// testado em tests/record/test_record.cpp.
//
// O escritor do WAV só acerta os tamanhos do cabeçalho (RIFF e data) quando fecha o arquivo ou no
// flush() periódico. Se o app cai, o áudio está no disco, mas o cabeçalho diz 0 (ou o tamanho do
// último flush) e os leitores mostram um arquivo vazio ou mais curto. Aqui o tamanho de verdade
// sai do tamanho do arquivo: tudo o que vem depois do cabeçalho do bloco "data" é áudio, até o
// último frame inteiro.
//
// plan() olha só o começo do arquivo (o cabeçalho) e o tamanho dele, e devolve os remendos: quais
// bytes trocar e o tamanho final. Quem aplica no disco é o InputRecorder::recoverOrphanTakes.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace whynot::record::recovery
{

enum class Status
{
    intact,      // o cabeçalho já bate com o arquivo: nada a fazer
    repaired,    // há remendos em `patches` (e talvez um tamanho final diferente)
    empty,       // não há nem um frame de áudio depois do cabeçalho (ou o arquivo tem 0 bytes)
    notWav,      // não é um WAV que dê para entender (sem RIFF/WAVE, sem "fmt " ou sem "data")
    needsRf64    // passa de 4 GB e o cabeçalho não tem o espaço reservado (JUNK) para virar RF64
};

/** Bytes a escrever numa posição do arquivo. */
struct Patch
{
    uint64_t offset = 0;
    std::vector<uint8_t> bytes;
};

struct Plan
{
    Status status = Status::notWav;

    // o formato, lido do "fmt " (vale para intact, repaired e empty)
    uint32_t sampleRate = 0;
    uint16_t channels = 0, bitsPerSample = 0, blockAlign = 0;
    bool isFloat = false;

    uint64_t dataOffset = 0;   // onde o áudio começa
    uint64_t dataBytes = 0;    // áudio de verdade, em frames inteiros
    uint64_t frames = 0;
    uint64_t finalSize = 0;    // o tamanho que o arquivo deve ter (corta o frame pela metade; par, como o RIFF pede).
                               // Quem aplica: primeiro acerta o tamanho (corta, ou estica com zero), depois os remendos.
    bool rf64 = false;         // o arquivo fica (ou já era) RF64

    std::vector<Patch> patches;
};

namespace detail
{
    inline uint16_t u16 (const uint8_t* p) noexcept   { return (uint16_t) (p[0] | (p[1] << 8)); }
    inline uint32_t u32 (const uint8_t* p) noexcept   { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    inline uint64_t u64 (const uint8_t* p) noexcept   { return (uint64_t) u32 (p) | ((uint64_t) u32 (p + 4) << 32); }
    inline bool is (const uint8_t* p, const char* id) noexcept   { return std::memcmp (p, id, 4) == 0; }

    inline void put32 (std::vector<uint8_t>& out, uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back ((uint8_t) (v >> (8 * i)));
    }

    inline void put64 (std::vector<uint8_t>& out, uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
            out.push_back ((uint8_t) (v >> (8 * i)));
    }

    inline void putId (std::vector<uint8_t>& out, const char* id)   { out.insert (out.end(), id, id + 4); }
}

/** Quantos bytes do começo do arquivo bastam para o plan(): o cabeçalho que o JUCE escreve tem 104. */
inline constexpr std::size_t headBytesWanted = 64 * 1024;

/** head: os primeiros headSize bytes do arquivo (o que houver, até headBytesWanted). fileSize: o tamanho do arquivo. */
inline Plan plan (const uint8_t* head, std::size_t headSize, uint64_t fileSize)
{
    using namespace detail;
    constexpr uint64_t max32 = 0xffffffffull;

    Plan p;

    if (fileSize == 0)
    {
        p.status = Status::empty;   // o app caiu antes do primeiro pedaço chegar ao disco
        return p;
    }

    if (head == nullptr || headSize < 12 || ! (is (head, "RIFF") || is (head, "RF64")) || ! is (head + 8, "WAVE"))
        return p;

    if (headSize > fileSize)
        headSize = (std::size_t) fileSize;

    const bool wasRf64 = is (head, "RF64");
    bool haveFmt = false, haveData = false;
    std::size_t first = 0;         // o primeiro bloco depois de "WAVE": JUNK (reserva) ou ds64
    uint32_t firstSize = 0;
    bool firstIsJunk = false, firstIsDs64 = false;
    uint32_t dataSize32 = 0;

    for (std::size_t pos = 12; pos + 8 <= headSize;)
    {
        const uint8_t* c = head + pos;
        const uint32_t size = u32 (c + 4);

        if (pos == 12)
        {
            first = pos;
            firstSize = size;
            firstIsJunk = is (c, "JUNK");
            firstIsDs64 = is (c, "ds64");
        }

        if (is (c, "data"))
        {
            haveData = true;
            dataSize32 = size;
            p.dataOffset = pos + 8;
            break;
        }

        if (is (c, "fmt "))
        {
            if (size < 16 || pos + 8 + 16 > headSize)
                return p;

            const uint8_t* f = c + 8;
            const uint16_t tag = u16 (f);
            p.channels = u16 (f + 2);
            p.sampleRate = u32 (f + 4);
            p.blockAlign = u16 (f + 12);
            p.bitsPerSample = u16 (f + 14);
            p.isFloat = tag == 3;

            // WAVE_FORMAT_EXTENSIBLE: o formato de verdade são os dois primeiros bytes do GUID
            if (tag == 0xfffe && size >= 40 && pos + 8 + 40 <= headSize)
                p.isFloat = u16 (f + 24) == 3;

            if (p.blockAlign == 0)
                p.blockAlign = (uint16_t) (p.channels * ((p.bitsPerSample + 7) / 8));

            haveFmt = true;
        }

        const uint64_t next = (uint64_t) pos + 8 + size + (size & 1);

        if (next > headSize)
            break;   // um bloco que passa do que foi lido: sem "data" à vista

        pos = (std::size_t) next;
    }

    if (! haveFmt || ! haveData || p.blockAlign == 0 || p.channels == 0 || p.sampleRate == 0 || p.dataOffset > fileSize)
        return p;

    const bool ds64Usable = firstIsDs64 && firstSize >= 28 && first + 8 + 28 <= headSize;

    if (wasRf64 && ! ds64Usable)
        return p;   // RF64 sem ds64 logo depois de "WAVE": fora do padrão

    // o que o cabeçalho diz hoje
    uint64_t declaredRiff = u32 (head + 4), declaredData = dataSize32;

    if (wasRf64)
    {
        declaredRiff = u64 (head + first + 8);

        if (dataSize32 == max32)
            declaredData = u64 (head + first + 16);
    }

    const uint64_t available = fileSize - p.dataOffset;

    if (declaredData > 0 && declaredData % p.blockAlign == 0 && declaredData + (declaredData & 1) <= available
        && declaredRiff + 8 == fileSize)
    {
        p.status = Status::intact;
        p.dataBytes = declaredData;
        p.frames = declaredData / p.blockAlign;
        p.finalSize = fileSize;
        p.rf64 = wasRf64;
        return p;
    }

    // o tamanho de verdade: tudo depois do cabeçalho do "data", até o último frame inteiro
    p.dataBytes = available - available % p.blockAlign;
    p.frames = p.dataBytes / p.blockAlign;
    p.finalSize = p.dataOffset + p.dataBytes + (p.dataBytes & 1);

    if (p.dataBytes == 0)
    {
        p.status = Status::empty;
        return p;
    }

    const bool needs64 = p.dataBytes >= max32 || p.finalSize - 8 > max32;

    // áudio com número ímpar de bytes (8 bits mono, 24 bits mono): o byte de enchimento é zero
    const auto addPad = [&p]
    {
        if ((p.dataBytes & 1) != 0)
            p.patches.push_back ({ p.dataOffset + p.dataBytes, { (uint8_t) 0 } });
    };

    if (! wasRf64 && ! needs64)
    {
        Patch riff { 4, {} }, data { p.dataOffset - 4, {} };
        put32 (riff.bytes, (uint32_t) (p.finalSize - 8));
        put32 (data.bytes, (uint32_t) p.dataBytes);
        p.patches = { riff, data };
        addPad();
        p.status = Status::repaired;
        return p;
    }

    // RF64: os tamanhos de 64 bits moram no bloco ds64, logo depois de "WAVE"; os de 32 bits viram -1.
    // Num RIFF, o JUCE reserva um bloco JUNK nesse lugar justamente para isso: ele vira o ds64, com o
    // mesmo tamanho (o resto do bloco fica zerado, e o leitor pula).
    if (! wasRf64 && ! (firstIsJunk && firstSize >= 28 && first + 8 + 28 <= headSize))
    {
        p.status = Status::needsRf64;
        return p;
    }

    Patch top { 0, {} }, ds64 { first, {} }, data { p.dataOffset - 4, {} };
    putId (top.bytes, "RF64");
    put32 (top.bytes, (uint32_t) max32);
    putId (ds64.bytes, "ds64");
    put32 (ds64.bytes, firstSize);
    put64 (ds64.bytes, p.finalSize - 8);
    put64 (ds64.bytes, p.dataBytes);
    put64 (ds64.bytes, p.frames);
    put32 (ds64.bytes, 0);   // sem tabela de outros blocos grandes
    put32 (data.bytes, (uint32_t) max32);
    p.patches = { top, ds64, data };
    addPad();
    p.rf64 = true;
    p.status = Status::repaired;
    return p;
}

/** O arquivo inteiro na memória (para os testes e para arquivos pequenos): aplica o plano no próprio vetor.
    Devolve o plano; o vetor só muda quando o status é `repaired`. */
inline Plan repairInMemory (std::vector<uint8_t>& file)
{
    const auto p = plan (file.data(), file.size() < headBytesWanted ? file.size() : headBytesWanted, file.size());

    if (p.status == Status::repaired)
    {
        file.resize ((std::size_t) p.finalSize, 0);

        for (const auto& patch : p.patches)
            std::memcpy (file.data() + patch.offset, patch.bytes.data(), patch.bytes.size());
    }

    return p;
}

} // namespace whynot::record::recovery

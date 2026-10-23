// Autoteste headless: whynot-lab --self-test <relatorio.txt>. O código de saída é o número de falhas.
//
// Cada área fica num arquivo em src/app/selftest/ (um dono por arquivo, ver o quadro das sessões).
// Frente nova: crie selftest/<Área>.cpp com `void run<Área>SelfTest (SelfTestContext&)`, declare em
// SelfTestKit.h e chame aqui, antes dos arquivos inválidos. Cada área começa com project.new e monta
// o que precisa; o que passa de uma área para outra fica nos campos do SelfTestContext.

#include "SelfTest.h"
#include "selftest/SelfTestKit.h"

namespace whynot
{

int runSelfTest (const juce::File& reportFile, const juce::File& workFolder)
{
    using namespace selftest;

    Report rep;
    rep.file = reportFile;
    workFolder.deleteRecursively();
    workFolder.createDirectory();

    // catálogo de plugins e registro de carga próprios do teste, sem tocar nos do usuário
    const auto dataFolder = workFolder.getChildFile ("dados");
    AppController app (true, dataFolder);
    SelfTestContext ctx { rep, app, app.getModel(), workFolder, dataFolder, {}, {} };

    runCoreSelfTest (ctx);
    runCompositionSelfTest (ctx);
    runMixingSelfTest (ctx);
    runPluginsSelfTest (ctx);
    runFinishingSelfTest (ctx);
    runSamplerSelfTest (ctx);
    runIdeasSelfTest (ctx);
    runSynthSelfTest (ctx);
    runDesignSelfTest (ctx);
    runRecordingSelfTest (ctx);
    runAutomationSelfTest (ctx);
    runPerformanceSelfTest (ctx);
    runPianoSelfTest (ctx);
    runGridSelfTest (ctx);
    runRobustnessSelfTest (ctx);
    runStretchSelfTest (ctx);
    runVoiceSelfTest (ctx);
    runSlicingSelfTest (ctx);
    runProducerSelfTest (ctx);
    runFluiditySelfTest (ctx);
    runBeatKeySelfTest (ctx);
    runLibrarySelfTest (ctx);
    runFavoritesSelfTest (ctx);
    runCommunitySelfTest (ctx);
    runProducerPianoSelfTest (ctx);
    runRenderPackSelfTest (ctx);
    runWarpCaptureSelfTest (ctx);
    runControlSelfTest (ctx);
    runInvalidFilesSelfTest (ctx);

    rep.lines.add ({});
    rep.lines.add (rep.failures == 0 ? "TODOS OS TESTES PASSARAM" : juce::String (rep.failures) + " TESTE(S) FALHARAM");
    reportFile.replaceWithText (rep.lines.joinIntoString ("\n") + "\n");
    return rep.failures;
}

} // namespace whynot

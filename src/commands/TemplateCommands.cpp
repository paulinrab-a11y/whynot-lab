#include "CommandRegistry.h"
#include "../library/Templates.h"

// Templates de projeto (frente templates, 16/11): guardar o projeto aberto como ponto de partida, abrir um projeto
// novo a partir de um template (de fábrica ou do usuário), listar, apagar e escolher o do "novo projeto".
// Os arquivos ficam em <dados>/Templates (src/library/Templates.h). Nenhum destes comandos entra no desfazer:
// template.new troca o projeto aberto e limpa o histórico, como o project.new; os outros gravam fora do projeto.

namespace whynot
{

namespace
{
    using params::schema;

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto* o = new juce::DynamicObject();

        for (auto& [key, value] : kv)
            o->setProperty (key, value);

        return juce::var (o);
    }

    CommandResult fromResult (const juce::Result& r, juce::var value = {})
    {
        return r.wasOk() ? CommandResult::success (std::move (value)) : CommandResult::fail (r.getErrorMessage());
    }
}

void registerTemplateCommands (CommandRegistry& r)
{
    r.add ({ "template.list", "Templates de projeto", schema ({}), false,
             [] (CommandContext&, const juce::var&)
             {
                 juce::Array<juce::var> items;
                 juce::String chosen;

                 for (auto& t : templates::list())
                 {
                     items.add (object ({ { "name", t.name }, { "factory", t.factory }, { "default", t.isDefault }, { "description", t.description } }));

                     if (t.isDefault)
                         chosen = t.name;
                 }

                 return CommandResult::success (object ({ { "templates", items }, { "default", chosen },
                                                          { "folder", templates::folder().getFullPathName() } }));
             } });

    r.add ({ "template.save", "Salvar como template",
             schema ({ { "name", "string", true, "Nome do template (vira o nome da pasta; vazio, de fábrica ou repetido é recusado)"_t },
                       { "clearNotes", "boolean", false, "true = só a estrutura: sem notas, passos, blocos da playlist nem automação (padrão false)"_t },
                       { "replace", "boolean", false, "true = troca um template seu de mesmo nome; o antigo vai para a pasta apagados (padrão false)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 // o timbre de agora dos plugins VST3 vai junto (lido para o modelo sem passo de desfazer, como no salvar)
                 c.app.captureLivePluginStates();

                 templates::SaveOptions options;
                 options.clearNotes = params::boolean (a, "clearNotes", false);
                 options.replace = params::boolean (a, "replace", false);

                 juce::String name;
                 const auto result = templates::save (params::string (a, "name"), c.model.get(), c.model.projectFolder(), options, &name);

                 return fromResult (result, object ({ { "name", name }, { "file", templates::fileFor (name).getFullPathName() },
                                                      { "clearNotes", options.clearNotes } }));
             } });

    r.add ({ "template.new", "Novo projeto a partir de template",
             schema ({ { "name", "string", true, "Nome do template, de fábrica ou seu (ver template.list)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 // confere antes de largar o projeto aberto: o nome existe e o arquivo abre
                 if (const auto asked = templates::request (params::string (a, "name")); asked.failed())
                     return CommandResult::fail (asked.getErrorMessage());

                 // o mesmo caminho do project.new: para o transporte, troca o projeto e limpa o desfazer
                 const auto opened = c.app.newProject();

                 if (! templates::endRequest() && opened.wasOk())
                     return CommandResult::fail ("O projeto novo abriu sem o template: o novo projeto do app não passa pelos templates."_t);

                 return fromResult (opened, object ({ { "name", templates::folderNameOf (params::string (a, "name")) },
                                                      { "channels", (int) c.model.get().channels.size() }, { "bpm", c.model.get().bpm } }));
             } });

    r.add ({ "template.delete", "Apagar template",
             schema ({ { "name", "string", true, "Nome de um template seu (os de fábrica não se apagam); a pasta vai para Templates/apagados"_t } }), false,
             [] (CommandContext&, const juce::var& a)
             {
                 return fromResult (templates::remove (params::string (a, "name")),
                                    object ({ { "movedTo", templates::deletedFolder().getFullPathName() } }));
             } });

    r.add ({ "template.setDefault", "Template do novo projeto",
             schema ({ { "name", "string", true, "Template que o \"novo projeto\" passa a usar; \"\" volta ao de fábrica"_t } }), false,
             [] (CommandContext&, const juce::var& a)
             {
                 const auto result = templates::setDefault (params::string (a, "name"));
                 return fromResult (result, object ({ { "default", templates::defaultName() } }));
             } });

    // As duas entradas da paleta de comandos (comandos sem parâmetro entram nela sozinhos): a tela abre a escolha
    // ou pede o nome. Sem janela (autoteste, IA sem tela) não acontece nada.
    r.add ({ "template.choose", "Novo a partir de template...", schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 c.app.togglePanel ("template.choose");
                 return CommandResult::success();
             } });

    r.add ({ "template.saveDialog", "Salvar como template...", schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 c.app.togglePanel ("template.saveDialog");
                 return CommandResult::success();
             } });
}

} // namespace whynot

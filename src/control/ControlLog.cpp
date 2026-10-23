#include "ControlLog.h"

namespace whynot::control
{

ActivityLog::ActivityLog (juce::File f) : file (std::move (f))
{
}

juce::String ActivityLog::shortParams (const juce::var& params, int maxChars)
{
    // no JUCE uma lista também é isObject(), sem DynamicObject: confere pelo objeto
    if (auto* obj = params.getDynamicObject(); params.isVoid() || (obj != nullptr && obj->getProperties().isEmpty()))
        return {};

    // quebras de linha dentro de um texto viram espaço: o registro é uma linha por pedido
    auto text = juce::JSON::toString (params, juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::none))
                    .replaceCharacters ("\r\n\t", "   ");

    if (text.length() > maxChars)
        text = text.substring (0, maxChars - 1) + juce::String::charToString ((juce::juce_wchar) 0x2026);

    return text;
}

juce::String ActivityLog::formatLine (juce::Time when, const juce::String& method, const juce::String& command,
                                      const juce::String& outcome, double milliseconds, const juce::var& params)
{
    juce::String line;
    line << when.formatted ("%Y-%m-%d %H:%M:%S") << "  " << method;

    if (command.isNotEmpty())
        line << "  " << command;

    line << "  " << outcome.replaceCharacters ("\r\n", "  ");

    if (milliseconds >= 0.0)
        line << "  " << juce::String (juce::roundToInt (milliseconds)) << " ms";

    if (const auto p = shortParams (params); p.isNotEmpty())
        line << "  " << p;

    return line;
}

void ActivityLog::write (const juce::String& method, const juce::String& command, const juce::String& outcome,
                         double milliseconds, const juce::var& params)
{
    append (formatLine (juce::Time::getCurrentTime(), method, command, outcome, milliseconds, params));
}

void ActivityLog::event (const juce::String& what)
{
    append (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") + "  -- " + what);
}

void ActivityLog::append (const juce::String& line)
{
    if (file == juce::File())
        return;

    if (file.getSize() > rotateBytes)
    {
        const auto previous = file.getSiblingFile (file.getFileNameWithoutExtension() + ".anterior" + file.getFileExtension());
        previous.deleteFile();
        file.moveFileTo (previous);
    }

    file.getParentDirectory().createDirectory();
    file.appendText (line + "\r\n", false, false, nullptr);
}

} // namespace whynot::control

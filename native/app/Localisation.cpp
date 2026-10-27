#include "UI.h"
namespace kn {
namespace {
std::atomic<bool> english{false};
const std::map<String, String> &dictionary() {
    static const auto values = [] {
        std::map<String, String> result;
        auto file = resource("locales/en.json");
        if (file.existsAsFile()) {
            const auto catalogue = readJson(file);
            for (auto &[key, value] : catalogue.items())
                if (value.is_string())
                    result.emplace(str(key), str(value.get<std::string>()));
        }
        return result;
    }();
    return values;
}
String sourceText(const String &value) {
    for (auto &[fr, en] : dictionary())
        if (en == value)
            return fr;
    return value;
}
} // namespace
bool isEnglish() { return english; }
void languageEnglish(bool enabled) { english = enabled; }
String tr(const char *value) { return tr(String::fromUTF8(value)); }
String tr(const String &value) {
    if (!isEnglish())
        return value;
    auto &d = dictionary();
    auto found = d.find(value);
    if (found != d.end())
        return found->second;
    // Error messages may append a file name or native diagnostic to a fixed prefix.
    size_t longest = 0;
    String result = value;
    for (auto &[fr, en] : d)
        if (fr.endsWith(": ") && value.startsWith(fr) && (size_t)fr.length() > longest) {
            longest = fr.length();
            result = en + value.substring(fr.length());
        }
    return result;
}
void translateControls(juce::Component &root) {
    // Text editors, media names, table contents and user lyrics are deliberately excluded.
    if (auto *button = dynamic_cast<juce::Button *>(&root)) {
        button->setButtonText(tr(sourceText(button->getButtonText())));
        button->setTooltip(tr(sourceText(button->getTooltip())));
    }
    if (auto *label = dynamic_cast<juce::Label *>(&root))
        if (!label->isEditable())
            label->setText(tr(sourceText(label->getText())), juce::dontSendNotification);
    if (auto *box = dynamic_cast<juce::ComboBox *>(&root))
        for (int n = 0; n < box->getNumItems(); ++n)
            box->changeItemText(box->getItemId(n), tr(sourceText(box->getItemText(n))));
    if (auto *tabs = dynamic_cast<juce::TabbedComponent *>(&root))
        for (int n = 0; n < tabs->getNumTabs(); ++n)
            tabs->setTabName(n, tr(sourceText(tabs->getTabNames()[n])));
    if (auto *table = dynamic_cast<juce::TableListBox *>(&root)) {
        auto &header = table->getHeader();
        for (int n = 0; n < header.getNumColumns(true); ++n) {
            int id = header.getColumnIdOfIndex(n, true);
            header.setColumnName(id, tr(sourceText(header.getColumnName(id))));
        }
    }
    for (auto *child : root.getChildren())
        if (dynamic_cast<juce::TextEditor *>(child) == nullptr)
            translateControls(*child);
}
void App::setLanguage(bool english) {
    languageEnglish(english);
    preferences["uiLanguage"] = english ? "en" : "fr";
    translateControls(*this);
    translateControls(tabs);
    // Status fragments only: never translate the user document or media file names.
    String translated = status;
    for (const char *fragment : {" accords · ", " lignes de paroles", " accords · analyse terminée",
                                 " mots alignés · vérifier à l’écoute"}) {
        auto fr = String::fromUTF8(fragment);
        auto found = dictionary().find(fr);
        if (found != dictionary().end())
            translated = translated.replace(english ? fr : found->second, english ? found->second : fr);
    }
    setStatus(translated);
    refresh();
    savePreferences();
    repaint();
}
void App::ownLicense() {
    struct Content : juce::Component {
        juce::TextEditor body;
        Content() {
            addAndMakeVisible(body);
            body.setMultiLine(true, false);
            body.setReadOnly(true);
            body.setFont(juce::FontOptions(16));
            auto file =
                resource(isEnglish() ? "legal/LICENSE_KITHARA_NEA_EN.md" : "legal/LICENSE_KITHARA_NEA.md");
            body.setText(file.loadFileAsString(), false);
            body.setCaretPosition(0);
            setSize(800, 570);
        }
        void resized() override { body.setBounds(getLocalBounds().reduced(22)); }
    };
    presentDialog(tr("Licence de KITHÁRA NÉA"), std::make_unique<Content>(), 840, 620);
}
void App::manual() {
    auto file =
        resource(isEnglish() ? "manuals/KITHARA-NEA-Manual-EN.pdf" : "manuals/KITHARA-NEA-Manuel-FR.pdf");
    if (!file.existsAsFile() || !file.startAsProcess())
        setStatus(isEnglish() ? "Unable to open the bundled PDF manual."
                              : "Impossible d’ouvrir le manuel PDF livré.");
}
} // namespace kn

#include "UI.h"
namespace kn {
File whisperModelFile(String model) {
    if (model == "medium") {
        const auto link = dataFolder().getChildFile("models/whisper/external-medium.json");
        if (link.existsAsFile()) {
            try {
                const auto saved = readJson(link);
                File external(text(saved, "path"));
                if (external.existsAsFile() && external.getSize() == 1533763059 &&
                    external.getLastModificationTime().toMilliseconds() ==
                        saved.value("modified", int64_t(0)))
                    return external;
            } catch (...) {
                // A missing or changed external model returns to the download/locate choice.
            }
        }
        auto cached = dataFolder().getChildFile("models/whisper/ggml-medium.bin");
        if (cached.existsAsFile() && cached.getSize() == 1533763059)
            return cached;
    }
    return resource("whisper/models/ggml-" + model + ".bin");
}
void useExistingWhisperMedium(const File &file, Cancel cancel) {
    checkCancel(cancel);
    const auto metadata = readJson(resource("whisper/optional-models.json")).at("medium");
    require(file.existsAsFile() && file.getSize() == metadata.at("bytes").get<int64_t>(),
            tr("Ce fichier n’est pas le modèle Medium attendu."));
    checkFile(file, text(metadata, "sha256"));
    checkCancel(cancel);
    const auto link = dataFolder().getChildFile("models/whisper/external-medium.json");
    require(link.getParentDirectory().createDirectory().wasOk(),
            tr("Impossible de créer le dossier du modèle."));
    writeJson(link, {{"path", utf(file.getFullPathName())},
                     {"bytes", file.getSize()},
                     {"sha256", utf(text(metadata, "sha256"))},
                     {"modified", file.getLastModificationTime().toMilliseconds()}});
}
bool whisperModelAvailable(String model) {
    auto file = whisperModelFile(model);
    return file.existsAsFile() && (model != "medium" || file.getSize() == 1533763059);
}
File downloadWhisperMedium(Cancel cancel, Progress progress) {
    checkCancel(cancel);
    if (progress)
        progress(0, {});
    auto metadata = readJson(resource("whisper/optional-models.json")).at("medium");
    const auto expected = metadata.at("bytes").get<int64_t>();
    auto target = dataFolder().getChildFile("models/whisper/ggml-medium.bin");
    require(target.getParentDirectory().createDirectory().wasOk(),
            tr("Impossible de créer le dossier du modèle."));
    require(target.getParentDirectory().getBytesFreeOnVolume() > expected + 16 * 1024 * 1024,
            tr("Espace disque insuffisant pour le modèle."));
    auto staging = target.getSiblingFile("ggml-medium-" + juce::Uuid().toString() + ".part");
    struct Cleanup {
        File file;
        ~Cleanup() { file.deleteFile(); }
    } cleanup{staging};
    int status = 0;
    auto input = juce::URL(text(metadata, "url"))
                     .createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                            .withConnectionTimeoutMs(15000)
                                            .withNumRedirectsToFollow(8)
                                            .withStatusCode(&status));
    checkCancel(cancel);
    require(input && status == 200, tr("Téléchargement impossible. Vérifier la connexion et réessayer.") +
                                        " (HTTP " + String(status) + ")");
    auto stream = staging.createOutputStream();
    require(stream && stream->openedOk(), tr("Impossible d’écrire le modèle téléchargé."));
    stream->setPosition(0);
    stream->truncate();
    std::array<char, 65536> buffer{};
    int64_t bytes = 0;
    while (bytes < expected) {
        checkCancel(cancel);
        int count = input->read(buffer.data(), (int)std::min<int64_t>(buffer.size(), expected - bytes));
        require(count > 0, tr("Téléchargement incomplet. Réessayer."));
        require(stream->write(buffer.data(), (size_t)count), tr("Impossible d’écrire le modèle téléchargé."));
        bytes += count;
        if (progress)
            progress((double)bytes / expected * .97, {});
    }
    stream->flush();
    stream.reset();
    checkCancel(cancel);
    checkFile(staging, text(metadata, "sha256"));
    checkCancel(cancel);
    require(MoveFileExW(staging.getFullPathName().toWideCharPointer(),
                        target.getFullPathName().toWideCharPointer(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
            tr("Impossible d’installer le modèle vérifié."));
    if (progress)
        progress(1, {});
    return target;
}
void App::selectWhisperQuality() {
    if (filling || settings.quality.getSelectedId() != 2 || whisperModelAvailable("medium"))
        return;
    settings.quality.setSelectedId(1, juce::dontSendNotification);
    struct Choice : juce::Component {
        juce::Label explanation;
        juce::HyperlinkButton source{
            tr("Voir la source du modèle"),
            juce::URL("https://huggingface.co/ggerganov/whisper.cpp/blob/main/ggml-medium.bin")};
        juce::TextButton existing{tr("Utiliser un modèle existant…")},
            download{tr("Télécharger et installer")}, cancel{tr("Pas maintenant")};
        explicit Choice(App &app) {
            for (auto *c : std::initializer_list<juce::Component *>{&explanation, &source, &existing,
                                                                    &download, &cancel})
                addAndMakeVisible(c);
            explanation.setText(tr("La qualité approfondie nécessite un modèle supplémentaire de 1,43 Gio. "
                                   "Après téléchargement, il reste disponible hors ligne. Son intégrité sera "
                                   "vérifiée avant installation."),
                                juce::dontSendNotification);
            explanation.setFont(juce::FontOptions(17));
            explanation.setJustificationType(juce::Justification::topLeft);
            source.setFont(juce::Font(juce::FontOptions("Segoe UI", 14, juce::Font::plain)), false,
                           juce::Justification::left);
            source.setColour(juce::HyperlinkButton::textColourId, app.theme.ink.withAlpha(.75f));
            download.setColour(juce::TextButton::buttonColourId, app.theme.bronze);
            download.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            setSize(610, 290);
        }
        void resized() override {
            explanation.setBounds(22, 14, getWidth() - 44, 90);
            source.setBounds(22, 108, getWidth() - 44, 22);
            existing.setBounds(22, 148, getWidth() - 44, 34);
            cancel.setBounds(getWidth() - 175, getHeight() - 55, 150, 34);
            download.setBounds(getWidth() - 400, getHeight() - 55, 210, 34);
        }
    };
    auto content = std::make_unique<Choice>(*this);
    auto *controls = content.get();
    auto *dialog =
        presentDialog(tr("Qualité approfondie"), std::move(content), 650, 340,
                      [safe = safePointer()](int result, juce::Component &) {
                          if (!safe || (result != 1 && result != 2))
                              return;
                          File local;
                          if (result == 2) {
                              juce::FileChooser chooser(tr("Choisir le modèle Medium existant"), {}, "*.bin");
                              if (!chooser.browseForFileToOpen())
                                  return;
                              local = chooser.getResult();
                          }
                          safe->runJob(
                              result == 2 ? tr("Vérification du modèle") : tr("Téléchargement en cours"),
                              [safe, local](Cancel cancel) {
                                  JobResult result;
                                  if (local != File())
                                      useExistingWhisperMedium(local, cancel);
                                  else
                                      downloadWhisperMedium(cancel, [safe](double value, String) {
                                          if (safe)
                                              safe->jobProgress.store(value);
                                      });
                                  return result;
                              },
                              [safe](JobResult) {
                                  if (safe) {
                                      safe->settings.quality.setSelectedId(2, juce::dontSendNotification);
                                      safe->savePreferences();
                                      safe->setStatus(tr("Qualité approfondie disponible hors ligne."));
                                  }
                              });
                      });
    juce::Component::SafePointer<CloseableDialog> weak(dialog);
    controls->download.onClick = [weak] {
        if (weak)
            weak->exitModalState(1);
    };
    controls->existing.onClick = [weak] {
        if (weak)
            weak->exitModalState(2);
    };
    controls->cancel.onClick = [weak] {
        if (weak)
            weak->exitModalState(0);
    };
}
} // namespace kn

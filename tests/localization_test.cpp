#include "menu/Localization.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include "dlssnr/DlssNr_StageControls.h"

struct StageUiFixture
{
    NrOptional<uint32_t> DlssNrRoute {2}, DlssNrUiAfterMethod {0};
    NrOptional<int32_t> DlssNrRenderingMode {1};
    NrOptional<bool> DlssNrRunBeforeSr {false}, DlssNrUiManualResolution {false};
    NrOptional<float> DlssNrWorkingScale {1.0f}, DlssNrUiManualScale {1.0f};
    NrOptional<uint32_t> DlssNrPresentResolution {1}, DlssNrPresentCustomScale {0};
    NrOptional<uint32_t> DlssNrEnhancedResolution {0}, DlssNrEnhancedCustomScale {0};
    StageUiFixture GetDlssNrConfigSnapshot() const
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        return *this;
    }
};

struct CatalogEntry
{
    const char* source;
    const char* translated[Neurotic::LanguageCount - 1];
};
#include "menu/locales/catalog.inc"
#include "menu/locales/integration.inc"

static int checks = 0;
static void Check(bool condition, const char* description)
{
    if (!condition)
        throw std::runtime_error(description);
    ++checks;
}

int main()
{
    try
    {
        Check(Neurotic::LanguageIndex("invalid") == 0, "unknown language falls back to English");
        Check(Neurotic::LanguageIndex("PT-BR") == 4, "Portuguese alias");
        for (int language = 0; language < Neurotic::LanguageCount; ++language)
        {
            Neurotic::SetLanguage(Neurotic::Languages[language].code);
            for (const auto& entry : Catalog)
                Check(Neurotic::Translate(entry.source) == (language ? entry.translated[language - 1] : entry.source),
                      entry.source);
            Check(Neurotic::Translate("  user file Z:/my data.bin\n") == "  user file Z:/my data.bin\n",
                  "unknown bytes preserved");
            const auto timing = Neurotic::Translate("Frame 3840x2160 | Work 1920x1080 | Guides 1920x1080");
            Check(timing.find("3840x2160") != std::string::npos && timing.find("1920x1080") != std::string::npos,
                  "dynamic numbers preserved");
            Check(language == 0 || timing != "Frame 3840x2160 | Work 1920x1080 | Guides 1920x1080",
                  "dynamic template translated");
            const auto state = Neurotic::Translate("Status: running | Mode: Pre-SR requested | Reset pending: yes");
            Check(language == 0 || state.find("running") == std::string::npos, "nested status translated");
            const auto title = Neurotic::Translate("Neurotic Alpha 0.9.4 | Based on OptiScaler v10.0.0-dev");
            Check(title.find("OptiScaler v10.0.0-dev") != std::string::npos, "upstream version preserved");
            Check(language == 0 || title.find("Based on") == std::string::npos, "title translated");
            const auto joke = Neurotic::Translate("MFG totally works with Nukem's 100% no scam");
            Check(joke.find("100%") != std::string::npos && joke.find("100%%") == std::string::npos,
                  "literal percent preserved");
            Check(language == 0 || joke.find("totally works") == std::string::npos, "percent-only template translated");
            std::string source = "Show Graphs ignored suffix";
            const char* begin = source.c_str();
            const char* end = begin + 11;
            {
                Neurotic::LocalizedRange range(begin, end);
                Check(std::string(begin, end) == Neurotic::Translate("Show Graphs"), "bounded range");
                const char* inner = "Close";
                const char* innerEnd = nullptr;
                Neurotic::LocalizedRange nested(inner, innerEnd);
                Check(std::string(inner) == "Close", "nested presentation not translated twice");
            }
            for (int n = 0; n < 2200; ++n)
                Check(Neurotic::Translate("Unknown value " + std::to_string(n)) == "Unknown value " + std::to_string(n),
                      "cache eviction");
        }
        Neurotic::SetLanguage("de");
        bool threadEnglish = false;
        std::thread isolated([&] { threadEnglish = Neurotic::Translate("Close") == "Close"; });
        isolated.join();
        Check(threadEnglish, "translation context is thread-local");

        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = io.LogFilename = nullptr;
        io.DisplaySize = ImVec2(1920, 1080);
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        Neurotic::AddLanguageFonts(io.Fonts, 16.0f);
        ImGuiID checkboxId = 0, tabId = 0;
        const ImGuiStyle baseStyle = ImGui::GetStyle();
        const auto start = std::chrono::steady_clock::now();
        for (int language = 0; language < Neurotic::LanguageCount; ++language)
        {
            Neurotic::SetLanguage(Neurotic::Languages[language].code);
            for (bool lightTheme : {false, true})
            for (int scaleStep = 5; scaleStep <= 20; ++scaleStep)
            {
                const float scale = scaleStep / 10.0f;
                ImGui::GetStyle() = baseStyle;
                if (lightTheme) ImGui::StyleColorsLight();
                else ImGui::StyleColorsDark();
                ImGui::GetStyle().ScaleAllSizes(scale);
                ImGui::NewFrame();
                ImGui::PushFont(nullptr, 16.0f * scale);
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2(900 * scale, 520 * scale));
                ImGui::Begin("Localization test###fixed");
                const ImGuiID id = ImGui::GetID("Show Graphs");
                const ImGuiID tab = ImGui::GetID("Neural Rendering");
                if (!checkboxId)
                {
                    checkboxId = id;
                    tabId = tab;
                }
                Check(id == checkboxId && tab == tabId, "widget IDs survive language changes");
                const auto englishLabel = ImGui::CalcTextSize("Save Settings");
                const auto translated = Neurotic::Translate("Save Settings");
                const auto nativeSize =
                    ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0, translated.c_str());
                Check(std::abs(englishLabel.x - nativeSize.x) <= 1, "measurement uses translated text");
                ImGui::SetNextItemWidth(100 * scale);
                if (ImGui::BeginCombo("Menu Scale", "Auto"))
                    ImGui::EndCombo();
                ImGui::SameLine(0, 15);
                ImGui::Button("Save Settings");
                ImGui::SameLine(0, 6);
                ImGui::Button("Close");
                ImGui::SameLine();
                const auto& headerStyle = ImGui::GetStyle();
                const float wikiWidth = ImGui::CalcTextSize("Open Wiki").x + headerStyle.FramePadding.x * 2.0f +
                                        headerStyle.ItemSpacing.x + ImGui::CalcTextSize("(?)").x;
                const float languageWidth =
                    100.0f * scale + headerStyle.ItemInnerSpacing.x + ImGui::CalcTextSize("Language").x;
                const float headerGroupWidth = wikiWidth + headerStyle.ItemSpacing.x + languageWidth;
                if (ImGui::GetContentRegionAvail().x > headerGroupWidth)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - headerGroupWidth);
                ImGui::Button("Open Wiki");
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100 * scale);
                if (ImGui::BeginCombo("Language", Neurotic::Languages[language].name))
                    ImGui::EndCombo();
                if (ImGui::GetItemRectMax().x >= 900 * scale)
                    std::fprintf(stderr, "header overflow: language=%s scale=%.1f right=%.1f limit=%.1f group=%.1f\n",
                                 Neurotic::Languages[language].code, scale, ImGui::GetItemRectMax().x, 900 * scale,
                                 headerGroupWidth);
                Check(ImGui::GetItemRectMax().x < 900 * scale, "action and language row fits");
                bool showGraphs = true;
                ImGui::Checkbox("Show Graphs", &showGraphs);
                ImGui::Text("3840x2160 -> 1920x1080 (2.0) [3840x2160 (1.0)]");
                ImGui::SameLine(0, 10 * scale);
                ImGui::Text("GPU: %s", "Test GPU");
                Check(ImGui::GetItemRectMax().x < 900 * scale, "resolution and GPU row fits");
                // Same translated measurement/render hooks and wrap boundary used by the NR page.
                // Measure every new tooltip/status/diagnostic at every supported scale, including
                // the long German and French strings. Scroll vertically, never widen the page.
                for (const auto& entry : IntegrationCatalog)
                {
                    const auto text = Neurotic::Translate(entry.source);
                    Check(text == (language ? entry.translated[language - 1] : entry.source),
                          "integration translation including explicit line breaks");
                    const float wrapWidth = 820.0f * scale;
                    const auto size = ImGui::GetFont()->CalcTextSizeA(
                        ImGui::GetFontSize(), FLT_MAX, wrapWidth, text.c_str());
                    Check(size.x <= wrapWidth + 1.0f, "integration text fits wrapped NR page");
                }
                if (ImGui::BeginTabBar("Pages"))
                {
                    for (const char* label : { "General", "Upscaling", "Frame Generation", "Neural Rendering",
                                               "Advanced", "Tools", "Diagnostics" })
                        if (ImGui::BeginTabItem(label))
                        {
                ImGui::Text("Running: %.2f ms", 3.25);
                            ImGui::EndTabItem();
                        }
                    ImGui::EndTabBar();
                }
                const char* supportPrompt = "Enjoying NeuRotic?";
                const char* supportButton = "Send Coffee";
                const auto& style = ImGui::GetStyle();
                const float supportWidth = ImGui::CalcTextSize(supportPrompt).x + style.ItemSpacing.x +
                                           ImGui::CalcTextSize(supportButton).x + style.FramePadding.x * 2.0f;
                const float contentRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - supportWidth);
                ImGui::TextUnformatted(supportPrompt);
                ImGui::SameLine();
                ImGui::Button(supportButton);
                Check(std::abs(ImGui::GetItemRectMax().x - contentRight) <= 1.0f, "support row anchored right");
                auto* baked = ImGui::GetFont()->GetFontBaked(ImGui::GetFontSize());
                for (const auto& entry : Catalog)
                {
                    const char* cursor = language ? entry.translated[language - 1] : entry.source;
                    while (*cursor)
                    {
                        unsigned int codepoint;
                        const int bytes = ImTextCharFromUtf8(&codepoint, cursor, nullptr);
                        Check(bytes > 0, "valid UTF-8");
                        cursor += bytes;
                        if (codepoint > 127)
                            Check(baked->FindGlyphNoFallback((ImWchar) codepoint) != nullptr, "accent glyph available");
                    }
                }
                ImGui::End();
                ImGui::PopFont();
                ImGui::Render();
                // Exercise the actual production controls for every valid stage/method/preset.
                for (int choice = 0; choice < 18; ++choice)
                {
                    StageUiFixture cfg;
                    if (choice < 4)
                    {
                        DlssNr::StageUi::SelectMethod(cfg, 0);
                        if (choice < 2) DlssNr::StageUi::SelectStage(cfg, 0);
                        DlssNr::StageUi::SelectManual(cfg, choice % 2 != 0);
                    }
                    else
                    {
                        DlssNr::StageUi::SelectMethod(cfg, choice < 11 ? 1 : 2);
                        DlssNr::StageUi::SelectPreset(cfg, (choice - 4) % 7);
                    }
                    const auto before = cfg.GetDlssNrConfigSnapshot();
                    ImGui::NewFrame();
                    ImGui::PushFont(nullptr, 16.0f * scale);
                    ImGui::SetNextWindowPos(ImVec2(0, 0));
                    ImGui::SetNextWindowSize(ImVec2(820 * scale, 480 * scale));
                    ImGui::Begin("Stage-first test###stage");
                    bool enabled = true;
                    ImGui::Checkbox("Enable Neural Rendering", &enabled);
                    const auto top = ImGui::GetCursorScreenPos();
                    const auto right = top.x + ImGui::GetContentRegionAvail().x;
                    Check(!DlssNr::StageUi::RenderControls(cfg), "drawing stage controls is read-only");
                    Check(ImGui::GetItemID() == ImGui::GetID("##NrResolution"), "resolution is always the final primary row");
                    Check(ImGui::GetItemRectMax().x <= right + 1, "primary controls fit every language and scale");
                    Check(ImGui::GetItemRectMin().y > top.y, "primary rows retain vertical order");
                    const float previewWidth = ImGui::GetItemRectSize().x - ImGui::GetFrameHeight() - 2 * ImGui::GetStyle().FramePadding.x;
                    for (const char* label : DlssNr::StageUi::PresentPresets)
                        Check(ImGui::CalcTextSize(label).x < previewWidth, "complete preset names fit the selector");
                    Check(cfg.DlssNrRoute.value_or_default() == before.DlssNrRoute.value_or_default() &&
                          cfg.DlssNrWorkingScale.value_or_default() == before.DlssNrWorkingScale.value_or_default(),
                          "layout never mutates rendering choices");
                    Check(!DlssNr::StageUi::RenderRuntimeStatus(std::nullopt), "absent provider snapshot retains generic status");
                    DlssNr::MenuStatus::RuntimeStatus status;
                    status.state = DlssNr::MenuStatus::State(choice % 7);
                    status.realFrame = 1842;
                    status.workWidth = 1712; status.workHeight = 960;
                    status.outputWidth = 2560; status.outputHeight = 1440;
                    std::snprintf(status.reason.data(), status.reason.size(), "fixture reason");
                    Check(DlssNr::StageUi::RenderRuntimeStatus(status), "bounded read-only state snapshot rendered");
                    Check(status.realFrame == 1842, "status rendering does not mutate telemetry");
                    ImGui::End();
                    ImGui::PopFont();
                    ImGui::Render();
                }
                Check(ImGui::GetDrawData()->TotalVtxCount > 0, "translated draw data emitted");
                for (auto* texture : io.Fonts->TexList)
                {
                    if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates)
                    {
                        texture->SetTexID(1);
                        texture->SetStatus(ImTextureStatus_OK);
                    }
                }
            }
        }
        // Real keyboard/controller events through the same production controls.
        Neurotic::SetLanguage("en");
        ImGui::GetStyle() = baseStyle;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        StageUiFixture navigation;
        DlssNr::StageUi::SelectMethod(navigation, 0);
        DlssNr::StageUi::SelectStage(navigation, 0);
        ImGuiID stageId = 0, resolutionId = 0, methodId = 0;
        const auto navFrame = [&](bool focus = false) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(820, 480));
            ImGui::Begin("Navigation###stage-nav");
            stageId = ImGui::GetID("##NrStage");
            methodId = ImGui::GetID("##NrMethod");
            resolutionId = ImGui::GetID("##NrResolution");
            if (focus) ImGui::SetKeyboardFocusHere();
            DlssNr::StageUi::RenderControls(navigation);
            ImGui::End();
            ImGui::Render();
        };
        const auto key = [&](ImGuiKey k) {
            io.AddKeyEvent(k, true); navFrame();
            io.AddKeyEvent(k, false); navFrame(); navFrame();
        };
        navFrame(true); navFrame();
        Check(GImGui->NavId == stageId, "keyboard focus starts on stage selector");
        key(ImGuiKey_Tab);
        // SetKeyboardFocusHere deliberately keeps the nav cursor hidden; the first Tab
        // enters keyboard navigation on that item, the next advances to the next control.
        key(ImGuiKey_Tab);
        Check(GImGui->NavId == resolutionId, "Tab skips the fixed Before-stage method");
        navFrame(true); navFrame();
        key(ImGuiKey_Enter); key(ImGuiKey_DownArrow); key(ImGuiKey_Enter);
        Check(DlssNr::StageUi::Stage(navigation) == 1, "keyboard selects After stage");
        navFrame(true); navFrame();
        key(ImGuiKey_GamepadDpadDown);
        Check(GImGui->NavId == methodId, "controller navigation reaches the After-stage method");
        ImGui::DestroyContext();
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        std::printf("PASS: %d checks; catalogs, dynamic values, IDs, five languages at all 16 scales, light/dark themes, UTF-8 glyphs; UI "
                    "test %lld ms\n",
                    checks, ms);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

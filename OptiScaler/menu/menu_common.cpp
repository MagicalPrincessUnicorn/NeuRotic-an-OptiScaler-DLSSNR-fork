#include "pch.h"
#include <menu/Localization.h>
#include "localization/LanguageRuntime.h"
#include "input/HotkeyChord.h"
#include <dlssnr/NrStatusPanel.h>
#include "SleekPilotLight.h"
#include "MenuHeightControl.h"
#include "SleekContentCard.h"
#include <mfg/ExperimentalMfgRuntime.h>
#include "ExperimentalMfgControls.h"
#include "menu_common.h"
#include "SleekTheme.h"
#include "SleekWater.h"
#include "Localization.h"
#include "UiBrightness.h"
#include "nr/semantic/character/CharacterInspectorMenu.h"
#include "nr/semantic/character/CharacterFgActivity.h"
#include "nr/semantic/object_rules/ObjectRuleGameMenu.h"
#include <dlssnr/DlssNr_ExposureScan.h>
#include <dlssnr/DlssNr_Present.h>
#include <mfg/MfgAdaUnlock.h>
#include <mfg/MfgSetup.h>
#include "MfgRestartWarning.h"
#include "MfgRequestReadout.h"
#include "PresetComboLayout.h"
#include "PersistenceFailureView.h"
#include <dlssnr/DlssNrFeature_Vk.h>

#include <algorithm>
#include <cfloat>

#include <dlssnr/DlssNr.h>
#include <dlssnr/NrExperimentalPolicy.h>
#include <dlssnr/NrExperimentalSession.h>

#include "input/input_system.h"
#include "input/MenuEscapeClose.h"
#include <KeyChord.h>

#include "font/Hack_Compressed.h"

#include <proxies/XeSS_Proxy.h>
#include <proxies/XeFG_Proxy.h>
#include <proxies/FfxApi_Proxy.h>
#include <proxies/Streamline_Proxy.h>

#include <framegen/nvngx/Nvngx_FG.h>

#include <nvapi/fakenvapi.h>
#include <hooks/Reflex_Hooks.h>

#include <version_check.h>

#include <upscaler_time/UpscalerTime_Vk.h>

#include <imgui/imgui_internal.h>
#include <imgui/ImGuiNotify.hpp>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_uwp.h>

#include <mutex>
#include <cstdarg>

#include <array>
#include <chrono>
#include <memory>
#include <type_traits>
#include <misc/IdentifyGpu.h>
#include <hooks/Xell_Hooks.h>
#include <low_latency/input/input_common.h>

#define MARK_ALL_BACKENDS_CHANGED()                                                                                    \
    for (auto& singleChangeBackend : State::Instance().changeBackend)                                                  \
        singleChangeBackend.second = true;

static float fontSize = 16.0f;
static bool reduceMenuMotion = false;
static Neurotic::Sleek::MenuHeightControl menuHeight;
static Neurotic::Sleek::WaterTransition menuWater;
static Neurotic::Sleek::WaterColors menuWaterColors;
static ImGuiWindow* waterMenuRoot = nullptr;
static int waterMenuFrame = -1;
static uint64_t waterVisibilityGeneration = 0;
static ImVec2 overlaySize(0.0f, 0.0f);
static ImVec2 overlayPosition(-1000.0f, -1000.0f);
static bool _hdrTonemapApplied = false;
static ImVec4 SdrColors[ImGuiCol_COUNT];

static bool inputMenu = false;
static bool inputCloseMenu = false;
static bool inputFG = false;
static bool inputFps = false;
static bool inputFpsCycle = false;
static uint64_t lastInputTick = 0;
constexpr uint64_t debounceThreshold = 1000;

static bool hasGamepad = false;
struct MenuInputDraft
{
    bool initialized = false;
    bool dirty = false;
    bool mouse = false;
    bool keyboard = true;
    bool controller = true;
};
static MenuInputDraft menuInputDraft;

static void ResetMenuInputDraft(const Config& config)
{
    menuInputDraft = { true, false, config.AllowGameMouse.value_or_default(),
                       config.AllowGameKeyboard.value_or_default(),
                       config.AllowGameController.value_or_default() };
}
static bool ffxInitTried = false;
static bool xefgInitTried = false;
static std::string windowTitle;
static std::string selectedUpscalerName = "";
static Upscaler currentBackend = Upscaler::Reset;
static std::string currentBackendName = "";
static int refreshRate = 0;

static ImVec2 splashPosition(-1000.0f, -1000.0f);
static ImVec2 splashSize(0.0f, 0.0f);
static double splashStart = 0.0;
static double splashLimit = 0.0;
static std::vector<std::string> splashText = { Neurotic::UiLiteral("ingame.menu-common.cope_smarter_not_harder_ab9f08e7", "Cope smarter, not harder"),
                                               Neurotic::UiLiteral("ingame.menu-common.coping_is_strong_with_this_one_8c12616d", "Coping is strong with this one..."),
                                               Neurotic::UiLiteral("ingame.menu-common.this_is_where_the_fun_begins_cab17475", "This is where the fun begins..."),
                                               Neurotic::UiLiteral("ingame.menu-common.got_any_more_of_them_scalers_7779b417", "Got any more of them scalers?..."),
                                               Neurotic::UiLiteral("ingame.menu-common.fake_pixels_and_even_faker_frames_c5372a74", "Fake pixels and even faker frames..."),
                                               Neurotic::UiLiteral("ingame.menu-common.fake_frames_get_your_fake_frames_8ec66f56", "Fake frames, get your fake frames..."),
                                               Neurotic::UiLiteral("ingame.menu-common.i_m_here_to_kick_pixels_and_chew_frames_64e182db", "I'm here to kick pixels and chew frames..."),
                                               Neurotic::UiLiteral("ingame.menu-common.i_find_your_lack_of_supersampling_disturbing_0a1e1134", "I find your lack of supersampling disturbing..."),
                                               Neurotic::UiLiteral("ingame.menu-common.frame_by_frame_i_scale_up_0e3dc729", "Frame by frame, I scale-up!"),
                                               Neurotic::UiLiteral("ingame.menu-common.resistance_is_futile_your_pixels_will_be_upscale_cd794b7f", "Resistance is futile. Your pixels will be upscaled."),
                                               Neurotic::UiLiteral("ingame.menu-common.i_ve_got_99_problems_but_low_res_ain_t_one_8ec5af4d", "I've got 99 problems, but low-res ain't one."),
                                               Neurotic::UiLiteral("ingame.menu-common.it_s_over_dlss_i_have_the_higher_ground_bc752a92", "It's over, DLSS, I have the higher ground!"),
                                               Neurotic::UiLiteral("ingame.menu-common.this_isn_t_the_resolution_you_re_looking_for_5532bebb", "This isn't the resolution you're looking for"),
                                               Neurotic::UiLiteral("ingame.menu-common.to_infinity_and_beyond_with_ray_tracing_off_ab1e5f1d", "To infinity and beyond... with ray tracing off"),
                                               Neurotic::UiLiteral("ingame.menu-common.i_have_a_bad_feeling_about_this_frame_pacing_8588e65a", "I have a bad feeling about this frame pacing"),
                                               Neurotic::UiLiteral("ingame.menu-common.it_s_dangerous_to_go_alone_take_this_upscaler_1c3c14f4", "It's Dangerous to Go Alone-Take This Upscaler"),
                                               Neurotic::UiLiteral("ingame.menu-common.upscaled_beyond_recognition_98b5c3b7", "Upscaled beyond recognition."),
                                               Neurotic::UiLiteral("ingame.menu-common.trust_the_process_ignore_the_shimmer_4f5cf2f9", "Trust the process. Ignore the shimmer."),
                                               Neurotic::UiLiteral("ingame.menu-common.real_fake_frames_certified_8aab3998", "Real fake frames. Certified."),
                                               Neurotic::UiLiteral("ingame.menu-common.the_illusion_of_performance_e2a9213b", "The illusion of performance"),
                                               Neurotic::UiLiteral("ingame.menu-common.this_upscaler_belongs_in_a_museum_92e755f5", "This upscaler belongs in a museum!"),
                                               Neurotic::UiLiteral("ingame.menu-common.because_native_rendering_is_overrated_a6ff695d", "Because native rendering is overrated."),
                                               Neurotic::UiLiteral("ingame.menu-common.the_more_you_upscaler_the_more_you_save_92a316fe", "The more you upscaler, the more you save"),
                                               Neurotic::UiLiteral("ingame.menu-common.it_s_never_too_late_to_buy_a_better_gpu_ff1475a4", "It's never too late to buy a better GPU"),
                                               Neurotic::UiLiteral("ingame.menu-common.we_don_t_need_real_pixels_where_we_re_going_32ea7509", "We don't need real pixels where we're going"),
                                               Neurotic::UiLiteral("ingame.menu-common.did_you_know_that_intel_released_xefg_for_everyo_53912c76", "Did you know that Intel released XeFG for everyone?"),
                                               Neurotic::UiLiteral("ingame.menu-common.mfg_totally_works_with_nukem_s_100_no_scam_790fd427", "MFG totally works with Nukem's 100%% no scam"),
                                               Neurotic::UiLiteral("ingame.menu-common.some_of_those_pixels_might_even_be_real_9b132442", "Some of those pixels might even be real!"),
                                               Neurotic::UiLiteral("ingame.menu-common.just_don_t_look_too_closely_at_the_image_21011b51", "Just don't look too closely at the image"),
                                               Neurotic::UiLiteral("ingame.menu-common.even_supports_software_xess_3011ca65", "Even supports \"software\" XeSS!"),
                                               Neurotic::UiLiteral("ingame.menu-common.it_s_too_blurry_to_go_alone_take_rcas_with_you_9a65da53", "It's too blurry to go alone, take RCAS with you"),
                                               Neurotic::UiLiteral("ingame.menu-common.thanks_nitec_back_to_you_nitec_211bd2a7", "Thanks nitec, back to you nitec"),
                                               Neurotic::UiLiteral("ingame.menu-common.tested_and_approved_by_by_u_de8f4b58", "Tested and approved by By-U"),
                                               Neurotic::UiLiteral("ingame.menu-common.0_8_was_an_inside_job_203ecc35", "0.8 was an inside job"),
                                               Neurotic::UiLiteral("ingame.menu-common.fsr4_dp4a_weneta_amd_plz_184ad910", "FSR4 DP4a wenETA, AMD plz"),
                                               "OptiCopers, assemble!",
                                               Neurotic::UiLiteral("ingame.menu-common.the_way_it_s_meant_to_be_upscaled_25653469", "The Way It's Meant To Be Upscaled"),
                                               Neurotic::UiLiteral("ingame.menu-common.your_game_may_not_even_crash_today_a0db6497", "Your game may not even crash today"),
                                               Neurotic::UiLiteral("ingame.menu-common.expanded_and_enhanced_56d4bf44", "Expanded and Enhanced"),
                                               Neurotic::UiLiteral("ingame.menu-common.it_s_only_my_5th_crash_today_aada8725", "It's only my 5th crash today"),
                                               Neurotic::UiLiteral("ingame.menu-common.latency_with_fg_but_i_have_good_internet_d3452006", "Latency with FG? But I have good internet"),
                                               Neurotic::UiLiteral("ingame.menu-common.console_peasants_can_t_do_that_42447060", "Console peasants can't do that"),
                                               Neurotic::UiLiteral("ingame.menu-common.hope_you_don_t_have_a_good_eyesight_cb21d14b", "Hope you don't have a good eyesight"),
                                               Neurotic::UiLiteral("ingame.menu-common.such_an_aggressive_upscaling_a_bold_move_8797066c", "Such an aggressive upscaling? A bold move"),
                                               Neurotic::UiLiteral("ingame.menu-common.i_almost_don_t_feel_the_input_lag_2279a559", "I almost don't feel the input lag"),
                                               Neurotic::UiLiteral("ingame.menu-common.and_that_s_how_you_get_to_60_fps_3198087e", "And that's how you get to 60 FPS"),
                                               Neurotic::UiLiteral("ingame.menu-common.together_we_upscale_60821af3", "Together We Upscale"),
                                               Neurotic::UiLiteral("ingame.menu-common.for_upscalers_by_upscalers_36f30e1a", "For upscalers, by upscalers"),
                                               Neurotic::UiLiteral("ingame.menu-common.opti_sports_it_s_in_the_sampling_a4decc91", "Opti Sports, it's in the sampling"),
                                               Neurotic::UiLiteral("ingame.menu-common.render_in_your_world_upscale_in_ours_5789c56b", "Render in your world. Upscale in ours"),
                                               Neurotic::UiLiteral("ingame.menu-common.all_your_pixels_are_belong_to_us_07990e1d", "All your pixels are belong to us"),
                                               Neurotic::UiLiteral("ingame.menu-common.upscaling_for_the_masses_not_the_classes_86fb2d3a", "Upscaling for the masses, not the classes"),
                                               Neurotic::UiLiteral("ingame.menu-common.generating_discord_since_2023_5d5a3630", "Generating discord since 2023"),
                                               Neurotic::UiLiteral("ingame.menu-common.enabling_dlss_since_2023_7326d3f2", "Enabling DLSS since 2023"),
                                               Neurotic::UiLiteral("ingame.menu-common.redacted_never_looked_better_c3ff1cc2", "[REDACTED] never looked better"),
                                               Neurotic::UiLiteral("ingame.menu-common.free_and_always_free_5699a57e", "Free and always free"),
                                               Neurotic::UiLiteral("ingame.menu-common.getting_unshackled_from_green_chains_in_progress_26391735", "Getting unshackled from green chains in progress..."),
                                               Neurotic::UiLiteral("ingame.menu-common.who_s_nukem_anyway_503254e8", "Who's Nukem anyway?"),
                                               Neurotic::UiLiteral("ingame.menu-common.compiling_shaders_eta_05h_49m_fabaa2c5", "Compiling shaders... ETA: 05h:49m"),
                                               Neurotic::UiLiteral("ingame.menu-common.did_you_really_just_pay_70_eur_for_this_game_3a6fac20", "Did you really just pay 70 EUR for this game?!"),
                                               Neurotic::UiLiteral("ingame.menu-common.guess_who_forgot_about_a_nullptr_check_again_a09705e8", "Guess who forgot about a nullptr check again"),
                                               Neurotic::UiLiteral("ingame.menu-common.ai_can_t_outslop_this_d35825df", "AI can't outslop this"),
                                               Neurotic::UiLiteral("ingame.menu-common.guess_we_re_pre_alpha_build_demos_now_a48b17c2", "Guess we're pre-alpha build demos now"),
                                               Neurotic::UiLiteral("ingame.menu-common.new_app_on_the_block_th_62f6116e", "New app on the block - TH"),
                                               Neurotic::UiLiteral("ingame.menu-common.one_more_stutter_and_i_might_lose_it_92f9feff", "One more stutter and I might lose it"),
                                               Neurotic::UiLiteral("ingame.menu-common.mostly_stable_unlike_the_driver_14381648", "Mostly stable, unlike the driver"),
                                               "Vul... what? ~AMD",
                                               Neurotic::UiLiteral("ingame.menu-common.my_8_points_are_floating_155821dd", "My 8 points are floating"),
                                               Neurotic::UiLiteral("ingame.menu-common.no_floating_here_i_m_strictly_between_128_and_12_be7a13bd", "No floating here - I'm strictly between -128 and 127"),
                                               Neurotic::UiLiteral("ingame.menu-common.fake_it_til_you_bake_it_0d8ffae8", "Fake it til you bake it"),
                                               Neurotic::UiLiteral("ingame.menu-common.worst_case_just_turn_it_off_and_on_bf2d57ae", "Worst case just turn it off and on"),
                                               Neurotic::UiLiteral("ingame.menu-common.on_a_generative_damage_control_mode_at_geometry__ea674cbf", "*On a generative damage control mode at geometry level*"),
                                               Neurotic::UiLiteral("ingame.menu-common.deep_learning_slop_sampling_5_c47ea0bb", "Deep Learning Slop Sampling 5"),
                                               Neurotic::UiLiteral("ingame.menu-common.2d_ai_filters_now_powered_by_just_2x_5090s_ca02bacc", "2D AI filters, now powered by just 2x 5090s"),
                                               Neurotic::UiLiteral("ingame.menu-common.neural_slop_sampling_with_dlss5_6ba414eb", "Neural Slop Sampling with DLSS5"),
                                               Neurotic::UiLiteral("ingame.menu-common.dlss_5_the_way_it_s_meant_to_be_slopped_18b0b08f", "DLSS 5 - the way it's meant to be slopped"),
                                               Neurotic::UiLiteral("ingame.menu-common.just_when_i_think_i_m_out_they_scale_me_back_in_d5c0990e", "Just when I think I'm out, they scale me back in"),
                                               Neurotic::UiLiteral("ingame.menu-common.like_going_in_the_first_gear_on_the_highway_091a2c84", "Like going in the first gear on the highway"),
                                               Neurotic::UiLiteral("ingame.menu-common.nitec_s_bizarre_upscaling_50cec957", "Nitec's Bizarre Upscaling"),
                                               Neurotic::UiLiteral("ingame.menu-common.framegen_really_attracts_some_strange_clientelle_aa8d334b", "\"Framegen really attracts some strange clientelle\""),
                                               Neurotic::UiLiteral("ingame.menu-common.how_to_remove_those_corny_messages_e4dbf69f", "How to remove those corny messages?!"),
                                               Neurotic::UiLiteral("ingame.menu-common.your_funny_text_goes_here_fae3838d", "<Your funny text goes here>") };

static std::string updateNoticeTag;
static std::string updateNoticeUrl;
static float lastMenuScale = 0.0f;
static CustomOptional<uint32_t> comboPreset { 0 };
static int lastKey = 0;
static bool inputDlssNr = false;
static bool inputScreenshot = false;
static bool capturingKey = false;
static OptiInput::EscapeCloseGesture escapeClose;

template <typename T, size_t N> struct RingBuffer
{
    std::array<T, N> data {};
    size_t head { 0 };
    size_t count { N };
    double sum { 0.0 };

    RingBuffer() { data.fill(static_cast<T>(0)); }

    void Push(T v)
    {
        if (count == N)
        {
            sum -= data[head];
        }
        else
        {
            ++count;
        }
        data[head] = v;
        sum += v;
        head = (head + 1) % N;
    }

    size_t Size() const { return N; }

    T At(size_t i) const
    {
        size_t start = head;
        return data[(start + i) % N];
    }

    float Average() const { return static_cast<float>(sum / static_cast<double>(N)); }
};

const int plotWidth = 360;
static RingBuffer<float, plotWidth> gFrameTimes;
static RingBuffer<float, plotWidth> gUpscalerTimes;

static bool HasUpscalerGpuTiming(const State& state)
{
    if (state.api != Vulkan) return true;
    return !UpscalerTimeVk::UnavailableFor(true,
        state.currentFeature && state.currentFeature->IsWithDx12());
}


struct FsExistsCache
{
    std::wstring lastPath;
    bool cached { false };
    std::chrono::steady_clock::time_point nextRefresh { std::chrono::steady_clock::time_point::min() };
    std::chrono::milliseconds interval { 2000 };

    bool Get(const std::filesystem::path& path)
    {
        auto now = std::chrono::steady_clock::now();
        if (path != lastPath || now >= nextRefresh)
        {
            lastPath = path;
            cached = std::filesystem::exists(path);
            nextRefresh = now + interval;
        }
        return cached;
    }
};

static FsExistsCache nukemsExists;
static FsExistsCache enablerExists;

struct FlagDefinition
{
    std::string name;
    uint32_t mask;
    std::string description;
};

inline std::string StrFmt(const char* fmt, ...)
{
    Neurotic::LocalizedFormat localized(fmt);
    va_list args;
    va_start(args, fmt);
    int len = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    std::string out(len, '\0');
    va_start(args, fmt);
    std::vsnprintf(out.data(), len + 1, fmt, args);
    va_end(args);
    return out;
}

static Neurotic::KeyChord::ReleaseTracker uwpShortcuts;
static bool uwpShortcutsFocused=true;
static std::mutex uwpShortcutsMutex;

void MenuCommon::UpdateManualInput(HWND targetHwnd)
{
    OptiInput::BeginFrame(targetHwnd);

    const auto config = Config::Instance();
    static Neurotic::KeyChord::ReleaseTracker shortcuts;
    const auto focusGeneration=OptiInput::GetFocusGeneration();
    static auto lastFocusGeneration=focusGeneration;
    if(lastFocusGeneration!=focusGeneration)
    {
        shortcuts.Reset();
        lastFocusGeneration=focusGeneration;
    }
    std::array<int,256> released{};
    if (!OptiInput::IsFocused() || capturingKey)
    {
        shortcuts.Reset();
        std::lock_guard lock(uwpShortcutsMutex);
        uwpShortcuts.Reset();
    }
    else
    {
        using namespace Neurotic::KeyChord;
        auto modifierDown = [](int generic,int left,int right) {
            return OptiInput::IsKeyDown(generic)||OptiInput::IsKeyDown(left)||OptiInput::IsKeyDown(right)||
                   OptiInput::IsKeyReleased(generic)||OptiInput::IsKeyReleased(left)||OptiInput::IsKeyReleased(right);
        };
        const int modifiers=(modifierDown(VK_CONTROL,VK_LCONTROL,VK_RCONTROL)?Ctrl:0)|
                            (modifierDown(VK_SHIFT,VK_LSHIFT,VK_RSHIFT)?Shift:0)|
                            (modifierDown(VK_MENU,VK_LMENU,VK_RMENU)?Alt:0);
        for(int key=1;key<256;key++)
            if(auto chord=shortcuts.Observe(key,modifiers,OptiInput::IsKeyPressed(key),OptiInput::IsKeyReleased(key)))
                released[key]=*chord;
    }

    auto CheckShortcut = [&](int vk, bool& inputFlag, const char* logMessage)
    {
        if(OptiInput::GetFocusGeneration()!=focusGeneration||!OptiInput::IsFocused())
            return;
        if (inputFlag)
            return;

        if (!Neurotic::KeyChord::Valid(vk) || vk <= 0)
            return;

        if (Neurotic::KeyChord::Matches(vk,released[vk&255]))
        {
            lastKey = vk&255;
            // receivingWmInputs = false;
            inputFlag = true;
            LOG_DEBUG("{}", logMessage);
        }
    };

    const auto currentTick = GetTickCount64();
    const bool canAcceptInputs = lastInputTick + debounceThreshold < currentTick;

    if (OptiInput::HandleEscapeClose(escapeClose, config->EscapeClosesMenu.value_or_default(),
                                    _isVisible, OptiInput::IsFocused(), capturingKey,
                                    OptiInput::IsKeyPressed(VK_ESCAPE), OptiInput::IsKeyReleased(VK_ESCAPE)))
        inputCloseMenu = true;

    if (!capturingKey && canAcceptInputs)
    {
        CheckShortcut(config->ShortcutKey.value_or_default(), inputMenu, Neurotic::UiLiteral("ingame.menu-common.menu_key_pressed_will_be_switching_menu_01e6be20", "Menu key pressed, will be switching menu"));
        CheckShortcut(config->FpsShortcutKey.value_or_default(), inputFps, Neurotic::UiLiteral("ingame.menu-common.menu_key_pressed_will_be_switching_fps_81882d87", "Menu key pressed, will be switching FPS"));
        CheckShortcut(config->FGShortcutKey.value_or_default(), inputFG, Neurotic::UiLiteral("ingame.menu-common.menu_key_pressed_will_be_switching_fg_mode_12a728a9", "Menu key pressed, will be switching FG mode"));
        CheckShortcut(config->FpsCycleShortcutKey.value_or_default(), inputFpsCycle,
                      Neurotic::UiLiteral("ingame.menu-common.menu_key_pressed_will_be_switching_fps_mode_42ff3f66", "Menu key pressed, will be switching FPS mode"));
        CheckShortcut(config->DlssNrToggleKey.value_or_default(), inputDlssNr,
                      Neurotic::UiLiteral("ingame.menu-common.neural_rendering_key_pressed_will_be_toggling_th_0b51b990", "Neural Rendering key pressed, will be toggling the pass"));
        CheckShortcut(config->ScreenshotKey.value_or_default(), inputScreenshot,
                      Neurotic::UiLiteral("ingame.menu-common.screenshot_key_pressed_will_capture_the_selected_2d3d0f47", "Screenshot key pressed, will capture the selected same-frame comparisons"));
    }
    else if (capturingKey)
    {
        lastInputTick = currentTick;
    }

    lastKey = OptiInput::GetLastPressedKey();
}

// Retained helper signatures for existing callers; hover help has no presentation.
void MenuCommon::ShowTooltip(const char*) {}
void MenuCommon::ShowHelpMarker(const char*) {}

void MenuCommon::ShowResetButton(CustomOptional<bool, NoDefault>* initFlag, const char* buttonName)
{
    ImGui::SameLine();

    ImGui::BeginDisabled(!initFlag->has_value());

    if (ImGui::Button(buttonName))
    {
        initFlag->reset();
        ReInitUpscaler();
    }

    ImGui::EndDisabled();
}

inline void MenuCommon::ReInitUpscaler()
{
    if (!State::Instance().currentFeature)
        return;

    if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSSD)
        State::Instance().newBackend = Upscaler::DLSSD;
    else
        State::Instance().newBackend = currentBackend;

    MARK_ALL_BACKENDS_CHANGED();
}

void MenuCommon::SeparatorWithHelpMarker(const char* label, const char* tip)
{
    ImGui::SeparatorText(label);
    ShowHelpMarker(tip);
}

class Keybind
{
    std::string name;
    int id;
    bool waitingForKey = false;
    int pendingModifier = 0;

  public:
    std::string nameId;
    Keybind(const char* name, int id) : name(name), id(id),nameId(Neurotic::Localization::BoundLiteralId(name)) {}

    static std::string KeyNameFromVirtualKeyCode(int virtualKey)
    {
        if (virtualKey == UnboundKey)
            return Neurotic::UiMessage("ingame.keybind.unbound", "Unbound");
        if (!Neurotic::KeyChord::Valid(virtualKey))
            return Neurotic::UiMessage("ingame.menu-common.unsupported_shortcut_c8cf2e85", "Unsupported shortcut");
        if (virtualKey & Neurotic::KeyChord::ModifierMask)
            return Neurotic::KeyChord::Label(virtualKey);

        UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);

        // Keys like Home would display as Num 0 without this fix
        switch (virtualKey)
        {
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
            scanCode |= 0xE000;
            break;
        }

        LONG lParam = (scanCode & 0xFF) << 16;
        if (scanCode & 0xE000)
            lParam |= 1 << 24;

        wchar_t buf[64] = {};
        if (GetKeyNameTextW(lParam, buf, static_cast<int>(std::size(buf))) != 0)
            return wstring_to_string(buf);

        return Neurotic::UiMessage("ingame.menu-common.unknown_d80d0833", "Unknown");
    }

    template<class Option>
        requires (std::same_as<Option, CustomOptional<int>> || std::same_as<Option, NrOptional<int>>)
    void Render(Option& configKey)
    {
        ImGui::PushID(id);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        Neurotic::ScopedUiLiteral localizedName(nameId,name.c_str());
        ImGui::TextUnformatted(name.c_str());
        ImGui::TableSetColumnIndex(1);
        const int binding = configKey.value_or_default();
        if (binding > 0) pendingModifier = binding & Neurotic::KeyChord::ModifierMask;
        const int selected = Neurotic::HotkeyChord::ModifierIndex(pendingModifier | VK_F8);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##Modifier", selected >= 0 ? Neurotic::HotkeyChord::ModifierLabels[selected] : Neurotic::UiLiteral("ingame.menu-common.custom_d16cb1e0", "Custom")))
        {
            for (int i = 0; i < static_cast<int>(std::size(Neurotic::HotkeyChord::ModifierValues)); ++i)
                if (ImGui::Selectable(Neurotic::HotkeyChord::ModifierLabels[i], i == selected))
                {
                    pendingModifier = Neurotic::HotkeyChord::ModifierValues[i];
                    if (binding > 0) configKey = Neurotic::HotkeyChord::WithModifier(binding, pendingModifier);
                }
            ImGui::EndCombo();
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("+");
        ImGui::TableSetColumnIndex(3);
        const std::string bindingLabel = waitingForKey ? Neurotic::UiLiteral("ingame.menu-common.press_any_key_eaf7066e", "Press any key...") :
            KeyNameFromVirtualKeyCode(binding > 0 ? binding & 255 : binding);
        if (ImGui::Button(bindingLabel.c_str(), ImVec2(-FLT_MIN, 0.0f)))
        {
            waitingForKey = true;
            capturingKey = true;
            OptiInput::SetGameplayPolicy(false, false, false, true);
            lastKey = 0;
        }
        if (waitingForKey && lastKey != 0 && lastKey != VK_LBUTTON &&
            lastKey != VK_RBUTTON && lastKey != VK_MBUTTON)
        {
            if (lastKey == VK_ESCAPE)
            {
                waitingForKey = false;
                capturingKey = false;
            }
            else
            {
                if (lastKey == VK_BACK || Neurotic::KeyChord::IsOrdinary(lastKey))
                {
                    configKey = lastKey == VK_BACK ? UnboundKey :
                        Neurotic::HotkeyChord::WithKey(pendingModifier | VK_F8, lastKey);
                    waitingForKey = false;
                    capturingKey = false;
                }
            }
        }
        ImGui::TableSetColumnIndex(4);
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")))
        {
            configKey.reset();
            pendingModifier = 0;
            waitingForKey = false;
            capturingKey = false;
        }
        ImGui::PopID();
    }
};

Upscaler MenuCommon::GetBackendCode(const API api)
{
    if (auto feature = State::Instance().currentFeature)
        return feature->GetUpscalerType();

    Upscaler upscaler;

    if (api == DX11)
        upscaler = Config::Instance()->Dx11Upscaler.value_or_default();
    else if (api == DX12)
        upscaler = Config::Instance()->Dx12Upscaler.value_or_default();
    else
        upscaler = Config::Instance()->VulkanUpscaler.value_or_default();

    return upscaler;
}

void MenuCommon::GetCurrentBackendInfo(const API api, Upscaler& upscaler, std::string* name)
{
    upscaler = GetBackendCode(api);
    *name = UpscalerDisplayName(upscaler, api);
}

void MenuCommon::RenderUpscalerCombo(const API api, Upscaler currentUpscaler, const std::vector<Upscaler>& options)
{
    auto primaryGpu = IdentifyGpu::getPrimaryGpu();

    // Determine display name
    Upscaler targetBackend = State::Instance().newBackend;
    if (targetBackend == Upscaler::Reset)
        targetBackend = currentUpscaler;

    std::string selectedName = UpscalerDisplayName(targetBackend, api);

    if (ImGui::BeginCombo("##UpscalerCombo", selectedName.c_str()))
    {
        for (auto opt : options)
        {
            // Check if GPU is capable of a given backend
            if (opt == Upscaler::DLSS && !primaryGpu.dlssCapable)
                continue;

            // Not all Intel GPUs support native DX11 XeSS but don't think we have a good way to check exactly
            if (opt == Upscaler::XeSS && api == API::DX11 && primaryGpu.vendorId != VendorId::Intel)
                continue;

            bool isSelected = (currentUpscaler == opt);
            if (ImGui::Selectable(UpscalerDisplayName(opt, api).c_str(), isSelected))
            {
                State::Instance().newBackend = opt;
            }
        }
        ImGui::EndCombo();
    }
}

void MenuCommon::AddDx11Backends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::DX11, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR22, Upscaler::FSR31, Upscaler::XeSS_on12, Upscaler::FSR21_on12,
                          Upscaler::FSR22_on12, Upscaler::FFX_on12, Upscaler::DLSS, Upscaler::DLSS_on12 });
}

void MenuCommon::AddDx12Backends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::DX12, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR21, Upscaler::FSR22, Upscaler::FFX, Upscaler::DLSS });
}

void MenuCommon::AddVulkanBackends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::Vulkan, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR21, Upscaler::FSR22, Upscaler::FFX, Upscaler::FSR21_on12,
                          Upscaler::FFX_on12, Upscaler::DLSS });
}

template <class Option> void MenuCommon::AddResourceBarrier(const char* name, Option* value)
{
    const char* states[] = { Neurotic::UiLiteral("ingame.option.6ea56fae9eac", "AUTO"),
                             Neurotic::UiLiteral("ingame.option.aa2e225c4fd9", "COMMON"),
                             Neurotic::UiLiteral("ingame.option.eba85a7b96dc", "VERTEX_AND_CONSTANT_BUFFER"),
                             Neurotic::UiLiteral("ingame.option.bca4502c9e2d", "INDEX_BUFFER"),
                             Neurotic::UiLiteral("ingame.option.745cbdc4ba86", "RENDER_TARGET"),
                             Neurotic::UiLiteral("ingame.option.94df715f6bc8", "UNORDERED_ACCESS"),
                             Neurotic::UiLiteral("ingame.option.e7055cfcb978", "DEPTH_WRITE"),
                             Neurotic::UiLiteral("ingame.option.94d01e379459", "DEPTH_READ"),
                             Neurotic::UiLiteral("ingame.option.a10273730565", "NON_PIXEL_SHADER_RESOURCE"),
                             Neurotic::UiLiteral("ingame.option.2736b7f4ea6c", "PIXEL_SHADER_RESOURCE"),
                             Neurotic::UiLiteral("ingame.option.d7e2090c5831", "STREAM_OUT"),
                             Neurotic::UiLiteral("ingame.option.a021d9100067", "INDIRECT_ARGUMENT"),
                             Neurotic::UiLiteral("ingame.option.933540054c23", "COPY_DEST"),
                             Neurotic::UiLiteral("ingame.option.f7089d320b4a", "COPY_SOURCE"),
                             Neurotic::UiLiteral("ingame.option.f174a60262a1", "RESOLVE_DEST"),
                             Neurotic::UiLiteral("ingame.option.1468be857474", "RESOLVE_SOURCE"),
                             Neurotic::UiLiteral("ingame.option.b0f8dcbe88a5", "RAYTRACING_ACCELERATION_STRUCTURE"),
                             Neurotic::UiLiteral("ingame.option.16c59107a2ef", "SHADING_RATE_SOURCE"),
                             Neurotic::UiLiteral("ingame.option.4e4898413304", "GENERIC_READ"),
                             Neurotic::UiLiteral("ingame.option.b65dd814dfaf", "ALL_SHADER_RESOURCE"),
                             Neurotic::UiLiteral("ingame.option.83aaae26be52", "PRESENT"),
                             Neurotic::UiLiteral("ingame.option.f0d1452334c3", "PREDICATION"),
                             Neurotic::UiLiteral("ingame.option.ab111b0f5b3c", "VIDEO_DECODE_READ"),
                             Neurotic::UiLiteral("ingame.option.5efdac99d504", "VIDEO_DECODE_WRITE"),
                             Neurotic::UiLiteral("ingame.option.241ca2f6fd5a", "VIDEO_PROCESS_READ"),
                             Neurotic::UiLiteral("ingame.option.f084dd05df2a", "VIDEO_PROCESS_WRITE"),
                             Neurotic::UiLiteral("ingame.option.2200a8da4b76", "VIDEO_ENCODE_READ"),
                             Neurotic::UiLiteral("ingame.option.08189e7db526", "VIDEO_ENCODE_WRITE") };
    const int values[] = { -1,  0,   1,     2,      4,      8,      16,      32,       64,   128,
                           256, 512, 1024,  2048,   4096,   8192,   4194304, 16777216, 2755, 192,
                           0,   D3D12_RESOURCE_STATE_PREDICATION, 65536, 131072, 262144, 524288, 2097152, 8388608 };

    int selected = value->value_or(-1);

    const char* selectedName = "";

    for (int n = 0; n < 28; n++)
    {
        if (values[n] == selected)
        {
            selectedName = states[n];
            break;
        }
    }

    if (ImGui::BeginCombo(name, selectedName))
    {
        if (ImGui::Selectable(states[0], !value->has_value()))
            value->reset();

        for (int n = 1; n < 28; n++)
        {
            if (ImGui::Selectable(states[n], selected == values[n]))
                *value = values[n];
        }

        ImGui::EndCombo();
    }
}

static uint32_t GetPresetIndex(IFeature* feature, bool dlssd = false)
{
    auto ratio = (float) feature->TargetWidth() / (float) feature->RenderWidth();

    if (!dlssd)
    {
        if (State::Instance().dlssPresetsOverridenByOpti)
        {
            LOG_DEBUG("DLSS Presets overridden by Opti, using Opti preset indices with ratio: {}", ratio);

            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetUltraPerformance.value_or(
                    Config::Instance()->RenderPresetForAll.value_or(State::Instance().dlssRenderPresetUltraPerformance));
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetPerformance.value_or(
                    Config::Instance()->RenderPresetForAll.value_or(State::Instance().dlssRenderPresetPerformance));
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetBalanced.value_or(
                    Config::Instance()->RenderPresetForAll.value_or(State::Instance().dlssRenderPresetBalanced));
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetQuality.value_or(
                    Config::Instance()->RenderPresetForAll.value_or(State::Instance().dlssRenderPresetQuality));
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetUltraQuality.value_or(
                    Config::Instance()->RenderPresetForAll.value_or(State::Instance().dlssRenderPresetUltraQuality));
            }
            else
            {
                return Config::Instance()->RenderPresetDLAA.value_or(
                    Config::Instance()->RenderPresetForAll.value_or(State::Instance().dlssRenderPresetDLAA));
            }
        }
        else if (State::Instance().dlssPresetsOverriddenExternally)
        {
            LOG_DEBUG("DLSS Presets overridden externally, using external preset index: {}",
                      State::Instance().dlssRenderPresetExternal);

            return State::Instance().dlssRenderPresetExternal;
        }
        else
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetUltraPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetBalanced;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetQuality;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetUltraQuality;
            }
            else
            {
                return State::Instance().dlssRenderPresetDLAA;
            }
        }
    }
    else
    {
        if (State::Instance().dlssdPresetsOverridenByOpti)
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetUltraPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetBalanced.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetQuality.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetUltraQuality.value_or_default());
            }
            else
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetDLAA.value_or_default());
            }
        }
        else if (State::Instance().dlssdPresetsOverriddenExternally)
        {
            return State::Instance().dlssdRenderPresetExternal;
        }
        else
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetUltraPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetBalanced;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetQuality;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetUltraQuality;
            }
            else
            {
                return State::Instance().dlssdRenderPresetDLAA;
            }
        }
    }

    return 0;
}

// TODO: disable presets based on the detected DLSS version
template <HasDefaultValue B> void MenuCommon::AddDLSSRenderPreset(const char* name, CustomOptional<uint32_t, B>* value)
{
    // clang-format off
    static const std::vector<MenuOption<uint32_t>> presets = {
        { NVSDK_NGX_DLSS_Hint_Render_Preset_Default, Neurotic::UiLiteral("ingame.menu-common.nvidia_default_4d29d728", "NVIDIA DEFAULT"),
            Neurotic::UiLiteral("ingame.menu-common.use_the_nvidia_game_default_preset_afd455ba", "Use the NVIDIA/game default preset") },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_A, "PRESET A",
            Neurotic::UiLiteral("ingame.menu-common.intended_for_performance_balanced_quality_modes__3af12e57", "Intended for Performance/Balanced/Quality modes.\nAn older variant best suited to combat ghosting...\nRemoved on recent versions!") },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_B, "PRESET B",
            Neurotic::UiLiteral("ingame.menu-common.intended_for_ultra_performance_mode_similar_to_p_015a91c5", "Intended for Ultra Performance mode.\nSimilar to Preset A...\nRemoved on recent versions!") },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_C, "PRESET C",
            Neurotic::UiLiteral("ingame.menu-common.intended_for_performance_balanced_quality_modes__411aa672", "Intended for Performance/Balanced/Quality modes.\nGenerally favors current frame information...\nRemoved on recent versions!") },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_D, "PRESET D",
            Neurotic::UiLiteral("ingame.menu-common.default_preset_for_performance_balanced_quality__411283de", "Default preset for Performance/Balanced/Quality modes;\ngenerally favors image stability.\nRemoved on recent versions!") },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_E, "PRESET E",
            Neurotic::UiLiteral("ingame.menu-common.dlss_3_7_a_better_d_preset_removed_on_recent_ver_efc7897f", "DLSS 3.7+, a better D preset\nRemoved on recent versions!") },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_F, "PRESET F",
            Neurotic::UiLiteral("ingame.menu-common.default_preset_for_ultra_performance_and_dlaa_mo_b94aa4bc", "Default preset for Ultra Performance and DLAA modes\nRemoved on recent versions!") },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_G, "PRESET G",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_H_Reserved, "PRESET H",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_I_Reserved, "PRESET I",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_J, "PRESET J",
            Neurotic::UiLiteral("ingame.menu-common.similar_to_preset_k_preset_j_might_exhibit_sligh_26e8a97a", "Similar to preset K. Preset J might exhibit slightly\nless ghosting...\n1st Gen Transformer") },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_K, "PRESET K",
            Neurotic::UiLiteral("ingame.menu-common.default_preset_for_dlaa_balanced_quality_modes_1_eb686a08", "Default preset for DLAA/Balanced/Quality modes...\n1st Gen Transformer") },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_L, "PRESET L",
            Neurotic::UiLiteral("ingame.menu-common.default_for_ultra_perf_mode_2nd_gen_transformers_e9e42cc5", "Default for Ultra Perf mode\n2nd Gen Transformers") },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_M, "PRESET M",
            Neurotic::UiLiteral("ingame.menu-common.default_for_perf_mode_2nd_gen_transformer_ff87dfaa", "Default for Perf mode\n2nd Gen Transformer") },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_N, "PRESET N",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_O, "PRESET O",
            "Unused" },
        { NV_PRESET_LATEST, "Latest",
            Neurotic::UiLiteral("ingame.menu-common.latest_supported_by_the_dll_8649e946", "Latest supported by the dll") }
    };
    // clang-format on

    if constexpr (B == SoftDefault)
    {
        std::string preview = Neurotic::UiMessage("ingame.menu-common.use_global_7994c5e6", "USE GLOBAL");

        if (value->has_value())
        {
            preview = Neurotic::UiLiteral("ingame.menu-common.unknown_d80d0833", "Unknown");
            for (const auto& opt : presets)
            {
                if (opt.value == value->value())
                {
                    preview = opt.labelId.empty()?opt.label:Neurotic::UiText(opt.labelId);
                    break;
                }
            }
        }

        Neurotic::FitPresetComboWidth(name,preview.c_str());
        if (ImGui::BeginCombo(name, preview.c_str()))
        {
            const bool useGlobalSelected = !value->has_value();
            if (ImGui::Selectable(Neurotic::UiLiteral("ingame.menu-common.use_global_7994c5e6", "USE GLOBAL"), useGlobalSelected))
                *value = std::optional<uint32_t> {};

            ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.inherit_the_global_dlss_preset_override_a7fa8571", "Inherit the global DLSS preset override"));

            for (const auto& opt : presets)
            {
                Neurotic::ScopedUiLiteral optionLabel(opt.labelId,opt.label.c_str()),optionTooltip(opt.tooltipId,opt.tooltip.c_str());
            if (opt.hidden)
                    continue;

                if (opt.disabled)
                    ImGui::BeginDisabled();

                const bool isSelected = value->has_value() && value->value() == opt.value;
                if (ImGui::Selectable(opt.label.c_str(), isSelected))
                    *value = opt.value;

                if (opt.disabled && !opt.tooltip.empty())
                    ImGui::TextWrapped("%s",Neurotic::Translate(opt.tooltip.c_str()).c_str());

                if (opt.disabled)
                    ImGui::EndDisabled();
            }

            ImGui::EndCombo();
        }

        return;
    }

    const auto selected=std::find_if(presets.begin(),presets.end(),[&](const auto& option){return option.value==value->value_or_default();});
    if(selected!=presets.end())Neurotic::FitPresetComboWidth(name,selected->labelId.empty()?selected->label.c_str():Neurotic::UiText(selected->labelId).c_str());
    PopulateCombo(name, *value, presets);
}

template <HasDefaultValue B> void MenuCommon::AddDLSSDRenderPreset(const char* name, CustomOptional<uint32_t, B>* value)
{
    // We don't have DLSSD definitions so using raw values
    static const std::vector<MenuOption<uint32_t>> presets = {
        { 0, "DEFAULT", Neurotic::UiLiteral("ingame.menu-common.whatever_the_game_uses_c195dab0", "Whatever the game uses") },
        { 1, "PRESET A", Neurotic::UiLiteral("ingame.menu-common.preset_a_removed_on_recent_versions_d097bc9e", "Preset A\nRemoved on recent versions!") },
        { 2, "PRESET B", Neurotic::UiLiteral("ingame.menu-common.preset_b_removed_on_recent_versions_459caace", "Preset B\nRemoved on recent versions!") },
        { 3, "PRESET C", Neurotic::UiLiteral("ingame.menu-common.preset_c_removed_on_recent_versions_aa36fa00", "Preset C\nRemoved on recent versions!") },
        { 4, "PRESET D", Neurotic::UiLiteral("ingame.menu-common.default_model_transformer_123a9fa3", "Default model, Transformer") },
        { 5, "PRESET E", Neurotic::UiLiteral("ingame.menu-common.latest_transformer_model_must_use_if_dof_guide_i_6481dcc7", "Latest Transformer model\nMust use if DoF guide is needed") },
        { 6, "PRESET F", Neurotic::UiLiteral("ingame.menu-common.latest_transformer_model_must_use_if_dof_guide_i_6481dcc7", "Latest Transformer model\nMust use if DoF guide is needed") },
        { NV_PRESET_LATEST, "Latest", Neurotic::UiLiteral("ingame.menu-common.latest_supported_by_the_dll_8649e946", "Latest supported by the dll") }
    };

    if constexpr (B == SoftDefault || B == WithDefault)
    {
        const char* inherit = B == SoftDefault ? Neurotic::UiLiteral("ingame.menu-common.use_global_700ecb74", "Use Global") : Neurotic::UiLiteral("ingame.menu-common.use_game_22d102bf", "Use Game");
        const char* preview = inherit;
        if (value->has_value()) {
            preview = Neurotic::UiLiteral("ingame.menu-common.unavailable_saved_preset_398e605d", "Unavailable saved preset");
            for (const auto& option : presets) if (option.value == value->value()) preview = option.label.c_str();
        }
        Neurotic::FitPresetComboWidth(name,preview);
        if (ImGui::BeginCombo(name, preview)) {
            if (ImGui::Selectable(inherit, !value->has_value())) *value = std::optional<uint32_t>{};
            for (const auto& option : presets) {
                Neurotic::ScopedUiLiteral optionLabel(option.labelId,option.label.c_str());
                if (ImGui::Selectable(option.label.c_str(), value->has_value() && value->value() == option.value)) *value = option.value;
            }
            ImGui::EndCombo();
        }
    }
    else {
        const auto selected=std::find_if(presets.begin(),presets.end(),[&](const auto& option){return option.value==value->value_or_default();});
        if(selected!=presets.end())Neurotic::FitPresetComboWidth(name,selected->labelId.empty()?selected->label.c_str():Neurotic::UiText(selected->labelId).c_str());
        PopulateCombo(name, *value, presets);
    }
}

template <typename TStorage, typename T>
void MenuCommon::PopulateCombo(const char* name, TStorage& currentValue,
                               const std::vector<MenuOption<T>>& options)
{
    if (options.empty())
        return;

    // Assumes that different types mean that TStorage is std::optional
    T currentVal;
    if constexpr (std::is_same_v<TStorage, T>)
        currentVal = currentValue;
    else
        currentVal = currentValue.value_or(options[0].value);

    // Find the label for the currently selected item
    std::string preview = Neurotic::UiMessage("ingame.menu-common.unknown_d80d0833", "Unknown");
    for (const auto& opt : options)
    {
        if (opt.value == currentVal)
        {
            preview = opt.labelId.empty()?opt.label:Neurotic::UiText(opt.labelId);
            break;
        }
    }

    if (ImGui::BeginCombo(name, preview.c_str()))
    {
        for (const auto& opt : options)
        {
            Neurotic::ScopedUiLiteral optionLabel(opt.labelId,opt.label.c_str()),optionTooltip(opt.tooltipId,opt.tooltip.c_str());
            if (opt.hidden)
                continue;

            if (opt.disabled)
                ImGui::BeginDisabled();

            bool isSelected = (currentVal == opt.value);
            if (ImGui::Selectable(opt.label.c_str(), isSelected))
                currentValue = opt.value;

            // Keep refusal reasons visible for disabled choices without hover help.
            if (opt.disabled && !opt.tooltip.empty())
                ImGui::TextWrapped("%s",Neurotic::Translate(opt.tooltip.c_str()).c_str());

            if (opt.disabled)
                ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
}

static ImVec4 toneMapColor(const ImVec4& color)
{
    if (State::Instance().isHdrActive ||
        (!Config::Instance()->OverlayMenu.value_or_default() && State::Instance().currentFeature != nullptr &&
         State::Instance().currentFeature->IsHdr()))
    {
        // Controls how strongly HDR/UI colors are pushed into the tone mapper before compression.
        // Higher values make colors brighter before mapping; lower values make the result dimmer.
        constexpr float exposure = 1.0f;

        // Blends between original color and fully tone-mapped color.
        // 0.0 = no tone mapping, 1.0 = full Reinhard compression.
        constexpr float strength = 1.0f;

        float peak = std::max(color.x, std::max(color.y, color.z));

        if (peak <= 0.0f)
            return color;

        float exposedPeak = peak * exposure;
        float mappedPeak = exposedPeak / (1.0f + exposedPeak);

        float reinhardScale = mappedPeak / peak;
        float scale = 1.0f + (reinhardScale - 1.0f) * strength;

        return ImVec4(color.x * scale, color.y * scale, color.z * scale, color.w);
    }

    return color;
}

static void MenuHdrCheck(ImGuiIO io)
{
    // If game is using HDR, apply tone mapping to the ImGui style
    if (State::Instance().isHdrActive ||
        (!Config::Instance()->OverlayMenu.value_or_default() && State::Instance().currentFeature != nullptr &&
         State::Instance().currentFeature->IsHdr()))
    {
        if (!_hdrTonemapApplied)
        {
            ImGuiStyle& style = ImGui::GetStyle();

            CopyMemory(SdrColors, style.Colors, sizeof(style.Colors));

            // Apply tone mapping to the ImGui style
            for (int i = 0; i < ImGuiCol_COUNT; ++i)
            {
                ImVec4 color = style.Colors[i];
                style.Colors[i] = toneMapColor(color);
            }

            _hdrTonemapApplied = true;
        }
    }
    else
    {
        if (_hdrTonemapApplied)
        {
            ImGuiStyle& style = ImGui::GetStyle();
            CopyMemory(style.Colors, SdrColors, sizeof(style.Colors));
            _hdrTonemapApplied = false;
        }
    }
}

static float MenuResolutionScale(ImGuiIO io)
{
    if (Config::Instance()->MenuScale.has_value())
        return Config::Instance()->MenuScale.value();

    // Calculate menu scale according to display resolution
    float y = State::Instance().screenHeight;

    if (io.DisplaySize.y != 0)
        y = (float) io.DisplaySize.y;

    // 1000p is minimum for 1.0 menu ratio
    float result = (float) ((int) (y / 108.0f)) / 10.0f;

    result = std::round(result * 10.0f) / 10.0f;

    if (result < 0.5f)
        result = 0.5f;

    if (result > 2.0f)
        result = 2.0f;

    return (std::max)(0.5f,result*0.75f);
}

inline static std::string GetSourceString(UINT source)
{
    switch (source)
    {
    case 1:
        return "RTV";
    case 2:
        return "SRV";
    case 4:
        return "UAV";
    case 8:
        return "OM";
    case 16:
        return "Ups";
    case 32:
        return "SCR";
    case 64:
        return "SGR";
    default:
        return std::format("{}", source);
    }
}

inline static std::string GetDispatchString(UINT source)
{
    switch (source)
    {
    case 512:
        return "DI";
    case 1024:
        return "DII";
    case 256:
        return "Disp";
    default:
        return std::format("{}", source);
    }
}

static void ApplyThemeStyle(std::optional<bool> lightOverride = std::nullopt)
{
    Neurotic::Sleek::ApplyTheme(Config::Instance(), lightOverride.value_or(Config::Instance()->LightTheme.value_or_default()));
    _hdrTonemapApplied = false;
    MenuHdrCheck(ImGui::GetIO());
}

static double lastTime = 0.0;
static double lastFrameTime = 0.0;
static UINT64 uwpTargetFrame = 0;

void MenuCommon::Present()
{
    _frameCount++;

    auto now = Util::MillisecondsNow();

    if (lastTime > 0.0)
        lastFrameTime = now - lastTime;

    lastTime = now;

    if (_handle != nullptr)
        UpdateManualInput(_handle);
}

struct VersionCheckStatus
{
    bool completed = false;
    bool updateAvailable = false;
    std::string latestTag;
    std::string latestUrl;
    std::string error;
};

struct MenuCommon::RenderMenuContext
{
    State& state;
    decltype(Config::Instance()) config;
    ImGuiIO& io;
    IFeature* currentFeature = nullptr;

    double now = 0.0;
    double frameTime = 0.0;
    double frameRate = 0.0;
    float menuResScale = 1.0f;
    float fpsScale = 1.0f;
    float averageFrameTime = 0.0f;
    float averageUpscalerFT = 0.0f;

    int neuralPage = 0;
    int childPage = 0;
    int requestedNeuralPage = -1;
    bool frameTimesCalculated = false;
    bool newFrame = false;

    VersionCheckStatus versionStatus;
    std::string currentVersionText;

    // Cached when the menu is visible and shared by RenderMainMenuWindow section helpers.
    std::unique_ptr<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>> primaryGpu;
};

static std::string splashMessage;

void MenuCommon::UpdateRenderTiming(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& now = ctx.now;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;

    if (config->OverlayMenu.value_or_default())
    {
        _frameCount++;

        // FPS & frame time calculation
        if (lastTime > 0.0)
        {
            frameTime = now - lastTime;
            frameRate = 1000.0 / frameTime;
        }

        lastTime = now;

        if (_handle != nullptr)
            UpdateManualInput(_handle);
    }
    else
    {
        if (state.activeFgInput == FGInput::NoFG || state.activeFgOutput == FGOutput::NoFG)
            MenuCommon::Present();

        frameTime = lastFrameTime;
        frameRate = 1000.0 / frameTime;
    }

    state.frameTimes.pop_front();
    state.frameTimes.push_back(frameTime);
}

void MenuCommon::UpdateMenuInputMode(RenderMenuContext& ctx)
{
    auto& io = ctx.io;
    const bool allowKeyboard = ctx.config->AllowGameKeyboard.value_or_default() && !capturingKey;
    const bool allowController = ctx.config->AllowGameController.value_or_default() && !capturingKey;
    OptiInput::SetGameplayPolicy(ctx.config->AllowGameMouse.value_or_default(),
        ctx.config->AllowGameKeyboard.value_or_default(), ctx.config->AllowGameController.value_or_default(),
        capturingKey);

    // Moved here to prevent gamepad key replay
    if (_isVisible)
    {
        if (hasGamepad && !allowController)
            io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        else
            io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange |
                            ImGuiConfigFlags_NoKeyboard | ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
        io.ConfigFlags |= allowKeyboard ? ImGuiConfigFlags_NoKeyboard : ImGuiConfigFlags_NavEnableKeyboard;
        if (ctx.config->AllowGameMouse.value_or_default() && !capturingKey)
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        if (!allowController) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }
    else
    {
        capturingKey = false;
        hasGamepad = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;
    }
}

void MenuCommon::HandleMenuShortcuts(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;

    // Handle Inputs
    {
        if (inputFG)
        {
            inputFG = false;

            if (state.activeFgInput != FGInput::NoFG && state.activeFgOutput != FGOutput::NoFG &&
                (state.currentFGSwapchain != nullptr || state.activeFgInput == FGInput::NvngxFG))
            {
                config->FGEnabled = !config->FGEnabled.value_or_default();
                LOG_DEBUG("FG toggle key pressed, setting FGEnabled to {}", config->FGEnabled.value_or_default());

                if (config->FGEnabled.value_or_default())
                    state.fgChanged = true;
            }
        }

        if (inputFps)
        {
            inputFps = false;
            config->ShowFps = !config->ShowFps.value_or_default();
        }

        if (inputScreenshot)
        {
            inputScreenshot = false;
            DlssNr::RequestComparisonScreenshot();
        }

        if (inputDlssNr)
        {
            inputDlssNr = false;
            const bool enabled = !config->GetDlssNrRuntimeSnapshot().enabled;
            config->SetDlssNrEnabled(enabled);
            LOG_DEBUG("Neural Rendering toggle key pressed, setting DlssNrEnabled to {}",
                      enabled);

            ImGuiToast toast { ImGuiToastType::Info, 2000 };
            toast.setTitle(Neurotic::UiLiteral("ingame.menu-common.dlss_neural_rendering_d8314eec", "DLSS Neural Rendering"));
            toast.setContent(enabled ? Neurotic::UiLiteral("ingame.menu-common.on_d2f9df8a", "On") : Neurotic::UiLiteral("ingame.objectruleeditor.off_dc516be5", "Off"));
            ImGui::InsertNotification(toast);
        }

        if (inputFpsCycle && config->ShowFps.value_or_default())
            config->FpsOverlayType = (FpsOverlay) ((config->FpsOverlayType.value_or_default() + 1) % FpsOverlay_COUNT);

        if (inputCloseMenu)
        {
            inputCloseMenu = false;
            inputMenu = false; // An Escape menu binding must not reopen on the same release.
            HideMenu();
            UpdateMenuInputMode(ctx);
        }

        if (inputMenu)
        {
            inputMenu = false;
            SetVisibility(!_isVisible);

            LOG_DEBUG("Menu key pressed, {0}", _isVisible ? "opening ImGui" : "closing ImGui");

            if (_isVisible)
            {
                io.ClearEventsQueue();
                io.ClearInputKeys();
                io.ClearInputMouse();

                OptiInput::ResetMenuInputTransientState();

                ApplyThemeStyle();

                refreshRate = Util::GetActiveRefreshRate(_handle);

                auto optiPath = std::filesystem::path(Config::Instance()->MainDllPath.value());
                state.artursFgFileAvailable = enablerExists.Get(optiPath / L"dlss-enabler-headless.dll");
                state.nukemsFgFileAvailable = nukemsExists.Get(optiPath / L"dlssg_to_fsr3_amd_is_better.dll");

                if (State::Instance().currentFeature != nullptr)
                {
                    if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSSD)
                        comboPreset = static_cast<const std::optional<uint32_t>&>(config->DLSSDRenderPresetForAll);
                    else if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSS)
                        comboPreset = config->RenderPresetForAll.value_or_default();
                }
            }
            else
            {
                ImGui::CloseCurrentPopup();

                _showMipmapCalcWindow = false;
                _showHudlessWindow = false;
            }

            io.MouseDrawCursor = _isVisible;
            io.WantCaptureKeyboard = _isVisible;
            io.WantCaptureMouse = _isVisible;
        }

        inputFpsCycle = false;
    }
}

void MenuCommon::UpdateVersionAndStartupNotifications(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& now = ctx.now;
    auto& versionStatus = ctx.versionStatus;

    constexpr double splashTime = 7000.0;
    constexpr int updateNoticeTime = 10000;

    // Version check state is copied while locked, then consumed by the UI render pass.
    {
        std::scoped_lock lock(state.versionCheckMutex);
        versionStatus.completed = state.versionCheckCompleted;
        versionStatus.updateAvailable = state.updateAvailable;
        versionStatus.latestTag = state.latestVersionTag;
        versionStatus.latestUrl = state.latestVersionUrl;
        versionStatus.error = state.versionCheckError;
    }

    ctx.currentVersionText = VersionCheck::CurrentVersionString();

    if (versionStatus.completed && versionStatus.updateAvailable && !versionStatus.latestTag.empty())
    {
        if (updateNoticeTag != versionStatus.latestTag)
        {
            updateNoticeTag = versionStatus.latestTag;
            updateNoticeUrl = versionStatus.latestUrl;
            const auto notice = [&]()
            {
                ImGuiToast updateNotification { ImGuiToastType::Error, updateNoticeTime };
                updateNotification.setTitle(Neurotic::UiLiteral("ingame.menu-common.neurotic_update_available_8cf4441c", "NeuRotic Update available"));
                updateNotification.setContent(
                    Neurotic::UiLiteral("ingame.menu-common.press_s_for_more_info_0c66f809", "Press %s for more info"),
                    Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str());
                ImGui::InsertNotification(updateNotification);
                return true;
            };
            static auto res = notice();
        }
    }

    // One-shot startup warning notifications.
    if (!state.postDone)
    {
        if (state.postCodes & PostCode::SlPluginsAlreadyInMemory)
        {
            auto filename = Util::DllPath().filename().string();
            to_lower_in_place(filename);

            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            notification.setTitle(Neurotic::UiLiteral("ingame.menu-common.late_streamline_hook_detected_1a49595a", "Late Streamline hook detected"));
            notification.setContent(
                Neurotic::UiLiteral("ingame.menu-common.consider_renaming_optiscaler_from_s_to_other_sup_631b615a", "Consider renaming OptiScaler from %s to other supported name.\nYou may experience issues otherwise."),
                filename.c_str());
            ImGui::InsertNotification(notification);
        }

        if (state.postCodes & PostCode::TryingFsr4Fp8OnUnsupported)
        {
            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            notification.setTitle(Neurotic::UiLiteral("ingame.menu-common.silly_goose_detected_4698dcbd", "Silly goose detected"));
            notification.setContent(Neurotic::UiLiteral("ingame.menu-common.fsr_4_fp8_only_works_on_amd_243f2928", "FSR 4 FP8 only works on AMD"));
            ImGui::InsertNotification(notification);
        }

        state.postDone = true;
    }

    // Initialize splash timing and select the splash text once per process.
    if (splashLimit < 1.0f)
    {
        splashStart = now + 100.0;
        splashLimit = splashStart + splashTime;

        std::srand(static_cast<unsigned>(std::time(nullptr)));
        splashMessage = splashText[std::rand() % splashText.size()];
    }
}

void MenuCommon::BeginMenuFrameIfNeeded(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& now = ctx.now;
    auto& newFrame = ctx.newFrame;

    // New frame check
    // The lamp is drawn while the menu is closed, which is the whole point of it. Tied to its own
    // setting and nothing else: an overlay that appears because a scan is running, rather than
    // because someone asked for it, is an overlay nobody asked for.
    const bool scanIndicator = config->DlssNrScanMeter.value_or_default() &&
                               (State::Instance().api==API::Vulkan || DlssNr::ExposureScan::Where() != DlssNr::ExposureScan::Verdict::Off);

    if ((!config->DisableSplash.value_or_default() && now > splashStart && now < splashLimit) ||
        config->ShowFps.value_or_default() || _isVisible || ImGui::notifications.size() > 0 || scanIndicator ||
        (config->CharacterInspectorEnabled.value_or_default() && Neurotic::Semantic::Character::CharacterWorkerRequested()) ||
        (config->DlssNrCompare.value_or_default() != 0 && config->DlssNrCompareTags.value_or_default()))
    {
        if (!_isUWP)
        {
            OptiInput::PollMenuPlatform(ImGui_ImplWin32_NewFrame);
        }
        else
        {
            ImVec2 displaySize { state.screenWidth, state.screenHeight };
            ImGui_ImplUwp_NewFrame(displaySize);
        }

        OptiInput::FeedImGui(_isVisible);

        MenuHdrCheck(io);
        ImGui::NewFrame();

        newFrame = true;
    }
}

void MenuCommon::RenderSplashWindow(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& now = ctx.now;

    constexpr double fadeTime = 1000.0;

    // Splash screen
    if (!config->DisableSplash.value_or_default())
    {
        if (now > splashStart && now < splashLimit)
        {

            ImGui::SetNextWindowSize({ 0.0f, 0.0f });
            ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default());
            ImGui::SetNextWindowPos(splashPosition, ImGuiCond_Always);

            float windowAlpha = 1.0f;
            if (auto diff = now - splashStart; diff < fadeTime)
                windowAlpha = static_cast<float>(diff / fadeTime);
            else if (auto diff = splashLimit - now; diff < fadeTime)
                windowAlpha = static_cast<float>(diff / fadeTime);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, windowAlpha);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));

            if (!config->OverlaysUseTheme.value_or_default())
            {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
            }

            if (ImGui::Begin("Splash", nullptr,
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav))
            {
                float splashScale = 1.0f;
                float baseScaleHeight = 720.0f;

                if (io.DisplaySize.y > baseScaleHeight)
                    splashScale = io.DisplaySize.y / baseScaleHeight;

                if (config->UseHQFont.value_or_default())
                    ImGui::PushFontSize(std::round(splashScale * fontSize));
                else
                    ImGui::SetWindowFontScale(splashScale);

                ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.optiscaler_s_for_menu_b648615f", "OptiScaler - %s for menu"),Neurotic::Translate(Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str()).c_str());
                ImGui::TextColored(toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 0.7f)), splashMessage.c_str());

                splashSize = ImGui::GetWindowSize();

                if (config->UseHQFont.value_or_default())
                    ImGui::PopFontSize();

                ImGui::End();

                splashPosition.x = 0.0f; // io.DisplaySize.x - splashWinSize.x;
                splashPosition.y = io.DisplaySize.y - splashSize.y;
            }

            if (!config->OverlaysUseTheme.value_or_default())
                ImGui::PopStyleColor(4);
            else
                ImGui::PopStyleColor(2);

            ImGui::PopStyleVar(2);
        }
    }
}

void MenuCommon::RenderNotifications(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;

    // Notifications
    bool tonemapRequired = State::Instance().isHdrActive ||
                           (!Config::Instance()->OverlayMenu.value_or_default() &&
                            State::Instance().currentFeature != nullptr && State::Instance().currentFeature->IsHdr());

    float screenHeight = State::Instance().screenHeight;
    if (io.DisplaySize.y != 0)
        screenHeight = io.DisplaySize.y;

    // Map resolution height to scale, 0.5 for 480p, 2.0 for 1440p
    constexpr float slope = (2.0f - 0.5f) / (1440.f - 480.f);
    float notificationScale = 0.5f + slope * (screenHeight - 480.f);
    notificationScale = std::clamp(notificationScale, 0.5f, 2.0f);

    if (config->UseHQFont.value_or_default())
        ImGui::PushFontSize(std::round(notificationScale * fontSize));

    // No fallback font, SetWindowFontScale needs to be called after Begin()

    ImGui::RenderNotifications(ImGuiToastPos::TopCenter, notificationScale, tonemapRequired);

    if (config->UseHQFont.value_or_default())
        ImGui::PopFontSize();
}

void MenuCommon::UpdateFrameTimeAverages(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& frameTimesCalculated = ctx.frameTimesCalculated;
    auto& menuResScale = ctx.menuResScale;
    auto& fpsScale = ctx.fpsScale;
    auto& averageFrameTime = ctx.averageFrameTime;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    // FPS Overlay font
    fpsScale = config->FpsScale.value_or(menuResScale);

    // Update frame time & upscaler time averages
    averageFrameTime = 0.0f;
    averageUpscalerFT = 0.0f;

    if (config->ShowFps.value_or_default() || _isVisible)
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (state.frameTimes[i] > 0.0)
            {
                frameTime += state.frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
        frameTimesCalculated = true;

        float lastFT = static_cast<float>(state.frameTimes.empty() ? 0.0f : state.frameTimes.back());
        float lastUT = static_cast<float>(state.upscaleTimes.empty() ? 0.0f : state.upscaleTimes.back());
        gFrameTimes.Push(lastFT);
        if (HasUpscalerGpuTiming(state)) gUpscalerTimes.Push(lastUT);

        averageFrameTime = gFrameTimes.Average();
        averageUpscalerFT = gUpscalerTimes.Average();
    }
}

// Labels for the comparison views.
//
// Drawn straight onto the foreground draw list, not as ImGui windows -- the last attempt made them
// draggable windows and the clamping fought the split. Here each label is clipped to its own side of
// the comparison, so in the wipe the moving split reveals and hides it exactly as it does the
// pictures, and there is nothing to drag. Both wipe labels sit in the same top-left corner, each
// clipped to its side, so whichever picture currently owns that corner is the one whose label shows.
void MenuCommon::RenderNrCompareTags()
{
    auto* config = Config::Instance();

    const auto snapshot = config->GetDlssNrConfigSnapshot();
    const bool vulkan = snapshot.DlssNrRoute.value_or_default() != 0
        ? DlssNr::PresentTelemetry().api == DlssNr::PresentApi::Vulkan
        : State::Instance().api == API::Vulkan;
    if (!snapshot.DlssNrEnabled.value_or_default() || !snapshot.DlssNrApplyModel.value_or_default() ||
        (!vulkan && snapshot.DlssNrRoute.value_or_default() != 0) ||
        (vulkan && !DlssNr::NativeRayReconstructionVk() && snapshot.DlssNrRoute.value_or_default() == 0 && snapshot.DlssNrRenderingMode.value_or_default() != 0)) return;
    const uint32_t mode = config->DlssNrCompare.value_or_default();

    if (mode == 0 || !config->DlssNrCompareTags.value_or_default())
        return;

    const ImVec2 screen = ImGui::GetIO().DisplaySize;

    if (screen.x < 1.0f || screen.y < 1.0f)
        return;

    const bool swap = config->DlssNrCompareSwap.value_or_default();
    const float split = mode == 1 ? 0.5f
                                  : std::clamp(config->DlssNrCompareSplit.value_or_default(), 0.0f, 1.0f);
    const float splitX = split * screen.x;

    const float scale = std::clamp(config->DlssNrTagScale.value_or_default(), 0.5f, 5.0f);

    // The left side is the untouched frame unless swapped -- matching the shader's
    // showOriginal = (uv.x < split) != swap.
    const char* leftText = swap ? Neurotic::UiLiteral("ingame.menu-common.dlss_nr_on_a841c71d", "DLSS NR : ON") : Neurotic::UiLiteral("ingame.menu-common.dlss_nr_off_e2f9ead7", "DLSS NR : OFF");
    const char* rightText = swap ? Neurotic::UiLiteral("ingame.menu-common.dlss_nr_off_e2f9ead7", "DLSS NR : OFF") : Neurotic::UiLiteral("ingame.menu-common.dlss_nr_on_a841c71d", "DLSS NR : ON");

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * scale;
    const float margin = 10.0f * scale;

    // Both labels flank the divider along the top: the left picture's label is right-aligned just
    // left of the split, the right picture's is left-aligned just right of it. Each is clipped to its
    // own side, so in the wipe the split reveals and hides them along with the images.
    auto drawTag = [&](const char* text, float x, ImVec2 clipMin, ImVec2 clipMax)
    {
        const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);

        // Never let a label run off the visible frame as it grows.
        x = std::min(std::max(x, 0.0f), screen.x - size.x);
        float y = std::min(margin, screen.y - size.y - margin);
        y = std::max(y, 0.0f);

        dl->PushClipRect(clipMin, clipMax, true);
        dl->AddText(font, fontSize, ImVec2(x + 2.0f, y + 2.0f), IM_COL32(0, 0, 0, 210), text);
        dl->AddText(font, fontSize, ImVec2(x, y), IM_COL32(255, 255, 255, 255), text);
        dl->PopClipRect();
    };

    const ImVec2 leftSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, leftText);

    // Left picture's label: right edge a margin in from the split. Right picture's: left edge a margin
    // out from the split.
    drawTag(leftText, splitX - margin - leftSize.x, ImVec2(0.0f, 0.0f), ImVec2(splitX, screen.y));
    drawTag(rightText, splitX + margin, ImVec2(splitX, 0.0f), ImVec2(screen.x, screen.y));
}

void MenuCommon::RenderPerformanceOverlay(RenderMenuContext& ctx)
{
    RenderNrCompareTags();


    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;
    auto& now = ctx.now;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& menuResScale = ctx.menuResScale;
    auto& fpsScale = ctx.fpsScale;
    auto& averageFrameTime = ctx.averageFrameTime;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    // If Fps overlay is visible
    if (config->ShowFps.value_or_default())
    {
        bool stylePushed = false;

        const static auto defaultStyle = ImGuiStyle();

        // Rescale the fps overlay every frame because it shares style with the main menu
        if (config->FpsScale.has_value() && config->FpsScale.value() != menuResScale)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, defaultStyle.WindowPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, defaultStyle.FramePadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, defaultStyle.CellPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, defaultStyle.SeparatorTextPadding * fpsScale);

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, defaultStyle.ItemSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, defaultStyle.ItemInnerSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, defaultStyle.IndentSpacing * fpsScale);

            stylePushed = true;
        }

        // Set overlay position
        ImGui::SetNextWindowPos(overlayPosition, ImGuiCond_Always);

        // Set overlay window properties
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));  // Transparent border
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // Transparent frame background

        if (!config->OverlaysUseTheme.value_or_default())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        }

        ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default()); // Transparent background

        if (!config->OverlaysUseTheme.value_or_default())
        {
            ImVec4 green(0.0f, 1.0f, 0.0f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotLines, toneMapColor(green));
        }

        if (ImGui::Begin(Neurotic::UiLiteral("ingame.menu-common.performance_overlay_4e0c0f8c", "Performance Overlay"), nullptr,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav))
        {
            std::string api;
            if (IdentifyGpu::getPrimaryGpu().usesDxvk && state.api == DX11)
            {
                if (state.swapchainInteropApi == SwapchainInteropApi::None)
                    api = "DXVK";
                else
                    api = "DXVK w/Dx12";
            }
            else if (IdentifyGpu::getPrimaryGpu().usesVkd3dProton && state.api == DX12)
            {
                api = "VKD3D";
            }
            else
            {
                switch (state.swapchainApi)
                {
                case Vulkan:
                    api = "VLK";
                    break;

                case DX11:
                    api = "D3D11";
                    break;

                case DX12:
                    if (state.swapchainInteropApi == SwapchainInteropApi::Dx11wDx12)
                        api = "D3D11 w/DX12";
                    else
                        api = "D3D12";

                    break;

                default:
                    switch (state.api)
                    {
                    case Vulkan:
                        api = "VLK";
                        break;

                    case DX11:
                        api = "D3D11";
                        break;

                    case DX12:
                        api = "D3D12";
                        break;

                    default:
                        api = "???";
                        break;
                    }

                    break;
                }
            }

            if (config->UseHQFont.value_or_default())
                ImGui::PushFontSize(std::round(fpsScale * fontSize));
            else
                ImGui::SetWindowFontScale(fpsScale);

            std::string firstLine = "";
            std::string secondLine = "";
            std::string thirdLine = "";

            auto fg = state.currentFG;
            auto fgText = (fg != nullptr && fg->IsActive() && !fg->IsPaused()) ? (" (" + std::string(fg->Name()) + ")")
                                                                               : std::string();

            const int fakeFramesCount = state.dlssgDetectedInterpolationCount;
            auto formatFg = [&](std::string_view name, int maxFakeFrames)
            {
                if (fakeFramesCount > maxFakeFrames)
                    return StrFmt(Neurotic::UiLiteral("ingame.menu-common.doesn_t_support_more_than_x_ead0e5a1", " (%s doesn't support more than %dx)"), std::string(name).c_str(), maxFakeFrames);

                else if (fakeFramesCount == 0)
                    return StrFmt(Neurotic::UiLiteral("ingame.provider.5270f73048df", " (%s off)"), std::string(name).c_str());

                return StrFmt(Neurotic::UiLiteral("ingame.provider.bedc51f9ec28", " (%s x%d)"), std::string(name).c_str(), fakeFramesCount + 1);
            };

            const FGNvngxReplacement activeNvngxFg = state.activeFgNvngx;
            if (activeNvngxFg == FGNvngxReplacement::Arturs)
            {
                fgText = formatFg("Enabler", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::Nukems)
            {
                fgText = formatFg("Nukems", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::FFX)
            {
                fgText = formatFg("FFX", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::Combo)
            {
                fgText = formatFg("Combo", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (state.activeFgOutput == FGOutput::DLSSG && fg)
            {
                fgText = formatFg(Neurotic::UiLiteral("ingame.menu-common.dlssg_86366b33", "DLSSG"), fg->GetMaxInterpolationCount());
            }

            const auto overlayType = config->FpsOverlayType.value_or_default();
            const bool hasFeature = currentFeature && !currentFeature->IsFrozen();

            // Prepare Line 1
            std::string featurePart;
            std::string fpsPart;

            if (hasFeature)
            {
                const bool usesDx12CompatLayer = currentFeature->IsWithDx12();

                featurePart = StrFmt(" | %s -> %s %u.%u.%u%s",Neurotic::Translate(ApiUpscalerInputName(state.currentInputApiName).c_str()).c_str(),Neurotic::Translate(currentFeature->ShortName().c_str()).c_str(), currentFeature->Version().major,
                                     currentFeature->Version().minor, currentFeature->Version().patch,Neurotic::Translate(usesDx12CompatLayer ? " w/Dx12" : "").c_str());
            }

            if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
            {
                const double baseFps = frameRate / (double) (fg->GetInterpolatedFrameCount() + 1);

                switch (overlayType)
                {
                case FpsOverlay_JustFPS:
                    fpsPart = StrFmt("%6.1f/%5.1f ", frameRate, baseFps);
                    break;

                case FpsOverlay_Simple:
                    fpsPart = StrFmt("FPS: %6.1f/%5.1f, %7.2f ms", frameRate, baseFps, frameTime);
                    break;

                default:
                    fpsPart = StrFmt("FPS: %6.1f/%5.1f, Avg: %6.1f", frameRate, baseFps, 1000.0f / averageFrameTime);
                    break;
                }
            }
            else
            {
                switch (overlayType)
                {
                case FpsOverlay_JustFPS:
                    fpsPart = StrFmt("%6.1f ", frameRate);
                    break;

                case FpsOverlay_Simple:
                    fpsPart = StrFmt("FPS: %6.1f, %7.2f ms", frameRate, frameTime);
                    break;

                default:
                    fpsPart = StrFmt("FPS: %6.1f, Avg: %6.1f", frameRate, 1000.0f / averageFrameTime);
                    break;
                }
            }

            if (overlayType == FpsOverlay_JustFPS)
                firstLine = StrFmt("%s",Neurotic::Translate(fpsPart.c_str()).c_str());
            else
                firstLine = StrFmt("%s | %s%s%s",Neurotic::Translate(api.c_str()).c_str(),Neurotic::Translate(fpsPart.c_str()).c_str(),Neurotic::Translate(fgText.c_str()).c_str(),Neurotic::Translate(featurePart.c_str()).c_str());

            // Prepare Line 2
            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Detailed)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                secondLine = StrFmt(Neurotic::UiLiteral("ingame.menu-common.frame_time_7_2f_ms_avg_7_2f_ms_155ac9bd", "Frame Time: %7.2f ms, Avg: %7.2f ms"), state.frameTimes.back(), averageFrameTime);
            }

            // Prepare Line 3
            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Full)
            {
                thirdLine = HasUpscalerGpuTiming(state)
                    ? StrFmt(Neurotic::UiLiteral("ingame.menu-common.upscaler_time_7_2f_ms_avg_7_2f_ms_8caed2a9", "Upscaler Time: %7.2f ms, Avg: %7.2f ms"), state.upscaleTimes.back(), averageUpscalerFT)
                    : Neurotic::UiLiteral("ingame.menu-common.gpu_upscaler_timing_unavailable_b6357532", "GPU upscaler timing unavailable");
            }

            ImVec2 plotSize;
            if (config->FpsOverlayHorizontal.value_or_default())
            {
                plotSize = { fpsScale * 150, fpsScale * 16 };
            }
            else
            {
                // Find the widest text width
                auto firstSize = ImGui::CalcTextSize(firstLine.c_str());
                auto secondSize = ImGui::CalcTextSize(secondLine.c_str());
                auto thirdSize = ImGui::CalcTextSize(thirdLine.c_str());
                auto textWidth = 0.0f;

                if (firstSize.x > secondSize.x)
                    textWidth = firstSize.x > thirdSize.x ? firstSize.x : thirdSize.x;
                else
                    textWidth = secondSize.x > thirdSize.x ? secondSize.x : thirdSize.x;

                auto minWidth = fpsScale * 300.0f;
                auto plotWidth = textWidth < minWidth ? minWidth : textWidth;

                plotSize = { plotWidth, fpsScale * 30 };
            }

            // Draw the overlay
            ImGui::Text(firstLine.c_str());

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Detailed)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(secondLine.c_str());
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_DetailedGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of frame times
                ImGui::PlotLines(
                    "##FrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gFrameTimes, plotWidth, 0, nullptr, 0.0f, 66.6f, plotSize);
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Full)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(thirdLine.c_str());
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_FullGraph && HasUpscalerGpuTiming(state))
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of upscaler times
                ImGui::PlotLines(
                    "##UpscalerFrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gUpscalerTimes, plotWidth, 0, nullptr, 0.0f, 20.0f, plotSize);
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_ReflexTimings)
            {
                constexpr auto delayBetweenPollsMs = 500;
                static auto previousPoll = 0.0;
                static bool gotData = false;

#ifdef LOW_LATENCY_INPUTS
                static TimingData timingData {};

                if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
                {
                    gotData = InputCommon::get_timing_data(timingData);
                    previousPoll = now;
                }

                if (gotData && timingData.timeRange.has_value())
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    constexpr float offsetForText = 155;

                    const auto& rangeInNs = timingData.timeRange.value().length;

                    UINT64 localFrameCount = 0;

                    if (fg != nullptr)
                        localFrameCount = fg->FrameCount();

                    ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.fgid_llu_rfxid_llu_7e3dc07d", "FGId: %llu, RfxId: %llu"), localFrameCount, state.reflexFrameId);
                    ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.low_latency_timings_whole_frame_1fms_8c587a80", "Low latency timings, whole frame: %.1fms"), rangeInNs / 1000.0);

                    const auto maxWidth =
                        config->FpsOverlayHorizontal.value_or_default() ? ImGui::GetWindowWidth() : plotSize.x;

                    const auto drawTiming = [&](const auto& timingOpt, const char* desc, ImVec4 color)
                    {
                        if (!timingOpt.has_value())
                            return;

                        auto toneMappedColor = State::Instance().isHdrActive ? toneMapColor(color) : color;

                        const auto& timing = timingOpt.value();
                        float duration = static_cast<float>(timing.length * rangeInNs / 1000.0);

                        ImGui::TextColored(toneMappedColor, Neurotic::UiLiteral("ingame.menu-common.12s_4_1fms_23260da8", "%-12s %4.1fms"),Neurotic::Translate(desc).c_str(), duration);

                        auto leftLimit = ImGui::GetItemRectMin().x + offsetForText * fpsScale;

                        auto start = static_cast<float>(leftLimit + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                        timing.position);

                        auto end = static_cast<float>(start + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                  timing.length);

                        auto pos = ImVec2(start, ImGui::GetItemRectMin().y);
                        auto size = ImVec2(end, ImGui::GetItemRectMax().y);

                        drawList->AddRectFilled(pos, size, ImGui::ColorConvertFloat4ToU32(toneMappedColor));
                    };

                    drawTiming(timingData.simulation, "Simulation", ImVec4(0.768f, 0.169f, 0.169f, 1.0f));
                    drawTiming(timingData.renderSubmit, "RenderSubmit", ImVec4(0.235f, 0.705f, 0.294f, 1.0f));
                    drawTiming(timingData.present, "Present", ImVec4(1.0f, 0.88f, 0.098f, 1.0f));
                    drawTiming(timingData.driver, "Driver", ImVec4(0.263f, 0.388f, 0.847f, 1.0f));
                    drawTiming(timingData.osRenderQueue, "RenderQueue", ImVec4(0.76f, 0.51f, 0.188f, 1.0f));
                    drawTiming(timingData.gpuRender, "GpuRender", ImVec4(0.569f, 0.117f, 0.705f, 1.0f));
                }
#else
                if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
                {
                    gotData = ReflexHooks::updateTimingData();
                    previousPoll = now;
                }

                auto& timingData = ReflexHooks::timingData;

                if (gotData && timingData[TimingType::TimeRange].has_value())
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    constexpr float offsetForText = 155;

                    const auto& rangeInNs = timingData[TimingType::TimeRange].value().length;

                    UINT64 localFrameCount = 0;

                    if (fg != nullptr)
                        localFrameCount = fg->FrameCount();

                    ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.fgid_llu_rfxid_llu_7e3dc07d", "FGId: %llu, RfxId: %llu"), localFrameCount, state.reflexFrameId);
                    ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.reflex_timings_whole_frame_1fms_f6adec3a", "Reflex timings, whole frame: %.1fms"), rangeInNs / 1000.0);

                    const auto maxWidth =
                        config->FpsOverlayHorizontal.value_or_default() ? ImGui::GetWindowWidth() : plotSize.x;

                    const auto drawTiming = [&](TimingType type, const char* desc, ImVec4 color)
                    {
                        if (!timingData[type].has_value())
                            return;

                        auto toneMappedColor = toneMapColor(color);

                        auto& timing = timingData[type].value();
                        float duration = static_cast<float>(timing.length * rangeInNs / 1000.0);
                        ImGui::TextColored(toneMappedColor, Neurotic::UiLiteral("ingame.menu-common.12s_4_1fms_23260da8", "%-12s %4.1fms"),Neurotic::Translate(desc).c_str(), duration);
                        auto leftLimit = ImGui::GetItemRectMin().x + offsetForText * fpsScale;
                        auto start = static_cast<float>(leftLimit + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                        timing.position);
                        auto end = static_cast<float>(start + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                  timing.length);
                        auto pos = ImVec2(start, ImGui::GetItemRectMin().y);
                        auto size = ImVec2(end, ImGui::GetItemRectMax().y);
                        drawList->AddRectFilled(pos, size, ImGui::ColorConvertFloat4ToU32(toneMappedColor));
                    };

                    drawTiming(TimingType::Simulation, "Simulation", ImVec4(0.768f, 0.169f, 0.169f, 1.0f));
                    drawTiming(TimingType::RenderSubmit, "RenderSubmit", ImVec4(0.235f, 0.705f, 0.294f, 1.0f));
                    drawTiming(TimingType::Present, "Present", ImVec4(1.0f, 0.88f, 0.098f, 1.0f));
                    drawTiming(TimingType::Driver, "Driver", ImVec4(0.263f, 0.388f, 0.847f, 1.0f));
                    drawTiming(TimingType::OsRenderQueue, "RenderQueue", ImVec4(0.76f, 0.51f, 0.188f, 1.0f));
                    drawTiming(TimingType::GpuRender, "GpuRender", ImVec4(0.569f, 0.117f, 0.705f, 1.0f));
                }
#endif
            }
        }

        // Restore the style
        if (!config->OverlaysUseTheme.value_or_default())
            ImGui::PopStyleColor(5);
        else
            ImGui::PopStyleColor(2);

        // Get size for postioning
        overlaySize = ImGui::GetWindowSize();

        if (config->UseHQFont.value_or_default())
            ImGui::PopFontSize();

        ImGui::End();

        if (stylePushed)
            ImGui::PopStyleVar(7);

        // Left / Right
        if (config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopLeft ||
            config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_BottomLeft)
        {
            overlayPosition.x = 0;
        }
        else
        {
            overlayPosition.x = io.DisplaySize.x - overlaySize.x;
        }

        // Top / Bottom
        if (config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopLeft ||
            config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopRight)
        {
            overlayPosition.y = 0;
        }
        else
        {
            // Prevent overlapping with splash message
            if (!config->DisableSplash.value_or_default() && now > splashStart && now < splashLimit)
                overlayPosition.y = io.DisplaySize.y - overlaySize.y - splashSize.y;
            else
                overlayPosition.y = io.DisplaySize.y - overlaySize.y;
        }
    }
}

void MenuCommon::RenderMainMenuHeaderMessages(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;
    auto& versionStatus = ctx.versionStatus;
    auto& currentVersionText = ctx.currentVersionText;

    if (!_showMipmapCalcWindow && !_showHudlessWindow && !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
        ImGui::SetWindowFocus();

    if (config->MenuScale.has_value())
    {
        _selectedScale = ((int) (menuResScale * 10.0f)) - 4;
    }
    else
    {
        _selectedScale = 0;
    }

    if (versionStatus.completed)
    {
        if (versionStatus.updateAvailable && !versionStatus.latestTag.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), Neurotic::UiLiteral("ingame.menu-common.update_available_s_current_s_935af44f", "Update available: %s (current %s)"),Neurotic::Translate(versionStatus.latestTag.c_str()).c_str(),Neurotic::Translate(currentVersionText.c_str()).c_str());

            if (!versionStatus.latestUrl.empty())
            {
                ImGui::SameLine();
                ImGui::TextLinkOpenURL(Neurotic::UiLiteral("ingame.menu-common.open_release_page_89121111", "Open release page"), versionStatus.latestUrl.c_str());
            }

            ImGui::Spacing();
        }
        else if (!versionStatus.error.empty())
        {
            LOG_ERROR("Version check failed: {0}", versionStatus.error);
            versionStatus.error.clear();
        }
        // Disabled error message
        // else if (!versionStatus.error.empty())
        //{
        //    ImGui::Spacing();
        //    ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.4f, 0.f, 1.f)), "%s", versionStatus.error.c_str());
        //    ImGui::Spacing();
        //}
    }

}

// Both the notice and its tab lamp project the same already-detected library facts.
static bool HasDetectedUpscalerLibraries(const State& state)
{
    return state.nvngxExists || state.nvngxReplacement.has_value() ||
        state.libxessExists || XeSSProxy::Module() != nullptr;
}

void MenuCommon::RenderUpscalerPreflight(RenderMenuContext& ctx)
{
    auto& state=ctx.state;
    auto& currentFeature=ctx.currentFeature;
    auto& primaryGpu=*ctx.primaryGpu;
    // Read-only detection facts use the same compact status language as NR signals.

        const bool libraries = HasDetectedUpscalerLibraries(state);
        const Neurotic::Sleek::DetectedSource sources[] = {
            {primaryGpu.dlssCapable ? "nvngx_dlss" : "nvngx.dll",
                primaryGpu.dlssCapable ? state.NVNGX_DLSS_Path.has_value() : state.nvngxExists},
            {primaryGpu.dlssCapable ? "nvngx_dlssd" : Neurotic::UiLiteral("ingame.menu-common.nvngx_replacement_2c0b52ec", "nvngx replacement"),
                primaryGpu.dlssCapable ? state.NVNGX_DLSSD_Path.has_value() : state.nvngxReplacement.has_value()},
            {"libxess",state.libxessExists || XeSSProxy::Module() != nullptr},
            {Neurotic::UiLiteral("ingame.menu-common.fsr_hooks_8b7540ce", "FSR Hooks"),state.fsrHooks},
            {Neurotic::UiLiteral("ingame.menu-common.fsr_3_1_93c8f581", "FSR 3.1"),FfxApiProxy::Dx12Module() != nullptr},
            {"FSR 3.1 SR",FfxApiProxy::Dx12Module_SR() != nullptr},
            {"FSR 3.1 FG",FfxApiProxy::Dx12Module_FG() != nullptr}
        };
    const bool initialized=currentFeature && currentFeature->IsInited();
    const auto backend=currentFeature?currentFeature->Name():std::string(Neurotic::UiLiteral("ingame.menu-common.super_resolution_31e1b2d8", "Super resolution"));
    Neurotic::Sleek::UpscalerNotice(initialized ? Neurotic::UiLiteral("ingame.menu-common.component_presence_is_shown_below_95f32f8f", "Component presence is shown below.")
        : libraries ? Neurotic::UiLiteral("ingame.menu-common.select_an_available_upscaler_in_the_game_and_ent_d58c128c", "Select an available upscaler in the game and enter gameplay.")
        : Neurotic::UiLiteral("ingame.menu-common.no_supported_sr_input_detected_review_input_hook_43ef83d7", "No supported SR input detected. Review input hooks; NR Anything is available for unsupported integration."),
        sources,(int)std::size(sources),backend.c_str(),initialized&&!currentFeature->IsFrozen(),initialized&&currentFeature->IsFrozen());
    {Neurotic::Sleek::ContentCard input("##SrInputHooks",Neurotic::UiLiteral("ingame.menu-common.input_hooks_01e6e5d3", "Input hooks"),true);RenderUpscalerInputsSettings(ctx);}
    Neurotic::Sleek::ContentCard advanced("##SrAdvancedSetup",Neurotic::UiLiteral("ingame.menu-common.advanced_setup_82c859f0", "Advanced setup"),true);
    if (ImGui::CollapsingHeader(Neurotic::UiLiteral("ingame.menu-common.advanced_input_interpretation_7b4eb49b", "Advanced input interpretation")))
    {
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.overrides_how_sr_interprets_game_inputs_flag_cha_fad19abd", "Overrides how SR interprets game inputs. Flag changes recreate the upscaler; Reset restores automatic values."));
        const int page = ctx.childPage;
        ctx.childPage = 4;
        RenderActiveImageSettings(ctx);
        ctx.childPage = page;
    }
    if (ImGui::CollapsingHeader(Neurotic::UiLiteral("ingame.menu-common.ngx_recovery_and_resolution_limits_69362669", "NGX recovery and resolution limits")))
    {
        bool generic = ctx.config->UseGenericAppIdWithDlss.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_generic_app_id_with_dlss_74ce7fd0", "Use Generic App Id with DLSS"), &generic)) ctx.config->UseGenericAppIdWithDlss = generic;
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.save_and_restart_the_game_after_changing_the_app_50e746f3", "Save and restart the game after changing the application ID or input hooks."));
        if (bool value = ctx.config->DrsMinOverrideEnabled.value_or_default(); ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.override_drs_minimum_6d41a225", "Override DRS Minimum"), &value)) ctx.config->DrsMinOverrideEnabled = value;
        if (bool value = ctx.config->DrsMaxOverrideEnabled.value_or_default(); ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.override_drs_maximum_0f0dc478", "Override DRS Maximum"), &value)) ctx.config->DrsMaxOverrideEnabled = value;
    }

}

template<class T> static bool ResetSliderSetting(const char* id, T& setting)
{
    ImGui::PushID(id);
    Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")));
    const bool reset = ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset"));
    if (reset) setting = std::optional<typename T::value_type>{};
    ImGui::PopID();
    return reset;
}

void MenuCommon::RenderActiveUpscalerSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
        GetCurrentBackendInfo(state.api, currentBackend, &currentBackendName);

    if (ctx.childPage == 0 && currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        Neurotic::Sleek::ContentCard choice("##SrProviderChoice", Neurotic::UiLiteral("ingame.menu-common.active_upscaler_090e86f6", "Active upscaler"), true);
        // UPSCALERS -----------------------------

        std::string spoofingText;

        ImGui::PushItemWidth(180.0f * menuResScale);

        const bool usesDlssd = currentFeature->GetUpscalerType() == Upscaler::DLSSD;
        const bool usesDx12CompatLayer = currentFeature->IsWithDx12();

        switch (state.api)
        {
        case DX11:
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.device_s_6ed0e8fb", "Device: %s"),Neurotic::Translate(primaryGpu.name.c_str()).c_str());

            ImGui::Text("D3D11 %s| %s %d.%d.%d%s",Neurotic::Translate(primaryGpu.usesDxvk ? Neurotic::UiLiteral("ingame.menu-common.dxvk_fc0fbf36", "(DXVK) ") : "").c_str(),Neurotic::Translate(currentFeature->ShortName().c_str()).c_str(), currentFeature->Version().major,
                        currentFeature->Version().minor, currentFeature->Version().patch,Neurotic::Translate(usesDx12CompatLayer ? " w/Dx12" : "").c_str());
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.input_s_e9536956", "| Input: %s"),Neurotic::Translate(ApiUpscalerInputName(state.currentInputApiName).c_str()).c_str());

            ImGui::SameLine(0.0f, 6.0f);
            spoofingText = config->DxgiSpoofing.value_or_default() ? Neurotic::UiLiteral("ingame.menu-common.on_d2f9df8a", "On") : Neurotic::UiLiteral("ingame.objectruleeditor.off_dc516be5", "Off");
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.spoof_s_1a437f0b", "| Spoof: %s"),Neurotic::Translate(spoofingText.c_str()).c_str());

            if (!usesDlssd)
                AddDx11Backends(currentBackend);

            break;

        case DX12:
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.device_s_6ed0e8fb", "Device: %s"),Neurotic::Translate(primaryGpu.name.c_str()).c_str());

            ImGui::Text("D3D12 %s| %s %d.%d.%d",Neurotic::Translate(primaryGpu.usesDxvk ? Neurotic::UiLiteral("ingame.menu-common.dxvk_fc0fbf36", "(DXVK) ") : "").c_str(),Neurotic::Translate(currentFeature->ShortName().c_str()).c_str(), currentFeature->Version().major,
                        currentFeature->Version().minor, currentFeature->Version().patch);
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.input_s_e9536956", "| Input: %s"),Neurotic::Translate(ApiUpscalerInputName(state.currentInputApiName).c_str()).c_str());

            ImGui::SameLine(0.0f, 6.0f);
            spoofingText = config->DxgiSpoofing.value_or_default() ? Neurotic::UiLiteral("ingame.menu-common.on_d2f9df8a", "On") : Neurotic::UiLiteral("ingame.objectruleeditor.off_dc516be5", "Off");
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.spoof_s_1a437f0b", "| Spoof: %s"),Neurotic::Translate(spoofingText.c_str()).c_str());

            if (!usesDlssd)
                AddDx12Backends(currentBackend);

            break;

        default:
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.device_s_6ed0e8fb", "Device: %s"),Neurotic::Translate(primaryGpu.name.c_str()).c_str());

            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.vulkan_s_s_d_d_d_s_de11c178", "Vulkan %s| %s %d.%d.%d%s"),Neurotic::Translate(primaryGpu.usesDxvk ? Neurotic::UiLiteral("ingame.menu-common.dxvk_fc0fbf36", "(DXVK) ") : "").c_str(),Neurotic::Translate(currentFeature->ShortName().c_str()).c_str(), currentFeature->Version().major,
                        currentFeature->Version().minor, currentFeature->Version().patch,Neurotic::Translate(usesDx12CompatLayer ? " w/Dx12" : "").c_str());
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.input_s_e9536956", "| Input: %s"),Neurotic::Translate(ApiUpscalerInputName(state.currentInputApiName).c_str()).c_str());

            auto vlkSpoof = config->VulkanSpoofing.value_or_default();
            auto vlkExtSpoof = config->VulkanExtensionSpoofing.value_or_default();

            if (vlkSpoof && vlkExtSpoof)
                spoofingText = "On + Ext";
            else if (vlkSpoof)
                spoofingText = Neurotic::UiLiteral("ingame.menu-common.on_d2f9df8a", "On");
            else if (vlkExtSpoof)
                spoofingText = Neurotic::UiLiteral("ingame.menu-common.just_ext_9c0608ac", "Just Ext");
            else
                spoofingText = Neurotic::UiLiteral("ingame.objectruleeditor.off_dc516be5", "Off");

            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.spoof_s_1a437f0b", "| Spoof: %s"),Neurotic::Translate(spoofingText.c_str()).c_str());

            if (!usesDlssd)
                AddVulkanBackends(currentBackend);
        }

        ImGui::PopItemWidth();

        if (!usesDlssd)
        {
            ImGui::SameLine(0.0f, 6.0f);

            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.change_upscaler_fa9a7441", "Change Upscaler##2")) && state.newBackend != Upscaler::Reset &&
                state.newBackend != currentBackend)
            {
                if (state.newBackend == Upscaler::XeSS)
                {
                    // Reseting them for xess
                    config->DisableReactiveMask.reset();
                    config->DlssReactiveMaskBias.reset();
                }

                MARK_ALL_BACKENDS_CHANGED();
            }
        }

        if (currentFeature->AccessToReactiveMask())
        {
            ImGui::BeginDisabled(config->DisableReactiveMask.value_or(false));

            auto useAsTransparency = config->FsrUseMaskForTransparency.value_or_default();
            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_reactive_mask_as_transparency_mask_50ab97c3", "Use Reactive Mask as Transparency Mask"), &useAsTransparency))
                config->FsrUseMaskForTransparency = useAsTransparency;

            ImGui::EndDisabled();
        }

        if (primaryGpu.dlssCapable && !state.NVNGX_DLSS_Path.has_value())
        {
            ImGui::Spacing();
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), Neurotic::UiLiteral("ingame.menu-common.nvngx_dlss_dll_not_found_dlss_disabled_9fefbc04", "nvngx_dlss.dll not found, DLSS disabled!"));
        }
    }

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        const bool usesDlssd = currentFeature->GetUpscalerType() == Upscaler::DLSSD;

        // Dx11 with Dx12
        if (ctx.childPage == 0 && state.api == DX11 && currentFeature->IsWithDx12())
        {
            ImGui::Spacing();

            {
                Neurotic::Sleek::ContentCard bridge("##SrDx11Bridge", Neurotic::UiLiteral("ingame.menu-common.d3d11_bridge_32eeb8c3", "D3D11 bridge"), true);

                if (bool dontUseNTShared = config->DontUseNTShared.value_or_default();
                    ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.don_t_use_ntshared_68b68167", "Don't Use NTShared"), &dontUseNTShared))
                    config->DontUseNTShared = dontUseNTShared;

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }

        if (ctx.childPage == 0 && state.api == Vulkan && currentFeature->IsWithDx12())
        {
            ImGui::Spacing();

            {
                Neurotic::Sleek::ContentCard bridge("##SrVulkanBridge", Neurotic::UiLiteral("ingame.menu-common.vulkan_bridge_ff72cada", "Vulkan bridge"), true);

                if (bool inputsUseCopy = config->VulkanUseCopyForInputs.value_or_default();
                    ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_copyresource_for_inputs_5e99b65b", "Use CopyResource for Inputs"), &inputsUseCopy))
                    config->VulkanUseCopyForInputs = inputsUseCopy;

                if (bool outputUseCopy = config->VulkanUseCopyForOutput.value_or_default();
                    ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_copyresource_for_output_451b140a", "Use CopyResource for Output"), &outputUseCopy))
                    config->VulkanUseCopyForOutput = outputUseCopy;

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }

        // UPSCALER SPECIFIC -----------------------------

        // XeSS -----------------------------
        if (ctx.childPage == 2 && currentBackend == Upscaler::XeSS && !usesDlssd)
        {
            ImGui::Spacing();
            ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.xess_settings_b0d57935", "XeSS Settings"));
            {
                ImGui::Spacing();

                const char* models[] = { Neurotic::UiLiteral("ingame.option.7792d8bd7e68", "KPSS"), Neurotic::UiLiteral("ingame.option.a194298b40f3", "SPLAT"), Neurotic::UiLiteral("ingame.option.08045f90940e", "MODEL_3"), Neurotic::UiLiteral("ingame.option.559bb17287b7", "MODEL_4"), Neurotic::UiLiteral("ingame.option.7890da857d3b", "MODEL_5"), Neurotic::UiLiteral("ingame.option.c4e8a022d0b3", "MODEL_6") };
                auto configModes = config->NetworkModel.value_or_default();

                if (configModes < 0 || configModes > 5)
                    configModes = 0;

                const char* selectedModel = models[configModes];

                if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.network_models_027b99b2", "Network Models"), selectedModel))
                {
                    for (int n = 0; n < 6; n++)
                    {
                        if (ImGui::Selectable(models[n], (config->NetworkModel.value_or_default() == n)))
                        {
                            config->NetworkModel = n;
                            state.newBackend = currentBackend;
                            MARK_ALL_BACKENDS_CHANGED();
                        }
                    }

                    ImGui::EndCombo();
                }
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.likely_doesn_t_do_much_d28ae490", "Likely doesn't do much"));

                if (bool dbg = state.xessDebug; ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.dump_shift_del_83d7eb41", "Dump (Shift+Del)"), &dbg))
                    state.xessDebug = dbg;

                ImGui::SameLine(0.0f, 6.0f);
                int dbgCount = state.xessDebugFrames;

                ImGui::PushItemWidth(95.0f * menuResScale);
                if (ImGui::InputInt("frames", &dbgCount))
                {
                    if (dbgCount < 4)
                        dbgCount = 4;
                    else if (dbgCount > 999)
                        dbgCount = 999;

                    state.xessDebugFrames = dbgCount;
                }

                ImGui::PopItemWidth();

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }

        // FFX -----------------
        if (ctx.childPage == 2 && !usesDlssd && (currentBackend == Upscaler::FFX || currentBackend == Upscaler::FFX_on12))
        {
            ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.ffx_settings_aac30c17", "FFX Settings"));

            if (_ffxUpscalerIndex < 0)
                _ffxUpscalerIndex = config->FfxUpscalerIndex.value_or_default();

            if (!state.ffxUpscalerVersionNames.empty() &&
                state.ffxUpscalerVersionNames.size() == state.ffxUpscalerVersionIds.size())
            {
                ImGui::PushItemWidth(135.0f * menuResScale);

                const bool validIndex = _ffxUpscalerIndex >= 0 && static_cast<size_t>(_ffxUpscalerIndex) < state.ffxUpscalerVersionNames.size();
                auto currentName = validIndex ? StrFmt("FSR %s",Neurotic::Translate(state.ffxUpscalerVersionNames[_ffxUpscalerIndex]).c_str()) : std::string(Neurotic::UiLiteral("ingame.menu-common.unavailable_saved_version_1b4bf860", "Unavailable saved version"));
                if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.ffx_upscaler_1e66cdc5", "FFX Upscaler"), currentName.c_str()))
                {
                    for (int n = 0; n < state.ffxUpscalerVersionIds.size(); n++)
                    {
                        auto name = StrFmt("FSR %s##%d",Neurotic::Translate(state.ffxUpscalerVersionNames[n]).c_str(), n);
                        if (ImGui::Selectable(name.c_str(), config->FfxUpscalerIndex.value_or_default() == n))
                            _ffxUpscalerIndex = n;
                    }

                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.list_of_upscalers_reported_by_ffx_sdk_7a39e57f", "List of upscalers reported by FFX SDK"));

                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.change_upscaler_fa9a7441", "Change Upscaler")) && _ffxUpscalerIndex >= 0 &&
                    static_cast<size_t>(_ffxUpscalerIndex) < state.ffxUpscalerVersionIds.size() &&
                    _ffxUpscalerIndex != config->FfxUpscalerIndex.value_or_default())
                {
                    config->FfxUpscalerIndex = _ffxUpscalerIndex;
                    state.newBackend = currentBackend;
                    MARK_ALL_BACKENDS_CHANGED();
                }

                auto majorFsrVersion = currentFeature->Version().major;

                if (majorFsrVersion >= 4)
                {
                    ImGui::Spacing();

                    // Colorspaces
                    const char* colorSpaces[] = { Neurotic::UiLiteral("ingame.provider.d1ace1c70fa9", "Linear (Default)"), Neurotic::UiLiteral("ingame.option.ee8eec8539d4", "Non-Linear"), Neurotic::UiLiteral("ingame.menu-common.non_linear_srgb_c191bc4e", "Non-Linear sRGB"),
                                                  Neurotic::UiLiteral("ingame.menu-common.non_linear_pq_d17e7845", "Non-Linear PQ") };
                    int currentColorSpace = 0;
                    if (config->FsrNonLinearPQ.value_or_default())
                        currentColorSpace = 3;
                    else if (config->FsrNonLinearSRGB.value_or_default())
                        currentColorSpace = 2;
                    else if (config->FsrNonLinearColorSpace.value_or_default())
                        currentColorSpace = 1;

                    ImGui::SetNextItemWidth(150.0f * menuResScale);
                    if (ImGui::Combo(Neurotic::UiLiteral("ingame.menu-common.input_color_space_87415b13", "Input Color Space"), &currentColorSpace, colorSpaces, IM_ARRAYSIZE(colorSpaces)))
                    {
                        bool isSrgb = (currentColorSpace == 2);
                        bool isPq = (currentColorSpace == 3);

                        config->FsrNonLinearSRGB = isSrgb;
                        config->FsrNonLinearPQ = isPq;

                        if (isSrgb || isPq)
                        {
                            config->FsrNonLinearColorSpace.set_volatile_value(true);
                        }
                        else if (currentColorSpace == 1) // Just non-Linear
                        {
                            config->FsrNonLinearColorSpace = true;
                        }
                        else // Linear
                        {
                            config->FsrNonLinearColorSpace = false;
                        }

                        state.newBackend = currentBackend;
                        MARK_ALL_BACKENDS_CHANGED();
                    }
                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.select_the_input_color_space_that_the_game_uses__caa897a7", "Select the input color space that the game uses.\n"
                                   "Non-Linear / sRGB: Might improve FSR4 upscaling quality, might increase ghosting.\n"
                                   "PQ: Rarest, might increase ghosting and break lights."));

                    // FSR 4 Presets
                    const char* presets[] = { Neurotic::UiLiteral("ingame.menu-common.default_92fe477b", "Default"),  Neurotic::UiLiteral("ingame.provider.ab226fcf34fb", "Preset 0"), Neurotic::UiLiteral("ingame.provider.d1ced7405198", "Preset 1"), Neurotic::UiLiteral("ingame.provider.c475c1784510", "Preset 2"),
                                              Neurotic::UiLiteral("ingame.provider.e7aebbc38aae", "Preset 3"), Neurotic::UiLiteral("ingame.provider.19a6c035a739", "Preset 4"), Neurotic::UiLiteral("ingame.provider.1aa0e96c21b2", "Preset 5") };
                    int currentPresetIdx = config->Fsr4Preset.has_value() ? config->Fsr4Preset.value() + 1 : 0;

                    if (currentPresetIdx < 0 || currentPresetIdx >= IM_ARRAYSIZE(presets))
                        currentPresetIdx = 0;

                    ImGui::SetNextItemWidth(150.0f * menuResScale);
                    if (ImGui::Combo(Neurotic::UiLiteral("ingame.menu-common.fsr4_preset_b58f58b6", "FSR4 Preset"), &currentPresetIdx, presets, IM_ARRAYSIZE(presets)))
                    {
                        if (currentPresetIdx == 0)
                            config->Fsr4Preset.reset();
                        else
                            config->Fsr4Preset = currentPresetIdx - 1;

                        state.newBackend = currentBackend;
                        MARK_ALL_BACKENDS_CHANGED();
                    }
                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.each_internal_fsr4_preset_is_tuned_for_a_specifi_afaa6767", "Each internal FSR4 preset is tuned for a specific resolution.\n"
                                   "Selecting an FSR4 preset won't change the in-game\nupscaler preset!!!\n\n"
                                   "Preset 0 is meant for FSR Native AA\n"
                                   "Preset 1 is meant for Quality/Ultra Quality\n"
                                   "Preset 2 is meant for Balanced\n"
                                   "Preset 3 is meant for Performance\n"
                                   "Preset 4 is meant for DRS\n"
                                   "Preset 5 is meant for Ultra Performance"));

                    // Display the active preset right next to the combo box instead of using a table
                    ImGui::SameLine();
                    if (state.currentFsr4Preset.has_value())
                        ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.active_d_3dbc0593", "(Active: %d)"), state.currentFsr4Preset.value());
                    else if (FSR4ModelSelection::IsInt8FsrHooked())
                        ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), Neurotic::UiLiteral("ingame.menu-common.potential_fsr3_fallback_50e189f9", "(Potential FSR3 fallback)"));
                    else
                        ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.failed_to_hook_379d5871", "(Failed to hook)"));
                }

                if (majorFsrVersion >= 3)
                {
                    ImGui::Spacing();

                    bool debugView = config->FsrDebugView.value_or_default();
                    if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.upscaler_debug_view_c93fb4e4", "Upscaler Debug View"), &debugView))
                    {
                        config->FsrDebugView = debugView;

                        // FSR 4's debug view requires backend reinit
                        if (majorFsrVersion > 3)
                        {
                            state.newBackend = currentBackend;
                            MARK_ALL_BACKENDS_CHANGED();
                        }
                    }

                    if (majorFsrVersion > 3)
                    {
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.top_left_dilated_motion_vectors_top_right_predic_dd73d9f8", "Top left: Dilated Motion Vectors\n"
                                       "Top right: Predicted Blend Factor"));
                    }
                    else
                    {
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.top_left_dilated_motion_vectors_top_middle_prote_0aedef69", "Top left: Dilated Motion Vectors\n"
                                       "Top middle: Protected Areas\n"
                                       "Top right: Dilated Depth\n"
                                       "Middle: Upscaled frame\n"
                                       "Bottom left: Disocclusion mask\n"
                                       "Bottom middle: Reactiveness\n"
                                       "Bottom right: Detail Protection Takedown"));
                    }

                    if (majorFsrVersion > 3)
                    {
                        ImGui::SameLine(0.0f, 20.0f * menuResScale);
                        bool fsr4wm = config->Fsr4EnableWatermark.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.watermark_712d9e67", "Watermark"), &fsr4wm))
                        {
                            LOG_DEBUG("FSR4 Watermark set to {}", fsr4wm);
                            config->Fsr4EnableWatermark = fsr4wm;
                        }

                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.after_changing_this_option_please_save_settings__5325c92b", "After changing this option, please Save Settings.\n"
                                       "It will be applied on next launch."));
                    }
                }

                if (currentFeature->Version() >= feature_version { 3, 1, 1 } &&
                    currentFeature->Version() < feature_version { 4, 0, 0 })
                {
                    ImGui::Spacing();

                    if (currentFeature != nullptr)
                    {
                        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.fsr_3_1_presets_837b6af7", "FSR 3.1 Presets:"));

                        ImGui::SameLine(0.0f, 6.0f);

                        // This will be applied by default
                        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.stability_048bc449", "Stability")))
                        {
                            auto const scaleRatioX =
                                (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth();
                            auto const scaleRatioY =
                                (float) currentFeature->TargetHeight() / (float) currentFeature->RenderHeight();
                            auto const scaleRatio = std::max(scaleRatioX, scaleRatioY);

                            config->FsrVelocity = 0.5f;
                            config->FsrReactiveScale = 0.25f;

                            config->FsrShadingScale.reset();
                            config->FsrAccAddPerFrame.reset();
                            config->FsrMinDisOccAcc.reset();
                            config->FsrShadingScale.set_volatile_value(0.5f / scaleRatio);
                            config->FsrAccAddPerFrame.set_volatile_value(scaleRatio / 10.0f);
                            config->FsrMinDisOccAcc.set_volatile_value(scaleRatio / 20.0f);
                        }

                        ImGui::SameLine(0.0f, 6.0f);

                        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.motion_6cf3bc87", "Motion")))
                        {
                            auto const scaleRatioX =
                                (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth();
                            auto const scaleRatioY =
                                (float) currentFeature->TargetHeight() / (float) currentFeature->RenderHeight();
                            auto const scaleRatio = std::max(scaleRatioX, scaleRatioY);

                            config->FsrVelocity = 1.0f;
                            config->FsrReactiveScale = 0.5f;

                            config->FsrShadingScale.reset();
                            config->FsrAccAddPerFrame.reset();
                            config->FsrMinDisOccAcc.reset();
                            config->FsrShadingScale.set_volatile_value(1.0f / scaleRatio);
                            config->FsrAccAddPerFrame.set_volatile_value(scaleRatio / 10.0f);
                            config->FsrMinDisOccAcc.set_volatile_value(scaleRatio / 20.0f);
                        }

                        ImGui::SameLine(0.0f, 6.0f);

                        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.default_92fe477b", "Default")))
                        {
                            config->FsrVelocity = 1.0f;
                            config->FsrReactiveScale = 1.0f;
                            config->FsrShadingScale = 1.0f;
                            config->FsrAccAddPerFrame = 0.333f;
                            config->FsrMinDisOccAcc = -0.333f;
                        }
                    }

                    ImGui::Spacing();

                    if (auto ch = ScopedCollapsingHeader(Neurotic::UiLiteral("ingame.menu-common.fsr_3_upscaler_manual_tuning_e29233d3", "FSR 3 Upscaler Manual Tuning")); ch.IsHeaderOpen())
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();
                        ImGui::Spacing();

                        ImGui::PushItemWidth(220.0f * menuResScale);

                        float velocity = config->FsrVelocity.value_or_default();
                        if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.velocity_factor_666fde36", "Velocity Factor"), &velocity, 0.00f, 1.0f, "%.2f"))
                            config->FsrVelocity = velocity;
                        if (ResetSliderSetting("FsrVelocity", config->FsrVelocity)) ReInitUpscaler();

                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.value_of_0_0f_can_improve_temporal_stability_of__1a01c905", "Value of 0.0f can improve temporal stability of bright pixels\n"
                                       "Lower values are more stable with ghosting\n"
                                       "Higher values are more pixelly, but less ghosting"));

                        if (currentFeature->Version() >= feature_version { 3, 1, 4 })
                        {
                            // Reactive Scale
                            float reactiveScale = config->FsrReactiveScale.value_or_default();
                            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.reactive_scale_dfa22762", "Reactive Scale"), &reactiveScale, 0.0f, 1.0f, "%.3f"))
                                config->FsrReactiveScale = reactiveScale;
                            if (ResetSliderSetting("FsrReactiveScale", config->FsrReactiveScale)) ReInitUpscaler();

                            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.meant_for_development_purpose_to_test_if_writing_13955b5e", "Meant for development purpose to test if\n"
                                           "writing a larger value to reactive mask, reduces ghosting."));

                            // Shading Scale
                            float shadingScale = config->FsrShadingScale.value_or_default();
                            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.shading_scale_ea027aac", "Shading Scale"), &shadingScale, 0.0f, 1.0f, "%.3f"))
                                config->FsrShadingScale = shadingScale;
                            if (ResetSliderSetting("FsrShadingScale", config->FsrShadingScale)) ReInitUpscaler();

                            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.increasing_this_scales_fsr3_1_computed_shading_c_13ab77d6", "Increasing this scales FSR3.1 computed shading\n"
                                           "change value at read to have higher reactiveness."));

                            // Accumulation Added Per Frame
                            float accAddPerFrame = config->FsrAccAddPerFrame.value_or_default();
                            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.acc_added_per_frame_0bcddeb5", "Acc. Added Per Frame"), &accAddPerFrame, 0.0f, 1.0f, "%.3f"))
                                config->FsrAccAddPerFrame = accAddPerFrame;
                            if (ResetSliderSetting("FsrAccAddPerFrame", config->FsrAccAddPerFrame)) ReInitUpscaler();

                            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.corresponds_to_amount_of_accumulation_added_per__a71e0ab2", "Corresponds to amount of accumulation added per frame\n"
                                           "at pixel coordinate where disocclusion occured or when\n"
                                           "reactive mask value is > 0.0f. Decreasing this and \n"
                                           "drawing the ghosting object (IE no mv) to reactive mask \n"
                                           "with value close to 1.0f can decrease temporal ghosting.\n"
                                           "Decreasing this could result in more thin feature pixels flickering."));

                            // Min Disocclusion Accumulation
                            float minDisOccAcc = config->FsrMinDisOccAcc.value_or_default();
                            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.min_disocclusion_acc_b05c9919", "Min. Disocclusion Acc."), &minDisOccAcc, -1.0f, 1.0f, "%.3f"))
                                config->FsrMinDisOccAcc = minDisOccAcc;
                            if (ResetSliderSetting("FsrMinDisOccAcc", config->FsrMinDisOccAcc)) ReInitUpscaler();

                            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.increasing_this_value_may_reduce_white_pixel_tem_2cd32129", "Increasing this value may reduce white pixel temporal\n"
                                           "flickering around swaying thin objects that are disoccluding \n"
                                           "one another often. Too high value may increase ghosting."));
                        }

                        ImGui::PopItemWidth();

                        ImGui::Spacing();
                        ImGui::Spacing();
                    }
                }
            }
        }

        // DLSS -----------------
        if (ctx.childPage == 1 && ((config->DLSSEnabled.value_or_default() && currentBackend == Upscaler::DLSS &&
             currentFeature->Version().major > 2) ||
            usesDlssd))
        {

            auto overridden =
                usesDlssd ? state.dlssdPresetsOverriddenExternally : state.dlssPresetsOverriddenExternally;

            if (overridden)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), Neurotic::UiLiteral("ingame.menu-common.external_preset_override_observed_effective_mode_4e9edf9f", "External preset override observed; effective model is not confirmed"));
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.this_usually_happens_due_to_using_tools_such_as__996244b2", "This usually happens due to using tools\n"
                               "such as Nvidia App or Nvidia Inspector"));
                // ImGui::Text("Selecting setting below will disable that external override\n"
                //             "but you need to Save Settings and restart the game");

                ImGui::Spacing();
            }

            if (usesDlssd)
            {
                if (bool pOverride = config->DLSSDRenderPresetOverride.value_or_default();
                    ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.render_presets_override_7320c973", "Render Presets Override"), &pOverride))
                    config->DLSSDRenderPresetOverride = pOverride;

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.each_render_preset_has_it_strengths_and_weakness_63f97423", "Each render preset has it strengths and weaknesses\n"
                               "Override to potentially improve image quality\n"
                               "Press apply after enable/disable"));

                /*
                auto currentPresetIndex = GetPresetIndex(currentFeature, true);

                if (currentPresetIndex == 0)
                    ImGui::Text("Current Preset: Default");
                else
                    ImGui::Text("Current Preset: %c", 64 + currentPresetIndex);
                */

                ImGui::BeginDisabled(!config->DLSSDRenderPresetOverride.value_or_default() /*|| overridden*/);
                ImGui::PushItemWidth(135.0f * menuResScale);

                AddDLSSDRenderPreset(Neurotic::UiLiteral("ingame.menu-common.override_preset_2e633856", "Override Preset"), &comboPreset);

                ImGui::PopItemWidth();
                ImGui::EndDisabled();
            }
            else
            {
                if (bool pOverride = config->RenderPresetOverride.value_or_default();
                    ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.render_presets_override_7320c973", "Render Presets Override"), &pOverride))
                    config->RenderPresetOverride = pOverride;

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.each_render_preset_has_it_strengths_and_weakness_590dfb9d", "Each render preset has it strengths and weaknesses\n"
                               "Override to potentially improve image quality\n"
                               "Press Apply after enable/disable"));

                /*
                auto currentPresetIndex = GetPresetIndex(currentFeature, false);

                if (currentPresetIndex == 0)
                    ImGui::Text("Current Preset: Default");
                else
                    ImGui::Text("Current Preset: %c", 64 + currentPresetIndex);
                */

                ImGui::BeginDisabled(!config->RenderPresetOverride.value_or_default() /*|| overridden*/);

                ImGui::PushItemWidth(135.0f * menuResScale);

                AddDLSSRenderPreset(Neurotic::UiLiteral("ingame.menu-common.override_preset_2e633856", "Override Preset"), &comboPreset);

                ImGui::PopItemWidth();
                ImGui::EndDisabled();
            }

            ImGui::SameLine(0.0f, 6.0f);

            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.apply_changes_287d89d1", "Apply Changes")))
            {
                LOG_DEBUG("Applying DLSS/DLSSD preset override changes, preset index: {}",
                          comboPreset.value_or_default());

                if (usesDlssd)
                {
                    config->DLSSDRenderPresetForAll = static_cast<const std::optional<uint32_t>&>(comboPreset);
                    state.newBackend = Upscaler::DLSSD;
                }
                else
                {
                    config->RenderPresetForAll = comboPreset.value_or_default();
                    state.newBackend = currentBackend;
                }

                MARK_ALL_BACKENDS_CHANGED();
            }

            ImGui::Spacing();

            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.requested_presets_depend_on_the_installed_model__b0463937", "Requested presets depend on the installed model. A selection alone does not confirm the effective model."));
            ImGui::SeparatorText(usesDlssd ? Neurotic::UiLiteral("ingame.menu-common.advanced_dlssd_settings_a0954523", "Advanced DLSSD Settings") : Neurotic::UiLiteral("ingame.menu-common.advanced_dlss_settings_4cf7ee16", "Advanced DLSS Settings"));
            {
                ImGui::Spacing();

                ImGui::BeginDisabled(!(usesDlssd ? config->DLSSDRenderPresetOverride.value_or_default() : config->RenderPresetOverride.value_or_default()));
                ImGui::Spacing();
                ImGui::PushItemWidth(135.0f * menuResScale);

                if (usesDlssd)
                {
                    AddDLSSDRenderPreset(Neurotic::UiLiteral("ingame.menu-common.dlaa_preset_d1ea72cb", "DLAA Preset"), &config->DLSSDRenderPresetDLAA);
                    AddDLSSDRenderPreset(Neurotic::UiLiteral("ingame.menu-common.ultraq_preset_7c74c188", "UltraQ Preset"), &config->DLSSDRenderPresetUltraQuality);
                    AddDLSSDRenderPreset(Neurotic::UiLiteral("ingame.menu-common.quality_preset_87a3d075", "Quality Preset"), &config->DLSSDRenderPresetQuality);
                    AddDLSSDRenderPreset(Neurotic::UiLiteral("ingame.menu-common.balanced_preset_ae0c358e", "Balanced Preset"), &config->DLSSDRenderPresetBalanced);
                    AddDLSSDRenderPreset(Neurotic::UiLiteral("ingame.menu-common.perf_preset_3d3e1c1f", "Perf Preset"), &config->DLSSDRenderPresetPerformance);
                    AddDLSSDRenderPreset(Neurotic::UiLiteral("ingame.menu-common.ultrap_preset_a9766933", "UltraP Preset"), &config->DLSSDRenderPresetUltraPerformance);
                }
                else
                {
                    AddDLSSRenderPreset(Neurotic::UiLiteral("ingame.menu-common.dlaa_preset_d1ea72cb", "DLAA Preset"), &config->RenderPresetDLAA);
                    AddDLSSRenderPreset(Neurotic::UiLiteral("ingame.menu-common.ultraq_preset_7c74c188", "UltraQ Preset"), &config->RenderPresetUltraQuality);
                    AddDLSSRenderPreset(Neurotic::UiLiteral("ingame.menu-common.quality_preset_87a3d075", "Quality Preset"), &config->RenderPresetQuality);
                    AddDLSSRenderPreset(Neurotic::UiLiteral("ingame.menu-common.balanced_preset_ae0c358e", "Balanced Preset"), &config->RenderPresetBalanced);
                    AddDLSSRenderPreset(Neurotic::UiLiteral("ingame.menu-common.perf_preset_3d3e1c1f", "Perf Preset"), &config->RenderPresetPerformance);
                    AddDLSSRenderPreset(Neurotic::UiLiteral("ingame.menu-common.ultrap_preset_a9766933", "UltraP Preset"), &config->RenderPresetUltraPerformance);
                }
                ImGui::PopItemWidth();
                ImGui::EndDisabled();

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }
}

static float& SharedDynamicFgTarget(Config* config)
{
    static float target = config->FGDLSSGFramerateTargetDMFG.value_or_default();
    return target;
}

void MenuCommon::RenderFrameGenerationSelection(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    /// FG INPUTS

    static std::vector<MenuOption<FGInput>> inputOptions;
    inputOptions.clear();

    // clang-format off

    inputOptions = {
        { FGInput::NoFG, Neurotic::UiLiteral("ingame.menu-common.none_331505ff", "None") },
        { FGInput::Upscaler, "OptiFG (Upscaler)",
            Neurotic::UiLiteral("ingame.menu-common.upscaler_must_be_enabled_can_be_used_with_any_fg_c2036c8c", "Upscaler must be enabled\n\nCan be used with any FG Output, but might be imperfect with some\nTo prevent UI glitching, HUDfix required") },
        { FGInput::DLSSG, Neurotic::UiLiteral("ingame.menu-common.dlssg_via_streamline_86ad3ff8", "DLSSG via Streamline"),
            Neurotic::UiLiteral("ingame.menu-common.can_be_used_with_any_fg_output_requires_enabling_2b0ad9a2", "Can be used with any FG Output\n\nRequires enabling DLSS-FG in game settings\nSupports HUDless out of the box\n\nLimited to games that use Streamline") },
        { FGInput::NvngxFG, Neurotic::UiLiteral("ingame.menu-common.dlssg_via_nvngx_a8052b50", "DLSSG via Nvngx"),
            Neurotic::UiLiteral("ingame.menu-common.limited_to_variants_of_fsr_fg_requires_enabling__b67e10a6", "Limited to variants of FSR FG\n\nRequires enabling DLSS-FG in game settings\nSupports HUDless out of the box\nUses Streamline swapchain for pacing") },
        { FGInput::FSRFG, "FSR 3.1 FG",
            Neurotic::UiLiteral("ingame.menu-common.can_be_used_with_any_fg_output_requires_enabling_8c132ea6", "Can be used with any FG Output\n\nRequires enabling FSR-FG in game settings\nSupports HUDless out of the box") },
        { FGInput::FSRFG30, "FSR 3.0 FG",
            Neurotic::UiLiteral("ingame.menu-common.can_be_used_with_any_fg_output_requires_enabling_8c132ea6", "Can be used with any FG Output\n\nRequires enabling FSR-FG in game settings\nSupports HUDless out of the box") },
        { FGInput::XeFG, Neurotic::UiLiteral("ingame.menu-common.xefg_5c446cf1", "XeFG") }
    };

    // clang-format on

    auto constexpr nvngxInputIndex = (uint32_t) FGInput::NvngxFG;

    // XeFG input requirements
    auto constexpr xefgInputIndex = (uint32_t) FGInput::XeFG;
    inputOptions[xefgInputIndex].set_disabled(true, Neurotic::UiLiteral("ingame.menu-common.support_not_implemented_they_meant_fg_output_1cdccb69", "Support not implemented, they meant FG Output"));

    // OptiFG requirements
    auto constexpr optiFgIndex = (uint32_t) FGInput::Upscaler;
    inputOptions[optiFgIndex].set_disabled(state.swapchainApi == API::Vulkan, Neurotic::UiLiteral("ingame.menu-common.unsupported_api_15da25ac", "Unsupported API"));

    if (!inputOptions[optiFgIndex].disabled && state.activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::IsFGReady() &&
        !ffxInitTried)
    {
        ffxInitTried = true;
        FfxApiProxy::InitFfxDx12();
        inputOptions[optiFgIndex].set_disabled(!FfxApiProxy::IsFGReady(), Neurotic::UiLiteral("ingame.menu-common.amd_fidelityfx_dx12_dll_is_missing_53688df2", "amd_fidelityfx_dx12.dll is missing"));
    }
    else if (!inputOptions[optiFgIndex].disabled && state.activeFgOutput == FGOutput::XeFG && !xefgInitTried &&
             XeFGProxy::Module() == nullptr)
    {
        xefgInitTried = true;
        XeFGProxy::InitXeFG();
        inputOptions[optiFgIndex].set_disabled(XeFGProxy::Module() == nullptr, Neurotic::UiLiteral("ingame.menu-common.libxess_fg_dll_is_missing_dc129e85", "libxess_fg.dll is missing"));
    }

    // DLSSG inputs requirements
    auto constexpr dlssgInputIndex = (uint32_t) FGInput::DLSSG;
    // inputOptions[dlssgInputIndex].set_disabled(state.streamlineVersion.major == 0, "Game doesn't use streamline");
    inputOptions[dlssgInputIndex].set_disabled(state.swapchainApi == API::DX11, Neurotic::UiLiteral("ingame.menu-common.unsupported_api_15da25ac", "Unsupported API"));

    // FSRFG inputs requirements
    auto constexpr fsrfgInputIndex = (uint32_t) FGInput::FSRFG;
    inputOptions[fsrfgInputIndex].set_disabled(state.swapchainApi != API::DX12, Neurotic::UiLiteral("ingame.menu-common.unsupported_api_15da25ac", "Unsupported API"));

    // FSRFG30 inputs requirements
    auto constexpr fsrfg30InputIndex = (uint32_t) FGInput::FSRFG30;
    inputOptions[fsrfg30InputIndex].set_disabled(state.swapchainApi != API::DX12, Neurotic::UiLiteral("ingame.menu-common.unsupported_api_15da25ac", "Unsupported API"));


    /// FG OUTPUTS

    static std::vector<MenuOption<FGOutput>> outputOptions;
    outputOptions.clear();

    // clang-format off

    outputOptions = {
        { FGOutput::NoFG, Neurotic::UiLiteral("ingame.menu-common.none_331505ff", "None") },
        { FGOutput::FSRFG, Neurotic::UiLiteral("ingame.menu-common.fsr_fg_745644b6", "FSR FG"), Neurotic::UiLiteral("ingame.menu-common.fsr3_4_fg_rdna4_autoupgrades_to_fsr4_fg_fsr4_fg__8c1196f4", "FSR3/4-FG, RDNA4 autoupgrades to FSR4-FG\n\nFSR4-FG sometimes better/worse than XeFG") },
        { FGOutput::DLSSG, Neurotic::UiLiteral("ingame.menu-common.dlssg_86366b33", "DLSSG"), Neurotic::UiLiteral("ingame.menu-common.dlssg_output_can_be_used_in_conjuction_with_nuke_98d7df34", "DLSSG output\ncan be used in conjuction with Nukem's for example") },
        { FGOutput::XeFG, Neurotic::UiLiteral("ingame.menu-common.xefg_5c446cf1", "XeFG"), Neurotic::UiLiteral("ingame.menu-common.xefg_heaviest_but_best_universal_fg_xefg_3_overa_a4f1300b", "XeFG - heaviest, but best universal FG\n\nXeFG 3 overall deals best with HUD\n\nEnable UI Composition if HUD ghosting") },
    };

    // clang-format on

    // DLSSG output requirements
    auto constexpr dlssgOutputIndex = (uint32_t) FGOutput::DLSSG;
    const bool supportsDlssg = primaryGpu.nvidiaArchInfo.architecture_id >= NV_GPU_ARCHITECTURE_AD100;
    const bool hasDlssgReplacement =
        state.nukemsFgFileAvailable || state.artursFgFileAvailable || FfxApiProxy::IsFGReady(false);

    if (!supportsDlssg && hasDlssgReplacement)
    {
        outputOptions[dlssgOutputIndex].tooltip =
            Neurotic::UiLiteral("ingame.menu-common.no_real_dlssg_unsupported_hardware_only_nvngx_fg_82b1375e", "No real DLSSG, unsupported hardware\nOnly Nvngx FG replacements available");
    }

    outputOptions[dlssgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, Neurotic::UiLiteral("ingame.menu-common.unsupported_api_15da25ac", "Unsupported API"));
    outputOptions[dlssgOutputIndex].set_disabled(!supportsDlssg && !hasDlssgReplacement,
                                                 Neurotic::UiLiteral("ingame.menu-common.unsupported_hardware_and_no_replacements_4bde7c41", "Unsupported hardware and no replacements"));

    // For that one case of DX11 DLSSG
    const auto streamlineVersion = state.streamlineVersion;
    const bool nukemsUnsupportedApi =
        state.swapchainApi == API::DX11 &&
        (streamlineVersion == feature_version { 0, 0, 0 } || streamlineVersion > feature_version { 2, 0, 1 });
    inputOptions[nvngxInputIndex].set_disabled(nukemsUnsupportedApi, Neurotic::UiLiteral("ingame.menu-common.unsupported_api_15da25ac", "Unsupported API"));

    // FSR FG output requirements
    auto constexpr fsrfgOutputIndex = (uint32_t) FGOutput::FSRFG;
    outputOptions[fsrfgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, Neurotic::UiLiteral("ingame.menu-common.unsupported_api_15da25ac", "Unsupported API"));

    // XeFG output requirements
    auto constexpr xefgOutputIndex = (uint32_t) FGOutput::XeFG;
    outputOptions[xefgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, Neurotic::UiLiteral("ingame.menu-common.unsupported_api_15da25ac", "Unsupported API"));
    // Rendering projects availability; only explicit selections change intent.
    // The runtime owner remains responsible for admitting/refusing a route.
    /// FG NVNGX REPLACEMENT

    static std::vector<MenuOption<FGNvngxReplacement>> nvngxOptions;
    nvngxOptions.clear();

    // clang-format off

    nvngxOptions = {
        { FGNvngxReplacement::None, Neurotic::UiLiteral("ingame.menu-common.none_real_dlssg_dca9af37", "None (Real DLSSG)"), Neurotic::UiLiteral("ingame.menu-common.real_dlssg_for_rtx_40xx_and_above_32d8faec", "Real DLSSG, For RTX 40xx and above")},
        { FGNvngxReplacement::Nukems, "Nukem's", "FSR 3 FG" },
        { FGNvngxReplacement::Arturs, "Enabler", "FSR 3 MFG" },
        { FGNvngxReplacement::FFX, "FSR 3/4 FG", Neurotic::UiLiteral("ingame.menu-common.fsr_3_4_fg_using_the_ffx_3c5925bf", "FSR 3/4 FG using the FFX") },
        { FGNvngxReplacement::Combo, "FFX + Enabler", Neurotic::UiLiteral("ingame.menu-common.ffx_for_the_middle_fake_frame_enabler_for_the_re_b58e18b8", "FFX for the middle fake frame, Enabler for the rest\n\n"
                                                      "2x - FFX\n3x - Enabler\n4x - FFX + Enabler\n5x - Enabler\n6x - FFX + Enabler") },
    };

    // clang-format on

    bool replaceFgOutputWithNvngx = false;
    bool showNvngxFgDowndown = false;

    if (config->FGInput == FGInput::NvngxFG)
    {
        replaceFgOutputWithNvngx = true;
    }
    else if (config->FGOutput == FGOutput::DLSSG)
    {
        showNvngxFgDowndown = true;
    }

    auto constexpr fgNvngxNoneIndex = (uint32_t) FGNvngxReplacement::None;
    nvngxOptions[fgNvngxNoneIndex].set_disabled(!supportsDlssg, Neurotic::UiLiteral("ingame.menu-common.unsupported_hardware_5aca3fa6", "Unsupported hardware"));

    if (replaceFgOutputWithNvngx)
    {
        nvngxOptions[fgNvngxNoneIndex].label = Neurotic::UiLiteral("ingame.menu-common.none_331505ff", "None");
        nvngxOptions[fgNvngxNoneIndex].set_hidden(true);
    }

    auto constexpr fgNvngxNukemsIndex = (uint32_t) FGNvngxReplacement::Nukems;
    nvngxOptions[fgNvngxNukemsIndex].set_disabled(!state.nukemsFgFileAvailable,
                                                  Neurotic::UiLiteral("ingame.menu-common.missing_dlssg_to_fsr3_amd_is_better_dll_3df9d712", "Missing dlssg_to_fsr3_amd_is_better.dll"));

    auto constexpr fgNvngxArtursIndex = (uint32_t) FGNvngxReplacement::Arturs;
    nvngxOptions[fgNvngxArtursIndex].set_disabled(!state.artursFgFileAvailable, Neurotic::UiLiteral("ingame.menu-common.missing_dlss_enabler_headless_dll_0b67bf2b", "Missing dlss-enabler-headless.dll"));

    auto constexpr fgNvngxFfxIndex = (uint32_t) FGNvngxReplacement::FFX;
    nvngxOptions[fgNvngxFfxIndex].set_disabled(!FfxApiProxy::IsFGReady(false),
                                               Neurotic::UiLiteral("ingame.menu-common.missing_amd_fidelityfx_framegeneration_dx12_dll_13e5746e", "Missing amd_fidelityfx_framegeneration_dx12.dll"));

    auto constexpr fgNvngxComboIndex = (uint32_t) FGNvngxReplacement::Combo;
    nvngxOptions[fgNvngxComboIndex].set_disabled(
        !FfxApiProxy::IsFGReady(false) || !state.artursFgFileAvailable,
        Neurotic::UiLiteral("ingame.menu-common.missing_amd_fidelityfx_framegeneration_dx12_dll__6bc8060e", "Missing amd_fidelityfx_framegeneration_dx12.dll\nor missing dlss-enabler-headless.dll"));

    // TODO: Automatically switch to any other option


    const bool nativeSupported = state.swapchainApi == API::DX12 &&
        primaryGpu.nvidiaArchInfo.architecture_id >= NV_GPU_ARCHITECTURE_AD100 &&
        primaryGpu.nvidiaArchInfo.architecture_id < NV_GPU_ARCHITECTURE_GB200;
    bool nativeEnabled = config->FGDLSSGNativeMfgExperimental.value_or_default();
    bool nativeSelected = nativeSupported && nativeEnabled;
    if (ctx.childPage == 0)
    {
    const bool nativeRoutePending = state.activeFgInput != FGInput::NoFG ||
        state.activeFgOutput != FGOutput::NoFG || state.activeFgNvngx != FGNvngxReplacement::None;
    const auto nativePublication = Neurotic::Mfg::AdaMfgSnapshot();
    const auto nativeRefusal = StreamlineHooks::mfgHighRatioRefusal();
    auto nativeSetup = Neurotic::Mfg::ResolveMfgSetup({nativeSupported, nativeSelected,
        config->FGDLSSGNativeMfgAtStartup, nativeRoutePending,
        nativePublication.status, nativePublication.reason});
    std::function<void()> renderRatioStatus;

    namespace EM = Neurotic::Mfg::Experimental;
    const auto experimental = EM::ReadSnapshot();
    std::optional<Neurotic::Sleek::ContentCard> nativeCard;
    nativeCard.emplace("##NativeMfgOutput", Neurotic::UiLiteral("ingame.menu-common.output_controls_1038c28f", "Output controls"));
    const bool dlssgInputOrOutput =
        state.activeFgOutput == FGOutput::DLSSG || state.activeFgInput == FGInput::DLSSG;

    ImGui::BeginDisabled(state.dlssgGameDMFGSupported && config->FGDLSSGOverrideForceDMFG.value_or_default());
    const bool nativeMfgSelected = nativeSelected;
    const bool experimentalSelected = experimental.family != EM::Family::None && experimental.requested;
    const auto experimentalLimit = EM::SelectableCeiling(experimental.prepared, experimental.ceiling);
    if ((nativeMfgSelected || experimentalSelected || (state.dlssgMfgMax.has_value() && state.dlssgMfgMax.value() >= 1)) &&
        (!dlssgInputOrOutput || nativeMfgSelected))
    {
        const auto publication = Neurotic::Mfg::AdaMfgSnapshot();
        const auto nativeLimit = publication.status == Neurotic::Mfg::MfgRuntimeStatus::Published ?
            publication.maxGenerated : 0u;
        const auto maxInterpolationCount = std::clamp(experimentalSelected ? static_cast<int>(experimentalLimit) : nativeMfgSelected ? static_cast<int>(Neurotic::Mfg::MfgSelectableGeneratedMax(
                nativeSetup == Neurotic::Mfg::MfgSetupStatus::Ready, nativeLimit, nativeRefusal.blocked)) :
            static_cast<int>(state.dlssgMfgMax.value()), 0, 5);

        if (maxInterpolationCount >= 1 || experimentalSelected)
        {
            const char* intModes[] = { Neurotic::UiLiteral("ingame.provider.5c412a262086", "Game"), Neurotic::UiLiteral("ingame.objectruleeditor.off_dc516be5", "Off"), "2X", "3X", "4X", "5X", "6X" };

            // Map config value to UI index
            int currentSet = 0;
            if (config->FGDLSSGOverrideInterpolationCount.has_value())
            {
                currentSet = config->FGDLSSGOverrideInterpolationCount.value() + 1;
            }

            const char* currentIntCount = currentSet >= 0 && currentSet < 7 ?
                intModes[currentSet] : Neurotic::UiLiteral("ingame.menu-common.unsupported_saved_value_765cef70", "Unsupported saved value");

            ImGui::PushItemWidth(95.0f * menuResScale);

            ImGui::BeginDisabled(nativeMfgSelected && nativeRoutePending);
            if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.override_dlssg_ratio_443833f5", "Override DLSSG Ratio"), currentIntCount))
            {
                for (int i = 0; i <= (nativeMfgSelected || experimentalSelected ? 6 : maxInterpolationCount + 1); i++)
                {
                    ImGui::BeginDisabled(i > maxInterpolationCount + 1);
                    if (ImGui::Selectable(intModes[i], (currentSet == i)))
                    {
                        if (i == 0)
                        {
                            // Default, no override
                            config->FGDLSSGOverrideInterpolationCount.reset();
                        }
                        else
                        {
                            // UI index, store value
                            int framesToGenerate = i - 1;

                            LOG_DEBUG("DLSSG Interpolation Count set to: {}", framesToGenerate);
                            config->FGDLSSGOverrideInterpolationCount = framesToGenerate;
                        }

                        StreamlineHooks::updateDlssgOptions();
                    }
                    ImGui::EndDisabled();
                }

                ImGui::EndCombo();
            }

            ImGui::EndDisabled();
            ImGui::PopItemWidth();
            renderRatioStatus = [&, maxInterpolationCount, nativeMfgSelected]() {
            if (state.swapchainApi == API::Vulkan)
            {
                const auto selected = config->FGDLSSGOverrideInterpolationCount.has_value() ?
                    Neurotic::Mfg::SelectionFromStoredGenerated(config->FGDLSSGOverrideInterpolationCount.value()) :
                    Neurotic::Mfg::MfgSelection::Game;
                const auto request = StreamlineHooks::mfgRequestReceipt();
                if (selected == Neurotic::Mfg::MfgSelection::Game)
                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.game_controls_frame_generation_7842ef02", "Game controls Frame Generation."));
                else if (!request.attempt || request.stale || request.selected != selected || !request.setterResult.has_value())
                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.waiting_for_the_game_s_next_fg_settings_update_4b8bfee0", "Waiting for the game's next FG settings update."));
                else if (!request.accepted)
                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.game_fg_override_rejected_code_d_1c486c7d", "Game FG override rejected (code %d)."), request.setterResult.value());
                else if (selected == Neurotic::Mfg::MfgSelection::Off && !request.forwardedEnabled)
                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.game_fg_override_off_accepted_7dc3de45", "Game FG override: Off (accepted)."));
                else
                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.fg_settings_accepted_opening_this_menu_keeps_gen_3aba4622", "FG settings accepted. Opening this menu keeps generation enabled."));
            }
            if (nativeMfgSelected && config->FGDLSSGOverrideInterpolationCount.has_value())
            {
                const int selected = config->FGDLSSGOverrideInterpolationCount.value();
                if (selected > maxInterpolationCount)
                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.selected_ratio_is_unavailable_passing_through_th_089f86c0", "Selected ratio is unavailable; passing through the game's setting."));
                else if (selected > 1)
                {
                    const auto publication = Neurotic::Mfg::AdaMfgSnapshot();
                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.mfg_s_cae71a08", "MFG: %s"),Neurotic::Translate(Neurotic::Mfg::MfgRuntimeReasonName(publication.reason)).c_str());
                    if (publication.status == Neurotic::Mfg::MfgRuntimeStatus::Published)
                        ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.mfg_unlock_applied_displayed_frames_unverified_6b6f129d", "MFG unlock applied; displayed frames unverified."));
                    else if (publication.status == Neurotic::Mfg::MfgRuntimeStatus::Indeterminate)
                        ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.mfg_publication_uncertain_high_ratio_blocked_for_060b6de1", "MFG publication uncertain; high ratio blocked for this provider."));
                    else if (publication.status == Neurotic::Mfg::MfgRuntimeStatus::Refused)
                        ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.provider_profile_refused_game_setting_is_used_9b5597b2", "Provider profile refused; game setting is used."));
                    else if (publication.status == Neurotic::Mfg::MfgRuntimeStatus::Stale)
                        ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.provider_binding_changed_high_ratio_blocked_for__69c8de22", "Provider binding changed; high ratio blocked for this session."));
                    else
                        ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.waiting_for_the_supported_dlss_g_provider_game_s_0e61ab2a", "Waiting for the supported DLSS-G provider; game setting is used."));
                }
                const auto request = StreamlineHooks::mfgRequestReceipt();
                Neurotic::Sleek::DrawMfgRequestReadout(request);
            }
            };
        }
    }

    ImGui::EndDisabled();

    if (renderRatioStatus) renderRatioStatus();

    if (state.dlssgGameDMFGSupported && !dlssgInputOrOutput)
    {
        ImGui::BeginDisabled(nativeSelected);

        if (bool dynamicMFG = config->FGDLSSGOverrideForceDMFG.value_or_default();
            ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.force_dynamic_mfg_7dd3df71", "Force Dynamic MFG"), &dynamicMFG))
        {
            config->FGDLSSGOverrideForceDMFG = dynamicMFG;
            StreamlineHooks::updateDlssgOptions();
        }

        ImGui::BeginDisabled(state.dlssgLastSetMode != sl::DLSSGMode::eDynamic);
        float& fpsTarget = SharedDynamicFgTarget(config);
        ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.dmfg_fps_target_12d65e45", "DMFG FPS Target"), &fpsTarget, 0, 200, "%.0f");

        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.an_active_limit_of_0_means_auto_detect_the_displ_a9a85250", "An active limit of 0 means auto-detect the display refresh rate"));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.apply_target_c8b65a23", "Apply Target")))
        {
            config->FGDLSSGFramerateTargetDMFG = fpsTarget;
            StreamlineHooks::updateDlssgOptions();
        }

        ImGui::SameLine(0.0f, 16.0f);

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_target_ecdec9b0", "Reset Target")))
        {
            fpsTarget = 0.0f;
            config->FGDLSSGFramerateTargetDMFG.reset();
        }

        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }
    nativeCard.reset();
    nativeCard.emplace("##NativeMfgCompatibility");
    const auto compatibilityHeader = ImGui::GetCursorScreenPos();
    // Draw again after edits, at the same reserved location, so warnings update immediately.
    Neurotic::Sleek::DrawMfgRestartWarning(false);
    bool rtx30 = config->FGDLSSGExperimentalUnlockRTX30.value_or_default();
    bool rtx20 = config->FGDLSSGExperimentalUnlockRTX20.value_or_default();
    const auto compatibilityChanged = Neurotic::Sleek::ExperimentalMfgControls(nativeEnabled, rtx30, rtx20, menuResScale);
    const bool nativeChanged = (compatibilityChanged & 1u) != 0;
    if (compatibilityChanged & 2u) config->FGDLSSGExperimentalUnlockRTX30 = rtx30;
    if (compatibilityChanged & 4u) config->FGDLSSGExperimentalUnlockRTX20 = rtx20;
    if (nativeChanged) Neurotic::Mfg::SetNativeMfgSelected(*config, nativeEnabled);
    nativeSelected = nativeSupported && nativeEnabled;
    const bool conflictingNativeIntent = config->FGInput.value_or_default() != FGInput::NoFG ||
        config->FGOutput.value_or_default() != FGOutput::NoFG || config->FGNvngxReplacement.value_or_default() != FGNvngxReplacement::None ||
        config->ForceXeLL.value_or_default() || config->FGDLSSGOverrideForceDMFG.value_or_default();
    bool applyNativeRoute = nativeChanged && nativeSelected;
    if (nativeSelected && conflictingNativeIntent && !applyNativeRoute) {
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.saved_replacement_settings_conflict_with_native__2bf46063", "Saved replacement settings conflict with native MFG. Choose the native route or turn its unlock off."));
        applyNativeRoute = ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.use_native_fg_route_c7fe0495", "Use native FG route"));
    }
    if (applyNativeRoute)
    {
        // Stage configuration only. Active route/ownership changes at restart.
        config->FGInput = FGInput::NoFG;
        config->FGOutput = FGOutput::NoFG;
        config->FGNvngxReplacement = FGNvngxReplacement::None;
        config->ForceXeLL = false;
        config->FGDLSSGOverrideForceDMFG = false;
    }
    if (nativeChanged && !nativeRoutePending) StreamlineHooks::updateDlssgOptions();
    const Neurotic::Mfg::MfgSetupInput nativeSetupInput {nativeSupported, nativeSelected,
        config->FGDLSSGNativeMfgAtStartup, nativeRoutePending,
        nativePublication.status, nativePublication.reason};
    nativeSetup = Neurotic::Mfg::ResolveMfgSetup(nativeSetupInput);
    const bool nativeRecoveryRequired = nativeSupported && nativeRefusal.blocked;
    const auto controlsEnd = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(compatibilityHeader);
    Neurotic::Sleek::DrawMfgRestartWarning(Neurotic::Sleek::MfgRestartRequired(
        nativeSetupInput, nativeRefusal.blocked, {rtx30,rtx20}, experimental.session));
    ImGui::SetCursorScreenPos(controlsEnd);
    const auto ink = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    std::string nativeStatus;
    if (nativeRecoveryRequired)
        nativeStatus = Neurotic::UiMessage("ingame.menu-common.higher_ratio_rejected_save_and_restart_to_retry__22bb9442", "Higher ratio rejected; save and restart to retry. NR Reset cannot clear it. Game, Off and 2X remain available.");
    else switch (nativeSetup)
    {
    case Neurotic::Mfg::MfgSetupStatus::Unsupported: nativeStatus = Neurotic::UiMessage("ingame.menu-common.rtx_40_native_mfg_requires_a_compatible_gpu_and__0205e499", "RTX 40 native MFG requires a compatible GPU and DirectX 12."); break;
    case Neurotic::Mfg::MfgSetupStatus::Restart: nativeStatus = Neurotic::UiMessage("ingame.menu-common.save_settings_and_restart_to_apply_the_native_fg_cecd4eb1", "Save settings and restart to apply the native FG route."); break;
    case Neurotic::Mfg::MfgSetupStatus::Waiting:
    case Neurotic::Mfg::MfgSetupStatus::Blocked:
        nativeStatus = DlssNr::StatusPanel::Format(Neurotic::UiLiteral("ingame.menu-common.native_mfg_s_40d16c72", "Native MFG: %s"),Neurotic::Translate(Neurotic::Mfg::MfgRuntimeReasonName(nativePublication.reason)).c_str()); break;
    case Neurotic::Mfg::MfgSetupStatus::Ready: nativeStatus = Neurotic::UiMessage("ingame.menu-common.unlock_applied_enable_fg_in_the_game_higher_rati_a5d42585", "Unlock applied; enable FG in the game. Higher ratios require runtime acceptance."); break;
    default: nativeStatus = nativePublication.status == Neurotic::Mfg::MfgRuntimeStatus::Published ?
        Neurotic::UiLiteral("ingame.menu-common.manual_overrides_off_native_unlock_remains_until_a0358bc1", "Manual overrides off; native unlock remains until exit.") : Neurotic::UiLiteral("ingame.menu-common.native_mfg_is_off_8d48fc2d", "Native MFG is off."); break;
    }
    std::string effectiveStatus;
    if (nativeSelected && nativeRefusal.blocked && !nativeRoutePending)
    {
        const auto effective = Neurotic::Mfg::AcceptedMfgMultiplier(StreamlineHooks::mfgRequestReceipt());
        effectiveStatus = !effective ? Neurotic::UiLiteral("ingame.menu-common.current_fg_setting_unconfirmed_be69c4e8", "Current FG setting unconfirmed.") : *effective == 0 ? Neurotic::UiLiteral("ingame.menu-common.current_fg_off_d662d85d", "Current FG: Off.") :
            DlssNr::StatusPanel::Format(Neurotic::UiLiteral("ingame.menu-common.current_fg_ux_a245f4ab", "Current FG: %uX."), *effective);
    }
    if (!effectiveStatus.empty()) nativeStatus += " " + effectiveStatus;
    DlssNr::StatusPanel::Line("##MfgNativeStatus", nativeStatus, ink);
    auto sessionStatus = DlssNr::StatusPanel::Format(Neurotic::UiLiteral("ingame.menu-common.current_session_s_s_07b04deb", "Current session: %s. %s"),Neurotic::Translate(EM::StageText(experimental.stage)).c_str(),Neurotic::Translate(EM::ReasonText(experimental.reason)).c_str());
    if (experimental.optionsObserved)
        sessionStatus += DlssNr::StatusPanel::Format(Neurotic::UiLiteral("ingame.menu-common.generated_frames_requested_u_forwarded_u_result__ef4dcf20", " Generated frames requested: %u; forwarded: %u; result: %d. Displayed delivery unknown."), experimental.original, experimental.forwarded, experimental.result);
    DlssNr::StatusPanel::Line("##MfgSession", sessionStatus, ink);
    const auto requestedCaption=Neurotic::UiMessage("ingame.mfg.requested", "requested");
    const auto offCaption=Neurotic::UiMessage("ingame.mfg.off", "off");
    DlssNr::StatusPanel::Linef("##MfgNextLaunch", ink, Neurotic::UiLiteral("ingame.menu-common.next_launch_rtx_30_s_rtx_20_s_c04de38c", "Next launch: RTX 30 %s; RTX 20 %s."), experimental.saved.rtx30 ? requestedCaption.c_str() : offCaption.c_str(), experimental.saved.rtx20 ? requestedCaption.c_str() : offCaption.c_str());
    const char* notice = experimental.saveFailed ? Neurotic::UiLiteral("ingame.menu-common.settings_could_not_be_saved_current_session_unch_a9be3790", "Settings could not be saved; current session unchanged.") :
        nativeEnabled && (rtx30 || rtx20) ? Neurotic::UiLiteral("ingame.menu-common.turn_off_the_rtx_40_unlock_before_testing_rtx_20_27cf0251", "Turn off the RTX 40 unlock before testing RTX 20/30 compatibility.") :
        EM::Preferences{rtx30,rtx20} != experimental.saved ? Neurotic::UiLiteral("ingame.menu-common.unsaved_compatibility_changes_daff39d0", "Unsaved compatibility changes.") :
        experimental.backendLoaded && ((experimental.family == EM::Family::Rtx30 && !experimental.saved.rtx30) ||
          (experimental.family == EM::Family::Rtx20 && !experimental.saved.rtx20)) ? Neurotic::UiLiteral("ingame.menu-common.off_next_launch_backend_stays_loaded_until_exit_cce7158e", "Off next launch; backend stays loaded until exit.") : "";
    std::string qualification = experimental.requested ?
        Neurotic::UiLiteral("ingame.menu-common.rtx_20_30_gameplay_qualification_pending_dynamic_b775d739", "RTX 20/30 gameplay qualification pending; dynamic MFG unavailable. Avoid external unlockers.") :
        Neurotic::UiLiteral("ingame.menu-common.none_leaves_fg_under_game_control_an_unlock_alon_c0191bbe", "None leaves FG under game control; an unlock alone does not enable FG.");
    // Four meaningful fixed slots preserve control positions without reserving
    // empty effective/notice/observation rows. Overflow keeps full hover details.
    if (*notice) qualification = std::string(notice) + " " + qualification;
    DlssNr::StatusPanel::Line("##MfgQualification", qualification,
        *notice ? toneMapColor(ImVec4(1,.72f,.25f,1)) : ink);

    }

    if (ctx.childPage == 1 && state.activeFgInput != FGInput::ForceXeLL)
    {

        ImGui::BeginDisabled(nativeSelected);
        if (ImGui::BeginTable("fgSelection", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();

            const auto previousInput = config->FGInput.value_or_default();
            PopulateCombo(Neurotic::UiLiteral("ingame.menu-common.fg_input_029fc693", "FG Input"), config->FGInput, inputOptions);
            if (previousInput != config->FGInput.value_or_default() && config->FGInput.value_or_default() == FGInput::NvngxFG)
                config->FGOutput = FGOutput::NoFG;
            ShowTooltip(Neurotic::UiLiteral("ingame.menu-common.the_data_source_to_be_used_for_fg_the_native_fg__c22f6e4b", "The data source to be used for FG\n"
                        "The native FG which the game supports"));

            ImGui::TableNextColumn();

            if (replaceFgOutputWithNvngx)
            {
                // Disable None?
                PopulateCombo(Neurotic::UiLiteral("ingame.menu-common.fg_nvngx_fae9a007", "FG Nvngx"), config->FGNvngxReplacement, nvngxOptions);
                ShowTooltip(Neurotic::UiLiteral("ingame.menu-common.what_backend_to_use_instead_of_the_real_dlssg_62512954", "What backend to use instead of the real DLSSG"));
            }
            else
            {
                PopulateCombo(Neurotic::UiLiteral("ingame.menu-common.fg_output_b24f5825", "FG Output"), config->FGOutput, outputOptions);
                ShowTooltip(Neurotic::UiLiteral("ingame.menu-common.the_fg_that_you_will_actually_be_using_9b4848b3", "The FG that you will actually be using"));
            }

            ImGui::EndTable();
        }

        // Should be on a new line
        if (showNvngxFgDowndown)
        {
            PopulateCombo(Neurotic::UiLiteral("ingame.menu-common.fg_nvngx_replacement_e0159d24", "FG Nvngx Replacement"), config->FGNvngxReplacement, nvngxOptions);
            ShowTooltip(Neurotic::UiLiteral("ingame.menu-common.what_backend_to_use_instead_of_the_real_dlssg_62512954", "What backend to use instead of the real DLSSG"));
        }

        ImGui::EndDisabled();

        if (!supportsDlssg && (replaceFgOutputWithNvngx || showNvngxFgDowndown) &&
            config->FGNvngxReplacement.value_or_default() == FGNvngxReplacement::None)
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.real_dlssg_is_unavailable_on_this_gpu_select_an__a4e56233", "Real DLSSG is unavailable on this GPU. Select an available replacement before saving."));
        if (replaceFgOutputWithNvngx && config->FGOutput.value_or_default() != FGOutput::NoFG)
        {
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.the_nvngx_input_uses_its_replacement_directly_th_2952f153", "The Nvngx input uses its replacement directly; the saved FG output is conflicting."));
            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.use_direct_replacement_output_6ccdca68", "Use direct replacement output"))) config->FGOutput = FGOutput::NoFG;
        }
        const bool nvngxFgChanged = (replaceFgOutputWithNvngx || showNvngxFgDowndown) &&
                                    state.activeFgNvngx != config->FGNvngxReplacement.value_or_default();
        state.fgSettingsChanged = state.activeFgOutput != config->FGOutput.value_or_default() ||
                                  state.activeFgInput != config->FGInput.value_or_default() || nvngxFgChanged;

        if (state.fgSettingsChanged)
        {
            ImGui::Spacing();
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.0f, 1.f)),
                               Neurotic::UiLiteral("ingame.menu-common.save_settings_and_restart_to_apply_the_changes_afff19ac", "Save Settings and restart to apply the changes"));
            ImGui::Spacing();
        }

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
        if (((state.activeFgOutput == FGOutput::FSRFG || state.activeFgOutput == FGOutput::XeFG ||
              state.activeFgOutput == FGOutput::DLSSG) &&
             state.activeFgInput != FGInput::NoFG && state.activeFgInput != FGInput::NvngxFG) &&
            fgOutput)
        {
            ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.show_detected_ui_1419a131", "Show Detected UI"), &state.fgHudlessCompare);
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.needs_hudless_texture_to_compare_with_final_imag_d8a3c41f", "Needs HUDless texture to compare with final image.\n"
                           "UI elements and ONLY UI elements should have a pink tint!"));

            const auto isUsingUIAny = fgOutput->IsUsingUIAny();

            ImGui::BeginDisabled(!isUsingUIAny);

            if (bool drawUIOverFG = config->FGDrawUIOverFG.value_or_default();
                ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.draw_ui_over_283eead9", "Draw UI over"), &drawUIOverFG))
            {
                config->FGDrawUIOverFG = drawUIOverFG;
            }
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.draws_ui_resource_over_the_final_image_if_no_ui__290f1246", "Draws UI resource over the final image\n"
                           "If no UI visible, enable this!"));

            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 16.0f);

            ImGui::BeginDisabled(!isUsingUIAny || !config->FGDrawUIOverFG.value_or_default());

            if (bool uiPremultipliedAlpha = config->FGUIPremultipliedAlpha.value_or_default();
                ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.ui_premult_alpha_5931a44c", "UI Premult. alpha"), &uiPremultipliedAlpha))
            {
                config->FGUIPremultipliedAlpha = uiPremultipliedAlpha;
            }
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.if_ui_is_too_faint_disable_this_option_7c02b075", "If UI is too faint, disable this option"));

            ImGui::EndDisabled();
        }

        const bool showOutputSpecificFGSettings = state.activeFgInput == FGInput::DLSSG ||
                                                  state.activeFgInput == FGInput::FSRFG ||
                                                  state.activeFgInput == FGInput::FSRFG30;

        const bool showHudCutoff = state.activeFgInput == FGInput::NvngxFG || state.activeFgOutput == FGOutput::FSRFG;

        if (showOutputSpecificFGSettings || showHudCutoff)
        {
            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader(Neurotic::UiLiteral("ingame.menu-common.advanced_fg_settings_196b4888", "Advanced FG Settings")); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                if (showOutputSpecificFGSettings)
                {
                    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
                    if (fgOutput)
                    {
                        ImGui::BeginDisabled(!fgOutput->IsActive());

                        const auto isUsingUIAny = fgOutput->IsUsingUIAny();
                        const auto isUsingHudlessAny = fgOutput->IsUsingHudlessAny();

                        bool disableUI = config->FGDisableUI.value_or_default();
                        ImGui::BeginDisabled(!isUsingUIAny && !disableUI);

                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_ui_texture_ef613671", "Disable UI texture"), &disableUI))
                        {
                            config->FGDisableUI = disableUI;
                            fgOutput->UpdateTarget();
                        }

                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.for_when_the_game_sends_a_ui_texture_but_you_wan_330ccbef", "For when the game sends a UI texture, but you want to disable it"));

                        ImGui::EndDisabled();

                        ImGui::SameLine(0.0f, 16.0f);

                        bool disableHudless = config->FGDisableHudless.value_or_default();
                        ImGui::BeginDisabled(!isUsingHudlessAny && !disableHudless);

                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_hudless_7420a9dc", "Disable HUDless"), &disableHudless))
                        {
                            config->FGDisableHudless = disableHudless;
                        }

                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.for_when_the_game_sends_hudless_but_you_want_to__94b7ae3a", "For when the game sends HUDless, but you want to disable it"));

                        ImGui::EndDisabled();

                        bool depthValidNow = config->FGDepthValidNow.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.depth_as_validnow_587065c7", "Depth as ValidNow"), &depthValidNow))
                            config->FGDepthValidNow = depthValidNow;

                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.will_use_more_vram_but_uniscaler_needs_this_mayb_576c6267", "Will use more VRAM, but Uniscaler needs this\n"
                                       "Maybe some other games might need too"));

                        ImGui::SameLine(0.0f, 16.0f);

                        bool velocityValidNow = config->FGVelocityValidNow.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.velocity_as_validnow_275cf9a6", "Velocity as ValidNow"), &velocityValidNow))
                            config->FGVelocityValidNow = velocityValidNow;

                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.will_use_more_vram_but_uniscaler_needs_this_mayb_576c6267", "Will use more VRAM, but Uniscaler needs this\n"
                                       "Maybe some other games might need too"));

                        bool hudlessValidNow = config->FGHudlessValidNow.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.hudless_as_validnow_85615d1f", "HUDless as ValidNow"), &hudlessValidNow))
                            config->FGHudlessValidNow = hudlessValidNow;

                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.will_use_more_vram_but_some_games_might_need_thi_8c0b4d83", "Will use more VRAM, but some games might need this"));

                        ImGui::SameLine(0.0f, 16.0f);

                        bool firstHudless = config->FGOnlyAcceptFirstHudless.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.accept_first_hudless_37071169", "Accept First HUDless"), &firstHudless))
                            config->FGOnlyAcceptFirstHudless = firstHudless;

                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.if_source_tags_more_than_one_hudless_only_use_th_753a6d38", "If source tags more than one HUDless, only use the first one"));

                        if (bool skipReset = config->FGSkipReset.value_or_default();
                            ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.skip_reset_4fc9b711", "Skip Reset"), &skipReset))
                        {
                            config->FGSkipReset = skipReset;
                        }

                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.don_t_use_reset_signals_from_fg_inputs_0b357c0d", "Don't use reset signals from FG Inputs"));

                        ImGui::EndDisabled();

                        ImGui::PushItemWidth(80.0f * menuResScale);

                        auto frameAhead = config->FGAllowedFrameAhead.value_or_default();
                        if (ImGui::InputInt(Neurotic::UiLiteral("ingame.menu-common.frame_ahead_0871ed97", "Frame Ahead"), &frameAhead, 1, 1) && frameAhead > 0 && frameAhead < 4)
                        {
                            config->FGAllowedFrameAhead = frameAhead;
                        }

                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.number_of_frames_the_fg_is_allowed_to_be_ahead_o_16b87762", "Number of frames the FG is allowed to be ahead of the game\n"
                                       "Might prevent FG on/off switching, but also might cause issues"));

                        ImGui::PopItemWidth();

                        ImGui::SameLine(0.0f, 16.0f);

                        const char* ftSources[] = { Neurotic::UiLiteral("ingame.menu-common.input_f238798e", "Input"), Neurotic::UiLiteral("ingame.option.c2854dc2b604", "Opti"), Neurotic::UiLiteral("ingame.option.973d0c649aa9", "Zero") };
                        const char* ftSourceInfos[] = { Neurotic::UiLiteral("ingame.menu-common.uses_frametimes_provided_by_dlssg_or_fsr_fg_9319aa2f", "Uses frametimes provided by\nDLSSG or FSR-FG "),
                                                        Neurotic::UiLiteral("ingame.menu-common.uses_frametimes_calculated_by_opti_b82cced1", "Uses frametimes calculated by Opti"),
                                                        Neurotic::UiLiteral("ingame.menu-common.let_xefg_to_handle_frametimes_344520c1", "Let XeFG to handle frametimes") };

                        auto currentSet = (int) config->FTInput.value_or_default();
                        auto currentSourceCount = state.activeFgOutput == FGOutput::XeFG ? 3 : 2;

                        ImGui::PushItemWidth(95.0f * menuResScale);

                        if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.ft_input_0962747c", "FT Input"), ftSources[currentSet]))
                        {
                            for (size_t i = 0; i < currentSourceCount; i++)
                            {

                                if (ImGui::Selectable(ftSources[i], currentSet == i))
                                {
                                    LOG_DEBUG("FTInput has changed {} -> {}", ftSources[currentSet], ftSources[i]);
                                    config->FTInput = (FrameTimeSource) i;
                                }

                                ImGui::TextWrapped("%s",Neurotic::Translate(ftSourceInfos[i]).c_str());
                            }

                            ImGui::EndCombo();
                        }

                        ImGui::PopItemWidth();

                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.select_source_for_frametime_might_help_frame_pac_e355f61c", "Select source for frametime\n"
                                       "Might help frame pacing and stutter issues"));
                    }
                }

                if (auto reveal = Neurotic::Sleek::AnimatedRegion("##HudCutoffOptions", showHudCutoff); reveal.Visible())
                {
                    float fgHudCutoff = config->FGHudCutoff.value_or_default();
                    if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.hud_cutoff_17d49b9a", "Hud Cutoff"), &fgHudCutoff, 0.00f, 1.0f, "%.2f"))
                        config->FGHudCutoff = fgHudCutoff;
                    ResetSliderSetting("FGHudCutoff", config->FGHudCutoff);

                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.cutoffs_transparency_from_ui_to_help_with_interp_2b0320cf", "Cutoffs transparency from UI to help with interpolation\n"
                                   "You can use Show Detected UI to see the difference\n0.0 is auto"));
                }
            }
        }
    }
}

void MenuCommon::RenderFrameGenerationRuntimeSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;
    auto fgOutput = state.currentFG;

    // FSR FG controls
    if (state.activeFgOutput == FGOutput::FSRFG && state.activeFgInput != FGInput::NoFG &&
        state.currentFGSwapchain != nullptr)
    {
        if (state.activeFgInput != FGInput::Upscaler ||
            (currentFeature != nullptr && !currentFeature->IsFrozen()) && FfxApiProxy::IsFGReady())
        {
            ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.frame_generation_fsr_fg_b0b9d948", "Frame Generation (FSR FG)"));

            if (_ffxFGIndex < 0)
                _ffxFGIndex = config->FfxFGIndex.value_or_default();

            if (state.ffxFGVersionNames.size() > 0)
            {
                ImGui::PushItemWidth(135.0f * menuResScale);

                auto currentName = StrFmt("FSR %s",Neurotic::Translate(state.ffxFGVersionNames[_ffxFGIndex]).c_str());
                if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.ffx_fg_f1bc94bf", "FFX FG"), currentName.c_str()))
                {
                    for (int n = 0; n < state.ffxFGVersionIds.size(); n++)
                    {
                        auto name = StrFmt("FSR %s",Neurotic::Translate(state.ffxFGVersionNames[n]).c_str());
                        if (ImGui::Selectable(name.c_str(), config->FfxFGIndex.value_or_default() == n))
                            _ffxFGIndex = n;
                    }

                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.list_of_fgs_reported_by_ffx_sdk_677919aa", "List of FGs reported by FFX SDK"));

                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.change_fg_2677b6d7", "Change FG")) && _ffxFGIndex != config->FfxFGIndex.value_or_default())
                {
                    config->FfxFGIndex = _ffxFGIndex;
                    state.fgChanged = true;
                    state.scChanged = true;
                }
            }

            bool fgActive = config->FGEnabled.value_or_default();
            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.active_3203f178", "Active##2"), &fgActive))
            {
                config->FGEnabled = fgActive;
                LOG_DEBUG("FGEnabled set FGEnabled: {}", fgActive);

                if (config->FGEnabled.value_or_default())
                    state.fgChanged = true;
            }
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enable_frame_generation_0e2e3cd4", "Enable Frame Generation"));

            bool fgAsync = config->FGAsync.value_or_default();
            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.allow_async_2ead5d15", "Allow Async"), &fgAsync))
            {
                config->FGAsync = fgAsync;

                if (config->FGEnabled.value_or_default())
                {
                    state.fgChanged = true;
                    state.scChanged = true;
                    LOG_DEBUG("Async set FGChanged");
                }
            }
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enable_async_for_better_fg_performance_might_cau_fb93cab1", "Enable Async for better FG performance\nMight cause crashes, especially with HUD Fix!"));

            ImGui::SameLine(0.0f, 16.0f);

            bool fgDV = config->FGDebugView.value_or_default();
            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.debug_view_7682ecca", "Debug View##2"), &fgDV))
            {
                config->FGDebugView = fgDV;

                if (config->FGEnabled.value_or_default())
                {
                    state.fgChanged = true;
                    LOG_DEBUG("DebugView set FGChanged");
                }
            }
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enable_fsr3_1_fg_debug_view_top_left_game_motion_b1c2921b", "Enable FSR3.1-FG Debug view\n\n"
                           "Top left: Game Motion Vectors\n"
                           "Top middle: GMV Depth\n"
                           "Top right: Optical Flow MV\n"
                           "Middle: Interpolated frame only\n"
                           "Bottom left: Disocclusion mask\n"
                           "Bottom middle: Interpolation source (w/o UI)\n"
                           "Bottom right: HUDless resource"));

            ImGui::SameLine(0.0f, 16.0f);

            if (state.currentFG && state.currentFG->Version().major > 3)
            {
                if (bool fgwm = config->FSRFGEnableWatermark.value_or_default();
                    ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.enable_watermark_e535d078", "Enable Watermark"), &fgwm))
                {
                    LOG_DEBUG("FSRFGEnableWatermark set FGWatermark: {}", fgwm);
                    config->FSRFGEnableWatermark = fgwm;
                }

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.after_changing_this_option_please_save_settings__4972a7f3", "After changing this option, please Save Settings\n"
                               "It will be applied on next launch."));
            }

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader(Neurotic::UiLiteral("ingame.menu-common.extended_fsr_fg_settings_08257c29", "Extended FSR FG Settings")); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.fg_only_generated_15e8bf6d", "FG Only Generated"), &state.fgOnlyGenerated);
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.display_only_fsr_3_1_generated_frames_cd533f55", "Display only FSR 3.1 Generated frames"));

                ImGui::SameLine(0.0f, 16.0f);
                auto debugResetLines = config->FGDebugResetLines.value_or_default();
                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.debug_reset_lines_b7abdb17", "Debug Reset Lines"), &debugResetLines))
                {
                    config->FGDebugResetLines = debugResetLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugResetLines);
                }
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enables_drawing_of_interpolation_skip_lines_a2427ba4", "Enables drawing of Interpolation skip lines"));

                auto debugTearLines = config->FGDebugTearLines.value_or_default();
                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.debug_tear_lines_c0062206", "Debug Tear Lines"), &debugTearLines))
                {
                    config->FGDebugTearLines = debugTearLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugTearLines);
                }
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enables_drawing_of_tear_and_interpolation_skip_l_6e7987c3", "Enables drawing of Tear and Interpolation skip lines"));

                ImGui::SameLine(0.0f, 16.0f);
                auto debugPacingLines = config->FGDebugPacingLines.value_or_default();
                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.debug_pacing_lines_0b057908", "Debug Pacing Lines"), &debugPacingLines))
                {
                    config->FGDebugPacingLines = debugPacingLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugPacingLines);
                }
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enables_drawing_of_pacing_lines_cbe34f94", "Enables drawing of Pacing lines"));

                ImGui::Spacing();
                if (auto tree = Neurotic::Sleek::ScopedTreeNode(Neurotic::UiLiteral("ingame.menu-common.fg_rectangle_settings_8798cb3e", "FG Rectangle Settings")); tree.IsOpen())
                {
                    ImGui::PushItemWidth(95.0f * menuResScale);
                    int rectLeft = config->FGRectLeft.value_or(0);
                    if (ImGui::InputInt(Neurotic::UiLiteral("ingame.menu-common.rect_left_8f884915", "Rect Left"), &rectLeft))
                        config->FGRectLeft = rectLeft;

                    ImGui::SameLine(0.0f, 16.0f);
                    int rectTop = config->FGRectTop.value_or(0);
                    if (ImGui::InputInt(Neurotic::UiLiteral("ingame.menu-common.rect_top_b0cb8311", "Rect Top"), &rectTop))
                        config->FGRectTop = rectTop;

                    int rectWidth = config->FGRectWidth.value_or(0);
                    if (ImGui::InputInt(Neurotic::UiLiteral("ingame.menu-common.rect_width_65204d77", "Rect Width"), &rectWidth))
                        config->FGRectWidth = rectWidth;

                    ImGui::SameLine(0.0f, 16.0f);
                    int rectHeight = config->FGRectHeight.value_or(0);
                    if (ImGui::InputInt(Neurotic::UiLiteral("ingame.menu-common.rect_height_9660ea59", "Rect Height"), &rectHeight))
                        config->FGRectHeight = rectHeight;

                    ImGui::PopItemWidth();
                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.frame_generation_rectangle_adjust_for_letterboxe_464ec96b", "Frame generation rectangle, adjust for letterboxed content"));

                    ImGui::BeginDisabled(!config->FGRectLeft.has_value() && !config->FGRectTop.has_value() &&
                                         !config->FGRectWidth.has_value() && !config->FGRectHeight.has_value());

                    if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_fg_rect_ad5af9e7", "Reset FG Rect")))
                    {
                        config->FGRectLeft.reset();
                        config->FGRectTop.reset();
                        config->FGRectWidth.reset();
                        config->FGRectHeight.reset();
                    }

                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.resets_frame_generation_rectangle_ff4b739e", "Resets Frame generation rectangle"));

                    ImGui::EndDisabled();

                }

                auto fg = state.currentFG;
                if (fg != nullptr && strcmp(fg->Name(), "FSR-FG") == 0 &&
                    FfxApiProxy::VersionDx12_FG() >= feature_version { 3, 1, 3 })
                {
                    ImGui::Spacing();

                    if (auto tree = Neurotic::Sleek::ScopedTreeNode(Neurotic::UiLiteral("ingame.menu-common.frame_pacing_tuning_1ff777c3", "Frame Pacing Tuning")); tree.IsOpen())
                    {
                        auto fptEnabled = config->FGFramePacingTuning.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.enable_tuning_efb20429", "Enable Tuning"), &fptEnabled))
                        {
                            config->FGFramePacingTuning = fptEnabled;
                            state.fsrfgFramePaceTuningChanged = true;
                        }

                        ImGui::BeginDisabled(!config->FGFramePacingTuning.value_or_default());

                        ImGui::PushItemWidth(115.0f * menuResScale);
                        auto fptSafetyMargin = config->FGFPTSafetyMarginInMs.value_or_default();
                        if (ImGui::InputFloat(Neurotic::UiLiteral("ingame.menu-common.safety_margins_in_ms_04de4899", "Safety Margins in ms"), &fptSafetyMargin, 0.01f, 0.1f, "%.2f"))
                            config->FGFPTSafetyMarginInMs = fptSafetyMargin;
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.safety_margins_in_millisecons_fsr_default_value__02613131", "Safety margins in millisecons\n"
                                       "FSR default value: 0.1ms\n"
                                       "Opti default value: 0.01ms"));

                        auto fptVarianceFactor = config->FGFPTVarianceFactor.value_or_default();
                        if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.variance_factor_50a91285", "Variance Factor"), &fptVarianceFactor, 0.0f, 1.0f, "%.2f"))
                            config->FGFPTVarianceFactor = fptVarianceFactor;
                        ResetSliderSetting("FGFPTVarianceFactor", config->FGFPTVarianceFactor);
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.variance_factor_fsr_default_value_0_1_opti_defau_c331d421", "Variance factor\n"
                                       "FSR default value: 0.1\n"
                                       "Opti default value: 0.3"));
                        ImGui::PopItemWidth();

                        auto fpHybridSpin = config->FGFPTAllowHybridSpin.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.enable_hybrid_spin_aba3a2e3", "Enable Hybrid Spin"), &fpHybridSpin))
                            config->FGFPTAllowHybridSpin = fpHybridSpin;
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.allows_pacing_spinlock_to_sleep_should_reduce_cp_65f432e8", "Allows pacing spinlock to sleep, should reduce CPU usage\n"
                                       "Might cause slow ramp up of FPS"));

                        ImGui::PushItemWidth(115.0f * menuResScale);
                        auto fptHybridSpinTime = config->FGFPTHybridSpinTime.value_or_default();
                        if (ImGui::SliderInt(Neurotic::UiLiteral("ingame.menu-common.hybrid_spin_time_e34bf519", "Hybrid Spin Time"), &fptHybridSpinTime, 0, 100))
                            config->FGFPTHybridSpinTime = fptHybridSpinTime;
                        ResetSliderSetting("FGFPTHybridSpinTime", config->FGFPTHybridSpinTime);
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.how_long_to_spin_if_fpthybridspin_is_true_measur_7cf01cf6", "How long to spin if FPTHybridSpin is true. Measured in timer "
                                       "resolution units.\n"
                                       "Not recommended to go below 2. Will result in frequent overshoots"));
                        ImGui::PopItemWidth();

                        auto fpWaitForSingleObjectOnFence =
                            config->FGFPTAllowWaitForSingleObjectOnFence.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.enable_waitforsingleobjectonfence_4f7410a2", "Enable WaitForSingleObjectOnFence"), &fpWaitForSingleObjectOnFence))
                        {
                            config->FGFPTAllowWaitForSingleObjectOnFence = fpWaitForSingleObjectOnFence;
                        }
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.allows_waitforsingleobject_instead_of_spinning_f_bfd40e40", "Allows WaitForSingleObject instead of spinning for fence value"));

                        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.apply_timing_changes_e25711a1", "Apply Timing Changes")))
                            state.fsrfgFramePaceTuningChanged = true;

                        ImGui::EndDisabled();

                    }
                }

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }

    // XeFG controls
    if (state.activeFgOutput == FGOutput::XeFG && state.activeFgInput != FGInput::NoFG &&
        state.activeFgInput != FGInput::ForceXeLL && state.currentFGSwapchain != nullptr && XeFGProxy::InitXeFG() &&
        fgOutput)
    {
        ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.frame_generation_xefg_61417f19", "Frame Generation (XeFG)"));

        bool ignoreChecks = config->FGXeFGIgnoreInitChecks.value_or_default();

        bool nativeAA = false;
        if (state.activeFgInput == FGInput::Upscaler && currentFeature != nullptr)
            nativeAA = currentFeature->RenderWidth() == currentFeature->DisplayWidth();

        const bool correctMVs = fgOutput->IsLowResMV() || nativeAA ||
                                (State::Instance().gameQuirks & GameQuirk::ForceFGRenderSizeMVs) || ignoreChecks;

        if (!correctMVs || state.realExclusiveFullscreen)
        {
            config->FGEnabled.reset();
            config->FGXeFGDebugView.reset();
        }

        const bool restartNeeded = config->FGXeFGDepthInverted.value_or_default() != fgOutput->IsInvertedDepth() ||
                                   config->FGXeFGJitteredMV.value_or_default() != fgOutput->IsJitteredMVs() ||
                                   config->FGXeFGHighResMV.value_or_default() == fgOutput->IsLowResMV();

        bool cantActivate = false;
        if (restartNeeded)
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                               Neurotic::UiLiteral("ingame.menu-common.restart_the_game_to_apply_correct_xefg_settings_3101bff9", "Restart the game to apply correct XeFG settings!"));
        }
        else
        {
            if (!correctMVs)
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                                   Neurotic::UiLiteral("ingame.menu-common.requires_disabling_dilated_motion_vectors_bd93201e", "Requires disabling dilated motion vectors"));

            if (!ignoreChecks && state.realExclusiveFullscreen)
            {
                cantActivate = true;
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), Neurotic::UiLiteral("ingame.menu-common.borderless_display_mode_required_7218cbd4", "Borderless display mode required!"));
            }

            if (!ignoreChecks && state.isHdrActive)
            {
                if (state.currentSwapchainDesc.BufferDesc.Format >= DXGI_FORMAT_R32G32B32A32_TYPELESS &&
                    state.currentSwapchainDesc.BufferDesc.Format <= DXGI_FORMAT_R16G16B16A16_SINT)
                {
                    cantActivate = true;
                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.0f, 0.0f, 1.f)), Neurotic::UiLiteral("ingame.menu-common.xefg_only_supports_hdr10_be35d815", "XeFG only supports HDR10"));
                }
            }
        }

        if (!correctMVs || cantActivate || ignoreChecks)
        {
            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.ignore_init_checks_1baf5b49", "Ignore Init Checks"), &ignoreChecks))
                config->FGXeFGIgnoreInitChecks = ignoreChecks;

            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.ignores_all_prechecks_for_xefg_don_t_use_this_op_7832f662", "Ignores all prechecks for XeFG\n"
                           "Don't use this option to skip MV size warning for UE games!\n"
                           "It might cause crashes and bad IQ!"));
        }

        ImGui::BeginDisabled(!correctMVs || cantActivate);

        bool fgActive = config->FGEnabled.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.active_3203f178", "Active##3"), &fgActive))
        {
            config->FGEnabled = fgActive;
            LOG_DEBUG("Enabled set FGEnabled: {}", fgActive);

            if (config->FGEnabled.value_or_default())
                state.fgChanged = true;
        }

        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enable_frame_generation_0e2e3cd4", "Enable Frame Generation"));

        auto maxInterpolationCount = fgOutput->GetMaxInterpolationCount();

        if (maxInterpolationCount > 1)
        {
            ImGui::SameLine(0.0f, 16.0f);

            const char* intModes[] = { "2X", "3X", "4X", "5X", "6X" };
            auto currentSet = fgOutput->GetInterpolatedFrameCount() - 1;
            auto currentIntCount = intModes[currentSet];

            ImGui::PushItemWidth(95.0f * menuResScale);

            if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.mfg_ccb57733", "MFG"), currentIntCount))
            {
                for (int i = 0; i < maxInterpolationCount; i++)
                {
                    if (ImGui::Selectable(intModes[i], (currentSet == i)))
                    {
                        LOG_DEBUG("XeFG Interpolation Count set to: {}", i + 1);
                        state.fgChanged = true;
                        config->FGXeFGInterpolationCount = i + 1;
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::PopItemWidth();

            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.set_xefg_interpolation_count_642e1509", "Set XeFG interpolation count"));
        }

        ImGui::SameLine(0.0f, 16.0f);
        ImGui::BeginDisabled(!fgOutput->IsUsingHudlessAny() || XeFGProxy::SetUiCompositionState() == nullptr);
        bool fgCompositeUI = config->FGXeFGUIComposition.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.ui_composition_c34708fc", "UI Composition"), &fgCompositeUI))
            config->FGXeFGUIComposition = fgCompositeUI;

        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.disable_hud_ui_interpolation_reverts_back_to_pre_9d79713d", "Disable HUD/UI interpolation\n"
                       "Reverts back to previous XeFG 2 behaviour\n\n"
                       "Fixes artifacting transparent HUD/UI"));
        ImGui::EndDisabled();

        bool fgDV = config->FGXeFGDebugView.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.debug_view_7682ecca", "Debug View##2"), &fgDV))
        {
            config->FGXeFGDebugView = fgDV;

            if (config->FGXeFGDebugView.value_or_default())
            {
                state.fgChanged = true;
                LOG_DEBUG("DebugView set FGChanged");
            }
        }
        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enable_xefg_debug_view_971856c0", "Enable XeFG Debug view"));

        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 16.0f);
        bool fgBorderless = config->FGXeFGForceBorderless.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.force_borderless_eacd0cfe", "Force Borderless"), &fgBorderless))
            config->FGXeFGForceBorderless = fgBorderless;

        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.forces_borderless_display_mode_for_best_results__452bcf33", "Forces Borderless display mode\n\n"
                       "For best results, set fullscreen \n"
                       "resolution to your display resolution\n"
                       "Might cause some instability issues.\n\n"
                       "NEEDS GAME RESTART TO BE ACTIVE!"));

        // Disable this for now
        // ImGui::SameLine(0.0f, 16.0f);
        // ImGui::Checkbox("Only Generated##2", &state.fgOnlyGenerated);
        // ShowHelpMarker("Display only XeFG generated frames");

        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader(Neurotic::UiLiteral("ingame.menu-common.extended_xefg_settings_3df4d069", "Extended XeFG Settings")); ch.IsHeaderOpen())
        {
            ImGui::Spacing();
            if (auto tree = Neurotic::Sleek::ScopedTreeNode(Neurotic::UiLiteral("ingame.menu-common.rectangle_settings_8c18e85a", "Rectangle Settings")); tree.IsOpen())
            {
                ImGui::PushItemWidth(95.0f * menuResScale);
                int rectLeft = config->FGRectLeft.value_or(0);
                if (ImGui::InputInt(Neurotic::UiLiteral("ingame.menu-common.rect_left_8f884915", "Rect Left##2"), &rectLeft))
                    config->FGRectLeft = rectLeft;

                ImGui::SameLine(0.0f, 16.0f);
                int rectTop = config->FGRectTop.value_or(0);
                if (ImGui::InputInt(Neurotic::UiLiteral("ingame.menu-common.rect_top_b0cb8311", "Rect Top##2"), &rectTop))
                    config->FGRectTop = rectTop;

                int rectWidth = config->FGRectWidth.value_or(0);
                if (ImGui::InputInt(Neurotic::UiLiteral("ingame.menu-common.rect_width_65204d77", "Rect Width##2"), &rectWidth))
                    config->FGRectWidth = rectWidth;

                ImGui::SameLine(0.0f, 16.0f);
                int rectHeight = config->FGRectHeight.value_or(0);
                if (ImGui::InputInt(Neurotic::UiLiteral("ingame.menu-common.rect_height_9660ea59", "Rect Height##2"), &rectHeight))
                    config->FGRectHeight = rectHeight;

                ImGui::PopItemWidth();
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.frame_generation_rectangle_adjust_for_letterboxe_464ec96b", "Frame generation rectangle, adjust for letterboxed content##2"));

                ImGui::BeginDisabled(!config->FGRectLeft.has_value() && !config->FGRectTop.has_value() &&
                                     !config->FGRectWidth.has_value() && !config->FGRectHeight.has_value());

                if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_fg_rect_ad5af9e7", "Reset FG Rect##2")))
                {
                    config->FGRectLeft.reset();
                    config->FGRectTop.reset();
                    config->FGRectWidth.reset();
                    config->FGRectHeight.reset();
                }

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.resets_frame_generation_rectangle_ff4b739e", "Resets Frame generation rectangle##2"));

                ImGui::EndDisabled();

            }

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }

    // DLSSG controls
    if (state.activeFgOutput == FGOutput::DLSSG && state.activeFgInput != FGInput::NoFG &&
        state.currentFGSwapchain != nullptr && StreamlineProxy::LoadStreamline() && fgOutput)
    {
        ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.frame_generation_dlssg_221d8092", "Frame Generation (DLSSG)"));

        if (state.activeFgNvngx == FGNvngxReplacement::None && state.isHdrActive)
        {
            if (state.currentSwapchainDesc.BufferDesc.Format >= DXGI_FORMAT_R32G32B32A32_TYPELESS &&
                state.currentSwapchainDesc.BufferDesc.Format <= DXGI_FORMAT_R16G16B16A16_SINT)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.0f, 0.0f, 1.f)), Neurotic::UiLiteral("ingame.menu-common.dlssg_only_supports_hdr10_77f67951", "DLSSG only supports HDR10"));
            }
        }

        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.current_dlssg_state_daef18ca", "Current DLSSG state:"));
        ImGui::SameLine();
        if (auto count = state.dlssgDetectedInterpolationCount.load(); count > 0)
        {
            ImGui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)), StrFmt(Neurotic::UiLiteral("ingame.menu-common.on_x_4c44c906", "ON %dx"), count + 1).c_str());
        }
        else
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), Neurotic::UiLiteral("ingame.dlssnr-menucontrols.off_aaedffb0", "OFF"));
        }

        bool fgActive = config->FGEnabled.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.active_3203f178", "Active##4"), &fgActive))
        {
            config->FGEnabled = fgActive;
            LOG_DEBUG("Enabled set FGEnabled: {}", fgActive);

            if (config->FGEnabled.value_or_default())
                state.fgChanged = true;
        }

        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enable_frame_generation_0e2e3cd4", "Enable Frame Generation"));

        auto maxInterpolationCount = fgOutput->GetMaxInterpolationCount();

        if (maxInterpolationCount > 1)
        {
            ImGui::SameLine(0.0f, 16.0f);

            ImGui::BeginDisabled(config->FGDLSSGForceDMFG.value_or_default());

            const char* intModes[] = { "2X", "3X", "4X", "5X", "6X" };
            auto currentSet = fgOutput->GetInterpolatedFrameCount() - 1;
            auto currentIntCount = intModes[currentSet];

            ImGui::PushItemWidth(95.0f * menuResScale);

            if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.mfg_ccb57733", "MFG"), currentIntCount))
            {
                for (int i = 0; i < maxInterpolationCount; i++)
                {
                    if (ImGui::Selectable(intModes[i], (currentSet == i)))
                    {
                        LOG_DEBUG("DLSSG Interpolation Count set to: {}", i + 1);
                        config->FGDLSSGInterpolationCount = i + 1;
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::PopItemWidth();

            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.set_dlssg_interpolation_count_c1a8a969", "Set DLSSG interpolation count"));

            ImGui::EndDisabled();

            if (fgOutput->GetDMFGSupport())
            {
                ImGui::SameLine(0.0f, 16.0f);

                if (bool dynamicMFG = config->FGDLSSGForceDMFG.value_or_default();
                    ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.force_dynamic_mfg_7dd3df71", "Force Dynamic MFG"), &dynamicMFG))
                {
                    config->FGDLSSGForceDMFG = dynamicMFG;
                }

                ImGui::BeginDisabled(!config->FGDLSSGForceDMFG.value_or_default());
                float& fpsTarget = SharedDynamicFgTarget(config);
                ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.dmfg_fps_target_12d65e45", "DMFG FPS Target"), &fpsTarget, 0, 200, "%.0f");

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.an_active_limit_of_0_means_auto_detect_the_displ_a9a85250", "An active limit of 0 means auto-detect the display refresh rate"));

                if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.apply_target_c8b65a23", "Apply Target")))
                {
                    config->FGDLSSGFramerateTargetDMFG = fpsTarget;
                }

                ImGui::SameLine(0.0f, 16.0f);

                if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_target_ecdec9b0", "Reset Target")))
                {
                    fpsTarget = 0.0f;
                    config->FGDLSSGFramerateTargetDMFG.reset();
                }

                ImGui::EndDisabled();
            }
        }

        bool useGamesMarkers = config->FGDLSSGUseGamesReflexMarkers.value_or_default();
        ImGui::BeginDisabled(!ReflexHooks::gameIsSendingMarkers());
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_game_s_reflex_markers_ecc23ea5", "Use Game's Reflex Markers"), &useGamesMarkers))
        {
            config->FGDLSSGUseGamesReflexMarkers = useGamesMarkers;
            LOG_DEBUG("Changed set FGDLSSGUseGamesReflexMarkers: {}", useGamesMarkers);
        }
        ImGui::EndDisabled();
    }

    // OptiFG
    if (state.api != API::Vulkan && state.currentFGSwapchain != nullptr && state.activeFgInput == FGInput::Upscaler)
    {
        SeparatorWithHelpMarker(Neurotic::UiLiteral("ingame.menu-common.frame_generation_optifg_6d830f6f", "Frame Generation (OptiFG)"), Neurotic::UiLiteral("ingame.menu-common.using_upscaler_data_for_fg_b63302bb", "Using upscaler data for FG"));

        if (currentFeature != nullptr && !currentFeature->IsFrozen() &&
            ((state.activeFgOutput == FGOutput::FSRFG && FfxApiProxy::IsFGReady()) ||
             (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() != nullptr) ||
             (state.activeFgOutput == FGOutput::DLSSG && StreamlineProxy::Module() != nullptr)))
        {
            if (!Config::Instance()->FGDisableHUDFix.value_or_default() &&
                state.swapchainInteropApi == SwapchainInteropApi::None)
            {
                bool fgHudfix = config->FGHUDFix.value_or_default();

                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.hudfix_2b3cc630", "HUDFix"), &fgHudfix))
                {
                    config->FGHUDFix = fgHudfix;
                    LOG_DEBUG("Enabled set FGHUDFix: {}", fgHudfix);
                    state.clearCapturedHudlesses = true;
                    state.fgChanged = true;
                }

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enable_hud_stability_fix_might_cause_crashes_7c722647", "Enable HUD stability fix, might cause crashes!"));

                ImGui::BeginDisabled(!config->FGHUDFix.value_or_default());

                ImGui::SameLine(0.0f, 16.0f);
                ImGui::PushItemWidth(95.0f * menuResScale);
                int hudFixLimit = config->FGHUDLimit.value_or_default();
                if (ImGui::InputInt("Limit", &hudFixLimit))
                {
                    if (hudFixLimit < 1)
                        hudFixLimit = 1;
                    else if (hudFixLimit > 999)
                        hudFixLimit = 999;

                    config->FGHUDLimit = hudFixLimit;
                    LOG_DEBUG("Enabled set FGHUDLimit: {}", hudFixLimit);
                }
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.delay_hudless_capture_high_values_might_cause_cr_9758910b", "Delay HUDless capture, high values might cause crash!"));

                ImGui::SameLine(0.0f, 16.0f);
                if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.res_5cf341a2", "Res##2")))
                    _showHudlessWindow = !_showHudlessWindow;

                ImGui::EndDisabled();

                auto hudExtended = config->FGHUDFixExtended.value_or_default();
                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.extended_4d6ec1d9", "Extended"), &hudExtended))
                {
                    LOG_DEBUG("Enabled set FGHUDFixExtended: {}", hudExtended);
                    config->FGHUDFixExtended = hudExtended;
                }
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.extended_format_checks_for_possible_hudless_migh_7c738ed7", "Extended format checks for possible HUDless\nMight cause crashes and slowdowns!"));
                ImGui::SameLine(0.0f, 16.0f);

                ImGui::BeginDisabled(!config->FGHUDFix.value_or_default());

                auto immediate = config->FGImmediateCapture.value_or_default();
                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.immediate_capture_541711c5", "Immediate Capture"), &immediate))
                {
                    LOG_DEBUG("Enabled set FGImmediateCapture: {}", immediate);
                    config->FGImmediateCapture = immediate;
                }
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enables_capturing_of_resources_before_shader_exe_7fa8fa63", "Enables capturing of resources before shader execution.\nIncrease HUDless "
                               "capture chances, but might cause capturing of unnecessary resources."));

                ImGui::PopItemWidth();

                ImGui::EndDisabled();
            }

            bool depthScale = config->FGEnableDepthScale.value_or_default();
            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.scale_depth_to_fix_dlss_rr_27a06fcb", "Scale Depth to fix DLSS RR"), &depthScale))
                config->FGEnableDepthScale = depthScale;
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.fix_for_dlss_d_wrong_depth_inputs_89fa1b6c", "Fix for DLSS-D wrong depth inputs"));

            bool resourceFlip = config->FGResourceFlip.value_or_default();
            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.flip_unity_5038300f", "Flip (Unity)"), &resourceFlip))
                config->FGResourceFlip = resourceFlip;
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.flip_velocity_depth_resources_of_unity_games_95e561c6", "Flip Velocity & Depth resources of Unity games"));

            ImGui::SameLine(0.0f, 16.0f);

            bool resourceFlipOffset = config->FGResourceFlipOffset.value_or_default();
            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.flip_use_offset_d13535ba", "Flip Use Offset"), &resourceFlipOffset))
                config->FGResourceFlipOffset = resourceFlipOffset;
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.use_height_difference_as_offset_ff139428", "Use height difference as offset"));

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader(Neurotic::UiLiteral("ingame.menu-common.advanced_optifg_settings_623e3850", "Advanced OptiFG Settings")); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};

                if (!Config::Instance()->FGDisableHUDFix.value_or_default() &&
                    state.swapchainInteropApi == SwapchainInteropApi::None)
                {
                    ImGui::Spacing();

                    auto rb = config->FGResourceBlocking.value_or_default();
                    if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.resource_blocking_b974292f", "Resource Blocking"), &rb))
                    {
                        config->FGResourceBlocking = rb;
                        LOG_DEBUG("Enabled set FGResourceBlocking: {}", rb);
                    }
                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.block_rarely_used_resources_from_using_as_hudles_ff477d2b", "Block rarely used resources from using as HUDless \n"
                                   "to prevent flickers and other issues\n\n"
                                   "HUDfix enable/disable will reset the block list!"));

                    ImGui::SameLine(0.0f, 16.0f);

                    auto rrc = config->FGRelaxedResolutionCheck.value_or_default();
                    if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.relaxed_resource_check_b7ddc760", "Relaxed Resource Check"), &rrc))
                    {
                        config->FGRelaxedResolutionCheck = rrc;
                        LOG_DEBUG("Enabled set FGRelaxedResolutionCheck: {}", rrc);
                    }
                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.relax_resolution_checks_for_hudless_by_32_pixels_c002036a", "Relax resolution checks for HUDless by 32 pixels \n"
                                   "Helps games which use black borders for some \n"
                                   "resolutions and screen ratios (e.g. Witcher 3)"));

                    ImGui::BeginDisabled(state.fgResetCapturedResources);
                    ImGui::PushItemWidth(95.0f * menuResScale);
                    if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.fg_create_list_b18e8ec2", "FG Create List"), &state.fgCaptureResources))
                    {
                        if (!state.fgCaptureResources)
                            config->FGHUDLimit = 1;
                        else
                            state.fgOnlyUseCapturedResources = false;
                    }

                    ImGui::SameLine(0.0f, 16.0f);
                    if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.fg_use_list_2dd56311", "FG Use List"), &state.fgOnlyUseCapturedResources))
                    {
                        if (state.fgCaptureResources)
                        {
                            state.fgCaptureResources = false;
                            config->FGHUDLimit = 1;
                        }
                    }

                    ImGui::SameLine(0.0f, 8.0f);
                    ImGui::Text("(%d)", state.fgCapturedResourceCount);

                    ImGui::PopItemWidth();

                    ImGui::SameLine(0.0f, 16.0f);

                    if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_list_e09fedb9", "Reset List")))
                    {
                        LOG_DEBUG("Resetting captured resource list");

                        state.fgResetCapturedResources = true;
                        state.fgOnlyUseCapturedResources = false;
                    }

                    ImGui::EndDisabled();

                    ImGui::Spacing();
                    ImGui::Spacing();
                    if (auto tree = Neurotic::Sleek::ScopedTreeNode(Neurotic::UiLiteral("ingame.menu-common.tracking_settings_a2596d76", "Tracking Settings")); tree.IsOpen())
                    {
                        auto ath = config->FGAlwaysTrackHeaps.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.always_track_heaps_fc7c4917", "Always Track Heaps"), &ath))
                        {
                            config->FGAlwaysTrackHeaps = ath;
                            LOG_DEBUG("Enabled set FGAlwaysTrackHeaps: {}", ath);
                        }
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.always_track_resources_might_cause_performance_i_bf1e717d", "Always track resources, might cause performance issues\n, but also might "
                                       "fix HUDFix related crashes!"));

                        auto disableRTV = config->FGHudfixDisableRTV.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_rtv_tracking_1e56736a", "Disable RTV Tracking"), &disableRTV))
                            config->FGHudfixDisableRTV = disableRTV;
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.disable_tracking_of_createrendertargetview_this__0125c716", "Disable tracking of CreateRenderTargetView\n"
                                       "This might help filtering of wrong HUDless resources"));

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableSRV = config->FGHudfixDisableSRV.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_srv_tracking_223952e0", "Disable SRV Tracking"), &disableSRV))
                            config->FGHudfixDisableSRV = disableSRV;
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.disable_tracking_of_createshaderresourceview_thi_c16f4ed2", "Disable tracking of CreateShaderResourceView\n"
                                       "This might help filtering of wrong HUDless resources"));

                        auto disableUAV = config->FGHudfixDisableUAV.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_uav_tracking_814c02fc", "Disable UAV Tracking"), &disableUAV))
                            config->FGHudfixDisableUAV = disableUAV;
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.disable_tracking_of_createunorderedaccessview_th_e56fc6c6", "Disable tracking of CreateUnorderedAccessView\n"
                                       "This might help filtering of wrong HUDless resources"));

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableOM = config->FGHudfixDisableOM.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_om_tracking_f0800e34", "Disable OM Tracking"), &disableOM))
                            config->FGHudfixDisableOM = disableOM;
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.disable_tracking_of_omsetrendertargets_this_migh_c00bd9e2", "Disable tracking of OMSetRenderTargets\n"
                                       "This might help filtering of wrong HUDless resources"));

                        auto disableSCR = config->FGHudfixDisableSCR.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_scr_tracking_b7ae1282", "Disable SCR Tracking"), &disableSCR))
                            config->FGHudfixDisableSCR = disableSCR;
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.disable_tracking_of_setcomputerootdescriptortabl_1d6bf4a2", "Disable tracking of SetComputeRootDescriptorTable\n"
                                       "This might help filtering of wrong HUDless resources"));

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableSGR = config->FGHudfixDisableSGR.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_sgr_tracking_96c24789", "Disable SGR Tracking"), &disableSGR))
                            config->FGHudfixDisableSGR = disableSGR;
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.disable_tracking_of_setgraphicsrootdescriptortab_9eecaf00", "Disable tracking of SetGraphicsRootDescriptorTable\n"
                                       "This might help filtering of wrong HUDless resources"));

                        ImGui::Spacing();

                        auto disableDI = config->FGHudfixDisableDI.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_di_tracking_ba891c4e", "Disable DI Tracking"), &disableDI))
                            config->FGHudfixDisableDI = disableDI;
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.disable_tracking_of_drawinstanced_this_might_hel_248c9891", "Disable tracking of DrawInstanced\n"
                                       "This might help filtering of wrong HUDless resources"));

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableDII = config->FGHudfixDisableDII.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_dii_tracking_345a5e8a", "Disable DII Tracking"), &disableDII))
                            config->FGHudfixDisableDII = disableDII;
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.disable_tracking_of_drawindexedinstanced_this_mi_b05b6d3a", "Disable tracking of DrawIndexedInstanced\n"
                                       "This might help filtering of wrong HUDless resources"));

                        auto disableDispatch = config->FGHudfixDisableDispatch.value_or_default();
                        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_dispatch_tracking_42e9570e", "Disable Dispatch Tracking"), &disableDispatch))
                            config->FGHudfixDisableDispatch = disableDispatch;
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.disable_tracking_of_dispatch_this_might_help_fil_7d6cb2f8", "Disable tracking of Dispatch\n"
                                       "This might help filtering of wrong HUDless resources"));


                    }
                }

                ImGui::Spacing();
                if (auto tree = Neurotic::Sleek::ScopedTreeNode(Neurotic::UiLiteral("ingame.menu-common.resource_settings_5e163e60", "Resource Settings")); tree.IsOpen())
                {
                    bool makeMVCopies = config->FGMakeMVCopy.value_or_default();
                    if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.fg_make_mv_copies_e0f8a12e", "FG Make MV Copies"), &makeMVCopies))
                        config->FGMakeMVCopy = makeMVCopies;
                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.make_a_copy_of_motion_vectors_to_use_with_optifg_16a9cc7e", "Make a copy of motion vectors to use with OptiFG\n"
                                   "For preventing corruptions that might happen"));

                    bool makeDepthCopies = config->FGMakeDepthCopy.value_or_default();
                    if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.fg_make_depth_copies_a0ed06a4", "FG Make Depth Copies"), &makeDepthCopies))
                        config->FGMakeDepthCopy = makeDepthCopies;
                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.make_a_copy_of_depth_to_use_with_optifg_for_prev_9f5424fb", "Make a copy of depth to use with OptiFG\n"
                                   "For preventing corruptions that might happen"));

                    ImGui::PushItemWidth(115.0f * menuResScale);
                    float depthScaleMax = config->FGDepthScaleMax.value_or_default();
                    if (ImGui::InputFloat(Neurotic::UiLiteral("ingame.menu-common.fg_scale_depth_max_0660d2c3", "FG Scale Depth Max"), &depthScaleMax, 10.0f, 100.0f, "%.1f"))
                        config->FGDepthScaleMax = depthScaleMax;
                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.depth_values_will_be_divided_to_this_value_f54f52de", "Depth values will be divided to this value"));
                    ImGui::PopItemWidth();


                }

                ImGui::Spacing();
                if (auto tree = Neurotic::Sleek::ScopedTreeNode(Neurotic::UiLiteral("ingame.menu-common.syncing_settings_f6633b33", "Syncing Settings")); tree.IsOpen())
                {
                    bool useMutexForPresent = config->FGUseMutexForSwapchain.value_or_default();
                    if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.fg_use_mutex_for_present_2553f581", "FG Use Mutex for Present"), &useMutexForPresent))
                        config->FGUseMutexForSwapchain = useMutexForPresent;
                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.use_mutex_to_prevent_desync_of_fg_and_crashes_di_95e2132c", "Use mutex to prevent desync of FG and crashes\n"
                                   "Disabling might improve the perf but decrease stability"));


                }

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
        else if (currentFeature == nullptr || currentFeature->IsFrozen())
        {
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.upscaler_is_not_active_d5fa3259", "Upscaler is not active")); // Probably never will be visible
        }
        else if (state.activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::IsFGReady())
        {
            ImGui::TextColored(toneMapColor({ 1.0f, 0.0f, 0.0f, 1.0f }),
                               Neurotic::UiLiteral("ingame.menu-common.amd_fidelityfx_dx12_dll_is_missing_6a5d379a", "amd_fidelityfx_dx12.dll is missing!")); // Probably never will be visible
        }
        else if (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() == nullptr)
        {
            ImGui::TextColored(toneMapColor({ 1.0f, 0.0f, 0.0f, 1.0f }),
                               Neurotic::UiLiteral("ingame.menu-common.libxess_fg_dll_is_missing_7bf176ec", "libxess_fg.dll is missing!")); // Probably never will be visible
        }
    }

    const FGNvngxReplacement activeNvngxFg = state.activeFgNvngx;
    if (activeNvngxFg != FGNvngxReplacement::None)
    {
        if (activeNvngxFg == FGNvngxReplacement::Nukems)
        {
            SeparatorWithHelpMarker(Neurotic::UiLiteral("ingame.menu-common.frame_generation_fsr3_fg_via_nukem_s_dlssg_ae406f68", "Frame Generation (FSR3-FG via Nukem's DLSSG)"),
                                    Neurotic::UiLiteral("ingame.menu-common.requires_nukem_s_dlssg_to_fsr3_dll_4354daae", "Requires Nukem's dlssg_to_fsr3 dll"));

            if (!state.nukemsFgFileAvailable)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                                   Neurotic::UiLiteral("ingame.menu-common.please_put_dlssg_to_fsr3_amd_is_better_dll_into__9cedeeb9", "Please put dlssg_to_fsr3_amd_is_better.dll into OptiScaler folder"));
            }
        }
        else if (activeNvngxFg == FGNvngxReplacement::Arturs)
        {
            SeparatorWithHelpMarker(Neurotic::UiLiteral("ingame.menu-common.frame_generation_fsr3_mfg_via_dlss_enabler_21c3383f", "Frame Generation (FSR3-MFG via DLSS Enabler)"),
                                    Neurotic::UiLiteral("ingame.menu-common.dlss_enabler_as_dlss_enabler_headless_dll_c1644d81", "DLSS Enabler as dlss-enabler-headless.dll"));

            if (!state.artursFgFileAvailable)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                                   Neurotic::UiLiteral("ingame.menu-common.please_put_dlss_enabler_headless_dll_into_optisc_3feba7cb", "Please put dlss-enabler-headless.dll into OptiScaler folder"));
            }

            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                               Neurotic::UiLiteral("ingame.menu-common.using_a_subset_of_features_from_dlss_enabler_27345716", "Using a subset of features from DLSS Enabler"));
        }
        else if (activeNvngxFg == FGNvngxReplacement::FFX)
        {
            SeparatorWithHelpMarker(Neurotic::UiLiteral("ingame.menu-common.frame_generation_fsrfg_via_ffx_acaacb8f", "Frame Generation (FSRFG via FFX)"), Neurotic::UiLiteral("ingame.menu-common.ffx_using_the_dlssg_swapchain_bc426042", "FFX using the DLSSG swapchain"));
        }
        else if (activeNvngxFg == FGNvngxReplacement::Combo)
        {
            SeparatorWithHelpMarker(Neurotic::UiLiteral("ingame.menu-common.frame_generation_enabler_ffx_944d026d", "Frame Generation (Enabler + FFX)"),
                                    Neurotic::UiLiteral("ingame.menu-common.ffx_for_middle_fake_frames_and_enabler_for_the_r_5346a2de", "FFX for middle fake frames, and Enabler for the rest\n\n2x - FFX\n"
                                    "3x - Enabler\n4x - FFX + Enabler\n5x - Enabler\n6x - FFX + Enabler"));
        }

        if (state.activeFgInput == FGInput::NvngxFG)
        {

            bool dmfgActive = state.dlssgGameDMFGSupported && config->FGDLSSGOverrideForceDMFG.value_or_default();

            if (!ReflexHooks::isReflexHooked())
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), Neurotic::UiLiteral("ingame.menu-common.reflex_not_hooked_4896e1c5", "Reflex not hooked"));
                ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.if_you_are_using_an_amd_intel_gpu_then_make_sure_83636051", "If you are using an AMD/Intel GPU, then make sure you have Fakenvapi"));
            }
            else if (ReflexHooks::dlssgFrameCountToGenerate() == 0 && !dmfgActive)
            {
                ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.please_select_dlss_frame_generation_in_the_game__45194c2a", "Please select DLSS Frame Generation in the game options\n"
                            "You might need to select DLSS first"));
            }

            if (state.swapchainApi == DX12)
            {
                ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.current_dlssg_state_daef18ca", "Current DLSSG state:"));
                ImGui::SameLine();
                if (auto count = state.dlssgDetectedInterpolationCount.load(); count > 0)
                {
                    ImGui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)),
                                       StrFmt(Neurotic::UiLiteral("ingame.menu-common.on_x_4c44c906", "ON %dx"), count + 1).c_str());
                }
                else
                {
                    ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), Neurotic::UiLiteral("ingame.dlssnr-menucontrols.off_aaedffb0", "OFF"));
                }

                // Issue mostly shows up on AMD on Windows on pre-RDNA3 in some non-UE games
                // Hide to reduce confusion, config is still read
                const bool isUnrealEngine = State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
                                            State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine;
                const bool isDllProxyNvngxType =
                    activeNvngxFg == FGNvngxReplacement::Nukems || activeNvngxFg == FGNvngxReplacement::Arturs;
                if (isDllProxyNvngxType && !primaryGpu.dlssCapable && primaryGpu.fsr4Support == FSR4Support::None &&
                    !primaryGpu.usesVkd3dProton && !isUnrealEngine)
                {
                    if (bool makeDepthCopy = config->NvngxFGMakeDepthCopy.value_or_default();
                        ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.fix_broken_visuals_636c7e5d", "Fix broken visuals"), &makeDepthCopy))
                    {
                        config->NvngxFGMakeDepthCopy = makeDepthCopy;
                    }
                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.makes_a_copy_of_the_depth_buffer_can_fix_broken__0bd04419", "Makes a copy of the depth buffer\nCan fix broken visuals in some games on AMD "
                                   "GPUs under Windows\nCan cause stutters, so best to use only when necessary"));
                }
            }
            else if (state.swapchainApi == Vulkan)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                                   Neurotic::UiLiteral("ingame.menu-common.dlssg_is_purposefully_disabled_when_this_menu_is_b6c50508", "DLSSG is purposefully disabled when this menu is visible"));
                ImGui::Spacing();
            }
        }

        bool isLoaded = false;
        if (state.swapchainApi == Vulkan)
            isLoaded = Nvngx_FG::isVulkanAvailable();
        if (state.swapchainApi == DX12)
            isLoaded = Nvngx_FG::isDx12Available();

        if (isLoaded)
        {
            if (activeNvngxFg == FGNvngxReplacement::Arturs || activeNvngxFg == FGNvngxReplacement::Combo)
            {
                auto featureVer = Nvngx_FG::version();
                auto antighostingVer = Nvngx_FG::extraVersion();
                ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.de_ver_d_d_d_d_gb_ver_d_d_b4db9564", "DE Ver: %d.%d.%d.%d   GB Ver: %d.%d"), featureVer.major, featureVer.minor, featureVer.patch,
                            featureVer.reserved, antighostingVer.major, antighostingVer.minor);

                static std::vector<FlagDefinition> common_flags = {
                    { "Antighosting (GB)", 0x00100000, Neurotic::UiLiteral("ingame.menu-common.enable_anti_ghosting_correction_86f7ced3", "Enable anti-ghosting correction") },
                    { Neurotic::UiLiteral("ingame.menu-common.temporal_hud_pin_3c9c64a7", "Temporal HUD pin"), 0x04000000, Neurotic::UiLiteral("ingame.menu-common.enable_temporal_hud_pinning_present_backbuffer_s_cc9bb0bd", "Enable temporal HUD pinning (present-backbuffer stability)") }
                };

                static std::vector<FlagDefinition> uncommon_flags = {
                    //{ "Hudless UI mask", 0x02000000, "Use HUD-less as UI mask (DL2 inverted semantics)" },
                    { Neurotic::UiLiteral("ingame.menu-common.hud_interpolation_5dfcd17f", "HUD interpolation"), 0x08000000, Neurotic::UiLiteral("ingame.menu-common.hud_of_interpolation_0_legacy_pin_present_1_of_w_db3452ff", "HUD OF interpolation (0=legacy pin-present, 1=OF warp)") },
                    { Neurotic::UiLiteral("ingame.menu-common.ignore_ui_texture_4da55d62", "Ignore UI texture"), 0x10000000, Neurotic::UiLiteral("ingame.menu-common.ignore_dedicated_dlssg_ui_texture_force_legacy_h_2eaf6287", "Ignore dedicated DLSSG.UI texture (force legacy HUD path)") },
                    //{ "Dp4a active", 0x20000000, "OF pipeline using dp4a-accelerated SSD (SM 6.4+)" },
                    { Neurotic::UiLiteral("ingame.menu-common.pin_backbuffer_89f49880", "Pin backbuffer"), 0x40000000, Neurotic::UiLiteral("ingame.menu-common.pin_dlssg_backbuffer_to_subframe_1_snapshot_acro_a2d1d451", "Pin DLSSG.Backbuffer to subframe-1 snapshot across MFG frame") }
                };

                static std::vector<FlagDefinition> debug_flags = {
                    { Neurotic::UiLiteral("ingame.menu-common.antighosting_red_tint_48420cbb", "Antighosting red tint"), 0x00200000, Neurotic::UiLiteral("ingame.menu-common.debug_red_tint_on_corrected_pixels_c74da3d0", "Debug: red tint on corrected pixels") },
                    { Neurotic::UiLiteral("ingame.menu-common.antighosting_split_screen_5e57636c", "Antighosting split screen"), 0x00400000, Neurotic::UiLiteral("ingame.menu-common.debug_split_screen_comparison_ecabc4e7", "Debug: split screen comparison") },
                    { Neurotic::UiLiteral("ingame.menu-common.frame_index_line_c02eaa7a", "Frame index line"), 0x00010000, "" },
                    { Neurotic::UiLiteral("ingame.menu-common.hud_detection_a4f1da29", "HUD detection"), 0x00020000, "" },
                    { Neurotic::UiLiteral("ingame.menu-common.disocclusion_tint_31616877", "Disocclusion tint"), 0x00040000, "" },
                    { Neurotic::UiLiteral("ingame.menu-common.artifacts_detection_7df06e58", "Artifacts detection"), 0x00080000, "" },
                    { Neurotic::UiLiteral("ingame.menu-common.camera_mv_debug_06fef5ef", "Camera MV debug"), 0x00800000, Neurotic::UiLiteral("ingame.menu-common.debug_blue_tint_where_camera_mv_fallback_is_used_fd64b2b1", "Debug: blue tint where camera MV fallback is used") },
                    { Neurotic::UiLiteral("ingame.menu-common.generic_visualization_d5b61889", "Generic visualization"), 0x01000000, Neurotic::UiLiteral("ingame.menu-common.debug_trapezoid_zone_visualization_06e103ac", "Debug: trapezoid zone visualization") }
                };

                uint32_t temp_flags = config->NvngxFGDispatchFlags.value_or_default();
                bool changed = false;

                ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.raw_dispatchflags_4409124e", "Raw DispatchFlags:"));
                changed |= ImGui::InputScalar("##RawFlags", ImGuiDataType_U32, &temp_flags, NULL, NULL, "%08X",
                                              ImGuiInputTextFlags_CharsHexadecimal);

                ImGui::SameLine(0.0f, 20.0f * menuResScale);
                if (bool showDebug = config->NvngxFGShowDebug.value_or_default();
                    ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.show_debug_6dbb45ff", "Show Debug"), &showDebug))
                {
                    config->NvngxFGShowDebug = showDebug;
                }
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.required_for_debug_flags_to_work_correctly_4379366f", "Required for Debug flags to work correctly"));

                ImGui::Spacing();

                if (auto ch = ScopedCollapsingHeader(Neurotic::UiLiteral("ingame.menu-common.active_dispatchflags_81c94551", "Active DispatchFlags")); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};

                    auto render_flags = [&](const std::vector<FlagDefinition>& flags)
                    {
                        for (const auto& flag : flags)
                        {
                            changed |= ImGui::CheckboxFlags(flag.name.c_str(), &temp_flags, flag.mask);

                            if (!flag.description.empty()) ShowHelpMarker(flag.description.c_str());
                        }
                    };

                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.common_1e235262", "Common"));
                    render_flags(common_flags);

                    ImGui::Spacing();
                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.uncommon_1a7b89dd", "Uncommon"));
                    render_flags(uncommon_flags);

                    if (config->NvngxFGShowDebug.value_or_default())
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.debug_c014083d", "Debug"));
                        render_flags(debug_flags);
                    }
                }

                if (changed)
                {
                    config->NvngxFGDispatchFlags = temp_flags;
                }
            }

            if (activeNvngxFg == FGNvngxReplacement::Nukems)
            {
                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.enable_debug_view_fada7193", "Enable Debug View"), &state.dlssgDebugView))
                {
                    Nvngx_FG::setDebugView(state.dlssgDebugView);
                }
                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.interpolated_frames_only_b64b20f9", "Interpolated frames only"), &state.dlssgInterpolatedOnly))
                {
                    Nvngx_FG::setInterpolatedOnly(state.dlssgInterpolatedOnly);
                }
            }

            if (activeNvngxFg == FGNvngxReplacement::FFX || activeNvngxFg == FGNvngxReplacement::Combo)
            {
                if (_ffxFGIndex < 0)
                    _ffxFGIndex = config->FfxFGIndex.value_or_default();

                if (state.ffxFGVersionNames.size() > 0)
                {
                    ImGui::PushItemWidth(135.0f * menuResScale);

                    auto currentName = StrFmt("FSR %s",Neurotic::Translate(state.ffxFGVersionNames[_ffxFGIndex]).c_str());
                    if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.ffx_fg_f1bc94bf", "FFX FG"), currentName.c_str()))
                    {
                        for (int n = 0; n < state.ffxFGVersionIds.size(); n++)
                        {
                            auto name = StrFmt("FSR %s",Neurotic::Translate(state.ffxFGVersionNames[n]).c_str());
                            if (ImGui::Selectable(name.c_str(), config->FfxFGIndex.value_or_default() == n))
                                _ffxFGIndex = n;
                        }

                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.list_of_fgs_reported_by_ffx_sdk_677919aa", "List of FGs reported by FFX SDK"));

                    ImGui::SameLine(0.0f, 6.0f);

                    if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.change_fg_2677b6d7", "Change FG")) && _ffxFGIndex != config->FfxFGIndex.value_or_default())
                    {
                        config->FfxFGIndex = _ffxFGIndex;
                        state.fgChanged = true;
                    }
                }

                bool fgAsync = config->FGAsync.value_or_default();
                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.allow_async_2ead5d15", "Allow Async##2"), &fgAsync))
                {
                    config->FGAsync = fgAsync;

                    if (config->FGEnabled.value_or_default())
                    {
                        state.fgChanged = true;
                        LOG_DEBUG("Async set FGChanged");
                    }
                }
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enable_async_for_better_fg_performance_might_cau_fb93cab1", "Enable Async for better FG performance\nMight cause crashes, especially with HUD Fix!"));

                ImGui::SameLine(0.0f, 20.0f * menuResScale);
                bool fgDV = config->FGDebugView.value_or_default();
                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.debug_view_7682ecca", "Debug View##3"), &fgDV))
                {
                    config->FGDebugView = fgDV;

                    if (config->FGEnabled.value_or_default())
                    {
                        state.fgChanged = true;
                        LOG_DEBUG("DebugView set FGChanged");
                    }
                }
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enable_fsr3_1_fg_debug_view_top_left_game_motion_b1c2921b", "Enable FSR3.1-FG Debug view\n\n"
                               "Top left: Game Motion Vectors\n"
                               "Top middle: GMV Depth\n"
                               "Top right: Optical Flow MV\n"
                               "Middle: Interpolated frame only\n"
                               "Bottom left: Disocclusion mask\n"
                               "Bottom middle: Interpolation source (w/o UI)\n"
                               "Bottom right: HUDless resource"));

                if (Nvngx_FG::version().major > 3)
                {
                    ImGui::SameLine(0.0f, 20.0f * menuResScale);
                    if (bool fgwm = config->FSRFGEnableWatermark.value_or_default();
                        ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.enable_watermark_e535d078", "Enable Watermark"), &fgwm))
                    {
                        LOG_DEBUG("FSRFGEnableWatermark set FGWatermark: {}", fgwm);
                        config->FSRFGEnableWatermark = fgwm;
                    }

                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.after_changing_this_option_please_save_settings__4972a7f3", "After changing this option, please Save Settings\n"
                                   "It will be applied on next launch."));
                }
            }

            if (bool disableHudless = config->NvngxFGDisableHudless.value_or_default();
                ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_hudless_7420a9dc", "Disable HUDless"), &disableHudless))
            {
                config->NvngxFGDisableHudless = disableHudless;
            }
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.might_be_required_for_some_sets_of_dispatchflags_abd5ad12", "Might be required for some sets of DispatchFlags"));
        }
    }

    // FSR-FG Inputs
    if (state.currentFGSwapchain != nullptr &&
        (state.activeFgInput == FGInput::FSRFG || state.activeFgInput == FGInput::FSRFG30))
    {
        SeparatorWithHelpMarker(Neurotic::UiLiteral("ingame.menu-common.frame_generation_fsr_fg_inputs_a0469c94", "Frame Generation (FSR-FG Inputs)"), Neurotic::UiLiteral("ingame.menu-common.select_fsr_fg_in_game_00cc236a", "Select FSR-FG in-game"));

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
        if (fgOutput != nullptr)
        {
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.current_fsr_fg_state_5f27e25c", "Current FSR-FG state:"));
            ImGui::SameLine();
            if (state.fsrfgInputActive)
            {
                if (fgOutput->IsActive())
                    ImGui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)), Neurotic::UiLiteral("ingame.dlssnr-menucontrols.on_0818b59f", "ON"));
                else
                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.647f, 0.0f, 1.f)), Neurotic::UiLiteral("ingame.menu-common.activate_fg_e137761d", "ACTIVATE FG"));
            }
            else
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), Neurotic::UiLiteral("ingame.dlssnr-menucontrols.off_aaedffb0", "OFF"));
                ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.please_select_fsr_frame_generation_in_the_game_o_d8cd3e11", "Please select FSR Frame Generation in the game options\n"
                            "You might need to select FSR first"));
            }
        }

        bool skipConfig = config->FSRFGSkipConfigForHudless.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.skip_config_for_hudless_4d191e32", "Skip Config for HUDless"), &skipConfig))
            config->FSRFGSkipConfigForHudless = skipConfig;

        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.do_not_use_hudless_set_at_ffxconfig_d0e7f55f", "Do not use HUDless set at ffxConfig"));

        ImGui::SameLine(0.0f, 6.0f);

        bool skipDispatch = config->FSRFGSkipDispatchForHudless.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.skip_dispatch_for_hudless_710af8c8", "Skip Dispatch for HUDless"), &skipDispatch))
            config->FSRFGSkipDispatchForHudless = skipDispatch;

        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.do_not_use_hudless_set_at_ffxdispatch_de9ecf6e", "Do not use HUDless set at ffxDispatch"));
    }

    // Streamline FG Inputs
    if (state.currentFGSwapchain != nullptr && state.activeFgInput == FGInput::DLSSG)
    {
        SeparatorWithHelpMarker(Neurotic::UiLiteral("ingame.menu-common.frame_generation_streamline_fg_inputs_04e66948", "Frame Generation (Streamline FG Inputs)"), Neurotic::UiLiteral("ingame.menu-common.select_dlss_fg_in_game_a17b715b", "Select DLSS-FG in-game"));

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);

        if (!ReflexHooks::isReflexHooked())
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), Neurotic::UiLiteral("ingame.menu-common.reflex_not_hooked_4896e1c5", "Reflex not hooked"));
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.if_you_are_using_an_amd_intel_gpu_then_make_sure_b0c535f2", "If you are using an AMD/Intel GPU, then make sure you have fakenvapi"));
        }
        else if (fgOutput != nullptr)
        {
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.current_streamline_fg_state_adf444f2", "Current Streamline FG state:"));
            ImGui::SameLine();
            if ((state.fgLastFrame - state.dlssgLastFrame) < 3)
            {
                if (fgOutput->IsActive())
                    ImGui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)), Neurotic::UiLiteral("ingame.dlssnr-menucontrols.on_0818b59f", "ON"));
                else
                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.647f, 0.0f, 1.f)), Neurotic::UiLiteral("ingame.menu-common.activate_fg_e137761d", "ACTIVATE FG"));
            }
            else
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), Neurotic::UiLiteral("ingame.dlssnr-menucontrols.off_aaedffb0", "OFF"));
                ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.please_select_dlss_frame_generation_in_the_game__45194c2a", "Please select DLSS Frame Generation in the game options\n"
                            "You might need to select DLSS first"));
            }
        }
    }
}

void MenuCommon::RenderFsrCommonSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        // FSR Common -----------------
        if (currentFeature != nullptr && !currentFeature->IsFrozen() &&
            (state.activeFgOutput == FGOutput::FSRFG || IsFsr(currentBackend)))
        {
            SeparatorWithHelpMarker(Neurotic::UiLiteral("ingame.menu-common.fsr_common_settings_03facf96", "FSR Common Settings"), Neurotic::UiLiteral("ingame.menu-common.affects_both_fsr_fg_upscalers_2689d95e", "Affects both FSR-FG & Upscalers"));

            bool useFsrVales = config->FsrUseFsrInputValues.value_or_default();
            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_fsr_input_values_9c6378dc", "Use FSR Input Values"), &useFsrVales))
                config->FsrUseFsrInputValues = useFsrVales;

            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader(Neurotic::UiLiteral("ingame.menu-common.fov_camera_values_18eca371", "FoV & Camera Values")); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                bool useVFov = config->FsrVerticalFov.has_value() || !config->FsrHorizontalFov.has_value();

                float vfov = config->FsrVerticalFov.value_or_default();
                float hfov = config->FsrHorizontalFov.value_or(90.0f);

                if (useVFov && !config->FsrVerticalFov.has_value())
                    config->FsrVerticalFov = vfov;
                else if (!useVFov && !config->FsrHorizontalFov.has_value())
                    config->FsrHorizontalFov = hfov;

                if (ImGui::RadioButton(Neurotic::UiLiteral("ingame.menu-common.use_vert_fov_b6d1a321", "Use Vert. Fov"), useVFov))
                {
                    config->FsrHorizontalFov.reset();
                    config->FsrVerticalFov = vfov;
                    useVFov = true;
                }

                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::RadioButton(Neurotic::UiLiteral("ingame.menu-common.use_horz_fov_0acb424a", "Use Horz. Fov"), !useVFov))
                {
                    config->FsrVerticalFov.reset();
                    config->FsrHorizontalFov = hfov;
                    useVFov = false;
                }

                if (useVFov)
                {
                    if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.vert_fov_cb299c5a", "Vert. FOV"), &vfov, 0.0f, 180.0f, "%.1f"))
                        config->FsrVerticalFov = vfov;
                    ResetSliderSetting("FsrVerticalFov", config->FsrVerticalFov);

                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.might_help_achieve_better_image_quality_94c50d27", "Might help achieve better image quality"));
                }
                else
                {
                    if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.horz_fov_e2d319ef", "Horz. FOV"), &hfov, 0.0f, 180.0f, "%.1f"))
                        config->FsrHorizontalFov = hfov;
                    ResetSliderSetting("FsrHorizontalFov", config->FsrHorizontalFov);

                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.might_help_achieve_better_image_quality_94c50d27", "Might help achieve better image quality"));
                }

                float cameraNear;
                float cameraFar;

                cameraNear = config->FsrCameraNear.value_or_default();
                cameraFar = config->FsrCameraFar.value_or_default();

                if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.camera_near_4151a226", "Camera Near"), &cameraNear, 0.1f, 500000.0f, "%.1f"))
                    config->FsrCameraNear = cameraNear;
                ResetSliderSetting("FsrCameraNear", config->FsrCameraNear);
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.might_help_achieve_better_image_quality_and_pote_840f3d92", "Might help achieve better image quality\n"
                               "And potentially less ghosting"));

                if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.camera_far_47dec549", "Camera Far"), &cameraFar, 0.1f, 500000.0f, "%.1f"))
                    config->FsrCameraFar = cameraFar;
                ResetSliderSetting("FsrCameraFar", config->FsrCameraFar);
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.might_help_achieve_better_image_quality_and_pote_840f3d92", "Might help achieve better image quality\n"
                               "And potentially less ghosting"));

                if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_camera_values_e3480ad7", "Reset Camera Values")))
                {
                    config->FsrVerticalFov.reset();
                    config->FsrHorizontalFov.reset();
                    config->FsrCameraNear.reset();
                    config->FsrCameraFar.reset();
                }

                ImGui::SameLine(0.0f, 6.0f);
                ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.near_1f_far_1f_4dc7a047", "Near: %.1f Far: %.1f"),
                            state.lastFsrCameraNear < 500000.0f ? state.lastFsrCameraNear : 500000.0f,
                            state.lastFsrCameraFar < 500000.0f ? state.lastFsrCameraFar : 500000.0f);

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }
}

void MenuCommon::RenderFramerateSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;

    // Framerate ---------------------
    if (state.reflexLimitsFps || config->OverlayMenu.value_or_default())
    {
        SeparatorWithHelpMarker(
            Neurotic::UiLiteral("ingame.provider.76c739796a87", "Framerate"), Neurotic::UiLiteral("ingame.menu-common.uses_reflex_when_possible_on_amd_intel_cards_you_93a1fc33", "Uses Reflex when possible\nOn AMD/Intel cards, you can use Fakenvapi to substitute Reflex"));

        static std::string currentMethod {};
        LowLatencyMode fakenvapiMode = {};
        if (state.reflexLimitsFps)
        {
            fakenvapiMode = fakenvapi::getCurrentMode();

            if (fakenvapiMode == LowLatencyMode::AntiLag2)
                currentMethod = Neurotic::UiMessage("ingame.menu-common.fsr_anti_lag_2_0_119b280a", "FSR Anti-Lag 2.0");
            else if (fakenvapiMode == LowLatencyMode::LatencyFlex)
                currentMethod = "LatencyFlex";
            else if (fakenvapiMode == LowLatencyMode::XeLL)
                currentMethod = "XeLL";
            else if (fakenvapiMode == LowLatencyMode::AntiLagVk)
                currentMethod = Neurotic::UiMessage("ingame.menu-common.vulkan_antilag_587c8881", "Vulkan AntiLag");
            else if (fakenvapiMode == LowLatencyMode::None)
            {
                if (fakenvapi::isUsingAsMainNvapi())
                    currentMethod = Neurotic::UiLiteral("ingame.menu-common.none_331505ff", "None");
                else
                    currentMethod = "Reflex";
            }

            if (state.rtssReflexInjection && fakenvapiMode == LowLatencyMode::AntiLag2 &&
                config->FGOutput.value_or_default() == FGOutput::FSRFG)
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                                   Neurotic::UiLiteral("ingame.menu-common.using_rtss_reflex_injection_with_fsr_anti_lag_2__89997adb", "Using RTSS Reflex injection with FSR Anti-Lag 2.0 and FSR FG "
                                   "might cause issues"));
        }
        else
        {
            if (XellHooks::canLimit())
                currentMethod = "Game's XeLL";
            else
                currentMethod = "Fallback";
        }

        if (state.rtssReflexInjection)
            currentMethod.append(" (RTSS)");

        const bool fakenvapiInactive = (fakenvapi::isUsingAsMainNvapi() || fakenvapiMode == LowLatencyMode::XeLL) &&
                                       !fakenvapi::isLowLatencyActive() && state.reflexLimitsFps;

        if (fakenvapiInactive)
            currentMethod.append(" (inactive)");

        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.current_method_s_0e91ae93", "Current method: %s"),Neurotic::Translate(currentMethod.c_str()).c_str());

        if (fakenvapiMode == LowLatencyMode::AntiLag2)
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.fsr_anti_lag_2_0_is_the_new_name_for_antilag_2_d_309f7c4d", "FSR Anti-Lag 2.0 is the new name for AntiLag 2\nDon't ask me why"));

        if (state.reflexShowWarning)
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                               Neurotic::UiLiteral("ingame.menu-common.using_reflex_s_limit_with_fsr_fg_has_performance_488e9bf2", "Using Reflex's limit with FSR FG has performance overhead"));

            ImGui::Spacing();
        }

        // set initial value
        if (std::isinf(_limitFps))
            _limitFps = config->FramerateLimit.value_or_default();

        ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.fps_limit_9668bd31", "FPS Limit"), &_limitFps, 0, 200, "%.0f");

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.apply_limit_3c466181", "Apply Limit")))
        {
            config->FramerateLimit = _limitFps;
        }

        ImGui::SameLine(0.0f, 16.0f);

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_limit_9e622be4", "Reset Limit")))
        {
            _limitFps = 0.0f;
            config->FramerateLimit = _limitFps;
        }

        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader(Neurotic::UiLiteral("ingame.menu-common.vrr_frame_cap_calculator_b37452a5", "VRR Frame Cap Calculator")); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            ImGui::PushItemWidth(105.0f * menuResScale);
            ImGui::InputInt(Neurotic::UiLiteral("ingame.menu-common.refresh_rate_c36c984e", "Refresh Rate"), &refreshRate, 1, 1, ImGuiInputTextFlags_None);
            ImGui::PopItemWidth();

            float refreshRateF = static_cast<float>(refreshRate);
            // it's fine to use with real reflex, we only care about antilag
            auto fpsLimitTech = fakenvapi::getCurrentMode();
            constexpr float margin = 0.3f; // in ms
            float frameCap = std::round(10000.f / (1000.f / refreshRateF + margin)) / 10.f;

            if (fpsLimitTech == LowLatencyMode::AntiLag2 || fpsLimitTech == LowLatencyMode::AntiLagVk)
                frameCap = std::round(frameCap);

            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.calculated_cap_1f_63cec668", "Calculated Cap: %.1f"), frameCap);

            ImGui::SameLine(0.0f, 16.0f);

            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.set_as_fps_limit_962b337d", "Set as FPS Limit")))
            {
                _limitFps = frameCap;
                config->FramerateLimit = _limitFps;
            }
        }
    }
}

void MenuCommon::RenderFakenvapiSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // FAKENVAPI ---------------------------
    ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.fakenvapi_0354818b", "fakenvapi"));

    // Using state.reflexLimitsFps as a detection for Reflex being used on Nvidia
    bool showLatencyFlex =
        fakenvapi::isUsingAsMainNvapi() || (state.activeFgOutput == FGOutput::XeFG && state.reflexLimitsFps);

    if (showLatencyFlex)
    {
        ImGui::BeginDisabled(state.activeFgOutput == FGOutput::XeFG || state.activeFgInput == FGInput::ForceXeLL);
        if (bool forceLFX = config->FN_ForceLatencyFlex.value_or_default();
            ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.force_latencyflex_d56353ff", "Force LatencyFlex"), &forceLFX))
        {
            config->FN_ForceLatencyFlex = forceLFX;
        }
        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.by_default_fsr_anti_lag_2_0_xell_is_used_when_av_19a7ee2a", "By default, FSR Anti-Lag 2.0/XeLL is used when available.\n"
                       "This setting lets you force LatencyFlex instead"));
        ImGui::EndDisabled();

        // Keep Force XeLL on the same line if LatencyFlex is visible
        ImGui::SameLine(0.0f, 16.0f);
    }

    // Force XeLL is always visible
    bool forceXell = config->ForceXeLL.value_or_default();
    static bool activeForceXeLL = forceXell;

    const bool nativeMfgSelected = config->FGDLSSGNativeMfgExperimental.value_or_default() &&
        state.swapchainApi == API::DX12 &&
        ctx.primaryGpu->nvidiaArchInfo.architecture_id >= NV_GPU_ARCHITECTURE_AD100 &&
        ctx.primaryGpu->nvidiaArchInfo.architecture_id < NV_GPU_ARCHITECTURE_GB200;
    ImGui::BeginDisabled(nativeMfgSelected);
    if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.force_xell_cb2ccfdf", "Force XeLL"), &forceXell))
    {
        config->ForceXeLL = forceXell;
    }
    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.allows_xell_to_work_without_fg_on_non_intel_card_1886bddf", "Allows XeLL to work without FG on non-Intel cards.\n\nDisables FG "
                   "options\n\nRequires a restart"));
    ImGui::EndDisabled();

    if (activeForceXeLL != forceXell)
    {
        ImGui::Spacing();
        ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.0f, 1.f)), Neurotic::UiLiteral("ingame.menu-common.save_ini_and_restart_to_apply_the_changes_a8e5c189", "Save INI and restart to apply the changes"));
        ImGui::Spacing();
    }

    if (showLatencyFlex)
    {
        // clang-format off
        static const std::vector<MenuOption<LFXMode>> lfx_modes = {
            { LFXMode::Conservative, "Conservative",
                Neurotic::UiLiteral("ingame.menu-common.the_safest_but_might_not_reduce_latency_well_30520cdc", "The safest, but might not reduce latency well") },
            { LFXMode::Aggressive, "Aggressive",
                Neurotic::UiLiteral("ingame.menu-common.improves_latency_but_in_some_cases_will_lower_fp_02a6d4d5", "Improves latency, but in some cases will lower FPS more than expected") },
            { LFXMode::ReflexIDs, Neurotic::UiLiteral("ingame.menu-common.reflex_id_5921d435", "Reflex ID"),
                Neurotic::UiLiteral("ingame.menu-common.best_when_can_be_used_some_games_are_not_compati_7d923a90", "Best when can be used, some games are not compatible (e.g. Cyberpunk)\n"
                "and will fallback to Aggressive") }
        };

        bool usingLFX = fakenvapi::getCurrentMode() == LowLatencyMode::LatencyFlex;

        ImGui::BeginDisabled(!usingLFX);
        PopulateCombo(Neurotic::UiLiteral("ingame.menu-common.latencyflex_mode_1a628b6e", "LatencyFlex mode"), config->FN_LatencyFlexMode, lfx_modes);
        ImGui::EndDisabled();

        static std::vector<MenuOption<ForceReflex>> reflex_modes = { { ForceReflex::InGame, Neurotic::UiLiteral("ingame.menu-common.follow_in_game_fe976ca2", "Follow in-game") },
                                                                { ForceReflex::ForceDisable, Neurotic::UiLiteral("ingame.menu-common.force_disable_2fae5299", "Force Disable") },
                                                                { ForceReflex::ForceEnable, Neurotic::UiLiteral("ingame.menu-common.force_enable_bc250e2f", "Force Enable") } };

        PopulateCombo(Neurotic::UiLiteral("ingame.menu-common.force_reflex_1a34390f", "Force Reflex"), config->FN_ForceReflex, reflex_modes);
        // clang-format on
    }
}

template <typename T> std::string GetMenuOptionLabel(const std::vector<MenuOption<T>>& options, T targetValue)
{
    auto it = std::find_if(options.begin(), options.end(),
                           [targetValue](const MenuOption<T>& option) { return option.value == targetValue; });

    if (it != options.end())
    {
        return it->label;
    }

    return Neurotic::UiMessage("ingame.menu-common.unknown_d80d0833", "Unknown");
}

void MenuCommon::RenderLowLatencySettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // Low Latency ---------------------------
    ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.low_latency_f1ce5485", "Low Latency"));

    static std::vector<MenuOption<LowLatencyInput>> lowLatencyInput = {
        { LowLatencyInput::None, "None (Off)" },    { LowLatencyInput::Auto, Neurotic::UiLiteral("ingame.menu-common.auto_b980aecf", "Auto") },
        { LowLatencyInput::AntiLag2, "AntiLag 2" }, { LowLatencyInput::Reflex, "Reflex" },
        { LowLatencyInput::XeLL, "XeLL" },          { LowLatencyInput::UeLowLatency, "UeLowLatency" },
    };

    static std::vector<MenuOption<LowLatencyMode>> lowLatencyOutput = {
        { LowLatencyMode::None, "None (Off)" },
        { LowLatencyMode::Auto, Neurotic::UiLiteral("ingame.menu-common.auto_b980aecf", "Auto") },
        { LowLatencyMode::LatencyFlex, "LatencyFlex" },
        { LowLatencyMode::AntiLag2, "AntiLag 2" },
        { LowLatencyMode::XeLL, "XeLL" },
        { LowLatencyMode::AntiLagVk, Neurotic::UiLiteral("ingame.menu-common.antilag_vk_20cb88ce", "AntiLag Vk") },
        { LowLatencyMode::Reflex, "Reflex" },
    };

    LowLatencyInput activeInput {};
    LowLatencyMode activeOutput {};

    if (ImGui::BeginTable("lowLatencyActive", 2, ImGuiTableFlags_SizingStretchSame))
    {
        InputCommon::get_currently_active(activeInput, activeOutput);

        ImGui::TableNextColumn();

        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.active_input_s_2718a893", "Active input: %s"),Neurotic::Translate(GetMenuOptionLabel(lowLatencyInput, activeInput).c_str()).c_str());

        ImGui::TableNextColumn();

        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.active_output_s_bedc1e46", "Active output: %s"),Neurotic::Translate(GetMenuOptionLabel(lowLatencyOutput, activeOutput).c_str()).c_str());

        ImGui::EndTable();
    }

    if (ImGui::BeginTable("lowLatencySelection", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();

        auto avalibleInputs = InputCommon::get_avaliable_inputs();

        lowLatencyInput[(uint32_t) LowLatencyInput::AntiLag2].set_disabled(!avalibleInputs[LowLatencyInput::AntiLag2]);
        lowLatencyInput[(uint32_t) LowLatencyInput::Reflex].set_disabled(!avalibleInputs[LowLatencyInput::Reflex]);
        lowLatencyInput[(uint32_t) LowLatencyInput::XeLL].set_disabled(!avalibleInputs[LowLatencyInput::XeLL]);
        lowLatencyInput[(uint32_t) LowLatencyInput::UeLowLatency].set_disabled(
            !avalibleInputs[LowLatencyInput::UeLowLatency]);

        // need to have a value before combo
        if (!config->LowLatencyInput.has_value())
            config->LowLatencyInput = config->LowLatencyInput.value_or_default();

        PopulateCombo(Neurotic::UiLiteral("ingame.menu-common.input_f238798e", "Input"), config->LowLatencyInput, lowLatencyInput);

        ImGui::TableNextColumn();

        lowLatencyOutput[(uint32_t) LowLatencyMode::AntiLagVk].set_disabled(true, Neurotic::UiLiteral("ingame.menu-common.no_support_8fd5e814", "No support"));
        lowLatencyOutput[(uint32_t) LowLatencyMode::Reflex].set_disabled(true, Neurotic::UiLiteral("ingame.menu-common.no_support_8fd5e814", "No support"));

        // need to have a value before combo
        if (!config->LowLatencyOutput.has_value())
            config->LowLatencyOutput = config->LowLatencyOutput.value_or_default();

        PopulateCombo(Neurotic::UiLiteral("ingame.menu-common.output_b5db16a0", "Output"), config->LowLatencyOutput, lowLatencyOutput);

        ImGui::EndTable();
    }

    if (activeOutput == LowLatencyMode::LatencyFlex)
    {
        static const std::vector<MenuOption<LFXMode>> lfx_modes = {
            { LFXMode::Conservative, "Conservative", Neurotic::UiLiteral("ingame.menu-common.the_safest_but_might_not_reduce_latency_well_30520cdc", "The safest, but might not reduce latency well") },
            { LFXMode::Aggressive, "Aggressive",
              Neurotic::UiLiteral("ingame.menu-common.improves_latency_but_in_some_cases_will_lower_fp_02a6d4d5", "Improves latency, but in some cases will lower FPS more than expected") },
            { LFXMode::ReflexIDs, Neurotic::UiLiteral("ingame.menu-common.reflex_id_5921d435", "Reflex ID"),
              Neurotic::UiLiteral("ingame.menu-common.best_when_can_be_used_some_games_are_not_compati_7d923a90", "Best when can be used, some games are not compatible (e.g. Cyberpunk)\n"
              "and will fallback to Aggressive") }
        };

        PopulateCombo(Neurotic::UiLiteral("ingame.menu-common.latencyflex_mode_1a628b6e", "LatencyFlex mode"), config->FN_LatencyFlexMode, lfx_modes);
    }

    static std::vector<MenuOption<ForceReflex>> lowlatency_states = { { ForceReflex::InGame, Neurotic::UiLiteral("ingame.menu-common.follow_in_game_fe976ca2", "Follow in-game") },
                                                                      { ForceReflex::ForceDisable, Neurotic::UiLiteral("ingame.menu-common.force_disable_2fae5299", "Force Disable") },
                                                                      { ForceReflex::ForceEnable, Neurotic::UiLiteral("ingame.menu-common.force_enable_bc250e2f", "Force Enable") } };

    ImGui::SetNextItemWidth(150.0f * ctx.menuResScale);
    PopulateCombo(Neurotic::UiLiteral("ingame.menu-common.force_state_2380baf5", "Force State"), config->FN_ForceReflex, lowlatency_states);
}

void MenuCommon::RenderActiveImageSettings(RenderMenuContext& ctx)
{
    const float imageSliderWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x / 3.0f);

    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    bool rcasEnabled = false;

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        // SHARPNESS -----------------------------
        constexpr feature_version requiredDlssVersion = { 2, 5, 1 };
        rcasEnabled = (currentBackend == Upscaler::XeSS ||
                       (currentBackend == Upscaler::DLSS && currentFeature->Version() >= requiredDlssVersion));

        if (ctx.childPage == 3)
        {
            Neurotic::Sleek::ContentCard card("##SrSharpness", Neurotic::UiLiteral("ingame.menu-common.sharpness_ed6019c3", "Sharpness"), true);
            ImGui::Spacing();

            if (bool overrideSharpness = config->OverrideSharpness.value_or_default();
                ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.override_691179ee", "Override"), &overrideSharpness))
            {
                config->OverrideSharpness = overrideSharpness;

                if (currentBackend == Upscaler::DLSS && currentFeature->Version().major < 3)
                {
                    state.newBackend = currentBackend;
                    MARK_ALL_BACKENDS_CHANGED();
                }
            }
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.ignores_the_value_sent_by_the_game_and_uses_the__92620cb9", "Ignores the value sent by the game\n"
                           "and uses the value set below"));

            ImGui::SameLine(0.0f, 16.0f * menuResScale);

            float featuresCurrentSharpness = currentFeature->Sharpness();
            if (featuresCurrentSharpness > 0.0f)
                ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.current_sharpness_3f_bc20d3f8", "(Current sharpness: %.3f)"), featuresCurrentSharpness);
            else
                ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.current_sharpness_disabled_ad3fc9a1", "(Current sharpness: disabled)"));

            ImGui::BeginDisabled(!config->OverrideSharpness.value_or_default());

            float sharpness = config->Sharpness.value_or_default();

            ImGui::SetNextItemWidth(imageSliderWidth);
            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.sharpness_ed6019c3", "Sharpness"), &sharpness, 0.0f, 1.0f))
                config->Sharpness = sharpness;
            ResetSliderSetting(Neurotic::UiLiteral("ingame.menu-common.sharpness_ed6019c3", "Sharpness"), config->Sharpness);

            ImGui::EndDisabled();

            // RCAS
            // if (state.api == DX12 || state.api == DX11)
            {

                ImGui::Spacing();
                ImGui::Spacing();

                if (bool rcas = config->RcasEnabled.value_or(rcasEnabled); ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.enable_rcas_da_b0488b17", "Enable RCAS/DA"), &rcas))
                    config->RcasEnabled = rcas;

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enable_optiscaler_s_sharpening_filter_by_default_b59f8bf7", "Enable OptiScaler's sharpening filter\n"
                               "By default uses a sharpening value provided by the game\n"
                               "Select 'Override' under 'Sharpness' and adjust the slider\n"
                               "to change it\n\n"
                               "Some upscalers have their own sharpness filter, so this\n"
                               "option is not always needed"));

                ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.rcas_sharpens_contrast_da_limits_sharpening_acro_c54ee773", "RCAS sharpens contrast; DA limits sharpening across depth edges; MAS adapts it to motion."));
                ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.some_backends_require_this_pass_for_nonzero_shar_0f24978d", "Some backends require this pass for nonzero sharpening even when the optional toggle is off."));
                ImGui::BeginDisabled(!config->RcasEnabled.value_or(rcasEnabled) && currentFeature->Sharpness() <= 0.0f &&
                    !(config->MotionSharpnessEnabled.value_or_default() && config->MotionSharpness.value_or_default() > 0.0f));

                auto sharpnessShader = (int32_t) Config::Instance()->SharpnessShader.value_or_default();

                if (ImGui::RadioButton(Neurotic::UiLiteral("ingame.menu-common.rcas_5298f3a9", "RCAS"), &sharpnessShader, (int32_t) SharpenShader::RCAS))
                {
                    Config::Instance()->SharpnessShader = SharpenShader::RCAS;
                }

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.use_amd_s_rcas_modified_to_add_contrast_paramete_f622de24", "Use AMD's RCAS\n"
                               "Modified to add Contrast parameter\n"
                               "and MAS support"));

                Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.depth_aware_rcas_a184c3d1", "Depth Aware (RCAS)")));
                if (ImGui::RadioButton(Neurotic::UiLiteral("ingame.menu-common.depth_aware_rcas_a184c3d1", "Depth Aware (RCAS)"), &sharpnessShader, (int32_t) SharpenShader::DepthAware))
                {
                    Config::Instance()->SharpnessShader = SharpenShader::DepthAware;
                }

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.use_depth_aware_sharpening_rcas_smarter_sharpeni_c2ca117d", "Use Depth Aware Sharpening (RCAS)\n"
                               "Smarter sharpening with less artifacts,\n"
                               "but also heavier\n\n"
                               "The farther away is the object, the more\n"
                               "sharpening is applied"));

                Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.depth_aware_das_c5bc9bfb", "Depth Aware (DAS)")));
                if (ImGui::RadioButton(Neurotic::UiLiteral("ingame.menu-common.depth_aware_das_c5bc9bfb", "Depth Aware (DAS)"), &sharpnessShader,
                                       (int32_t) SharpenShader::LocalContrastDepthAware))
                {
                    Config::Instance()->SharpnessShader = SharpenShader::LocalContrastDepthAware;
                }

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.use_depth_aware_sharpening_das_depth_aware_direc_1d0c0193", "Use Depth Aware Sharpening (DAS)\n"
                               "Depth-aware directional adaptive luma sharpener\n"
                               "Smarter sharpening with less artifacts,\n"
                               "but also heavier\n\n"
                               "The farther away is the object, the more\n"
                               "sharpening is applied"));

                ImGui::Spacing();

                if (Config::Instance()->SharpnessShader.value_or_default() != SharpenShader::RCAS)
                {
                    if (auto ch = ScopedCollapsingHeader(Neurotic::UiLiteral("ingame.menu-common.advanced_da_parameters_e5775488", "Advanced DA Parameters")); ch.IsHeaderOpen())
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();

                        const char* clampChoices[] = { Neurotic::UiLiteral("ingame.menu-common.auto_b980aecf", "Auto"), Neurotic::UiLiteral("ingame.menu-common.on_d2f9df8a", "On"), Neurotic::UiLiteral("ingame.objectruleeditor.off_dc516be5", "Off") };
                        int clampMode = !config->DAClampOutput.has_value() ? 0 : config->DAClampOutput.value() ? 1 : 2;
                        if (ImGui::Combo(Neurotic::UiLiteral("ingame.menu-common.clamp_output_b1f7fb52", "Clamp Output"), &clampMode, clampChoices, IM_ARRAYSIZE(clampChoices)))
                        { if (clampMode == 0) config->DAClampOutput.reset(); else config->DAClampOutput = clampMode == 1; }

                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.clamps_the_final_image_to_the_0_1_range_prevents_e47bf852", "Clamps the final image to the [0, 1] range.\n\n"
                                       "Prevents overshoot artifacts such as bright halos or negative colors.\n"
                                       "Recommended for LDR pipelines; optional for HDR depending on tone-mapping.\n\n"
                                       "When not set OptiScaler controls it via upscalers HDR flag"));

                        if (currentFeature->DepthLinear())
                        {
                            float depthBias = config->DADepthBias.value_or(0.0015f);
                            ImGui::SetNextItemWidth(imageSliderWidth);
                            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.depth_bias_f5e3e0f2", "Depth Bias"), &depthBias, 0.0001f, 0.03f, "%.4f"))
                                config->DADepthBias = depthBias;
                            ResetSliderSetting("DADepthBias", config->DADepthBias);

                            ShowHelpMarker(
                                Neurotic::UiLiteral("ingame.menu-common.ignores_small_depth_differences_before_edge_dete_07c7366b", "Ignores small depth differences before edge detection.\n\n"
                                "Higher values reduce flickering and noise from minor depth changes, but may "
                                "soften real geometry edges.\n"
                                "Lower values preserve fine detail but can cause unstable or noisy edge "
                                "detection."));

                            float depthScale = config->DADepthScale.value_or(250.0f);
                            ImGui::SetNextItemWidth(imageSliderWidth);
                            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.depth_scale_d17e475c", "Depth Scale"), &depthScale, 100.0f, 600.0f, "%.1f"))
                                config->DADepthScale = depthScale;
                            ResetSliderSetting("DADepthScale", config->DADepthScale);

                            ShowHelpMarker(
                                Neurotic::UiLiteral("ingame.menu-common.controls_how_strongly_sharpening_is_reduced_acro_49743ae3", "Controls how strongly sharpening is reduced across depth edges.\n\n"
                                "Higher values more aggressively prevent sharpening across object boundaries "
                                "(reduces halos).\n"
                                "Lower values allow more sharpening to pass across edges (sharper but "
                                "riskier)."));
                        }
                        else
                        {
                            float depthBias = config->DADepthBias.value_or(0.001f);
                            ImGui::SetNextItemWidth(imageSliderWidth);
                            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.depth_bias_f5e3e0f2", "Depth Bias"), &depthBias, 0.0001f, 0.003f, "%.4f"))
                                config->DADepthBias = depthBias;
                            ResetSliderSetting("DADepthBias", config->DADepthBias);

                            ShowHelpMarker(
                                Neurotic::UiLiteral("ingame.menu-common.ignores_small_depth_differences_before_edge_dete_07c7366b", "Ignores small depth differences before edge detection.\n\n"
                                "Higher values reduce flickering and noise from minor depth changes, but may "
                                "soften real geometry edges.\n"
                                "Lower values preserve fine detail but can cause unstable or noisy edge "
                                "detection."));

                            float depthScale = config->DADepthScale.value_or(35.0f);
                            ImGui::SetNextItemWidth(imageSliderWidth);
                            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.depth_scale_d17e475c", "Depth Scale"), &depthScale, 25.0f, 400.0f, "%.1f"))
                                config->DADepthScale = depthScale;
                            ResetSliderSetting("DADepthScale", config->DADepthScale);

                            ShowHelpMarker(
                                Neurotic::UiLiteral("ingame.menu-common.controls_how_strongly_sharpening_is_reduced_acro_49743ae3", "Controls how strongly sharpening is reduced across depth edges.\n\n"
                                "Higher values more aggressively prevent sharpening across object boundaries "
                                "(reduces halos).\n"
                                "Lower values allow more sharpening to pass across edges (sharper but "
                                "riskier)."));
                        }

                        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_depth_values_95334890", "Reset Depth Values")))
                        {
                            config->DADepthBias.reset();
                            config->DADepthScale.reset();
                        }
                    }
                }
                else
                {
                    if (bool contrastEnabled = config->ContrastEnabled.value_or_default();
                        ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.contrast_enabled_5607ba3a", "Contrast Enabled"), &contrastEnabled))
                        config->ContrastEnabled = contrastEnabled;

                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.controls_sharpness_at_high_contrast_areas_a23b00e2", "Controls sharpness at high contrast areas."));

                    ImGui::BeginDisabled(!config->ContrastEnabled.value_or_default());

                    float contrast = config->Contrast.value_or_default();
                    ImGui::SetNextItemWidth(imageSliderWidth);
                    if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.contrast_7169a5ed", "Contrast"), &contrast, -2.0f, 2.0f, "%.2f"))
                        config->Contrast = contrast;
                    ResetSliderSetting(Neurotic::UiLiteral("ingame.menu-common.contrast_7169a5ed", "Contrast"), config->Contrast);

                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.positive_values_decrease_sharpness_at_high_contr_55306112", "Positive values decrease sharpness at high contrast areas.\n"
                                   "Negative values increase sharpness at high contrast areas."));

                    ImGui::EndDisabled();
                }

                ImGui::EndDisabled();
            }
        }

        if (ctx.childPage == 3)
        {
            Neurotic::Sleek::ContentCard card("##SrMotionSharpness", Neurotic::UiLiteral("ingame.menu-common.motion_adaptive_sharpness_3790d428", "Motion Adaptive Sharpness"), true);
            ImGui::Spacing();

            ImGui::BeginDisabled(!config->RcasEnabled.value_or(rcasEnabled) && currentFeature->Sharpness() <= 0.0f &&
                !(config->MotionSharpnessEnabled.value_or_default() && config->MotionSharpness.value_or_default() > 0.0f));

            if (bool overrideMotionSharpness = config->MotionSharpnessEnabled.value_or_default();
                ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.enable_motion_adaptive_sharpness_86eae7db", "Enable Motion Adaptive Sharpness"), &overrideMotionSharpness))
                config->MotionSharpnessEnabled = overrideMotionSharpness;
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enables_sharpness_adjustments_according_to_the_m_ec423dba", "Enables sharpness adjustments according to the motion"));

            if (Config::Instance()->SharpnessShader.value_or_default() != SharpenShader::RCAS)
            {
                Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.da_mas_debug_9d2049db", "DA + MAS Debug")));
                if (bool overrideMSDebug = config->MotionSharpnessDebug.value_or_default();
                    ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.da_mas_debug_9d2049db", "DA + MAS Debug"), &overrideMSDebug))
                    config->MotionSharpnessDebug = overrideMSDebug;

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.enable_da_mas_debug_views_blue_tint_for_da_detec_7f256293", "Enable DA + MAS debug views\n"
                               "Blue tint for DA detected edges\n\n"
                               "More red areas will have more sharpness applied\n"
                               "Green areas will get reduced sharpness"));
            }

            ImGui::BeginDisabled(!config->MotionSharpnessEnabled.value_or_default());

            if (Config::Instance()->SharpnessShader.value_or_default() == SharpenShader::RCAS)
            {
                Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.da_mas_debug_9d2049db", "DA + MAS Debug")));
                if (bool overrideMSDebug = config->MotionSharpnessDebug.value_or_default();
                    ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.mas_debug_7b0ec41e", "MAS Debug"), &overrideMSDebug))
                    config->MotionSharpnessDebug = overrideMSDebug;
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.areas_that_are_more_red_will_have_more_sharpness_57bc1208", "Areas that are more red will have more sharpness applied\n"
                               "Green areas will get reduced sharpness"));
            }

            float motionSharpness = config->MotionSharpness.value_or_default();
            ImGui::SetNextItemWidth(imageSliderWidth);
            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.motion_sharpness_359f5240", "Motion Sharpness###MotionSharpness"), &motionSharpness, -1.0f, 1.0f, "%.3f"))
                config->MotionSharpness = motionSharpness;
            ResetSliderSetting("MotionSharpness", config->MotionSharpness);

            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.maximum_amount_of_sharpness_that_motion_can_add__3ca7eb87", "Maximum amount of sharpness that motion can add or remove.\n\n"
                           "Negative values reduce sharpening in motion (recommended).\n"
                           "Positive values increase sharpening in motion.\n\n"
                           "The final adjustment scales with motion and is capped at this value."));

            float motionThreshod = config->MotionThreshold.value_or_default();
            ImGui::SetNextItemWidth(imageSliderWidth);
            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.motion_threshold_420a5738", "Motion Threshold###MotionThreshod"), &motionThreshod, 0.0f, 100.0f, "%.2f"))
                config->MotionThreshold = motionThreshod;
            ResetSliderSetting("MotionThreshold", config->MotionThreshold);

            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.minimum_motion_required_before_motion_based_shar_a48e8b6d", "Minimum motion required before motion-based sharpening adjustment begins.\n\n"
                           "Higher values ignore small movements (more stable).\n"
                           "Lower values react to subtle motion (more sensitive)."));

            float motionScale = config->MotionScaleLimit.value_or_default();
            ImGui::SetNextItemWidth(imageSliderWidth);
            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.motion_range_95dcd0be", "Motion Range###MotionRange"), &motionScale, 0.01f, 100.0f, "%.2f"))
                config->MotionScaleLimit = motionScale;
            ResetSliderSetting("MotionScaleLimit", config->MotionScaleLimit);

            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.defines_the_motion_range_over_which_the_effect_r_3f160eee", "Defines the motion range over which the effect ramps from zero to full strength.\n\n"
                           "Values above the threshold are mapped into this range.\n"
                           "Larger values make the response smoother and more gradual.\n"
                           "Smaller values make the effect react more quickly and aggressively."));

            ImGui::EndDisabled();
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Spacing();
        }

        // UPSCALE RATIO OVERRIDE -----------------

        if (ctx.childPage == 0)
        {
            Neurotic::Sleek::ContentCard card("##SrRatio", Neurotic::UiLiteral("ingame.menu-common.upscale_ratio_override_f3d4309e", "Upscale Ratio Override"), true);
            ImGui::Spacing();

            if (bool extended = config->ExtendedLimits.value_or_default(); ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.enable_extended_limits_5f1e2582", "Enable Extended Limits"), &extended)) config->ExtendedLimits = extended;
            if (config->ExtendedLimits.value_or_default()) ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.extended_ratios_change_resolution_detection_and__ceafdaae", "Extended ratios change resolution detection and can be incompatible with the game."));
            auto minSliderLimit = config->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
            auto maxSliderLimit = config->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;

            if (ImGui::BeginTable("##UpscaleRatioColumns", 3, ImGuiTableFlags_SizingStretchSame))
            {
            ImGui::TableNextColumn();
            if (bool upOverride = config->UpscaleRatioOverrideEnabled.value_or_default();
                ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.override_all_9528acc8", "Override all"), &upOverride))
            {
                config->UpscaleRatioOverrideEnabled = upOverride;

                if (upOverride)
                    config->QualityRatioOverrideEnabled = false;
            }
            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.overrides_every_upscaler_preset_with_the_set_val_82526cdf", "Overrides every upscaler preset with the set value\n\n"
                           "1.5x on a 1080p screen means an internal res of 720p\n"
                           "1080 / 1.5 = 720"));

            ImGui::TableNextColumn();
            if (bool qOverride = config->QualityRatioOverrideEnabled.value_or_default();
                ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.override_per_quality_preset_f8e60795", "Override per quality preset"), &qOverride))
            {
                config->QualityRatioOverrideEnabled = qOverride;

                if (qOverride)
                    config->UpscaleRatioOverrideEnabled = false;
            }

            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.lets_you_override_each_preset_s_ratio_individual_7581c421", "Lets you override each preset's ratio individually\n"
                           "Note that not every game supports every quality preset\n\n"
                           "1.5x on a 1080p screen means internal resolution of 720p\n"
                           "1080 / 1.5 = 720"));

            ImGui::TableNextColumn();
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            if (auto reveal = Neurotic::Sleek::AnimatedRegion("##AllRatioOptions", config->UpscaleRatioOverrideEnabled.value_or_default()); reveal.Visible())
            {
                float urOverride = config->UpscaleRatioOverrideValue.value_or_default();
                Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.menu-common.all_ratios_c1679812", "All Ratios"));
                if (Neurotic::Sleek::CardSliderFloat("###All Ratios", &urOverride, minSliderLimit, maxSliderLimit, "%.3f"))
                    config->UpscaleRatioOverrideValue = urOverride;
                ResetSliderSetting("UpscaleRatioOverrideValue", config->UpscaleRatioOverrideValue);
            }

            ImGui::TableSetColumnIndex(1);
            if (auto reveal = Neurotic::Sleek::AnimatedRegion("##QualityRatioOptions", config->QualityRatioOverrideEnabled.value_or_default()); reveal.Visible())
            {
                float qDlaa = config->QualityRatio_DLAA.value_or_default();
                Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.provider.26516f6a6bd8", "DLAA"));
                if (Neurotic::Sleek::CardSliderFloat("###DLAA", &qDlaa, minSliderLimit, maxSliderLimit, "%.3f"))
                    config->QualityRatio_DLAA = qDlaa;
                ResetSliderSetting("QualityRatio_DLAA", config->QualityRatio_DLAA);

                float qUq = config->QualityRatio_UltraQuality.value_or_default();
                Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.menu-common.ultra_quality_05f45017", "Ultra Quality"));
                if (Neurotic::Sleek::CardSliderFloat("###Ultra Quality", &qUq, minSliderLimit, maxSliderLimit, "%.3f"))
                    config->QualityRatio_UltraQuality = qUq;
                ResetSliderSetting("QualityRatio_UltraQuality", config->QualityRatio_UltraQuality);

                float qQ = config->QualityRatio_Quality.value_or_default();
                Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.provider.1b2c08a8733d", "Quality"));
                if (Neurotic::Sleek::CardSliderFloat("###Quality", &qQ, minSliderLimit, maxSliderLimit, "%.3f"))
                    config->QualityRatio_Quality = qQ;
                ResetSliderSetting("QualityRatio_Quality", config->QualityRatio_Quality);

                float qB = config->QualityRatio_Balanced.value_or_default();
                Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.provider.5386ea5db81c", "Balanced"));
                if (Neurotic::Sleek::CardSliderFloat("###Balanced", &qB, minSliderLimit, maxSliderLimit, "%.3f"))
                    config->QualityRatio_Balanced = qB;
                ResetSliderSetting("QualityRatio_Balanced", config->QualityRatio_Balanced);

                float qP = config->QualityRatio_Performance.value_or_default();
                Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.provider.442aded87a55", "Performance"));
                if (Neurotic::Sleek::CardSliderFloat("###Performance", &qP, minSliderLimit, maxSliderLimit, "%.3f"))
                    config->QualityRatio_Performance = qP;
                ResetSliderSetting("QualityRatio_Performance", config->QualityRatio_Performance);

                float qUp = config->QualityRatio_UltraPerformance.value_or_default();
                Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.menu-common.ultra_performance_f6500465", "Ultra Performance"));
                if (Neurotic::Sleek::CardSliderFloat("###Ultra Performance", &qUp, minSliderLimit, maxSliderLimit, "%.3f"))
                    config->QualityRatio_UltraPerformance = qUp;
                ResetSliderSetting("QualityRatio_UltraPerformance", config->QualityRatio_UltraPerformance);
            }
            ImGui::EndTable();
            }
        }

        if (ctx.childPage == 0 && currentFeature != nullptr && !currentFeature->IsFrozen())
        {
            // OUTPUT SCALING -----------------------------
            // if (state.api == DX12 || state.api == DX11)
            {
                // if motion vectors are not display size
                ImGui::BeginDisabled(!currentFeature->LowResMV() &&
                                     currentFeature->RenderWidth() != currentFeature->DisplayWidth());

                ImGui::Spacing();
                Neurotic::Sleek::ContentCard card("##SrOutputScaling", Neurotic::UiLiteral("ingame.menu-common.output_scaling_544efac3", "Output Scaling"), true);
                {
                    ImGui::Spacing();

                    float defaultRatio = 1.5f;

                    if (_ssRatio == 0.0f)
                    {
                        _ssRatio = config->OutputScalingMultiplier.value_or(defaultRatio);
                        _ssEnabled = config->OutputScalingEnabled.value_or_default();
                        _ssDownsampler = config->OutputScalingDownscaler.value_or_default();
                    }

                    ImGui::BeginDisabled((currentBackend == Upscaler::XeSS || currentBackend == Upscaler::DLSS) &&
                                         currentFeature->RenderWidth() > currentFeature->DisplayWidth());
                    ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.enable_b324cd61", "Enable"), &_ssEnabled);
                    ImGui::EndDisabled();

                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.upscales_the_image_internally_to_a_higher_output_85208d7c", "Upscales the image internally to a higher output resolution\n"
                                   "then downscales it back to your display resolution\n\n"
                                   "Values <1.0 make the upscaler cheaper\n"
                                   "Values >1.0 make image sharper at the cost of performance\n\n"
                                   "If greyed out, please check Git Wiki - Unreal Engine tweaks\n\n"
                                   "Target res and total ratio at the bottom (max. total 3.0!)"));

                    ImGui::SameLine(0.0f, 6.0f);

                    ImGui::BeginDisabled(!_ssEnabled);
                    {
                        ImGui::PushItemWidth(95.0f * menuResScale);

                        // clang-format off
                    std::vector<MenuOption<Scaler>> ds_options = {
                        { Scaler::FSR1, "FSR1",
                            Neurotic::UiLiteral("ingame.menu-common.default_option_good_enough_image_quality_and_ver_19f0f0cd", "Default option.\nGood enough image quality and very fast.") },
                        { Scaler::Bicubic, "Bicubic",
                            Neurotic::UiLiteral("ingame.menu-common.fastest_traditional_option_produces_a_very_soft__8d747cbf", "Fastest traditional option.\nProduces a very soft/blurry image, but might be okay for downscaling.") },
                        { Scaler::CatmullRom, "Catmull-Rom",
                            Neurotic::UiLiteral("ingame.menu-common.designed_primarily_for_downscaling_retains_good__289c7b0e", "Designed primarily for downscaling.\nRetains good contrast with minimal artefacts, but softer than Lanczos.") },
                        { Scaler::Lanczos2, "Lanczos2",
                            Neurotic::UiLiteral("ingame.menu-common.lighter_and_faster_than_lanczos3_less_prone_to_r_d2d2cc9d", "Lighter and faster than Lanczos3.\nLess prone to ringing artefacts, but slightly blurrier.") },
                        { Scaler::Lanczos3, "Lanczos3",
                            Neurotic::UiLiteral("ingame.menu-common.heavier_version_of_lanczos2_offers_the_sharpest__ab582c2e", "Heavier version of Lanczos2.\nOffers the sharpest image, but is the most prone to ringing.\nConsidered the best along with Kaiser3.") },
                        { Scaler::Kaiser2, "Kaiser2",
                            Neurotic::UiLiteral("ingame.menu-common.similar_to_lanczos2_smoother_and_less_prone_to_a_b68bcd9e", "Similar to Lanczos2.\nSmoother and less prone to artefacts than Lanczos, but slightly blurrier.") },
                        { Scaler::Kaiser3, "Kaiser3",
                            Neurotic::UiLiteral("ingame.menu-common.similar_to_lanczos3_far_less_prone_to_artefactin_0d2c10d6", "Similar to Lanczos3.\nFar less prone to artefacting than Lanczos3, but much heavier on the GPU.\nConsidered the best along with Lanczos3.") },
                        { Scaler::Magic, "MAGIC",
                            Neurotic::UiLiteral("ingame.menu-common.specialised_to_prevent_artifacts_eliminates_hars_471108f7", "Specialised to prevent artifacts.\nEliminates harsh halos for a natural look, but can appear slightly soft.") }
                    };
                        // clang-format on

                        const bool isUpsampleRatio = _ssRatio < 1.0f;
                        const std::string disabledReason =
                            Neurotic::UiMessage("ingame.menu-common.only_fsr1_and_bicubic_are_supported_when_ratio_i_e151c9f9", "Only FSR1 and Bicubic are supported when Ratio is below 1.0.");

                        for (auto& opt : ds_options)
                        {
                            if (isUpsampleRatio && opt.value > Scaler::Bicubic)
                                opt.set_disabled(true, opt.tooltip + "\n\n" + disabledReason);
                        }

                        if (isUpsampleRatio && _ssDownsampler > Scaler::Bicubic)
                            _ssDownsampler = Scaler::FSR1;

                        PopulateCombo(Neurotic::UiLiteral("ingame.dlssnr-menu.downscaler_b3373b8a", "Downscaler"), _ssDownsampler, ds_options);

                        ImGui::PopItemWidth();
                    }
                    ImGui::EndDisabled();

                    bool applyEnabled = _ssEnabled != config->OutputScalingEnabled.value_or_default() ||
                                        _ssRatio != config->OutputScalingMultiplier.value_or(defaultRatio) ||
                                        _ssDownsampler != config->OutputScalingDownscaler.value_or_default();

                    ImGui::BeginDisabled(!applyEnabled);
                    if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.apply_change_ad1e465e", "Apply Change")))
                    {
                        config->OutputScalingEnabled = _ssEnabled;
                        config->OutputScalingMultiplier = _ssRatio;

                        if (_ssRatio < 1.0f && _ssDownsampler > Scaler::Bicubic)
                            _ssDownsampler = Scaler::FSR1;

                        config->OutputScalingDownscaler = _ssDownsampler;

                        const bool usesDlssd = currentFeature->GetUpscalerType() == Upscaler::DLSSD;
                        if (usesDlssd)
                            state.newBackend = Upscaler::DLSSD;
                        else
                            state.newBackend = currentBackend;

                        MARK_ALL_BACKENDS_CHANGED();
                    }
                    ImGui::EndDisabled();

                    ImGui::BeginDisabled(!_ssEnabled || currentFeature->RenderWidth() > currentFeature->DisplayWidth());
                    ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x / 3.0f));
                    ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.ratio_2639f7da", "Ratio"), &_ssRatio, 0.5f, 3.0f, "%.2f");
                    ImGui::SameLine();
                    if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##OutputScalingRatio"))) _ssRatio = defaultRatio;
                    ImGui::EndDisabled();

                    if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.output_scaling_is_s_target_res_dx_d_2f_jitter_co_2405c09b", "Output Scaling is %s, Target Res: %dx%d (%.2f)\nJitter Count: %d"),Neurotic::Translate(config->OutputScalingEnabled.value_or_default() ? Neurotic::UiLiteral("ingame.menu-common.enabled_de1c9c71", "ENABLED") : Neurotic::UiLiteral("ingame.menu-common.disabled_938e7c61", "DISABLED")).c_str(),
                                    (uint32_t) (currentFeature->DisplayWidth() * _ssRatio),
                                    (uint32_t) (currentFeature->DisplayHeight() * _ssRatio),
                                    ((float) currentFeature->DisplayWidth() * _ssRatio) /
                                        (float) currentFeature->RenderWidth(),
                                    currentFeature->JitterCount());
                    }
                }

                ImGui::EndDisabled();
            }
        }

        // INIT -----------------------------
        if (ctx.childPage == 4)
        {
            ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.initialization_flags_caff431a", "Initialization Flags"));
            ImGui::Spacing();

            if (ImGui::BeginTable("init", 2, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableNextColumn();

                // AutoExposure is always enabled for XeSS with native Dx11
                bool autoExposureDisabled = state.api == API::DX11 && currentBackend == Upscaler::XeSS;
                ImGui::BeginDisabled(autoExposureDisabled);

                if (bool autoExposure = currentFeature->AutoExposure(); ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.auto_exposure_832bed58", "Auto Exposure"), &autoExposure))
                {
                    config->AutoExposure = autoExposure;
                    ReInitUpscaler();
                }
                ShowResetButton(&config->AutoExposure, Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset"));
                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.some_unreal_engine_games_need_this_try_using_if__a1ea5ccf", "Some Unreal Engine games need this\n\n"
                               "Try using if colours flickering or\n"
                               "objects have ghosting trails"));

                ImGui::EndDisabled();

                ImGui::TableNextColumn();
                auto accessToReactiveMask = currentFeature->AccessToReactiveMask();
                ImGui::BeginDisabled(!accessToReactiveMask);

                bool canUseReactiveMask =
                    accessToReactiveMask && currentBackend != Upscaler::DLSS &&
                    (currentBackend != Upscaler::XeSS || currentFeature->Version() >= feature_version { 2, 0, 1 });

                bool disableReactiveMask = config->DisableReactiveMask.value_or(!canUseReactiveMask);

                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.disable_reactive_mask_429af232", "Disable Reactive Mask"), &disableReactiveMask))
                {
                    config->DisableReactiveMask = disableReactiveMask;

                    if (currentBackend == Upscaler::XeSS)
                    {
                        state.newBackend = currentBackend;
                        MARK_ALL_BACKENDS_CHANGED();
                    }
                }

                ImGui::EndDisabled();

                if (accessToReactiveMask)
                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.allows_the_use_of_a_reactive_mask_keep_in_mind_t_af141a8f", "Allows the use of a Reactive mask\n"
                                   "Keep in mind that a Reactive mask sent to DLSS\n"
                                   "will not produce a good image in combination with FSR/XeSS"));
                else
                    ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.option_disabled_because_the_game_doesn_t_provide_dd27530f", "Option disabled because the game doesn't provide a Reactive mask"));

                ImGui::EndTable();

                ImGui::Spacing();
                ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.advanced_initialization_flags_5b4aa442", "Advanced Initialization Flags"));
                {

                    if (ImGui::BeginTable("init2", 2, ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableNextColumn();
                        if (bool depth = currentFeature->DepthInverted(); ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.depth_inverted_d5a695fd", "Depth Inverted"), &depth))
                        {
                            config->DepthInverted = depth;
                            ReInitUpscaler();
                        }
                        ShowResetButton(&config->DepthInverted, Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##2"));
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.you_shouldn_t_need_to_change_it_9527ba2a", "You shouldn't need to change it"));

                        ImGui::TableNextColumn();
                        if (bool hdr = currentFeature->IsHdr(); ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.hdr_input_d66485bc", "HDR input###HDR"), &hdr))
                        {
                            config->HDR = hdr;
                            ReInitUpscaler();
                        }
                        ShowResetButton(&config->HDR, Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##1"));
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.might_help_with_purple_hue_in_some_games_aed79d86", "Might help with purple hue in some games"));

                        ImGui::TableNextColumn();
                        if (bool mv = !currentFeature->LowResMV(); ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.display_resolution_mv_816924e9", "Display-resolution MV###Display Res. MV"), &mv))
                        {
                            config->DisplayResolution = mv;

                            // Disable output scaling when
                            // Display res MV is active
                            if (mv)
                            {
                                config->OutputScalingEnabled = false;
                                _ssEnabled = false;
                            }

                            ReInitUpscaler();
                        }
                        ShowResetButton(&config->DisplayResolution, Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##4"));
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.mostly_a_fix_for_unreal_engine_games_top_left_pa_acf01851", "Mostly a fix for Unreal Engine games\n"
                                       "Top left part of the screen will be blurry"));

                        ImGui::TableNextColumn();

                        if (bool jitter = currentFeature->JitteredMV(); ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.motion_vectors_include_jitter_a8ae46ee", "Motion vectors include jitter###Jitter Cancellation"), &jitter))
                        {
                            config->JitterCancellation = jitter;
                            ReInitUpscaler();
                        }
                        ShowResetButton(&config->JitterCancellation, Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##3"));
                        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.fix_for_games_that_send_motion_data_with_preappl_a7ce133d", "Fix for games that send motion data with preapplied jitter"));

                        ImGui::TableNextColumn();
                        ImGui::EndTable();
                    }

                    if (currentFeature->AccessToReactiveMask() && currentBackend != Upscaler::DLSS)
                    {
                        ImGui::BeginDisabled(config->DisableReactiveMask.value_or(currentBackend == Upscaler::XeSS));

                        bool binaryMask = state.api == Vulkan || currentBackend == Upscaler::XeSS;
                        auto defaultBias = binaryMask ? 0.0f : 0.45f;
                        auto maskBias = config->DlssReactiveMaskBias.value_or(defaultBias);

                        if (!binaryMask)
                        {
                            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.react_mask_bias_561b8ce1", "React. Mask Bias"), &maskBias, 0.0f, 0.9f, "%.2f"))
                                config->DlssReactiveMaskBias = maskBias;
                            ResetSliderSetting("DlssReactiveMaskBias", config->DlssReactiveMaskBias);

                            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.values_above_0_activate_usage_of_reactive_mask_44daa63d", "Values above 0 activate usage of Reactive mask"));
                        }
                        else
                        {
                            bool useRM = maskBias > 0.0f;
                            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_binary_reactive_mask_dfca7eef", "Use Binary Reactive Mask"), &useRM))
                            {
                                if (useRM)
                                    config->DlssReactiveMaskBias = 0.45f;
                                else
                                    config->DlssReactiveMaskBias.reset();
                            }
                        }

                        ImGui::EndDisabled();
                    }
                }
            }
        }
    }
}

void MenuCommon::RenderMagnifierSettings(RenderMenuContext& ctx)
{
    ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.zoom_a_region_of_the_game_image_to_inspect_fine__4c447300", "Zoom a region of the game image to inspect fine detail. Follow the cursor or use a fixed position."));
    auto& state = ctx.state;
    auto config = ctx.config;

    const auto sliderWidth = [](const char* label)
    {
        const float tail = ImGui::CalcTextSize(label).x + Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")) +
            ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth((std::max)(1.0f, (std::min)(ImGui::CalcItemWidth(),
            ImGui::GetContentRegionAvail().x - tail)));
    };

    // Magnifier -----------------------------
    ImGui::Spacing();
    {
        ImGui::Spacing();

        bool magnifierEnabled = config->MagnifierEnabled.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.enable_magnifier_c07d0b5c", "Enable Magnifier"), &magnifierEnabled))
            config->MagnifierEnabled = magnifierEnabled;

        ImGui::BeginDisabled(!magnifierEnabled);

        float magnifierSize = config->MagnifierSize.value_or_default();
        sliderWidth(Neurotic::UiLiteral("ingame.menu-common.size_fc2048cb", "Size"));
        if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.size_fc2048cb", "Size"), &magnifierSize, 5.0f, 50.0f, Neurotic::UiLiteral("ingame.menu-common.1f_of_screen_ab1cfbc4", "%.1f%% of screen")))
            config->MagnifierSize = magnifierSize;
        ImGui::SameLine();
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##MagnifierSize"))) config->MagnifierSize.reset();

        int zoomFactor = config->MagnifierZoomFactor.value_or_default();
        sliderWidth(Neurotic::UiLiteral("ingame.menu-common.zoom_factor_22c3327b", "Zoom Factor"));
        if (ImGui::SliderInt(Neurotic::UiLiteral("ingame.menu-common.zoom_factor_22c3327b", "Zoom Factor"), &zoomFactor, 2, 20, Neurotic::UiLiteral("ingame.menu-common.dx_b4086980", "%dx")))
            config->MagnifierZoomFactor = zoomFactor;
        ImGui::SameLine();
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##MagnifierZoomFactor"))) config->MagnifierZoomFactor.reset();

        float borderSize = config->MagnifierBorderSize.value_or_default();
        sliderWidth(Neurotic::UiLiteral("ingame.menu-common.border_size_3e0dd9a7", "Border Size"));
        if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.border_size_3e0dd9a7", "Border Size"), &borderSize, 0.0f, 2.0f, Neurotic::UiLiteral("ingame.menu-common.2f_of_screen_6c96ffc5", "%.2f%% of screen")))
            config->MagnifierBorderSize = borderSize;
        ImGui::SameLine();
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##MagnifierBorderSize"))) config->MagnifierBorderSize.reset();

        ImGui::Separator();
        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.positioning_21fc47f5", "Positioning"));

        bool staticMode = config->MagnifierStaticPosX.has_value() && config->MagnifierStaticPosY.has_value();
        if (staticMode)
        {
            float staticX = config->MagnifierStaticPosX.value();
            sliderWidth(Neurotic::UiLiteral("ingame.menu-common.static_pos_x_f477a2c7", "Static Pos X"));
            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.static_pos_x_f477a2c7", "Static Pos X"), &staticX, 0.0f, 100.0f, "%.1f%%"))
                config->MagnifierStaticPosX = staticX;
            ImGui::SameLine();
            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##MagnifierStaticPosX"))) config->MagnifierStaticPosX = 50.0f;

            float staticY = config->MagnifierStaticPosY.value();
            sliderWidth(Neurotic::UiLiteral("ingame.menu-common.static_pos_y_088ebd23", "Static Pos Y"));
            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.static_pos_y_088ebd23", "Static Pos Y"), &staticY, 0.0f, 100.0f, "%.1f%%"))
                config->MagnifierStaticPosY = staticY;
            ImGui::SameLine();
            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##MagnifierStaticPosY"))) config->MagnifierStaticPosY = 50.0f;

            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_static_position_follow_cursor_3ab5e598", "Reset Static Position (Follow Cursor)")))
            {
                config->MagnifierStaticPosX.reset();
                config->MagnifierStaticPosY.reset();
            }
        }
        else
        {
            // Button to initialize static position mode
            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.set_static_position_d5045a12", "Set Static Position")))
            {
                config->MagnifierStaticPosX = 50.0f;
                config->MagnifierStaticPosY = 50.0f;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(Neurotic::UiLiteral("ingame.menu-common.currently_following_cursor_139b087b", "(Currently following cursor)"));

            float offsetX = config->MagnifierCursorOffsetX.value_or_default();
            sliderWidth(Neurotic::UiLiteral("ingame.menu-common.cursor_offset_x_08e7d438", "Cursor Offset X"));
            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.cursor_offset_x_08e7d438", "Cursor Offset X"), &offsetX, -300.0f, 300.0f, Neurotic::UiLiteral("ingame.menu-common.0f_px_de63d9b4", "%.0f px")))
                config->MagnifierCursorOffsetX = offsetX;
            ImGui::SameLine();
            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##MagnifierCursorOffsetX"))) config->MagnifierCursorOffsetX.reset();

            float offsetY = config->MagnifierCursorOffsetY.value_or_default();
            sliderWidth(Neurotic::UiLiteral("ingame.menu-common.cursor_offset_y_f804ebcf", "Cursor Offset Y"));
            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.cursor_offset_y_f804ebcf", "Cursor Offset Y"), &offsetY, -300.0f, 300.0f, Neurotic::UiLiteral("ingame.menu-common.0f_px_de63d9b4", "%.0f px")))
                config->MagnifierCursorOffsetY = offsetY;
            ImGui::SameLine();
            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##MagnifierCursorOffsetY"))) config->MagnifierCursorOffsetY.reset();
        }

        ImGui::EndDisabled();
        ImGui::Spacing();
    }
}
void MenuCommon::RenderQuirksSettings(RenderMenuContext& ctx)
{
    ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.compatibility_adjustments_detected_for_this_game_35dff75d", "Compatibility adjustments detected for this game are listed below."));
    auto& state = ctx.state;

    // QUIRKS -----------------------------
    if (state.detectedQuirks.empty()) ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.menu-common.no_active_quirks_17ec67ed", "No active quirks."));
    if (state.detectedQuirks.size() > 0)
    {
        ImGui::Spacing();
        {
            ImGui::Spacing();

            for (const auto& quirk : state.detectedQuirks)
            {
                ImGui::TextWrapped("%s",Neurotic::Translate(quirk.c_str()).c_str());
            }
        }
    }
}

void MenuCommon::RenderAdvancedSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    // ADVANCED SETTINGS -----------------------------
    if (ctx.childPage == 2)
    {

        bool pcShaders = config->UsePrecompiledShaders.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_precompiled_shaders_8273108c", "Use Precompiled Shaders"), &pcShaders))
        {
            config->UsePrecompiledShaders = pcShaders;
            state.newBackend = currentBackend;
            MARK_ALL_BACKENDS_CHANGED();
        }


    }

    const bool resourceAvailable = currentFeature != nullptr && !currentFeature->IsFrozen() &&
        (state.api == DX12 || currentFeature->IsWithDx12()) && currentBackend != Upscaler::DLSS && currentBackend != Upscaler::DLSSD;
    // Non-DLSS hotfixes -----------------------------
    if (resourceAvailable)
    {
        // BARRIERS -----------------------------
        if (ctx.childPage == 3)
        {
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.manual_d3d12_arrival_state_overrides_for_sr_reso_729b6193", "Manual D3D12 arrival-state overrides for SR resources. These do not capture or generate inputs; keep Auto unless diagnosing a known state mismatch."));
            AddResourceBarrier(Neurotic::UiLiteral("ingame.menu-common.color_af6f8b8d", "Color"), &config->ColorResourceBarrier);
            AddResourceBarrier(Neurotic::UiLiteral("ingame.menu-common.depth_7d32f5df", "Depth"), &config->DepthResourceBarrier);
            AddResourceBarrier(Neurotic::UiLiteral("ingame.menu-common.motion_6cf3bc87", "Motion"), &config->MVResourceBarrier);
            AddResourceBarrier(Neurotic::UiLiteral("ingame.menu-common.exposure_05c9e7ca", "Exposure"), &config->ExposureResourceBarrier);
            AddResourceBarrier(Neurotic::UiLiteral("ingame.menu-common.mask_3895565a", "Mask"), &config->MaskResourceBarrier);
            AddResourceBarrier(Neurotic::UiLiteral("ingame.menu-common.output_b5db16a0", "Output"), &config->OutputResourceBarrier);
        }

        // HOTFIXES -----------------------------
    }
    if (ctx.childPage == 4 && state.api == DX12)
    {
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.restore_game_root_bindings_after_injected_proces_576e13ac", "Restore game root bindings after injected processing. Leave automatic defaults unless diagnosing state corruption."));

        {
            if (bool crs = config->RestoreComputeSignature.value_or_default();
                ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.restore_compute_root_signature_141cc102", "Restore Compute Root Signature"), &crs))
                config->RestoreComputeSignature = crs;

            if (bool grs = config->RestoreGraphicSignature.value_or_default();
                ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.restore_graphic_root_signature_1d7abe1c", "Restore Graphic Root Signature"), &grs))
                config->RestoreGraphicSignature = grs;
        }
    }
    if ((ctx.childPage == 3 && !resourceAvailable) ||
        (ctx.childPage == 4 && state.api != DX12))
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.these_settings_are_unavailable_for_the_active_gr_d4d19d9e", "These settings are unavailable for the active graphics API or upscaler."));
}

void MenuCommon::RenderLoggingSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // LOGGING -----------------------------
    ImGui::Spacing();
    {
        ImGui::Spacing();

        if (config->LogToConsole.value_or_default() || config->LogToFile.value_or_default() ||
            config->LogToNGX.value_or_default())
            spdlog::default_logger()->set_level((spdlog::level::level_enum) config->LogLevel.value_or_default());
        else
            spdlog::default_logger()->set_level(spdlog::level::off);

        if (bool toFile = config->LogToFile.value_or_default(); ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.to_file_5219586a", "To File"), &toFile))
        {
            config->LogToFile = toFile;
            PrepareLogger();
        }

        ImGui::SameLine(0.0f, 6.0f);
        if (bool toConsole = config->LogToConsole.value_or_default(); ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.to_console_3b158517", "To Console"), &toConsole))
        {
            config->LogToConsole = toConsole;
            PrepareLogger();
        }

        const char* logLevels[] = { Neurotic::UiLiteral("ingame.option.7e14c3d7dd12", "Trace"), Neurotic::UiLiteral("ingame.menu-common.debug_c014083d", "Debug"), Neurotic::UiLiteral("ingame.option.1cb0ba125f84", "Information"), Neurotic::UiLiteral("ingame.option.e981ddae45d8", "Warning"), Neurotic::UiLiteral("ingame.option.54a0e8c17ebb", "Error") };
        const char* selectedLevel = logLevels[config->LogLevel.value_or_default()];

        if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.log_level_3e0f47de", "Log Level"), selectedLevel))
        {
            for (int n = 0; n < 5; n++)
            {
                if (ImGui::Selectable(logLevels[n], (config->LogLevel.value_or_default() == n)))
                {
                    config->LogLevel = n;
                    spdlog::default_logger()->set_level(
                        (spdlog::level::level_enum) config->LogLevel.value_or_default());
                }
            }

            ImGui::EndCombo();
        }
    }
}

void MenuCommon::RenderThemeSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // THEME -----------------------------
    ImGui::Spacing();
    {
        ImGui::Spacing();

        bool lightTheme = config->LightTheme.value_or_default();

        const ImVec4 bgDark = lightTheme ? ImVec4(0.80f, 0.82f, 0.86f, 1.00f) : ImVec4(0.078f, 0.086f, 0.102f, 1.00f);
        const ImVec4 bgMid = lightTheme ? ImVec4(0.89f, 0.91f, 0.95f, 1.00f) : ImVec4(0.093f, 0.100f, 0.116f, 1.00f);
        const ImVec4 bgLight = lightTheme ? ImVec4(0.96f, 0.97f, 0.99f, 1.00f) : ImVec4(0.112f, 0.126f, 0.155f, 1.00f);

        auto Mix = [](const ImVec4& a, const ImVec4& b, float t, float alpha = 1.0f)
        { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha); };

        auto AccentSoft = [&](ImVec4 accent, float alpha = 1.0f)
        { return toneMapColor(lightTheme ? Mix(bgLight, accent, 0.24f, alpha) : Mix(bgDark, accent, 0.32f, alpha)); };

        auto AccentMed = [&](ImVec4 accent, float alpha = 1.0f)
        { return toneMapColor(lightTheme ? Mix(bgLight, accent, 0.42f, alpha) : Mix(bgDark, accent, 0.55f, alpha)); };

        auto AccentStrong = [&](ImVec4 accent, float alpha = 1.0f)
        { return toneMapColor(ImVec4(accent.x, accent.y, accent.z, alpha)); };

        std::optional<Neurotic::Sleek::ContentCard> colorCard;
        colorCard.emplace("##ThemeAccent");
        ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.menu-common.accent_color_a4018520", "Accent Color"));
        ImGui::SameLine((std::max)(ImGui::GetCursorPosX(), (ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x) - Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset"))));
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset###Reset Accent Color")))
        {
            config->MenuAccentColorR.reset();
            config->MenuAccentColorG.reset();
            config->MenuAccentColorB.reset();
            ApplyThemeStyle();
        }
        ImGui::Spacing();

        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.presets_ff204a78", "Presets:"));
        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.moonlight_b8901907", "Moonlight")),6);

        ImVec4 colorMoonlight = { 0.97f, 1.00f, 0.50f, 1.0f };
        ImVec4 colorBlue = { 0.00f, 0.40f, 0.77f, 1.0f };
        ImVec4 colorTeal = { 0.00f, 1.00f, 0.91f, 1.0f };
        ImVec4 colorGray = { 0.54f, 0.54f, 0.54f, 1.0f };
        ImVec4 colorYellow = { 1.00f, 0.89f, 0.00f, 1.0f };
        ImVec4 colorGreen = { 0.25f, 1.00f, 0.00f, 1.0f };
        ImVec4 colorRed = { 1.00f, 0.00f, 0.00f, 1.0f };
        ImVec4 colorOrange = { 1.00f, 0.52f, 0.00f, 1.0f };
        ImVec4 colorPurple = { 0.576f, 0.00f, 1.00f, 1.0f };

        ImVec4 color = {};

        color = colorMoonlight;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.moonlight_b8901907", "Moonlight")))
        {
            ImGui::PopStyleColor(3);
            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }
        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.blue_c0c96606", "Blue")),6);

        color = colorBlue;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.blue_c0c96606", "Blue")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.teal_1c9ce421", "Teal")),6);

        color = colorTeal;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.teal_1c9ce421", "Teal")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.gray_cb612120", "Gray")),6);

        color = colorGray;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.gray_cb612120", "Gray")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.yellow_7f79c961", "Yellow")),6);

        color = colorYellow;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.yellow_7f79c961", "Yellow")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.green_831f4ad5", "Green")),6);

        color = colorGreen;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.green_831f4ad5", "Green")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.red_a09023c8", "Red")),6);

        color = colorRed;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.red_a09023c8", "Red")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.orange_5ccf5a40", "Orange")),6);

        color = colorOrange;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.orange_5ccf5a40", "Orange")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.purple_d513957f", "Purple")),6);

        color = colorPurple;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.purple_d513957f", "Purple")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        float accentColor[3] = { config->MenuAccentColorR.value_or_default(),
                                 config->MenuAccentColorG.value_or_default(),
                                 config->MenuAccentColorB.value_or_default() };
        if (!config->MenuAccentColorR.has_value() && !config->MenuAccentColorG.has_value() && !config->MenuAccentColorB.has_value())
        {
            accentColor[0] = 0.36f; accentColor[1] = 0.62f; accentColor[2] = 0.98f;
        }

        if (ImGui::ColorEdit3(Neurotic::UiLiteral("ingame.menu-common.custom_accent_color_55295012", "Custom Accent Color"), accentColor))
        {
            config->MenuAccentColorR = accentColor[0];
            config->MenuAccentColorG = accentColor[1];
            config->MenuAccentColorB = accentColor[2];
            ApplyThemeStyle();
        }

        ImGui::Spacing();



        ImGui::Spacing();

        colorCard.reset();
        colorCard.emplace("##ThemeBackground");
        ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.menu-common.background_color_85651a50", "Background Color"));
        ImGui::SameLine((std::max)(ImGui::GetCursorPosX(), (ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x) - Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset"))));
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset###Reset BG Colour")))
        {
            config->MenuBGColorR.reset();
            config->MenuBGColorG.reset();
            config->MenuBGColorB.reset();
            config->MenuBGColorA.reset();
            ApplyThemeStyle();
        }
        ImGui::Spacing();

        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.presets_ff204a78", "Presets:"));
        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.blue_c0c96606", "Blue##2")),6);

        color = colorBlue;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.blue_c0c96606", "Blue##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.teal_1c9ce421", "Teal##2")),6);

        color = colorTeal;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.teal_1c9ce421", "Teal##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.gray_cb612120", "Gray##2")),6);

        color = colorGray;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.gray_cb612120", "Gray##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.yellow_7f79c961", "Yellow##2")),6);

        color = colorYellow;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.yellow_7f79c961", "Yellow##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.green_831f4ad5", "Green##2")),6);

        color = colorGreen;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.green_831f4ad5", "Green##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.red_a09023c8", "Red##2")),6);

        color = colorRed;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.red_a09023c8", "Red##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.orange_5ccf5a40", "Orange##2")),6);

        color = colorOrange;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.orange_5ccf5a40", "Orange##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.purple_d513957f", "Purple##2")),6);

        color = colorPurple;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.purple_d513957f", "Purple##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        float bgColor[3] = { config->MenuBGColorR.value_or_default(), config->MenuBGColorG.value_or_default(),
                             config->MenuBGColorB.value_or_default() };

        if (ImGui::ColorEdit3(Neurotic::UiLiteral("ingame.menu-common.custom_bg_colour_393e603b", "Custom BG Colour"), bgColor))
        {
            config->MenuBGColorR = bgColor[0];
            config->MenuBGColorG = bgColor[1];
            config->MenuBGColorB = bgColor[2];
            ApplyThemeStyle();
        }

        ImGui::Spacing();

        auto alpha = config->MenuBGColorA.value_or_default();
        if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.background_alpha_202a5511", "Background Alpha"), &alpha, 0.0f, 1.0f))
        {
            config->MenuBGColorA = alpha;
            ApplyThemeStyle();
        }
        Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")));
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##BackgroundAlpha"))) { config->MenuBGColorA = std::optional<float>{}; ApplyThemeStyle(); }

        ImGui::Spacing();



        ImGui::Spacing();
    }
}

void MenuCommon::RenderFpsOverlaySettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // FPS OVERLAY -----------------------------
    ImGui::Spacing();
    {
        ImGui::Spacing();

        bool fpsEnabled = config->ShowFps.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.fps_overlay_enabled_c213064b", "FPS Overlay Enabled"), &fpsEnabled))
            config->ShowFps = fpsEnabled;

        ImGui::SameLine(0.0f, 6.0f);

        bool fpsHorizontal = config->FpsOverlayHorizontal.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.horizontal_19a0b570", "Horizontal"), &fpsHorizontal))
            config->FpsOverlayHorizontal = fpsHorizontal;

        const char* fpsPosition[] = { Neurotic::UiLiteral("ingame.menu-common.top_left_2424cf90", "Top Left"), Neurotic::UiLiteral("ingame.menu-common.top_right_525c94c3", "Top Right"), Neurotic::UiLiteral("ingame.menu-common.bottom_left_41396c00", "Bottom Left"), Neurotic::UiLiteral("ingame.menu-common.bottom_right_8ac4ab43", "Bottom Right") };
        const char* selectedPosition = fpsPosition[config->FpsOverlayPosition.value_or_default()];

        if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.overlay_position_ba34eb2d", "Overlay Position"), selectedPosition))
        {
            for (int n = 0; n < std::size(fpsPosition); n++)
            {
                if (ImGui::Selectable(fpsPosition[n], (config->FpsOverlayPosition.value_or_default() == n)))
                    config->FpsOverlayPosition = (FpsOverlayPos) n;
            }

            ImGui::EndCombo();
        }

        const char* fpsType[] = { Neurotic::UiLiteral("ingame.menu-common.just_fps_b8500689", "Just FPS"), Neurotic::UiLiteral("ingame.option.3fee95da5ab6", "Simple"),       Neurotic::UiLiteral("ingame.option.6d46fcd50a63", "Detailed"),      Neurotic::UiLiteral("ingame.provider.9a6351f48d68", "Detailed + Graph"),
                                  Neurotic::UiLiteral("ingame.option.008dacb6d1e8", "Full"),     Neurotic::UiLiteral("ingame.provider.e7fac553f9ce", "Full + Graph"), Neurotic::UiLiteral("ingame.menu-common.reflex_timings_6c65fa6b", "Reflex timings") };
        const char* selectedType = fpsType[config->FpsOverlayType.value_or_default()];

        if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.overlay_type_edf909ab", "Overlay Type"), selectedType))
        {
            for (int n = 0; n < std::size(fpsType); n++)
            {
                if (ImGui::Selectable(fpsType[n], (config->FpsOverlayType.value_or_default() == n)))
                    config->FpsOverlayType = (FpsOverlay) n;
            }

            ImGui::EndCombo();
        }

        float fpsAlpha = config->FpsOverlayAlpha.value_or_default();
        if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.background_alpha_202a5511", "Background Alpha"), &fpsAlpha, 0.0f, 1.0f, "%.2f"))
            config->FpsOverlayAlpha = fpsAlpha;

        const char* options[] = { Neurotic::UiLiteral("ingame.menu-common.same_as_menu_7addf271", "Same as menu"), "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1", "1.2",
                                  "1.3",          "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
        int currentIndex = std::max(((int) (config->FpsScale.value_or(0.0f) * 10.0f)) - 4, 0);
        float values[] = { 0.0f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f,
                           1.3f, 1.4f, 1.5f, 1.6f, 1.7f, 1.8f, 1.9f, 2.0f };

        if (ImGui::SliderInt(Neurotic::UiLiteral("ingame.menu-common.scale_92419c71", "Scale"), &currentIndex, 0, IM_ARRAYSIZE(options) - 1, options[currentIndex],
                             ImGuiSliderFlags_ClampOnInput))
        {
            if (currentIndex == 0)
                config->FpsScale.reset();
            else
                config->FpsScale = values[currentIndex];
        }

        bool useTheme = config->OverlaysUseTheme.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_theme_colors_730178f8", "Use Theme Colors"), &useTheme))
            config->OverlaysUseTheme = useTheme;
    }
}

void MenuCommon::RenderUpscalerInputsSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    // UPSCALER INPUTS -----------------------------
    ImGui::Spacing();
    ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.sr_input_hooks_1c586e01", "SR input hooks"));
    ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.save_and_restart_the_game_after_changing_input_h_56ce7d24", "Save and restart the game after changing input hooks or pattern matching."));
    {
        ImGui::Spacing();

        if (ImGui::BeginTable("##UpscalerInputColumns", 3, ImGuiTableFlags_SizingStretchSame))
        {
        ImGui::TableNextColumn(); ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.menu-common.fsr_2_58acabbb", "FSR 2"));
        if (config->EnableFsr2Inputs.value_or_default())
        {
            bool fsr2Inputs = config->UseFsr2Inputs.value_or_default();
            bool fsr2Pattern = config->Fsr2Pattern.value_or_default();

            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_fsr2_inputs_674aadd5", "Use Fsr2 Inputs"), &fsr2Inputs))
                config->UseFsr2Inputs = fsr2Inputs;

            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_fsr2_pattern_matching_ee835d06", "Use Fsr2 Pattern Matching"), &fsr2Pattern))
                config->Fsr2Pattern = fsr2Pattern;
            ShowTooltip(Neurotic::UiLiteral("ingame.menu-common.this_setting_will_become_active_on_next_boot_01391268", "This setting will become active on next boot!"));
        }

        ImGui::TableNextColumn(); ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.menu-common.fsr_3_c6d7704a", "FSR 3"));
        if (config->EnableFsr3Inputs.value_or_default())
        {
            bool fsr3Inputs = config->UseFsr3Inputs.value_or_default();
            bool fsr3Pattern = config->Fsr3Pattern.value_or_default();

            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_fsr3_inputs_ba64b660", "Use Fsr3 Inputs"), &fsr3Inputs))
                config->UseFsr3Inputs = fsr3Inputs;

            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_fsr3_pattern_matching_ab154bdd", "Use Fsr3 Pattern Matching"), &fsr3Pattern))
                config->Fsr3Pattern = fsr3Pattern;
            ShowTooltip(Neurotic::UiLiteral("ingame.menu-common.this_setting_will_become_active_on_next_boot_01391268", "This setting will become active on next boot!"));
        }

        ImGui::TableNextColumn(); ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.menu-common.ffx_inputs_b916bde8", "FFX Inputs"));
        if (config->EnableFfxInputs.value_or_default())
        {
            bool ffxInputs = config->UseFfxInputs.value_or_default();

            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.use_ffx_inputs_708d5303", "Use Ffx Inputs"), &ffxInputs))
                config->UseFfxInputs = ffxInputs;
        }
        ImGui::EndTable();
        }
    }
}

void MenuCommon::RenderVsyncSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;

    if (state.swapchainApi == Vulkan)
    {
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.these_settings_are_unavailable_for_the_active_gr_d4d19d9e", "These settings are unavailable for the active graphics API or upscaler."));
        return;
    }

    // V-SYNC -----------------------------
    {

        const auto fitControl = [](const char* label) {
            const float reserve = ImGui::CalcTextSize(label).x + ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")).x +
                ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x * 4;
            ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - reserve));
        };
        const char* modes[] = { Neurotic::UiLiteral("ingame.provider.5c412a262086", "Game"), Neurotic::UiLiteral("ingame.menu-common.on_d2f9df8a", "On"), Neurotic::UiLiteral("ingame.objectruleeditor.off_dc516be5", "Off") };
        int mode = !config->ForceVsync.has_value() ? 0 : config->ForceVsync.value() ? 1 : 2;
        fitControl(Neurotic::UiLiteral("ingame.menu-common.v_sync_f402ef17", "V-Sync"));
        bool vsyncChanged = ImGui::Combo(Neurotic::UiLiteral("ingame.menu-common.v_sync_f402ef17", "V-Sync"), &mode, modes, IM_ARRAYSIZE(modes));
        if (vsyncChanged) { if (mode == 0) config->ForceVsync.reset(); else config->ForceVsync = mode == 1; }
        ImGui::SameLine();
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##10"))) { config->ForceVsync.reset(); mode = 0; vsyncChanged = true; }
        ImGui::BeginDisabled(mode != 1);
        int interval = static_cast<int>((std::max)(1u, config->VsyncInterval.value_or_default()));
        fitControl(Neurotic::UiLiteral("ingame.menu-common.sync_interval_bd987355", "Sync interval"));
        if (ImGui::SliderInt(Neurotic::UiLiteral("ingame.menu-common.sync_interval_bd987355", "Sync interval"), &interval, 1, 3)) { config->VsyncInterval = interval; vsyncChanged = true; }
        ImGui::SameLine();
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##VsyncInterval"))) { config->VsyncInterval.reset(); vsyncChanged = true; }
        ImGui::EndDisabled();
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.dxgi_presentation_override_on_waits_for_the_sele_51d73e62", "DXGI presentation override. On waits for the selected number of refreshes; Game retains the game's choice."));
        if (vsyncChanged && state.activeFgOutput == FGOutput::XeFG && state.currentFG != nullptr)
        {
            // To prevent XeLL issues
            LOG_DEBUG("V-Sync change detected, forcing XeFG reset");
            state.WAR_xefgRequestFGToggle = true;
        }
    }
}

void MenuCommon::RenderMipmapBiasSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    if (state.swapchainApi == Vulkan) { ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.mipmap_bias_controls_are_unavailable_for_vulkan_ed31495f", "Mipmap bias controls are unavailable for Vulkan.")); return; }

    // MIPMAP BIAS -----------------------------
    ImGui::Spacing();
    {
        ImGui::Spacing();
        if (config->MipmapBiasOverride.has_value() && _mipBias == 0.0f)
            _mipBias = config->MipmapBiasOverride.value();

        const float biasTail = ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.mipmap_bias_c3d9e7cf", "Mipmap Bias")).x + Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")) +
            ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth((std::max)(1.0f, (std::min)(ImGui::CalcItemWidth(),
            ImGui::GetContentRegionAvail().x - biasTail)));
        ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.mipmap_bias_c3d9e7cf", "Mipmap Bias##2"), &_mipBias, -15.0f, 15.0f, "%.6f");
        ImGui::SameLine();
        ImGui::BeginDisabled(!config->MipmapBiasOverride.has_value() && _mipBias == 0.0f);
        {
            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##MipmapBias")))
            {
                config->MipmapBiasOverride.reset();
                _mipBias = 0.0f;
                state.lastMipBias = 100.0f;
                state.lastMipBiasMax = -100.0f;
            }
        }
        ImGui::EndDisabled();

        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.can_help_with_blurry_textures_in_broken_games_ne_a14dd048", "Can help with blurry textures in broken games\n"
                       "Negative values will make textures sharper\n"
                       "Positive values will make textures more blurry\n\n"
                       "Has a small performance impact"));

        ImGui::BeginDisabled(!config->MipmapBiasOverride.has_value());
        {
            ImGui::BeginDisabled(config->MipmapBiasScaleOverride.has_value() &&
                                 config->MipmapBiasScaleOverride.value());
            {
                bool mbFixed = config->MipmapBiasFixedOverride.value_or_default();
                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.mb_fixed_override_c0645fc7", "MB Fixed Override"), &mbFixed))
                {
                    config->MipmapBiasScaleOverride.reset();
                    config->MipmapBiasFixedOverride = mbFixed;
                }

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.apply_same_override_value_to_all_textures_bfe9b2fb", "Apply same override value to all textures"));
            }
            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 6.0f);

            ImGui::BeginDisabled(config->MipmapBiasFixedOverride.has_value() &&
                                 config->MipmapBiasFixedOverride.value());
            {
                bool mbScale = config->MipmapBiasScaleOverride.value_or_default();
                if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.mb_scale_override_403bb218", "MB Scale Override"), &mbScale))
                {
                    config->MipmapBiasFixedOverride.reset();
                    config->MipmapBiasScaleOverride = mbScale;
                }

                ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.apply_override_value_as_scale_multiplier_when_us_8bb69bd7", "Apply override value as scale multiplier\n"
                               "When using scale mode, please use positive\n"
                               "override values to increase sharpness!"));
            }
            ImGui::EndDisabled();

            bool mbAll = config->MipmapBiasOverrideAll.value_or_default();
            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.mb_override_all_textures_da27e098", "MB Override All Textures"), &mbAll))
                config->MipmapBiasOverrideAll = mbAll;

            ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.override_all_textures_mipmap_values_normally_opt_08f27841", "Override all textures mipmap values\n"
                           "Normally OptiScaler only overrides\n"
                           "below zero mipmap values!"));
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(config->MipmapBiasOverride.has_value() && config->MipmapBiasOverride.value() == _mipBias);
        {
            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.set_38ccef08", "Set")))
            {
                config->MipmapBiasOverride = _mipBias;
                state.lastMipBias = 100.0f;
                state.lastMipBiasMax = -100.0f;
            }
        }
        ImGui::EndDisabled();

        if (currentFeature != nullptr && !currentFeature->IsFrozen())
        {
            ImGui::SameLine(0.0f, 6.0f);

            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.calculate_mipmap_bias_441c72b5", "Calculate Mipmap Bias")))
                _showMipmapCalcWindow = true;
        }

        if (config->MipmapBiasOverride.has_value())
        {
            if (config->MipmapBiasFixedOverride.value_or_default())
            {
                ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.current_3f_3f_target_3f_a6115ac5", "Current : %.3f / %.3f, Target: %.3f"), state.lastMipBias, state.lastMipBiasMax,
                            config->MipmapBiasOverride.value());
            }
            else if (config->MipmapBiasScaleOverride.value_or_default())
            {
                ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.current_3f_3f_target_base_3f_f9234585", "Current : %.3f / %.3f, Target: Base * %.3f"), state.lastMipBias, state.lastMipBiasMax,
                            config->MipmapBiasOverride.value());
            }
            else
            {
                ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.current_3f_3f_target_base_3f_d10a8645", "Current : %.3f / %.3f, Target: Base + %.3f"), state.lastMipBias, state.lastMipBiasMax,
                            config->MipmapBiasOverride.value());
            }
        }
        else
        {
            ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.current_3f_3f_177a2695", "Current : %.3f / %.3f"), state.lastMipBias, state.lastMipBiasMax);
        }

        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.will_be_applied_after_resolution_preset_change_467faf47", "Will be applied after RESOLUTION/PRESET change !!!"));
    }
}

void MenuCommon::RenderAnisotropicFilteringSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    if (state.swapchainApi == Vulkan)
    {
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.these_settings_are_unavailable_for_the_active_gr_d4d19d9e", "These settings are unavailable for the active graphics API or upscaler."));
        return;
    }

    {
        ImGui::PushItemWidth(65.0f * menuResScale);

        auto selectedAF =
            config->AnisotropyOverride.has_value() ? std::to_string(config->AnisotropyOverride.value()) : Neurotic::UiLiteral("ingame.menu-common.auto_b980aecf", "Auto");
        if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.force_anisotropic_filtering_9fdb47d3", "Force Anisotropic Filtering"), selectedAF.c_str()))
        {
            if (ImGui::Selectable(Neurotic::UiLiteral("ingame.menu-common.auto_b980aecf", "Auto"), !config->AnisotropyOverride.has_value()))
                config->AnisotropyOverride.reset();

            if (ImGui::Selectable("1", config->AnisotropyOverride.value_or(0) == 1))
                config->AnisotropyOverride = 1;

            if (ImGui::Selectable("2", config->AnisotropyOverride.value_or(0) == 2))
                config->AnisotropyOverride = 2;

            if (ImGui::Selectable("4", config->AnisotropyOverride.value_or(0) == 4))
                config->AnisotropyOverride = 4;

            if (ImGui::Selectable("8", config->AnisotropyOverride.value_or(0) == 8))
                config->AnisotropyOverride = 8;

            if (ImGui::Selectable("16", config->AnisotropyOverride.value_or(0) == 16))
                config->AnisotropyOverride = 16;

            ImGui::EndCombo();
        }

        ImGui::PopItemWidth();

        bool afComp = config->AnisotropyModifyComp.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.modify_compare_e9b44002", "Modify Compare"), &afComp))
            config->AnisotropyModifyComp = afComp;

        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.update_comparison_filters_15d6a06c", "Update comparison filters"));

        ImGui::SameLine(0.0f, 6.0f);

        bool afMinMax = config->AnisotropyModifyMinMax.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.modify_min_max_836ae7d0", "Modify Min/Max"), &afMinMax))
            config->AnisotropyModifyMinMax = afMinMax;

        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.update_min_max_filters_12aaecdb", "Update min/max filters"));

        bool afSkipPoint = config->AnisotropySkipPointFilter.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.skip_point_filters_48a35d84", "Skip Point Filters"), &afSkipPoint))
            config->AnisotropySkipPointFilter = afSkipPoint;

        ShowHelpMarker(Neurotic::UiLiteral("ingame.menu-common.skip_updating_of_point_filters_8631cabf", "Skip updating of point filters"));

        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.will_might_be_applied_after_resolution_preset_ch_06e477d1", "Will might be applied after RESOLUTION/PRESET change !!!"));
    }
}

void MenuCommon::RenderScreenshotKeybind(Config* config)
{
    // One binding and one key-listening state, shown in both locations.
    static auto screenshot = Keybind(Neurotic::UiLiteral("ingame.menu-common.comparison_screenshots_f7e8e471", "Comparison screenshots"), 15);
    // Keybind::Render emits a five-column table row. The screenshot panel
    // also calls this helper outside General's keybind table.
    if (ImGui::GetCurrentTable())
    {
        screenshot.Render(config->ScreenshotKey);
        return;
    }
    if (ImGui::BeginTable("##ScreenshotKeybind", 5,
                         ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
    {
        ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.provider.64cff1319d2f", "Action"), ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.provider.42e37604b638", "Modifier"), ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("##Plus", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize());
        ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.provider.dbb3807268a5", "Keybind"), ImGuiTableColumnFlags_WidthStretch, 0.36f);
        ImGui::TableSetupColumn("##Reset", ImGuiTableColumnFlags_WidthFixed, Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")));
        screenshot.Render(config->ScreenshotKey);
        ImGui::EndTable();
    }
}

void MenuCommon::RenderKeybindSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    {

        bool escapeClosesMenu = config->EscapeClosesMenu.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.esc_closes_menu_19412502", "Esc closes menu###Escape button closes menu"), &escapeClosesMenu))
            config->EscapeClosesMenu = escapeClosesMenu;

        static auto menu = Keybind(Neurotic::UiLiteral("ingame.menu-common.toggle_menu_88f1b004", "Toggle Menu"), 10);
        static auto fpsOverlay = Keybind(Neurotic::UiLiteral("ingame.menu-common.fps_overlay_85d6c2e4", "FPS Overlay"), 11);
        static auto fpsOverlayCycle = Keybind(Neurotic::UiLiteral("ingame.menu-common.fps_overlay_cycle_249d873c", "FPS Overlay Cycle"), 12);
        static auto fgEnable = Keybind(Neurotic::UiLiteral("ingame.dlssnr-menu.frame_generation_c41396f4", "Frame Generation"), 13);
        static auto dlssNrToggle = Keybind(Neurotic::UiLiteral("ingame.menu-common.neural_rendering_5cde3731", "Neural Rendering"), 14);

        if (ImGui::BeginTable("##KeybindSettings", 5,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
        {
            ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.provider.64cff1319d2f", "Action"), ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.provider.42e37604b638", "Modifier"), ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("##Plus", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize());
        ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.provider.dbb3807268a5", "Keybind"), ImGuiTableColumnFlags_WidthStretch, 0.36f);
        ImGui::TableSetupColumn("##Reset", ImGuiTableColumnFlags_WidthFixed, Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")));
            ImGui::TableHeadersRow();
            menu.Render(config->ShortcutKey);
            fpsOverlay.Render(config->FpsShortcutKey);
            fpsOverlayCycle.Render(config->FpsCycleShortcutKey);
            fgEnable.Render(config->FGShortcutKey);
            dlssNrToggle.Render(config->DlssNrToggleKey);
            RenderScreenshotKeybind(config);
            ImGui::EndTable();
        }
    }
}

// Draw the supplied block-letter mark as geometry. The menu font can be proportional or
// replaced by the user, so drawing the box strokes keeps every column aligned at any UI scale.
static void RenderNeuroticBanner(float menuResScale)
{
    static constexpr const char32_t* art[] = {
        U"\u2588\u2588\u2588\u2557   \u2588\u2588\u2557\u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2557\u2588\u2588\u2557   \u2588\u2588\u2557\u2588\u2588\u2588\u2588\u2588\u2588\u2557  \u2588\u2588\u2588\u2588\u2588\u2588\u2557 \u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2557\u2588\u2588\u2557 \u2588\u2588\u2588\u2588\u2588\u2588\u2557",
        U"\u2588\u2588\u2588\u2588\u2557  \u2588\u2588\u2551\u2588\u2588\u2554\u2550\u2550\u2550\u2550\u255D\u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2557\u2588\u2588\u2554\u2550\u2550\u2550\u2588\u2588\u2557\u255A\u2550\u2550\u2588\u2588\u2554\u2550\u2550\u255D\u2588\u2588\u2551\u2588\u2588\u2554\u2550\u2550\u2550\u2550\u255D",
        U"\u2588\u2588\u2554\u2588\u2588\u2557 \u2588\u2588\u2551\u2588\u2588\u2588\u2588\u2588\u2557  \u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255D\u2588\u2588\u2551   \u2588\u2588\u2551   \u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2551",
        U"\u2588\u2588\u2551\u255A\u2588\u2588\u2557\u2588\u2588\u2551\u2588\u2588\u2554\u2550\u2550\u255D  \u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2557\u2588\u2588\u2551   \u2588\u2588\u2551   \u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2551",
        U"\u2588\u2588\u2551 \u255A\u2588\u2588\u2588\u2588\u2551\u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2557\u255A\u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255D\u2588\u2588\u2551  \u2588\u2588\u2551\u255A\u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255D   \u2588\u2588\u2551   \u2588\u2588\u2551\u255A\u2588\u2588\u2588\u2588\u2588\u2588\u2557",
        U"\u255A\u2550\u255D  \u255A\u2550\u2550\u2550\u255D\u255A\u2550\u2550\u2550\u2550\u2550\u2550\u255D \u255A\u2550\u2550\u2550\u2550\u2550\u255D \u255A\u2550\u255D  \u255A\u2550\u255D \u255A\u2550\u2550\u2550\u2550\u2550\u255D    \u255A\u2550\u255D   \u255A\u2550\u255D \u255A\u2550\u2550\u2550\u2550\u2550\u255D"
    };
    const float available = ImGui::GetContentRegionAvail().x;
    if (available < 64.0f * 4.0f * menuResScale)
    {
        ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.menu-common.neurotic_813739a7", "NeuRotic"));
        return;
    }
    const float cell = std::clamp(available / 66.0f, 4.0f * menuResScale, 14.0f * menuResScale);
    const float height = cell * 1.25f;
    const float stroke = (std::max)(1.0f, cell * 0.2f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    for (int row = 0; row < 6; ++row)
    {
        for (int column = 0; art[row][column] != U'\0'; ++column)
        {
            const char32_t glyph = art[row][column];
            const float x = origin.x + column * cell;
            const float y = origin.y + row * height;
            const float midX = x + cell * 0.5f;
            const float midY = y + height * 0.5f;
            const auto horizontal = [&](float left, float right) {
                draw->AddRectFilled({ left, midY - stroke * 0.5f }, { right, midY + stroke * 0.5f }, color);
            };
            const auto vertical = [&](float top, float bottom) {
                draw->AddRectFilled({ midX - stroke * 0.5f, top }, { midX + stroke * 0.5f, bottom }, color);
            };
            switch (glyph)
            {
                case U'\u2588': draw->AddRectFilled({ x, y }, { x + cell, y + height }, color); break;
                case U'\u2550': horizontal(x, x + cell); break;
                case U'\u2551': vertical(y, y + height); break;
                case U'\u2554': horizontal(midX, x + cell); vertical(midY, y + height); break;
                case U'\u2557': horizontal(x, midX); vertical(midY, y + height); break;
                case U'\u255A': horizontal(midX, x + cell); vertical(y, midY); break;
                case U'\u255D': horizontal(x, midX); vertical(y, midY); break;
                default: break;
            }
        }
    }
    ImGui::Dummy({ 64.0f * cell, 6.0f * height });
    ImGui::Spacing();
}

void MenuCommon::RenderGeneralPage(RenderMenuContext& ctx)
{
    Neurotic::Sleek::ContentCard card("##GeneralControls");
    if (ctx.childPage == 0)
    {
        const char* repositoryUrl = "https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork";
        const auto& update = ctx.versionStatus;
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.25f);
        ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.menu-common.neurotic_813739a7", "NeuRotic"));
        ImGui::PopFont();
        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.installed_version_s_291edf6a", "Installed Version: %s"),Neurotic::Translate(ctx.currentVersionText.c_str()).c_str());
        ImGui::Spacing();
        std::string message;
        ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        if (!ctx.config->CheckForUpdate.value_or_default()) message = Neurotic::UiMessage("ingame.menu-common.update_checks_are_disabled_578cabf7", "Update checks are disabled.");
        else if (!update.completed) message = Neurotic::UiMessage("ingame.menu-common.checking_for_updates_3c759742", "Checking for updates...");
        else if (update.updateAvailable && !update.latestTag.empty())
        {
            message = Neurotic::UiMessage("ingame.menu-common.update_available_a060d2d5", "Update available");
            color = toneMapColor(ImVec4(1.f,.65f,.2f,1));
        }
        else if (!update.error.empty()) { message = Neurotic::UiMessage("ingame.menu-common.update_status_unknown_af6f1314", "Update status unknown"); color = toneMapColor(ImVec4(1.f,.3f,.3f,1)); }
        else { message = Neurotic::UiMessage("ingame.menu-common.up_to_date_cfecbb6b", "Up to date"); color = toneMapColor(ImVec4(.35f,.9f,.5f,1)); }
        DlssNr::StatusPanel::Line("##UpdateStatus", message, color);
        ImGui::TextLinkOpenURL(Neurotic::UiLiteral("ingame.menu-common.open_neurotic_on_github_bb120a94", "Open NeuRotic on GitHub"), repositoryUrl);
        if (update.completed && update.updateAvailable && !update.latestUrl.empty())
        { ImGui::SameLine(); ImGui::TextLinkOpenURL(Neurotic::UiLiteral("ingame.menu-common.view_patch_notes_e8bebdfd", "View patch notes"), update.latestUrl.c_str()); }
    }
    if (ctx.childPage == 1) RenderKeybindSettings(ctx);
    if (ctx.childPage == 2)
    {
        ImGui::Spacing();
        ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.enable_game_input_ba030e3f", "Enable Game Input"));
        if (!menuInputDraft.initialized) ResetMenuInputDraft(*ctx.config);
        bool changed = ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.mouse_46a49fbf", "Mouse###Allow mouse input in game"), &menuInputDraft.mouse);
        changed |= ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.keyboard_cdf8d634", "Keyboard###Allow keyboard input in game"), &menuInputDraft.keyboard);
        changed |= ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.controller_e4297533", "Controller###Allow controller input in game"), &menuInputDraft.controller);
        menuInputDraft.dirty |= changed;
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.save_d9978c49", "Save###Save Input Settings")))
        {
            if (ctx.config->SaveMenuInputSettings(menuInputDraft.mouse, menuInputDraft.keyboard,
                                                  menuInputDraft.controller))
            {
                menuInputDraft.dirty = false;
                UpdateMenuInputMode(ctx);
            }
            else
                ImGui::OpenPopup(Neurotic::UiLiteral("ingame.menu-common.input_settings_could_not_be_saved_ce5dda14", "Input settings could not be saved"));
        }
        DlssNr::StatusPanel::Line("##PendingChanges", menuInputDraft.dirty ? Neurotic::UiLiteral("ingame.menu-common.pending_changes_apply_only_after_saving_dc2202e5", "Pending changes apply only after saving.") : "",
            toneMapColor(ImVec4(1.0f, 0.72f, 0.25f, 1.0f)));
        if (ImGui::BeginPopupModal(Neurotic::UiLiteral("ingame.menu-common.input_settings_could_not_be_saved_ce5dda14", "Input settings could not be saved"), nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.the_existing_gameplay_input_policy_is_still_acti_4aec325a", "The existing gameplay-input policy is still active. Check that OptiScaler.ini can be written, then try again."));
            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.ok_1b5ecc61", "OK"))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

    }
    if (ctx.childPage == 3) RenderThemeSettings(ctx);
    if (ctx.childPage == 4)
    {
        const float brightnessTail = ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.ui_brightness_027627c2", "UI Brightness")).x +
            ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")).x + ImGui::GetStyle().FramePadding.x * 2 +
            ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth((std::max)(1.0f, (std::min)(ImGui::GetContentRegionAvail().x * 0.5f,
            ImGui::GetContentRegionAvail().x - brightnessTail)));
        float brightness = ctx.config->MenuBrightness.value_or_default();
        if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.ui_brightness_027627c2", "UI Brightness"), &brightness, Neurotic::UiBrightness::Minimum,
                               Neurotic::UiBrightness::Maximum, Neurotic::UiLiteral("ingame.dlssnr-menu.2fx_093b9cc9", "%.2fx"), ImGuiSliderFlags_AlwaysClamp))
            ctx.config->MenuBrightness = Neurotic::UiBrightness::Clamp(brightness);
        ImGui::SameLine();
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset"))) ctx.config->MenuBrightness = 1.0f;

    }
    if (ctx.childPage == 5)
    {
        bool off = ctx.config->MenuReduceMotion.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.menu-common.turn_animations_off_76c1ec66", "Turn animations off."), &off))
        {
            ctx.config->MenuReduceMotion = off;
            reduceMenuMotion = off;
        }
    }

}

void MenuCommon::RenderUpscalingPage(RenderMenuContext& ctx)
{
    if(ctx.childPage==5 || ctx.childPage==4){RenderUpscalerPreflight(ctx);return;}
    const bool active=ctx.currentFeature != nullptr && !ctx.currentFeature->IsFrozen();
    if (!active) ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.upscaler_settings_become_available_when_an_upsca_5ef72257", "Upscaler settings become available when an upscaler is active."));
    RenderActiveUpscalerSettings(ctx);
    if (ctx.childPage == 2) RenderFsrCommonSettings(ctx);
    if (ctx.childPage == 0 || ctx.childPage == 3 || ctx.childPage == 4) RenderActiveImageSettings(ctx);
    const bool dlssSettings=active && ((ctx.config->DLSSEnabled.value_or_default() &&
        currentBackend==Upscaler::DLSS && ctx.currentFeature->Version().major>2) ||
        ctx.currentFeature->GetUpscalerType()==Upscaler::DLSSD);
    if (active && ctx.childPage == 1 && !dlssSettings)
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.dlss_settings_are_unavailable_for_the_active_ups_9526bf97", "DLSS settings are unavailable for the active upscaler."));
    if (active && ctx.childPage == 2 && currentBackend!=Upscaler::XeSS && !IsFsr(currentBackend) &&
        ctx.state.activeFgOutput!=FGOutput::FSRFG)
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.these_settings_become_available_when_fsr_or_xess_7a9c1386", "These settings become available when FSR or XeSS is active."));
}

static bool RenderExperimentalSaveConfirmation(const char* popup, const Config& config,
                                                const DlssNr::ExperimentalPolicy::UiDraft& draft,
                                                float menuResScale)
{
    if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return false;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f * menuResScale);
    ImGui::TextWrapped(Neurotic::UiLiteral("ingame.experimentalmfgcontrols.enable_the_selected_experimental_options_these_p_fc7015ea", "Enable the selected experimental options? These paths are untested or still under development and may cause instability or crashes."));
    const bool saved = config.DlssNrExperimentalMode.value_or_default();
    if (draft.preSrSoftReset && !(saved && config.DlssNrPreSrSoftReset.value_or_default()))
        ImGui::BulletText(Neurotic::UiLiteral("ingame.menu-common.preserve_nr_during_camera_cuts_7aef5a21", "Preserve NR During Camera Cuts"));
    if (draft.preparedDepth && !(saved && config.DlssNrPreparedDepth.value_or_default()))
        ImGui::BulletText(Neurotic::UiLiteral("ingame.menu-common.depth_compatibility_experimental_0a1f074e", "Depth compatibility (Experimental)"));
    ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.confirmed_gpu_corruption_safety_checks_remain_ac_ba1b2228", "Confirmed GPU-corruption safety checks remain active. The selected changes apply only after you agree and the settings save succeeds."));
    ImGui::PopTextWrapPos();
    bool confirmed = false;
    if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.i_agree_save_settings_8496f2dd", "I agree, save settings")))
    {
        confirmed = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.cancel_7e4b3f1d", "Cancel"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return confirmed;
}

void MenuCommon::RenderNeuralRenderingExperimentalSettings(RenderMenuContext& ctx)
{
    Neurotic::Sleek::ContentCard card("##NrCompatibility", Neurotic::UiLiteral("ingame.menu-common.compatibility_options_1f9a3f38", "Compatibility options"));
    auto& draft = DlssNr::ExperimentalPolicy::Draft;
    DlssNr::ExperimentalPolicy::EnsureDraft(*ctx.config);
    {
        const auto option = [&](const char* label, bool& value, const char* description)
        {
            if (ImGui::Checkbox(label, &value)) draft.dirty = true;
            ImGui::SameLine(); ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize()*32);
                ImGui::TextUnformatted(description);
                ImGui::PopTextWrapPos(); ImGui::EndTooltip();
            }
        };
        option(Neurotic::UiLiteral("ingame.menu-common.preserve_nr_during_camera_cuts_7aef5a21", "Preserve NR During Camera Cuts"), draft.preSrSoftReset,
            Neurotic::UiLiteral("ingame.menu-common.may_avoid_nr_reloads_during_camera_cuts_experime_b277fddf", "May avoid NR reloads during camera cuts. Experimental: it can cause crashes when loading between worldspaces."));
        option(Neurotic::UiLiteral("ingame.menu-common.depth_compatibility_experimental_0a1f074e", "Depth compatibility (Experimental)"), draft.preparedDepth,
            Neurotic::UiLiteral("ingame.menu-common.experimental_native_d3d12_support_for_eligible_m_96e2152d", "Experimental Native D3D12 support for eligible multi-mip depth textures. Leave off unless testing a depth compatibility problem; game benefit remains unverified. Resource, format, synchronization, ownership and completion checks remain active."));

        const auto saveDraft = [&]()
        {
            DlssNr::CancelAdvisorAnalysis(ctx.config,
                Neurotic::UiLiteral("ingame.menu-common.experimental_settings_save_requested_analysis_st_f9853340", "Experimental settings save requested; analysis stopped and original settings restored."));
            if (ctx.config->SaveExperimentalSettings(false, false,
                                                     false, draft.preSrSoftReset, draft.preparedDepth))
            {
                if (DlssNr::ExperimentalSession::Applied(*ctx.config)) draft.dirty = false;
                else DlssNr::ExperimentalPolicy::ResetDraft(*ctx.config);
            }
            else
                ImGui::OpenPopup(Neurotic::UiLiteral("ingame.menu-common.experimental_settings_could_not_be_saved_b506a4ea", "Experimental settings could not be saved"));
        };
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.save_overrides_d3dac1dc", "Save Overrides")))
        {
            if (DlssNr::ExperimentalPolicy::NewChoicesRequested(*ctx.config, draft))
                ImGui::OpenPopup(Neurotic::UiLiteral("ingame.menu-common.confirm_experimental_settings_b6dbc89b", "Confirm experimental settings##Dedicated"));
            else
                saveDraft();
        }
        if (RenderExperimentalSaveConfirmation(Neurotic::UiLiteral("ingame.menu-common.confirm_experimental_settings_b6dbc89b", "Confirm experimental settings##Dedicated"), *ctx.config,
                                               draft, ctx.menuResScale)) saveDraft();
        DlssNr::StatusPanel::Line("##PendingChanges", draft.dirty ? Neurotic::UiLiteral("ingame.menu-common.pending_changes_apply_only_after_saving_dc2202e5", "Pending changes apply only after saving.") : "",
            toneMapColor(ImVec4(1.0f, 0.72f, 0.25f, 1.0f)));
        if (ImGui::BeginPopupModal(Neurotic::UiLiteral("ingame.menu-common.experimental_settings_could_not_be_saved_b506a4ea", "Experimental settings could not be saved"), nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.the_existing_experimental_policy_is_still_active_79e9bca8", "The existing experimental policy is still active. Check that OptiScaler.ini can be written, then try again."));
            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.ok_1b5ecc61", "OK"))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
}

#include "RenderingDiagnosticsNavigation.h"
void MenuCommon::RenderNeuralRenderingPage(RenderMenuContext& ctx)
{
    const char* gpuName = ctx.primaryGpu ? ctx.primaryGpu->name.c_str() : Neurotic::UiLiteral("ingame.dlssnr-menu.detecting_graphics_card_5066a0c7", "Detecting graphics card...");
    if (ctx.neuralPage == 2 || ctx.neuralPage == 5)
    {
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.35f);
        const auto experimentalText = Neurotic::Translate("(Experimental)");
        const auto experimentalPos = ImGui::GetCursorScreenPos();
        ImGui::TextUnformatted(experimentalText.c_str());
        ImGui::GetWindowDrawList()->AddText(ImVec2(experimentalPos.x + .65f, experimentalPos.y), ImGui::GetColorU32(ImGuiCol_Text), experimentalText.c_str());
        ImGui::PopFont();
        ImGui::Spacing();
    }
    if (ctx.neuralPage == 2)
        Neurotic::Semantic::Character::RenderInspectorMenu(*ctx.config,false);
    else if (ctx.neuralPage == 4)
        RenderNeuralRenderingExperimentalSettings(ctx);
    else if (ctx.neuralPage == 5)
        Neurotic::Semantic::Rules::DrawGameEditor(*ctx.config);
    else
        DlssNr::RenderMenu(ctx.config,ctx.menuResScale,std::nullopt,gpuName,
            ctx.neuralPage==6 ? DlssNr::MenuPage::Preflight :
            ctx.neuralPage==1 ? DlssNr::MenuPage::Multipass : DlssNr::MenuPage::Overview);
}

void MenuCommon::RenderFrameGenerationPage(RenderMenuContext& ctx)
{
    if (ctx.childPage == 0)
    {
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.game_owned_fg_and_native_mfg_overrides_unlocks_d_eea833c3", "Game-owned FG and native MFG overrides. Unlocks do not enable FG by themselves."));
        RenderFrameGenerationSelection(ctx);
    }
    else if (ctx.childPage == 1)
    {
        Neurotic::Sleek::ContentCard card("##FgSetup", Neurotic::UiLiteral("ingame.menu-common.next_launch_route_6bcb3d22", "Next-launch route"));
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.choose_the_game_s_fg_input_and_the_replacement_o_40c542c4", "Choose the game's FG input and the replacement output. Save and restart to change the running route."));
        const char* inputs[] = { Neurotic::UiLiteral("ingame.menu-common.none_331505ff", "None"), Neurotic::UiLiteral("ingame.menu-common.upscaler_d43a2f2b", "Upscaler"), Neurotic::UiLiteral("ingame.menu-common.streamline_dlssg_df928059", "Streamline DLSSG"), Neurotic::UiLiteral("ingame.menu-common.nvngx_dlssg_c530efe9", "Nvngx DLSSG"), Neurotic::UiLiteral("ingame.menu-common.fsr_3_1_93c8f581", "FSR 3.1"), Neurotic::UiLiteral("ingame.menu-common.fsr_3_0_1f9ee7df", "FSR 3.0"), Neurotic::UiLiteral("ingame.menu-common.xefg_5c446cf1", "XeFG"), Neurotic::UiLiteral("ingame.menu-common.force_xell_cb2ccfdf", "Force XeLL") };
        const char* outputs[] = { Neurotic::UiLiteral("ingame.menu-common.none_331505ff", "None"), Neurotic::UiLiteral("ingame.menu-common.fsr_fg_745644b6", "FSR FG"), Neurotic::UiLiteral("ingame.menu-common.dlssg_86366b33", "DLSSG"), Neurotic::UiLiteral("ingame.menu-common.xefg_5c446cf1", "XeFG") };
        const auto input = static_cast<size_t>(ctx.state.activeFgInput), output = static_cast<size_t>(ctx.state.activeFgOutput);
        ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.running_input_s_output_s_4ca8badb", "Running input: %s / output: %s"),Neurotic::Translate(input < std::size(inputs) ? inputs[input] : Neurotic::UiLiteral("ingame.menu-common.unknown_d80d0833", "Unknown")).c_str(),Neurotic::Translate(output < std::size(outputs) ? outputs[output] : Neurotic::UiLiteral("ingame.menu-common.unknown_d80d0833", "Unknown")).c_str());
        RenderFrameGenerationSelection(ctx);
        if (ctx.state.activeFgInput == FGInput::ForceXeLL)
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.force_xell_owns_the_current_route_change_it_unde_a2fae908", "Force XeLL owns the current route. Change it under Pacing & Latency, then save and restart."));
    }
    else if (ctx.childPage == 2)
    {
        Neurotic::Sleek::ContentCard card("##FgReplacement", Neurotic::UiLiteral("ingame.menu-common.running_replacement_6d94814f", "Running replacement"));
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.controls_apply_to_the_active_replacement_backend_e7fdb9ac", "Controls apply to the active replacement backend. Provider activity does not establish displayed frame delivery."));
        if (!ctx.state.currentFG && ctx.state.activeFgInput == FGInput::NoFG)
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.no_replacement_fg_is_active_select_a_route_in_se_1590b250", "No replacement FG is active. Select a route in Setup or use Game FG / Native MFG for the game's own path."));
        RenderFrameGenerationRuntimeSettings(ctx);
    }
    else
    {
        Neurotic::Sleek::ContentCard card("##FgPacing", Neurotic::UiLiteral("ingame.menu-common.pacing_latency_ed3df81a", "Pacing & Latency"));
        RenderFramerateSettings(ctx);
#ifdef LOW_LATENCY_INPUTS
        RenderLowLatencySettings(ctx);
#else
        RenderFakenvapiSettings(ctx);
#endif
    }
}

void MenuCommon::RenderAdvancedPage(RenderMenuContext& ctx)
{
    Neurotic::Sleek::ContentCard card("##AdvancedControls");
    if (ctx.childPage == 0 || ctx.childPage == 1) {
        ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.v_sync_f402ef17", "V-Sync")); RenderVsyncSettings(ctx);
        ImGui::SeparatorText(Neurotic::UiLiteral("ingame.menu-common.anisotropic_filtering_a7028718", "Anisotropic Filtering")); RenderAnisotropicFilteringSettings(ctx);
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.menu-common.filtering_applies_when_the_game_creates_samplers_0afdd15b", "Filtering applies when the game creates samplers. Existing samplers may require a game restart."));
    }
    else RenderAdvancedSettings(ctx);
}

void MenuCommon::RenderToolsPage(RenderMenuContext& ctx)
{
    Neurotic::Sleek::ContentCard card("##ToolControls");
    if (ctx.childPage == 0) DlssNr::RenderScreenshotMenu(ctx.config);
    if (ctx.childPage == 1) RenderMagnifierSettings(ctx);
    if (ctx.childPage == 2) RenderMipmapBiasSettings(ctx);
    if (ctx.childPage == 3) DlssNr::RenderCompareMenu(ctx.config, ctx.menuResScale);
}

void MenuCommon::RenderDiagnosticsPage(RenderMenuContext& ctx)
{
    if(ctx.childPage==4){
        const char* gpuName=ctx.primaryGpu?ctx.primaryGpu->name.c_str():Neurotic::UiLiteral("ingame.dlssnr-menu.detecting_graphics_card_5066a0c7", "Detecting graphics card...");
        DlssNr::RenderMenu(ctx.config,ctx.menuResScale,std::nullopt,gpuName,DlssNr::MenuPage::Diagnostics);
        return;
    }
    Neurotic::Sleek::ContentCard card("##DiagnosticControls");
    if (ctx.childPage == 0) RenderLoggingSettings(ctx);
    if (ctx.childPage == 1) RenderQuirksSettings(ctx);
    if (ctx.childPage == 2) RenderFpsOverlaySettings(ctx);
    if (ctx.childPage == 3)
    {
        ImGui::Spacing();
        const auto vkCapture=DlssNr::CurrentVulkanNrCapabilities().capture;
        ImGui::BeginDisabled(DlssNr::ComparisonScreenshotBusy() || (State::Instance().api==API::Vulkan && !vkCapture.available));
        if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.capture_model_stages_5_second_delay_ae44fe51", "Capture model stages (5-second delay)"))) DlssNr::RequestPresentStageCapture();
        ImGui::EndDisabled();
        if(State::Instance().api==API::Vulkan && !vkCapture.available) ImGui::TextWrapped("%s",Neurotic::Translate(vkCapture.reason.c_str()).c_str());
        ImGui::TextWrapped("%s",Neurotic::Translate(DlssNr::PresentStageCaptureStatus().c_str()).c_str());
    }
}

void MenuCommon::RenderMainMenuTabs(RenderMenuContext& ctx)
{
    ImGui::Spacing();
    using namespace Neurotic::Sleek;
    static RenderingDiagnosticsSession navigationSession;
    auto& storage = ImGui::GetCurrentWindow()->StateStorage;
    const auto selectedKey = ImGui::GetID("##SelectedMainPage");
    const auto neuralKey = ImGui::GetID("##SelectedNeuralPage");
    const auto requestKey = ImGui::GetID("##RequestedNeuralPage");
    const int requested = storage.GetInt(requestKey,-1);
    storage.SetInt(requestKey,-1);
    int selected = std::clamp(storage.GetInt(selectedKey,0),0,6);
    ImGui::PushID(6);const auto diagnosticsKey=ImGui::GetID("##SelectedChildPage");ImGui::PopID();
    const auto destination=ResolveRenderingDiagnosticsSession(navigationSession,selected,storage.GetInt(neuralKey,0),
        storage.GetInt(diagnosticsKey,4),requested);
    selected=destination.main;
    const bool wide = false; // Top navigation reclaims the side rail width.
    const float footerReserve = FooterHeight(ctx.currentFeature != nullptr && !ctx.currentFeature->IsFrozen()) +
        menuHeight.RowHeight() + ImGui::GetStyle().ItemSpacing.y * 2;
    const float pageHeight = std::max(1.0f,ImGui::GetContentRegionAvail().y-footerReserve);
    NavigationStatus status[PageCount] {};
    status[1] = UpscalingNavigationStatus(ctx.currentFeature!=nullptr,
        ctx.currentFeature && ctx.currentFeature->IsInited(),
        ctx.currentFeature && ctx.currentFeature->IsFrozen(), HasDetectedUpscalerLibraries(ctx.state));
    status[2].running = DlssNr::MenuIsActive(ctx.config);
    const auto managedFg = ctx.state.currentFG;
    const auto nativeFg = Neurotic::Semantic::Character::NativeFgWork().Read(
        Neurotic::Semantic::Character::CharacterActivityNow());
    const bool nativeFgActive = ctx.state.swapchainApi == API::Vulkan ?
        DlssNr::VulkanNrStreamlineAdapter().Activity() == DlssNr::VkNrFgActivity::On :
        nativeFg.available && nativeFg.active;
    const bool fgRequested = nativeFg.requested || ctx.state.activeFgInput != FGInput::NoFG ||
        ctx.state.activeFgOutput != FGOutput::NoFG || ctx.state.activeFgNvngx != FGNvngxReplacement::None ||
        ctx.config->FGDLSSGNativeMfgExperimental.value_or_default();
    status[3] = FrameGenerationNavigationStatus(fgRequested, nativeFgActive,
        managedFg && managedFg->IsActive(), managedFg && managedFg->IsPaused(),
        managedFg && managedFg->IsWaitingForFrameData());
    Navigation(selected,pageHeight,wide,status);
    // Apply the first-visit default when NR is entered from another main page.
    const auto entered=ResolveRenderingDiagnosticsSession(navigationSession,selected,destination.neural,destination.diagnosticsChild,-1);
    selected=entered.main;ctx.neuralPage=entered.neural;
    const bool forceDiagnostics=destination.forceDiagnostics||entered.forceDiagnostics;
    if(forceDiagnostics){storage.SetInt(neuralKey,ctx.neuralPage);storage.SetInt(diagnosticsKey,entered.diagnosticsChild);}
    storage.SetInt(selectedKey,selected);
    if (selected == 2) {
        const auto readiness = DlssNr::MenuPreflightState(ctx.config);
        NeuralNavigation(ctx.neuralPage,ctx.config->GetDlssNrRuntimeSnapshot().enabled,
            ctx.config->DlssNrMultipassEnabled.value_or_default(),
            DlssNr::ExperimentalPolicy::Capture(*ctx.config).active,
            ctx.config->CharacterInspectorEnabled.value_or_default(),destination.forceNeural||entered.forceNeural,
            readiness == DlssNr::MenuReadiness::Ready ? PilotState::On :
            readiness == DlssNr::MenuReadiness::Blocked ? PilotState::Blocked : PilotState::Degraded);
        storage.SetInt(neuralKey,ctx.neuralPage);
    }
    else if (ChildPageCount(selected)>0) {
        ImGui::PushID(selected);
        const auto childKey=ImGui::GetID("##SelectedChildPage");
        ctx.childPage=std::clamp(storage.GetInt(childKey,selected==6?4:0),0,ChildPageCount(selected)-1);
        SectionNavigation(selected,ctx.childPage,status[1].pilot,selected==6&&forceDiagnostics);
        storage.SetInt(childKey,ctx.childPage);
        ImGui::PopID();
    }
    const float contentHeight = wide ? pageHeight : std::max(1.0f,ImGui::GetContentRegionAvail().y-footerReserve);
    // Each page owns its scroll position and widget IDs. Selection edits take effect immediately.
    ImGui::PushID(selected);
    if (selected == 2) ImGui::PushID(ctx.neuralPage);
    else if (ChildPageCount(selected)>0) ImGui::PushID(ctx.childPage);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0,0,0,0));
    const bool pageVisible = ImGui::BeginChild("##MainMenuPageContent",{0,contentHeight},ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    if (pageVisible)
    {
        PageReveal reveal(selected);
        switch (selected)
        {
        case 0: RenderGeneralPage(ctx); break;
        case 1: RenderUpscalingPage(ctx); break;
        case 2: RenderNeuralRenderingPage(ctx); break;
        case 3: RenderFrameGenerationPage(ctx); break;
        case 4: RenderAdvancedPage(ctx); break;
        case 5: RenderToolsPage(ctx); break;
        case 6: RenderDiagnosticsPage(ctx); break;
        }

    }
    ImGui::EndChild();
    if (selected == 2 || ChildPageCount(selected)>0) ImGui::PopID();
    ImGui::PopID();
    if(const auto request=DlssNr::ConsumeMenuPageRequest())
        ctx.requestedNeuralPage=*request==DlssNr::MenuPage::Preflight?6:
            *request==DlssNr::MenuPage::Diagnostics?3:*request==DlssNr::MenuPage::Multipass?1:0;
    if (ctx.requestedNeuralPage >= 0)
    {
        const auto requestedDestination=ResolveRenderingDiagnosticsSession(navigationSession,2,ctx.neuralPage,
            storage.GetInt(diagnosticsKey,0),ctx.requestedNeuralPage);
        storage.SetInt(selectedKey,requestedDestination.main);
        storage.SetInt(neuralKey,requestedDestination.neural);
        if(requestedDestination.forceDiagnostics)storage.SetInt(diagnosticsKey,requestedDestination.diagnosticsChild);
        storage.SetInt(requestKey,ctx.requestedNeuralPage);
        ctx.requestedNeuralPage = -1;
    }
}

void MenuCommon::RenderMainMenuGraphs(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto& currentFeature = ctx.currentFeature;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;

    if (!_showMainMenuGraphs)
        return;



    const float graphHeight = ImGui::GetFrameHeight();
    const bool upscalerActive = currentFeature != nullptr && !currentFeature->IsFrozen();
    const bool timingAvailable = upscalerActive && HasUpscalerGpuTiming(state);
    if (ImGui::BeginTable("plots", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PreciseWidths |
        ImGuiTableFlags_NoPadOuterX))
    {
        ImGui::TableSetupColumn("##FrameGraph", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("##UpscalerGraph", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        // Readouts have their own fixed header row, so their text cannot shrink
        // either graph or shift one plot down when data becomes unavailable.
        ImGui::TableNextColumn();
        DlssNr::StatusPanel::Linef("##FrameGraphReadout", ImGui::GetStyleColorVec4(ImGuiCol_Text),
            Neurotic::UiLiteral("ingame.menu-common.frame_time_7_2f_ms_6_1f_fps_b5fe0163", "Frame Time: %7.2f ms / %6.1f fps"), frameTime, frameRate);
        ImGui::TableNextColumn();
        if (timingAvailable)
            DlssNr::StatusPanel::Linef("##UpscalerGraphReadout", ImGui::GetStyleColorVec4(ImGuiCol_Text),
                Neurotic::UiLiteral("ingame.timing.upscaler_ms", "Upscaler: %7.2f ms"), state.upscaleTimes.back());
        else
            DlssNr::StatusPanel::Line("##UpscalerGraphReadout",
                upscalerActive ? Neurotic::UiLiteral("ingame.menu-common.upscaler_gpu_timing_unavailable_1b42dde0", "Upscaler: GPU timing unavailable") : Neurotic::UiLiteral("ingame.menu-common.upscaler_inactive_4d344c54", "Upscaler: inactive"),
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TableNextColumn();
        ImGui::PlotLines(
            "##FrameTimePlot", [](void* rb, int idx) -> float
            { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); }, &gFrameTimes, plotWidth,
            0, nullptr, FLT_MAX, FLT_MAX, ImVec2((std::max)(1.0f, ImGui::GetContentRegionAvail().x), graphHeight));
        ImGui::TableNextColumn();
        ImGui::PlotLines(
            "##UpscalerTimePlot", [](void* rb, int idx) -> float
            { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); }, &gUpscalerTimes,
            timingAvailable ? plotWidth : 0, 0, nullptr, FLT_MAX, FLT_MAX, ImVec2((std::max)(1.0f, ImGui::GetContentRegionAvail().x), graphHeight));

        ImGui::EndTable();
    }
}

void MenuCommon::RenderMainMenuBottomBar(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    ImGui::Spacing();
    ImGui::Separator();

    if (state.nvngxIniDetected)
    {
        ImGui::Spacing();
        ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                           Neurotic::UiLiteral("ingame.menu-common.nvngx_ini_detected_please_move_over_to_using_opt_876fcc28", "nvngx.ini detected, please move over to using OptiScaler.ini and delete the old config"));
        ImGui::Spacing();
    }
}

void MenuCommon::RenderMainMenuWindowActions(RenderMenuContext& ctx)
{
    auto config=ctx.config;
    const auto menuResScale=ctx.menuResScale;
    bool lightTheme=config->LightTheme.value_or_default();
    const auto actions=Neurotic::Sleek::HeaderActions(lightTheme,nullptr,&_showMainMenuGraphs);
    if(actions.themeChanged) config->LightTheme=lightTheme; // apply at next frame boundary
    const auto saveAllSettings = [&]()
    {
        // Never serialize the Advisor's temporary route trial.  Cancellation is a no-op when idle.
        DlssNr::CancelAdvisorAnalysis(config, Neurotic::UiLiteral("ingame.menu-common.settings_save_requested_analysis_stopped_and_ori_9a6a9602", "Settings save requested; analysis stopped and original settings restored."));
        if (!menuInputDraft.initialized) ResetMenuInputDraft(*config);
        DlssNr::ExperimentalPolicy::EnsureDraft(*config);
        const std::optional<bool> oldMouse = config->AllowGameMouse.has_value()
            ? std::optional<bool>(config->AllowGameMouse.value()) : std::nullopt;
        const std::optional<bool> oldKeyboard = config->AllowGameKeyboard.has_value()
            ? std::optional<bool>(config->AllowGameKeyboard.value()) : std::nullopt;
        const std::optional<bool> oldController = config->AllowGameController.has_value()
            ? std::optional<bool>(config->AllowGameController.value()) : std::nullopt;
        config->AllowGameMouse = menuInputDraft.mouse;
        config->AllowGameKeyboard = menuInputDraft.keyboard;
        config->AllowGameController = menuInputDraft.controller;
        const bool saved=DlssNr::ExperimentalPolicy::SaveDraft(*config,
            [&](const DlssNr::ExperimentalPolicy::UiDraft& choice) { return config->SaveIni(&choice); });
        if (saved)
        {
            menuInputDraft.dirty = false;
            DlssNr::ExperimentalPolicy::Draft.dirty = false;
            UpdateMenuInputMode(ctx);
            if (!DlssNr::ExperimentalSession::Applied(*config))
                DlssNr::ExperimentalPolicy::ResetDraft(*config);
        }
        else
        {
            config->AllowGameMouse = oldMouse;
            config->AllowGameKeyboard = oldKeyboard;
            config->AllowGameController = oldController;
        }
        Neurotic::Sleek::CompleteButtonFeedback(actions.saveId,saved);
    };
    if (actions.saveClicked)
    {
        Neurotic::Sleek::BeginButtonFeedback(actions.saveId);
        DlssNr::ExperimentalPolicy::EnsureDraft(*config);
        if (DlssNr::ExperimentalPolicy::NewChoicesRequested(*config, DlssNr::ExperimentalPolicy::Draft))
            ImGui::OpenPopup(Neurotic::UiLiteral("ingame.menu-common.confirm_experimental_settings_b6dbc89b", "Confirm experimental settings##Global"));
        else
            saveAllSettings();
    }
    if (RenderExperimentalSaveConfirmation(Neurotic::UiLiteral("ingame.menu-common.confirm_experimental_settings_b6dbc89b", "Confirm experimental settings##Global"), *config,
                                           DlssNr::ExperimentalPolicy::Draft, menuResScale))
        saveAllSettings();

    if(Neurotic::Sleek::ButtonFeedbackPending(actions.saveId) &&
        !ImGui::IsPopupOpen(Neurotic::UiLiteral("ingame.menu-common.confirm_experimental_settings_b6dbc89b", "Confirm experimental settings##Global")))
        Neurotic::Sleek::CancelButtonFeedback(actions.saveId);

    Neurotic::RenderPersistenceFailure(config->GetPersistenceStatus());

    if(actions.closeClicked)
    {
        HideMenu();
        UpdateMenuInputMode(ctx);
    }
}

void MenuCommon::RenderMainMenuSupportLink(RenderMenuContext& ctx)
{
    const auto feature=ctx.currentFeature;
    const bool readout=feature != nullptr && !feature->IsFrozen();
    const float height=Neurotic::Sleek::FooterHeight(readout);
    menuHeight.Draw(ImGui::GetMainViewport()->Size, ctx.menuResScale, height);
    ImGui::SetCursorScreenPos({ImGui::GetWindowPos().x+ImGui::GetStyle().WindowPadding.x,
        ImGui::GetWindowPos().y+ImGui::GetWindowSize().y-Neurotic::Sleek::FooterBottomInset()-height});
    int language=Neurotic::LanguageIndex(ctx.config->MenuLanguage.value_or_default());
    _selectedScale=ctx.config->MenuScale.has_value()?((int)(ctx.menuResScale*10.0f))-4:0;
    const auto autoText=ctx.config->MenuScale.has_value()?std::string(Neurotic::UiLiteral("ingame.menu-common.auto_b980aecf", "Auto")):StrFmt("Auto (%3.1f)",ctx.menuResScale);
    const auto actions=Neurotic::Sleek::FooterControls(language,_selectedScale,autoText.c_str());
    if(actions.scaleChanged) {
        if (_selectedScale==0) ctx.config->MenuScale.reset();
        else ctx.config->MenuScale=.4f+(float)_selectedScale/10;
    }
    // Shared language belongs to the desktop Languages store; preserve legacy INI values.
    if(actions.supportClicked)
    {
        auto& platform=ImGui::GetPlatformIO();
        if(platform.Platform_OpenInShellFn)
            platform.Platform_OpenInShellFn(ImGui::GetCurrentContext(),"https://ko-fi.com/espiownage");
    }
    if(readout)
    {
        const auto information=StrFmt("%dx%d -> %dx%d (%.1f) [%dx%d (%.1f)] | %d | GPU: %s",
            feature->RenderWidth(),feature->RenderHeight(),feature->TargetWidth(),feature->TargetHeight(),
            (float)feature->TargetWidth()/(float)feature->RenderWidth(),feature->DisplayWidth(),feature->DisplayHeight(),
            (float)feature->DisplayWidth()/(float)feature->RenderWidth(),feature->FrameCount(),Neurotic::Translate(ctx.primaryGpu->name.c_str()).c_str());
        Neurotic::Sleek::FooterReadout(information.c_str());
    }

}

void MenuCommon::RenderMipmapBiasWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags)
{
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;

    // Metrics window (for debug)
    // ImGui::ShowMetricsWindow();

    // Mipmap calculation window
    if (_showMipmapCalcWindow && currentFeature != nullptr && !currentFeature->IsFrozen() && currentFeature->IsInited())
    {
        auto posX = (io.DisplaySize.x - 450.0f) / 2.0f;
        auto posY = (io.DisplaySize.y - 200.0f) / 2.0f;

        ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2 { 450.0f, 200.0f }, ImGuiCond_FirstUseEver);

        if (_displayWidth == 0)
        {
            if (config->OutputScalingEnabled.value_or_default())
            {
                _displayWidth = static_cast<uint32_t>(currentFeature->DisplayWidth() *
                                                      config->OutputScalingMultiplier.value_or_default());
            }
            else
            {
                _displayWidth = currentFeature->DisplayWidth();
            }

            _renderWidth = static_cast<uint32_t>(_displayWidth / 3.0f);
            _mipmapUpscalerQuality = 0;
            _mipmapUpscalerRatio = 3.0f;
            _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
        }

        if (ImGui::Begin(Neurotic::UiLiteral("ingame.menu-common.mipmap_bias_c3d9e7cf", "Mipmap Bias"), nullptr, flags))
        {
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetWindowFocus();

            if (ImGui::InputScalar(Neurotic::UiLiteral("ingame.menu-common.display_width_15e4bef9", "Display Width"), ImGuiDataType_U32, &_displayWidth, NULL, NULL, "%u"))
            {
                if (_displayWidth <= 0)
                {
                    if (config->OutputScalingEnabled.value_or_default())
                    {
                        _displayWidth = static_cast<uint32_t>(currentFeature->DisplayWidth() *
                                                              config->OutputScalingMultiplier.value_or_default());
                    }
                    else
                    {
                        _displayWidth = currentFeature->DisplayWidth();
                    }
                }

                _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
            }

            const char* q[] = { Neurotic::UiLiteral("ingame.menu-common.ultra_performance_f6500465", "Ultra Performance"), Neurotic::UiLiteral("ingame.provider.442aded87a55", "Performance"), Neurotic::UiLiteral("ingame.provider.5386ea5db81c", "Balanced"), Neurotic::UiLiteral("ingame.provider.1b2c08a8733d", "Quality"), Neurotic::UiLiteral("ingame.menu-common.ultra_quality_05f45017", "Ultra Quality"), Neurotic::UiLiteral("ingame.provider.26516f6a6bd8", "DLAA") };
            float fr[] = { 3.0f, 2.0f, 1.7f, 1.5f, 1.3f, 1.0f };
            auto configQ = _mipmapUpscalerQuality;

            const char* selectedQ = q[configQ];

            ImGui::BeginDisabled(config->UpscaleRatioOverrideEnabled.value_or_default());

            if (ImGui::BeginCombo(Neurotic::UiLiteral("ingame.menu-common.upscaler_quality_98e58f6b", "Upscaler Quality"), selectedQ))
            {
                for (int n = 0; n < 6; n++)
                {
                    if (ImGui::Selectable(q[n], (_mipmapUpscalerQuality == n)))
                    {
                        _mipmapUpscalerQuality = n;

                        float ov = -1.0f;

                        if (config->QualityRatioOverrideEnabled.value_or_default())
                        {
                            switch (n)
                            {
                            case 0:
                                ov = config->QualityRatio_UltraPerformance.value_or(-1.0f);
                                break;

                            case 1:
                                ov = config->QualityRatio_Performance.value_or(-1.0f);
                                break;

                            case 2:
                                ov = config->QualityRatio_Balanced.value_or(-1.0f);
                                break;

                            case 3:
                                ov = config->QualityRatio_Quality.value_or(-1.0f);
                                break;

                            case 4:
                                ov = config->QualityRatio_UltraQuality.value_or(-1.0f);
                                break;
                            }
                        }

                        if (ov > 0.0f)
                            _mipmapUpscalerRatio = ov;
                        else
                            _mipmapUpscalerRatio = fr[n];

                        _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::EndDisabled();

            auto minLimit = config->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
            auto maxLimit = config->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;
            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.upscaler_ratio_dd7e21ab", "Upscaler Ratio"), &_mipmapUpscalerRatio, minLimit, maxLimit, "%.2f"))
            {
                _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
            }

            if (ImGui::InputScalar(Neurotic::UiLiteral("ingame.menu-common.render_width_d6b0a958", "Render Width"), ImGuiDataType_U32, &_renderWidth, NULL, NULL, "%u"))
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);

            ImGui::SliderFloat(Neurotic::UiLiteral("ingame.menu-common.mipmap_bias_c3d9e7cf", "Mipmap Bias"), &_mipBiasCalculated, -15.0f, 0.0f, "%.6f");

            // BOTTOM LINE
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::SameLine();
            ImGui::Spacing();

            constexpr float spacing = 6.0f;
            auto textSize = ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.use_value_a11eaab6", "Use Value"));
            textSize += ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.close_6c32fa6b", "Close"));
            textSize.x += ImGui::GetStyle().FramePadding.x * 5.0f + spacing; // 2 sides * 2 buttons + 1

            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - textSize.x);

            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.use_value_a11eaab6", "Use Value")))
            {
                _mipBias = _mipBiasCalculated;
                _showMipmapCalcWindow = false;
            }

            ImGui::SameLine(0.0f, spacing);

            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.close_6c32fa6b", "Close")))
                _showMipmapCalcWindow = false;

            ImGui::Spacing();
            ImGui::Separator();

            ImGui::End();
        }
    }
}

void MenuCommon::RenderHudlessResourcesWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;

    auto fg = state.currentFG;
    if (_showHudlessWindow && config->FGHUDFix.value_or_default() && fg != nullptr && fg->IsActive())
    {
        auto posX = (io.DisplaySize.x - 400.0f) / 2.0f;
        auto posY = (io.DisplaySize.y - 300.0f) / 2.0f;

        ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2 { 400.0f, 300.0f });

        if (ImGui::Begin(Neurotic::UiLiteral("ingame.menu-common.hudless_resources_b3c57677", "HUDless Resources"), nullptr, flags))
        {
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetWindowFocus();

            int btnCount = 100;

            if (ImGui::BeginTable("HUDlessTable", 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("##1", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##2", ImGuiTableColumnFlags_WidthFixed);

                ankerl::unordered_dense::map<void*, CapturedHudlessInfo>::iterator it;

                for (it = state.capturedHudlesses.begin(); it != state.capturedHudlesses.end(); it++)
                {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::Text(Neurotic::UiLiteral("ingame.menu-common.08x_s_s_count_llu_s_0a949df8", "%08x, %s->%s, Count: %llu, %s"), (size_t) it->first,Neurotic::Translate(GetSourceString(it->second.captureInfo & 0xFF).c_str()).c_str(),Neurotic::Translate(GetDispatchString(it->second.captureInfo & 0xFF00).c_str()).c_str(), it->second.usageCount,Neurotic::Translate(it->second.enabled ? Neurotic::UiLiteral("ingame.menu-common.active_3203f178", "Active") : Neurotic::UiLiteral("ingame.menu-common.passive_59fdef63", "Passive")).c_str());

                    ImGui::TableSetColumnIndex(1);

                    btnCount++;
                    std::string text;

                    if (it->second.enabled)
                        text = StrFmt("Disable##%d", btnCount);
                    else
                        text = StrFmt(Neurotic::UiLiteral("ingame.menu-common.enable_b324cd61", "Enable##%d"), btnCount);

                    if (ImGui::Button(text.c_str()))
                    {
                        LOG_DEBUG("HUDless {:X}: {}", (size_t) it->first,
                                  it->second.enabled ? "Disabling" : "Enabling");
                        it->second.enabled = !it->second.enabled;
                    }
                }

                ImGui::EndTable();
            }

            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.clear_46ae3359", "Clear##4")))
            {
                LOG_DEBUG("Clearing captured HUDless resources");
                state.clearCapturedHudlesses = true;
            }

            ImGui::SameLine(0.0f, 8.0f);

            if (ImGui::Button(Neurotic::UiLiteral("ingame.menu-common.close_6c32fa6b", "Close##4")))
                _showHudlessWindow = false;

            ImGui::End();
        }
    }
}

void MenuCommon::RenderMainMenuWindow(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& frameTimesCalculated = ctx.frameTimesCalculated;
    auto& menuResScale = ctx.menuResScale;

    if (!_isVisible)
        return;

    // Check for GPU support once and reuse the result in all menu sections.
    // DXVK might call Vulkan device creation, which would destroy our objects.
    State::Instance().vulkanSkipHooks = true;
    ctx.primaryGpu =
        std::make_unique<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>>(IdentifyGpu::getPrimaryGpu());
    State::Instance().vulkanSkipHooks = false;

    // Overlay font
    if (config->UseHQFont.value_or_default())
        ImGui::PushFontSize(std::round(menuResScale * fontSize));

    // If overlay is not visible frame needs to be inited
    if (!frameTimesCalculated)
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (state.frameTimes[i] > 0.0)
            {
                frameTime += state.frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
    }

    ImGuiWindowFlags flags = 0;
    flags |= ImGuiWindowFlags_NoSavedSettings;
    flags |= ImGuiWindowFlags_NoCollapse;

    if (lastMenuScale != menuResScale)
    {
        lastMenuScale = menuResScale;

        // if UI scale is changed rescale the style
        ImGuiStyle& style = ImGui::GetStyle();
        ImGuiStyle styleold = style; // Backup colors
        style = ImGuiStyle();        // IMPORTANT: ScaleAllSizes will change the original size,
                                     // so we should reset all style config

        ApplyThemeStyle();

        Neurotic::Sleek::ApplyMetrics(style);
        style.ScaleAllSizes(menuResScale);
        style.MouseCursorScale = 1.0f;
        CopyMemory(style.Colors, styleold.Colors, sizeof(style.Colors)); // Restore colors
    }

    // Main menu window
    windowTitle = StrFmt(Neurotic::UiLiteral("ingame.menu-common.neurotic_v_s_based_on_s_69630466", "NeuRotic v%s | Based on %s"),
                         VersionCheck::CurrentVersionString().c_str(), VER_PRODUCT_NAME) +
                  StrFmt(" - %s %s %s %s###NeuroticMainMenu", state.gameExe.c_str(),
                         state.gameName.empty() ? "" : StrFmt("- %s", state.gameName.c_str()).c_str(),
                         state.detectedQuirks.empty() ? "" : "(Q)", state.isOptiPatcherSucceed ? "(OP)" : "");

    // Preserve a user's position while it fits; scale and viewport changes keep the full menu reachable.
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const auto fittedSize = menuHeight.Size(mainViewport->Size,menuResScale);
    const auto* existingWindow = ImGui::FindWindowByName(windowTitle.c_str());
    const ImVec2 position = existingWindow ? existingWindow->Pos :
        ImVec2(mainViewport->Pos.x+mainViewport->Size.x-fittedSize.x-12,mainViewport->Pos.y+12);
    ImGui::SetNextWindowPos(Neurotic::Sleek::WindowPosition(position,mainViewport->Pos,mainViewport->Size,fittedSize));
    ImGui::SetNextWindowSize(fittedSize);

    // Build one live UI frame. Only its menu triangles are recolored/refracted
    // after Render; scene, foreground, FPS and input ownership stay unchanged.
    if (waterVisibilityGeneration != _visibilityGeneration.load())
    {
        menuWater.Reset();
        waterVisibilityGeneration = _visibilityGeneration.load();
    }
    reduceMenuMotion = config->MenuReduceMotion.value_or_default();
    menuWater.Update(config->LightTheme.value_or_default(), reduceMenuMotion, ImGui::GetTime());
    const bool drawingLight = menuWater.DrawingLight();
    ApplyThemeStyle(drawingLight);
    if (menuWater.Active())
    {
        ImVec4 current[ImGuiCol_COUNT], opposite[ImGuiCol_COUNT];
        std::copy_n(ImGui::GetStyle().Colors, ImGuiCol_COUNT, current);
        Neurotic::Sleek::ApplyTheme(config, !drawingLight);
        for (int i=0;i<ImGuiCol_COUNT;++i) opposite[i]=toneMapColor(ImGui::GetStyle().Colors[i]);
        std::copy_n(current, ImGuiCol_COUNT, ImGui::GetStyle().Colors);
        menuWaterColors.Set(drawingLight?opposite:current,drawingLight?current:opposite,drawingLight);
        Neurotic::Sleek::waterColorCapture = &menuWaterColors;
    }
    waterMenuFrame = ImGui::GetFrameCount();
    Neurotic::Sleek::Scope sleekScope(reduceMenuMotion);
    ImGui::SetNextWindowScroll({0,0});
    if (ImGui::Begin(windowTitle.c_str(), NULL, flags | ImGuiWindowFlags_NoResize | Neurotic::Sleek::FixedShellFlags))
    {
        waterMenuRoot = ImGui::GetCurrentWindow();
        RenderMainMenuWindowActions(ctx);
        Neurotic::Sleek::Brand(state.gameExe.c_str(),Neurotic::Sleek::HeaderActionsWidth(true,false)+ImGui::GetStyle().ItemSpacing.x);
        RenderMainMenuBottomBar(ctx);

        // Performance graphs and their visibility toggle remain available above every page.
        RenderMainMenuGraphs(ctx);

        RenderMainMenuHeaderMessages(ctx);

        // One selected top-level page replaces the old vertical two-column settings list.
        RenderMainMenuTabs(ctx);

        // Footer content stays inside the existing bottom bezel.
        RenderMainMenuSupportLink(ctx);

    }
    ImGui::End();
    // Detached utility windows owned by the main menu.
    RenderMipmapBiasWindow(ctx, flags | ImGuiWindowFlags_AlwaysAutoResize);
    RenderHudlessResourcesWindow(ctx, flags | ImGuiWindowFlags_AlwaysAutoResize);

    if (config->UseHQFont.value_or_default())
        ImGui::PopFontSize();
}

void UwpShortcutFocusChanged(bool focused)
{
    std::lock_guard lock(uwpShortcutsMutex);
    uwpShortcutsFocused=focused;
    uwpShortcuts.Reset();
}

void KeyDown(UINT vKey,bool ctrl,bool shift,bool alt)
{
    using namespace Neurotic::KeyChord;
    const bool inputFocused=OptiInput::IsFocused();
    std::lock_guard lock(uwpShortcutsMutex);
    if(capturingKey||!uwpShortcutsFocused||!inputFocused){uwpShortcuts.Reset();return;}
    uwpShortcuts.Observe(static_cast<int>(vKey),(ctrl?Ctrl:0)|(shift?Shift:0)|(alt?Alt:0),true,false);
}

void KeyUp(UINT vKey)
{
    // A UWP release consumes the key-down snapshot; modifier release order
    // and autorepeat cannot turn a Ctrl+F8 binding into plain F8.
    using namespace Neurotic::KeyChord;
    const bool inputFocused=OptiInput::IsFocused();
    std::lock_guard lock(uwpShortcutsMutex);
    if(capturingKey||!uwpShortcutsFocused||!inputFocused){uwpShortcuts.Reset();return;}
    auto released=uwpShortcuts.Observe(static_cast<int>(vKey),0,false,true);
    if(!released)return;
    const int chord=*released;
    auto config=Config::Instance();
    inputMenu |= Matches(config->ShortcutKey.value_or_default(),chord);
    inputFps |= Matches(config->FpsShortcutKey.value_or_default(),chord);
    inputFG |= Matches(config->FGShortcutKey.value_or_default(),chord);
    inputFpsCycle |= Matches(config->FpsCycleShortcutKey.value_or_default(),chord);
    inputDlssNr |= Matches(config->DlssNrToggleKey.value_or_default(),chord);
    inputScreenshot |= Matches(config->ScreenshotKey.value_or_default(),chord);
}

// The lamp, and only the lamp.
//
// Red for dark, green for full light, with its reading beside it. No status sentence: the whole
// point of a light meter is that it is read at a glance while playing, and a paragraph in the corner
// of somebody's game is not that. Everything wordy lives in the menu, which is where someone has
// already decided to stop and read.
//
// Drawn only when its own setting is on. An overlay that appears because a scan happens to be
// running is an overlay nobody asked for.
void RenderExposureScanIndicator(float alpha)
{
    using DlssNr::ExposureScan::Verdict;

    if (!Config::Instance()->DlssNrScanMeter.value_or_default())
        return;

    if (State::Instance().api!=API::Vulkan && DlssNr::ExposureScan::Where() == Verdict::Off)
        return;

    int which = 0;
    float low = 0.0f, high = 0.0f;
    const float now = State::Instance().api==API::Vulkan ? DlssNr::BestExposureScanVk(&which,&low,&high) : DlssNr::ExposureScan::BestValue(&which, &low, &high);

    // Nothing found yet, or no range to place it in: a dim lamp, which says "watching, no reading"
    // without saying it in words.
    const bool reading = now > 0.0f && high > low;

    float lit = 0.0f;

    if (reading)
    {
        // An exposure falls as the scene brightens, so the value reads backwards unless the buffer
        // holds the reciprocal -- the same question the anchor asks, answered from the same setting,
        // because a lamp contradicting the picture would be worse than no lamp.
        lit = (high - now) / (high - low);

        if (Config::Instance()->DlssNrScanInverted.value_or_default())
            lit = 1.0f - lit;

        lit = lit < 0.0f ? 0.0f : (lit > 1.0f ? 1.0f : lit);
    }

    // Red to amber to green. A straight red-to-green fade passes through a muddy brown at the
    // midpoint, and the midpoint is where most of a session is spent.
    const ImVec4 dark(0.90f, 0.22f, 0.20f, 1.0f);
    const ImVec4 mid(0.95f, 0.75f, 0.20f, 1.0f);
    const ImVec4 bright(0.35f, 0.88f, 0.38f, 1.0f);
    const ImVec4 idle(0.45f, 0.45f, 0.45f, 1.0f);

    ImVec4 lamp = idle;

    if (reading)
    {
        const float t = lit < 0.5f ? lit * 2.0f : (lit - 0.5f) * 2.0f;
        const ImVec4& a = lit < 0.5f ? dark : mid;
        const ImVec4& b = lit < 0.5f ? mid : bright;
        lamp = ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 12.0f, vp->WorkPos.y + 12.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(alpha);

    if (ImGui::Begin("DlssNrExposureScan", nullptr,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove))
    {
        const float r = ImGui::GetFontSize() * 0.38f;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 centre(at.x + r, at.y + ImGui::GetTextLineHeight() * 0.5f);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddCircleFilled(centre, r, ImGui::GetColorU32(lamp), 20);
        draw->AddCircle(centre, r, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.6f)), 20, 1.5f);

        ImGui::Dummy(ImVec2(r * 2.0f + 6.0f, ImGui::GetTextLineHeight()));
        ImGui::SameLine();

        if (reading)
            ImGui::TextColored(lamp, "%3.0f%%  %.5f", lit * 100.0f, now);
        else
            ImGui::TextColored(idle, "--");
    }

    ImGui::End();
}

bool MenuCommon::RenderMenu()
{
    if (!_isInited)
        return false;

    RenderMenuContext ctx { State::Instance(), Config::Instance(), ImGui::GetIO() };
    DlssNr::ExperimentalSession::Initialize(ctx.config);
    if (!_isVisible)
    {
        menuInputDraft.initialized = false;
        DlssNr::ExperimentalPolicy::DiscardDraft();
    }
    Neurotic::Localization::LoadSharedCatalogOnce();
    Neurotic::Localization::PublishQueuedCatalog();
    ctx.now = Util::MillisecondsNow();
    ctx.currentFeature = ctx.state.currentFeature;

    // Retired Advisor sessions must restore any temporary route before drawing current controls.
    DlssNr::CancelAdvisorAnalysis(ctx.config, Neurotic::UiLiteral("ingame.dlssnr-menu.advisor_archived_original_settings_restored_1867d682", "Advisor archived; original settings restored."));

    // 1) Collect timing and input state before any ImGui drawing.
    UpdateRenderTiming(ctx);
    UpdateMenuInputMode(ctx);
    HandleMenuShortcuts(ctx);

    // 2) Prepare one-shot notifications and start a new ImGui frame only when needed.
    UpdateVersionAndStartupNotifications(ctx);
    BeginMenuFrameIfNeeded(ctx);
    if (DlssNr::ExperimentalSession::ConsumeRecoveryNotice(ctx.now))
    {
        ImGuiToast notification { ImGuiToastType::Warning, 12000,
            Neurotic::UiLiteral("ingame.menu-common.last_session_ended_unexpectedly_experimental_opt_d338698e", "Last session ended unexpectedly. Experimental options have been disabled.") };
        notification.setTitle(Neurotic::UiLiteral("ingame.menu-common.neurotic_experimental_safety_c6a5a790", "NeuRotic experimental safety"));
        ImGui::InsertNotification(notification);
    }
    if (DlssNr::ExperimentalSession::ConsumeConcurrentOwnerNotice())
    {
        ImGuiToast notification { ImGuiToastType::Warning, 12000,
            Neurotic::UiLiteral("ingame.menu-common.experimental_options_are_inactive_because_anothe_023c60b2", "Experimental options are inactive because another game process owns the experimental session marker.") };
        notification.setTitle(Neurotic::UiLiteral("ingame.menu-common.neurotic_experimental_safety_c6a5a790", "NeuRotic experimental safety"));
        ImGui::InsertNotification(notification);
    }
    if (DlssNr::ExperimentalSession::ConsumeMarkerUnavailableNotice())
    {
        ImGuiToast notification { ImGuiToastType::Warning, 12000,
            Neurotic::UiLiteral("ingame.menu-common.experimental_options_could_not_be_activated_beca_4349b33d", "Experimental options could not be activated because the crash-recovery marker could not be written.") };
        notification.setTitle(Neurotic::UiLiteral("ingame.menu-common.neurotic_experimental_safety_c6a5a790", "NeuRotic experimental safety"));
        ImGui::InsertNotification(notification);
    }
    OptiInput::EndFrame(_isVisible && (!_rendererOwnsCapture || _rendererCaptureAvailable));

    // 3) Draw lightweight overlay windows first, preserving the original order.
    ctx.menuResScale = MenuResolutionScale(ctx.io);
    RenderSplashWindow(ctx);
    RenderNotifications(ctx);
    UpdateFrameTimeAverages(ctx);
    RenderPerformanceOverlay(ctx);
    RenderExposureScanIndicator(ctx.config->FpsOverlayAlpha.value_or_default());

    if(ctx.newFrame)
    {
        Neurotic::Semantic::Character::BeginInspectorColors();
        Neurotic::Semantic::Character::HeldSnapshot snapshot;
        Neurotic::Semantic::Character::DisplayContext display;
        if(Neurotic::Semantic::Character::TryCharacterHeldDisplay(snapshot,display))
            Neurotic::Semantic::Character::RenderInspectorLiveOverlay(snapshot,display,
                Neurotic::Semantic::Character::ReadSettings(*ctx.config),*ImGui::GetMainViewport(),_isVisible);
    }

    // 4) Draw the full settings menu last so popups and child windows keep their existing behavior.
    RenderMainMenuWindow(ctx);

    if (ctx.newFrame)
        ImGui::EndFrame();

    return ctx.newFrame;
}

void MenuCommon::SetVisibility(bool visible)
{
    if (_isVisible != visible)
    {
        _isVisible = visible;
        _visibilityGeneration.fetch_add(1);
    }
    // Releasing the menu must release the hook policy immediately, even when
    // no renderer will run another EndFrame (shutdown, resize or GPU failure).
    if (!visible)
    {
        _rendererCaptureAvailable = false;
        _rendererCapturePresentedAt = 0.0;
        OptiInput::SetMenuVisible(false);
    }
}

void MenuCommon::DeferInputCapture() { _rendererOwnsCapture = true; }

void MenuCommon::SetRendererCaptureAvailable(bool available)
{
    // A notification/FPS-only Present does not grant capture to a menu that
    // might be requested on a later frame whose drawing then fails.
    _rendererCaptureAvailable = available && _isVisible;
    _rendererCapturePresentedAt = _rendererCaptureAvailable ? Util::MillisecondsNow() : 0.0;
    OptiInput::SetMenuVisible(_rendererCaptureAvailable);
}

bool MenuCommon::CanRetainRendererCaptureOnBusyFrame()
{
    // An occasional busy GPU slot must not restore the game's cursor clip or
    // admit camera movement while the already presented menu is still open.
    // Only a successful menu Present renews this short lease. A renderer that
    // stays busy releases capture; a requested but never drawn menu cannot acquire it.
    constexpr double BusyCaptureGraceMs = 250.0;
    const double age = Util::MillisecondsNow() - _rendererCapturePresentedAt;
    return _isVisible && _rendererCaptureAvailable && age >= 0.0 && age < BusyCaptureGraceMs;
}

void MenuCommon::ProcessUnavailableInput()
{
    DeferInputCapture();
    SetRendererCaptureAvailable(false);
    if (!_isInited || ImGui::GetCurrentContext() == nullptr)
        return;
    RenderMenuContext ctx { State::Instance(), Config::Instance(), ImGui::GetIO() };
    ctx.now = Util::MillisecondsNow();
    UpdateRenderTiming(ctx);
    UpdateMenuInputMode(ctx);
    HandleMenuShortcuts(ctx);
    // Keep requested open/closed state and hotkeys progressing without
    // starting an ImGui frame or acquiring invisible gameplay input.
    OptiInput::EndFrame(false);
}

void MenuCommon::FinalizeFrame()
{
    ImGui::Render();
    Neurotic::Sleek::waterColorCapture = nullptr;
    if (_isVisible && waterMenuFrame == ImGui::GetFrameCount())
    {
        menuWater.Render(ImGui::GetDrawData(), waterMenuRoot, menuWaterColors, reduceMenuMotion);
        if (reduceMenuMotion) menuWater.Reset();
    }
    else
        menuWater.Reset();
    const float gain = Neurotic::UiBrightness::Clamp(Config::Instance()->MenuBrightness.value_or_default());
    auto* data = ImGui::GetDrawData();
    if (!data) return;
    // Only this frame's UI vertex colors change. Never touch scene textures, NR settings or PNG data.
    if (gain != 1.0f)
        for (auto* list : data->CmdLists)
            for (int i=0;i<list->VtxBuffer.Size;++i)
                if (!Neurotic::Semantic::Character::InspectorOwnsColorVertex(list,i))
                    list->VtxBuffer[i].col = Neurotic::UiBrightness::Apply(list->VtxBuffer[i].col, gain);
    Neurotic::Semantic::Character::FinalizeInspectorColors(data);
}

void MenuCommon::Init(HWND InHwnd, bool isUWP)
{
    Neurotic::Localization::LoadSharedCatalogOnce();
    // Reset shutdown flag in case of re-init
    State::Instance().isShuttingDown = false;
    DlssNr::ExperimentalSession::Initialize(Config::Instance());

    HWND oldHandle = nullptr;

    if (_handle != nullptr)
    {
        oldHandle = _handle;
        LOG_DEBUG("Old Handle: {:X}, ImGui Handle: {:X}", (size_t) oldHandle,
                  (size_t) ImGui::GetMainViewport()->PlatformHandleRaw);
    }

    _handle = InHwnd;
    SetVisibility(false);
    _isUWP = isUWP;

    LOG_DEBUG("Handle: {0:X}", (size_t) _handle);

    // In case d3d12 wasn't yet used up to this point, try to update GPU info late here
    IdentifyGpu::updateD3d12Capabilities();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    Neurotic::Sleek::ApplyMetrics(ImGui::GetStyle());
    lastMenuScale = -1;

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
    io.BackendFlags &= 30;
    io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
    io.WantSetMousePos = _isVisible;

    io.IniFilename = io.LogFilename = nullptr;

    bool initResult = false;

    if (io.BackendPlatformUserData == nullptr)
    {
        if (!isUWP)
        {
            initResult = ImGui_ImplWin32_Init(InHwnd);
            LOG_DEBUG("ImGui_ImplWin32_Init result: {0}", initResult);
        }
        else
        {
            initResult = ImGui_ImplUwp_Init(InHwnd);
            ImGui_BindUwpKeyUp(KeyUp);
            ImGui_BindUwpKeyDown(KeyDown);
            ImGui_BindUwpFocusChanged(UwpShortcutFocusChanged);
            UwpShortcutFocusChanged(true);
            LOG_DEBUG("ImGui_ImplUwp_Init result: {0}", initResult);
        }
    }

    if (io.Fonts->Fonts.empty() && Config::Instance()->UseHQFont.value_or_default())
    {
        ImFontAtlas* atlas = io.Fonts;
        atlas->Clear();

        // This automatically becomes the next default font
        ImFontConfig fontConfig;

        if (Config::Instance()->FontSize.has_value())
            fontSize = Config::Instance()->FontSize.value();

        if (Config::Instance()->TTFFontPath.has_value())
        {
            io.FontDefault =
                atlas->AddFontFromFileTTF(wstring_to_string(Config::Instance()->TTFFontPath.value()).c_str(), fontSize,
                                          &fontConfig, io.Fonts->GetGlyphRangesDefault());
        }
        else
        {
            io.FontDefault = Neurotic::AddInterfaceFont(atlas,fontSize);
            if (!io.FontDefault)
                io.FontDefault = atlas->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,
                                                                             fontSize, &fontConfig);
        }
    }

    Neurotic::AddLanguageFonts(io.Fonts, fontSize);

    if (!Config::Instance()->OverlayMenu.value_or_default())
    {
        _hdrTonemapApplied = false;
    }

    DWORD hwndPid = 0;
    DWORD hwndTid = GetWindowThreadProcessId(_handle, &hwndPid);

    LOG_DEBUG("HWND: {:X}, IsWindow: {}, HWND PID: {}, Current PID: {}, HWND TID: {}, Current TID: {}",
              (ULONG64) _handle, IsWindow(_handle), hwndPid, GetCurrentProcessId(), hwndTid, GetCurrentThreadId());

    OptiInput::Initialize(_handle, isUWP);

    ApplyThemeStyle();
    _isInited = true;
}

void MenuCommon::Shutdown()
{
    Neurotic::Semantic::Character::StopCharacterWorker();
    Neurotic::Semantic::Character::ResetInspectorPreviewSession();
    SetVisibility(false);
    _rendererOwnsCapture = false;
    _rendererCaptureAvailable = false;
    if (!MenuCommon::_isInited)
        return;

    DlssNr::CancelAdvisorAnalysis(Config::Instance(), Neurotic::UiLiteral("ingame.menu-common.menu_shutdown_original_settings_restored_a7f87181", "Menu shutdown; original settings restored."));

    // if (_oWndProc != nullptr)
    //{
    //     auto handle = (HWND) ImGui::GetMainViewport()->PlatformHandleRaw;
    //     SetLastError(0);
    //     auto restoreResult = SetWindowLongPtr(handle, GWLP_WNDPROC, (LONG_PTR) _oWndProc);
    //     auto error = GetLastError();

    //    if (restoreResult == 0 && error != 0)
    //    {
    //        LOG_ERROR("Failed to restore old WndProc. Error: {:X}", error);
    //    }

    //    _oWndProc = nullptr;
    //}

    if (!_isUWP)
        ImGui_ImplWin32_Shutdown();
    else
        ImGui_ImplUwp_Shutdown();

    Neurotic::Sleek::waterColorCapture = nullptr;
    menuWater.Reset();
    waterMenuRoot = nullptr;
    waterMenuFrame = -1;
    ImGui::DestroyContext();

    _handle = nullptr;
    _isInited = false;
    SetVisibility(false);
}

void MenuCommon::HideMenu()
{
    OptiInput::SetMenuVisible(false);
    if (!_isVisible)
        return;

    DlssNr::CancelAdvisorAnalysis(Config::Instance());
    menuInputDraft.initialized = false;
    DlssNr::ExperimentalPolicy::DiscardDraft();
    capturingKey = false;
    escapeClose.armed = false;
    SetVisibility(false);

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    _showMipmapCalcWindow = false;
    _showHudlessWindow = false;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
}

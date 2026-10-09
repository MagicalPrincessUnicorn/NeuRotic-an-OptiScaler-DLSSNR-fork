// Included by DlssNr_Menu.cpp inside namespace DlssNr, after the existing menu helpers.
// Read-only projection: keep native evaluation, Present submission and completion separate.
static void RenderDiagnosticRuntime(Config* config,NrDiagnosticsUi::Page page,
    const std::optional<MenuStatus::RuntimeStatus>& status) {
    using namespace NrDiagnosticsUi;
    auto settings=config->GetDlssNrConfigSnapshot();
    if(BasicMultipass::Active(settings))BasicMultipass::Derive(settings);
    const bool enabled=settings.DlssNrEnabled.value_or_default();
    const int route=std::clamp(int(settings.DlssNrRoute.value_or_default()),0,3);
    const bool presentRoute=route==1||route==2;
    const bool nativeRoute=route==0;
    const bool vulkan=DlssNr::IsRunningVk()||IsVulkanInput();
    const auto native=SelectedNativeTelemetry();const auto present=DlssNr::PresentTelemetry();
    const auto bridge=DlssNr::BridgeTelemetry().Snapshot();const auto guides=PresentGuides::Instance().Inspect();
    const auto output=RenderingOutput::Query();const auto now=GetTickCount64();
    const bool active=RenderingOutput::Active(output,now);
    const bool nativeActive=nativeRoute&&enabled&&active&&output.producer==RenderingOutput::Producer::Native;
    static MenuStatus::SelectionObservation observation;
    const auto selection=(PresentResolution::CaptureKey(settings)<<1)|(enabled?1ull:0ull);
    const bool fresh=observation.Fresh(selection,present.presentAttempts+present.skippedFrames);
    const auto policy=PresentResolution::Selected(settings);
    const bool presentMatches=fresh&&present.requested&&present.requestedPlacement=="Present";
    const bool presentActive=enabled&&presentMatches&&present.active&&present.resolution==policy.mode&&present.workload==policy.scale;
    const auto unknown=Neurotic::Translate(Neurotic::UiLiteral("ingame.menu-common.unknown_d80d0833","Unknown"));
    const auto unavailable=Neurotic::Translate(Neurotic::UiLiteral("ingame.nr-diagnostics.unavailable","Unavailable"));
    const auto unobserved=Neurotic::Translate(Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6","Not observed"));
    const auto none=Neurotic::Translate(Neurotic::UiLiteral("ingame.dlssnr-menu.none_reported_9554bb9f","none reported"));
    const auto nativeFailure=native.failureReason&&native.failureReason[0]?std::string(native.failureReason):vulkan?DlssNr::FailureReasonVk():std::string();
    const bool historyObserved=presentRoute?present.presentAttempts!=0:nativeRoute&&native.frames!=0;
    const bool historyReset=presentRoute?present.historyResetPending:native.historyResetRequested;
    auto count=[](auto v){return std::to_string(v);};
    auto dimensions=[&](uint32_t w,uint32_t h){return w&&h?count(w)+" × "+count(h):std::string(unobserved);};
    auto text=[&](std::string_view v){return v.empty()?std::string(unobserved):std::string(v);};
    uint32_t workW=0,workH=0,outW=0,outH=0;
    if(presentRoute&&enabled&&presentMatches){outW=present.backbufferWidth;outH=present.backbufferHeight;if(presentActive){workW=present.workWidth;workH=present.workHeight;}}
    else if(nativeActive){workW=native.workWidth;workH=native.workHeight;
        const bool before=settings.DlssNrRenderingMode.value_or_default()!=0&&!native.nativeRayReconstructionActive;
        const auto feature=State::Instance().currentFeature;outW=before?(feature?feature->DisplayWidth():0u):native.frameWidth;outH=before?(feature?feature->DisplayHeight():0u):native.frameHeight;
        if(status){workW=status->workWidth;workH=status->workHeight;outW=status->outputWidth;outH=status->outputHeight;}}
    auto resolution=Neurotic::Translate(StageUi::ResolutionChoices[StageUi::ResolutionChoiceSelection(settings)]);
    if(StageUi::ResolutionSelection(settings)==1)resolution+=" ("+count(StageUi::DisplayPercent(StageUi::ResolutionScale(settings)))+"%)";
    const bool before=settings.DlssNrRenderingMode.value_or_default()!=0&&!native.nativeRayReconstructionActive;
    const char* method=route==3?"NR Anything":StageUi::Methods[std::clamp(route,0,2)];
    std::vector<Group> groups;
    if(page==Page::Runtime){
        groups.push_back({"rendering",Neurotic::UiLiteral("ingame.provider.e2fe353986e6","Rendering"),{
            {Neurotic::UiLiteral("ingame.nr-diagnostics.method","Method"),nativeRoute?Neurotic::UiMessage("ingame.dlssnr-menu.native_temporal_682e0cdf","Native Temporal"):Neurotic::Translate(method)},
            {Neurotic::UiLiteral("ingame.dlssnr-menu.graphics_api_66585b5d","Graphics API"),vulkan?"Vulkan":State::Instance().api==API::DX12?"D3D12":State::Instance().api==API::DX11?"D3D11":unknown},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.stage_request","Stage request"),presentRoute?Neurotic::UiMessage("ingame.dlssnr-menu.final_present_925b74b4","Final Present"):nativeRoute?(before?"Pre-SR":"Post-SR"):unavailable},
            {Neurotic::UiLiteral("ingame.dlssnr-menu.requested_placement_51130ae6","Requested placement"),presentRoute?(presentMatches?text(present.requestedPlacement):unobserved):nativeRoute?(before?"Pre-SR":"Post-SR"):unavailable,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.observed_placement","Observed placement"),presentRoute?(presentMatches?text(present.actualPlacement):unobserved):nativeActive?(native.runBeforeSr?(native.preSrDisplayReady?"Pre-SR":unobserved):"Post-SR"):unobserved,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.nr_size","NR size"),dimensions(workW,workH)},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.output_size","Output size"),dimensions(outW,outH)},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.resolution_request","Resolution request"),resolution,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.rendering_mode","Rendering mode request"),settings.DlssNrRenderingMode.value_or_default()!=0?Neurotic::UiMessage("ingame.provider.6c5d98ea2e5d","Performance (Default)"):Neurotic::UiMessage("ingame.provider.1b2c08a8733d","Quality"),Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.apply_model","Apply model request"),settings.DlssNrApplyModel.value_or_default()?Neurotic::UiMessage("ingame.option.on","On"):Neurotic::UiMessage("ingame.option.off","Off")},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.pre_sr_ready","Pre-SR output ready"),nativeActive&&before&&native.preSrDisplayReady?Neurotic::UiMessage("ingame.dlssnr-menu.yes_d6f3c5eb","Yes"):unobserved},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.output_quarantined","Output quarantined"),nativeRoute&&native.frames?(native.outputQuarantined?Neurotic::UiMessage("ingame.dlssnr-menu.yes_d6f3c5eb","Yes"):Neurotic::UiMessage("ingame.dlssnr-menu.no_d2ffb35e","No")):unobserved}
        }});
        groups.push_back({"completion",Neurotic::UiLiteral("ingame.nr-diagnostics.completion","Completion"),{
            {Neurotic::UiLiteral("ingame.nr-diagnostics.native_completed","Native GPU completed"),nativeRoute?count(native.gpuCompletedOutputEvaluations):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.present_evaluations","Present model evaluations"),presentRoute?count(present.modelEvaluations):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.present_submitted","Present model submitted"),presentRoute?count(present.modelSubmissions):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.composite_evaluations","Composite evaluations"),presentRoute?count(present.compositeEvaluations):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.composite_submitted","Composite submitted"),presentRoute?count(present.compositeSubmissions):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.canonical_readiness","Canonical readiness"),unknown},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.image_quality","Image quality"),Neurotic::UiMessage("ingame.dlssnr-menu.not_measured_3c3329c5","Not measured")},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.display_confirmation","Display confirmation"),Neurotic::UiMessage("ingame.nr-diagnostics.not_verified","Not independently verified"),Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.fg_status","FG status"),NativeFg::Status(),Kind::Detail}
        }});
        groups.push_back({"guides",Neurotic::UiLiteral("ingame.nr-diagnostics.guide_matching","Guide matching"),{
            {Neurotic::UiLiteral("ingame.nr-diagnostics.guide_status","Guide status"),presentRoute?text(guides.status):unobserved,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.capture_attempts","Capture attempts"),presentRoute?count(guides.captureAttempts):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.input_description","Input description"),presentRoute?text(guides.inputDescription):unobserved,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.capture_failure","Capture failure"),presentRoute?(guides.captureError.empty()?none:guides.captureError):none,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.guide_copies","Guide copies"),presentRoute?count(guides.captures):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.matched","Matched"),presentRoute?count(guides.matched):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.evaluated","Evaluated"),presentRoute?count(guides.evaluated):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.rejected","Rejected"),presentRoute?count(guides.rejected):unavailable}
        }});
        groups.push_back({"history",Neurotic::UiLiteral("ingame.nr-diagnostics.history_fallback","History and fallback"),{
            {Neurotic::UiLiteral("ingame.nr-diagnostics.history","History"),!enabled?Neurotic::UiMessage("ingame.nr-diagnostics.off_retained","Off · last recorded"):!historyObserved?unobserved:historyReset?Neurotic::UiMessage("ingame.dlssnr-menu.reset_pending_f25d1cc3","reset pending"):Neurotic::UiMessage("ingame.dlssnr-menu.no_reset_pending_reported_0f9b2595","No reset pending reported"),Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.uninterrupted","Uninterrupted outputs"),presentRoute?count(present.uninterruptedFrames):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.reset_reason","Reset reason"),presentRoute?text(present.historyResetReason):unobserved,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.last_interruption","Last interruption"),presentRoute?(present.historyInvalidationReason.empty()?none:present.historyInvalidationReason):unobserved,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.present_attempts","Present attempts"),presentRoute?count(present.presentAttempts):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.skipped","Skipped"),presentRoute?count(present.skippedFrames):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.fallback_streak","Fallback streak"),presentRoute?count(present.consecutiveFallbacks):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.last_fallback","Last fallback attempt"),presentRoute?count(present.lastFallbackAttempt):unavailable}
        }});
    }else if(page==Page::Timing){
        const auto gpu=vulkan?DlssNr::LastGpuTimeVk():native.totalGpuMs;
        groups.push_back({"cost",Neurotic::UiLiteral("ingame.nr-diagnostics.nr_cost","NR cost"),{
            {Neurotic::UiLiteral("ingame.nr-diagnostics.native_gpu","Native GPU"),nativeRoute&&enabled&&native.running&&gpu?StatusPanel::Format("%.2f ms",*gpu):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.vulkan_frames","Vulkan native frames"),vulkan&&nativeRoute?count(DlssNr::FramesVk()):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.cost_scope","Cost scope"),Neurotic::UiMessage("ingame.nr-diagnostics.cost_scope_value","Model, staging copies and resolve"),Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.model_edit","Model edit"),settings.DlssNrApplyModel.value_or_default()?Neurotic::UiMessage("ingame.nr-diagnostics.visible_requested","Visible · requested"):Neurotic::UiMessage("ingame.nr-diagnostics.hidden_requested","Hidden · requested"),Kind::Detail}
        }});
        groups.push_back({"cpu",Neurotic::UiLiteral("ingame.nr-diagnostics.present_cpu","Present CPU"),{
            {Neurotic::UiLiteral("ingame.nr-diagnostics.adapter_current_max","Adapter current / max"),presentRoute?StatusPanel::Format("%.2f / %.2f ms",present.adapterCpuMs,present.adapterCpuMaxMs):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.adapter_slow","Adapter ≥ 4 ms"),presentRoute?count(present.adapterCpuSlowCalls):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.original_present_current_max","Original Present current / max"),presentRoute?StatusPanel::Format("%.2f / %.2f ms",present.originalPresentMs,present.originalPresentMaxMs):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.present_slow","Original Present ≥ 33.3 ms"),presentRoute?count(present.originalPresentSlowCalls):unavailable}
        }});
        const auto& s=present.pacingSummary;
        const bool pacing=presentRoute&&present.hasPacingSummary;
        const bool gpuPacing=pacing&&s.route!=PresentPacing::Route::NativeTemporal;
        auto pair=[](double a,double b){return StatusPanel::Format("%.2f / %.2f ms",a,b);};
        auto cpuPair=[](double a,double b){return StatusPanel::Format("%.3f / %.3f ms",a,b);};
        groups.push_back({"pacing",Neurotic::UiLiteral("ingame.nr-diagnostics.pacing_window","Completed pacing window"),{
            {Neurotic::UiLiteral("ingame.nr-diagnostics.window","Window"),pacing?count(s.serial):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.window_route","Window route"),pacing?(s.route==PresentPacing::Route::PresentEnhanced?Neurotic::UiMessage("ingame.dlssnr-menu.present_enhanced_eb02b993","Present Enhanced"):s.route==PresentPacing::Route::PresentImageOnly?Neurotic::UiMessage("ingame.dlssnr-menu.present_image_only_23fa3c81","Present Image Only"):Neurotic::UiMessage("ingame.dlssnr-menu.native_temporal_682e0cdf","Native Temporal")):unavailable,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.samples_warmup","Samples / discarded warm-up"),pacing?count(s.frameInterval.samples)+" / "+count(s.warmupDiscarded):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.frame_avg_median","Frame avg / median"),pacing?pair(s.frameInterval.average,s.frameInterval.median):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.frame_p95_max","Frame p95 / max"),pacing?pair(s.frameInterval.p95,s.frameInterval.maximum):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.adapter_p95_max","Adapter CPU p95 / max"),pacing?cpuPair(s.adapterCpu.p95,s.adapterCpu.maximum):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.hook_p95_max","Hook CPU p95 / max"),pacing?cpuPair(s.hookCpu.p95,s.hookCpu.maximum):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.present_p95_max","Present CPU p95 / max"),pacing?cpuPair(s.originalPresentCpu.p95,s.originalPresentCpu.maximum):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.gpu_samples","Present GPU samples / expected"),gpuPacing?count(s.presentGpu.samples)+" / "+count(s.expectedGpuSamples):pacing?Neurotic::UiMessage("ingame.nr-diagnostics.native_window_na","Not applicable · Native window"):unavailable,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.gpu_median_p95_max","Present GPU median / p95 / max"),gpuPacing?StatusPanel::Format("%.2f / %.2f / %.2f ms",s.presentGpu.median,s.presentGpu.p95,s.presentGpu.maximum):pacing?Neurotic::UiMessage("ingame.nr-diagnostics.native_window_na","Not applicable · Native window"):unavailable}
        }});
        groups.push_back({"queue",Neurotic::UiLiteral("ingame.nr-diagnostics.completion_queue","Completion and queue"),{
            {Neurotic::UiLiteral("ingame.nr-diagnostics.fence_submitted_completed","Fence submitted / completed"),presentRoute?count(present.lastSubmittedFence)+" / "+count(present.lastCompletedFence):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.pending_slots","Pending slots"),presentRoute?count(present.pendingSlots)+" / 8":unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.completion_bound","Completion bound p95 / max"),gpuPacing?pair(s.completionObservation.p95,s.completionObservation.maximum):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.fence_age","Fence age p95 / max"),gpuPacing?StatusPanel::Format("%.0f / %.0f attempts",s.fenceAge.p95,s.fenceAge.maximum):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.pending_highwater","Pending high-water"),gpuPacing?count(s.pendingSlotsHighWater)+" / 8":unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.missing_gpu","Missing GPU samples"),gpuPacing?count(s.missingGpuSamples):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.unmatched_gpu","Unmatched late GPU samples"),gpuPacing?count(present.unmatchedGpuTimingSamples):unavailable}
        }});
    }else if(page==Page::Connections){
        groups.push_back({"compatibility",Neurotic::UiLiteral("ingame.nr-diagnostics.compatibility","Compatibility"),{
            {Neurotic::UiLiteral("ingame.nr-diagnostics.present_path","Present path"),presentRoute?text(present.compatibilityPath):unobserved,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.last_failure","Last owner failure"),presentRoute?(present.failure.empty()?none:present.failure):nativeRoute?(nativeFailure.empty()?none:nativeFailure):unavailable,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.fallback","Fallback"),presentRoute?(present.fallbackReason.empty()?none:present.fallbackReason):none,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.bridge","D3D11 native bridge"),!presentRoute&&bridge.observed?bridge.reason:unobserved,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.bridge_handoffs","Bridge handoffs"),!presentRoute&&bridge.observed?count(bridge.handoffs):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.bridge_builds","Bridge model builds"),!presentRoute&&bridge.observed?count(bridge.modelCreations):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.bridge_evaluations","Bridge evaluations"),!presentRoute&&bridge.observed?count(bridge.modelEvaluations):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.bridge_compositions","Bridge compositions"),!presentRoute&&bridge.observed?count(bridge.compositions):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.bridge_copybacks","Bridge copybacks"),!presentRoute&&bridge.observed?count(bridge.copyBacks):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.vulkan_tuning","Vulkan model tuning"),State::Instance().api==API::Vulkan?DlssNr::TuningStatusVk():unavailable,Kind::Detail}
        }});
    }
    if(!groups.empty())Groups(groups);
}

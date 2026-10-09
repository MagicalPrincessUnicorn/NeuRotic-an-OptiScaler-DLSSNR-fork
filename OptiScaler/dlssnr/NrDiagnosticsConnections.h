// Implementation include inside DlssNr. These are producer observations, not admission decisions.
static void RenderDiagnosticConnections() {
    using namespace NrDiagnosticsUi;
    const auto connection=PreparedGuides::QueryStatusV2(GetTickCount64());
    const auto prepared=PreparedGuides::QueryStatus();const auto native=NativeVulkanGuides::Status();
    const auto unknown=Neurotic::Translate(Neurotic::UiLiteral("ingame.menu-common.unknown_d80d0833","Unknown"));
    const auto unavailable=Neurotic::Translate(Neurotic::UiLiteral("ingame.nr-diagnostics.unavailable","Unavailable"));
    const auto unobserved=Neurotic::Translate(Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6","Not observed"));
    const auto yes=Neurotic::Translate(Neurotic::UiLiteral("ingame.dlssnr-menu.yes_d6f3c5eb","Yes"));
    const auto no=Neurotic::Translate(Neurotic::UiLiteral("ingame.dlssnr-menu.no_d2ffb35e","No"));
    auto count=[](auto value){return std::to_string(value);};
    auto dim=[&](uint32_t w,uint32_t h){return w&&h?count(w)+" × "+count(h):std::string(unobserved);};
    const auto& c=connection.status;const auto& p=prepared.status;
    std::vector<Group> groups={
        {"connection",Neurotic::UiLiteral("ingame.nr-diagnostics.selected_connection","Selected connection"),{
            {Neurotic::UiLiteral("ingame.nr-diagnostics.source","Source"),connection.available?Neurotic::Translate(ConnectionSourceLabel(c.selectedSource)):unobserved},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.transport","Transport"),connection.available?Neurotic::Translate(ConnectionTransportLabel(c.effectiveTransport)):unobserved,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.freshness","Freshness"),!connection.available?unobserved:connection.fresh?Neurotic::UiMessage("ingame.nr-diagnostics.current","Current"):Neurotic::UiMessage("ingame.nr-diagnostics.stale","Stale")},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.creation_ready","Creation ready"),connection.available?(c.creationReady?yes:no):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.guides_ready","Guides ready"),connection.available?(c.guideReady?yes:no):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.model_preparing","Model preparing"),connection.available?(c.modelPreparing?yes:no):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.output_valid","Output valid"),connection.available?(c.outputValid?yes:no):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.capture_size","Capture size"),connection.available?dim(c.captureWidth,c.captureHeight):unobserved},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.nr_size","NR size"),connection.available?dim(c.workWidth,c.workHeight):unobserved},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.output_size","Output size"),connection.available?dim(c.outputWidth,c.outputHeight):unobserved},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.display_observed","Display observed"),connection.available&&c.displayObserved?yes:unknown},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.reason","Reason"),connection.available&&c.reason[0]?c.reason:unobserved,Kind::Detail}
        }},
        {"vulkan",Neurotic::UiLiteral("ingame.dlssnr-menu.built_in_vulkan_inputs_ab636f3f","Built-in Vulkan inputs"),{
            {Neurotic::UiLiteral("ingame.nr-diagnostics.selected","Selected"),native.selected?yes:no},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.depth_origin","Depth origin"),native.selected?Neurotic::UiMessage("ingame.nr-diagnostics.captured_depth","Captured depth"):unobserved},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.motion_origin","Motion origin"),native.selected?Neurotic::UiMessage("ingame.dlssnr-menu.estimated_motion_c3d76169","Estimated motion"):unobserved},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.captured_frames","Captured frames"),native.selected?count(native.captured):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.copyback_completed","Copyback completed"),native.selected?count(native.delivered):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.render_scopes","Render scopes"),native.selected?count(native.scopesSeen):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.depth_copies","Depth copies"),native.selected?count(native.depthCopies):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.estimated_associations","Estimated associations"),native.selected?count(native.estimatedAssociations):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.reason","Reason"),native.selected&&!native.reason.empty()?native.reason:unobserved,Kind::Detail}
        }},
        {"prepared",Neurotic::UiLiteral("ingame.dlssnr-menu.reshade_prepared_inputs_d7778480","ReShade prepared inputs"),{
            {Neurotic::UiLiteral("ingame.nr-diagnostics.availability","Availability"),prepared.available?Neurotic::UiMessage("ingame.nr-diagnostics.available","Available"):unavailable},
            {Neurotic::UiLiteral("ingame.dlssnr-menu.graphics_api_66585b5d","Graphics API"),prepared.available?Neurotic::Translate(PreparedGuides::SourceApiName(p.sourceApi)):unobserved},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.stage","Stage"),prepared.available?Neurotic::Translate(PreparedGuides::StageName(p.stage)):unobserved,Kind::Detail},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.freshness","Freshness"),!prepared.available?unobserved:prepared.fresh?Neurotic::UiMessage("ingame.nr-diagnostics.current","Current"):Neurotic::UiMessage("ingame.nr-diagnostics.stale","Stale")},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.depth_ready","Depth ready"),prepared.available?(p.depthReady?yes:no):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.motion_ready","Motion ready"),prepared.available?(p.motionReady?yes:no):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.input_frames","Input frames"),prepared.available?count(p.inputFrames):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.model_completed","Model completed"),prepared.available?count(p.modelCompletions):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.copyback_completed","Copyback completed"),prepared.available?count(p.copybackCompletions):unavailable},
            {Neurotic::UiLiteral("ingame.nr-diagnostics.reason","Reason"),prepared.available&&p.reason[0]?p.reason:unobserved,Kind::Detail}
        }}
    };
    Groups(groups);
}

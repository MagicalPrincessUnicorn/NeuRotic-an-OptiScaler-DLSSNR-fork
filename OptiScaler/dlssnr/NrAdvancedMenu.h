#pragma once
#include "NrPipelineView.h"
class Config;
namespace DlssNr {
// Draw navigation and NR-owned panels. SR/FG forward to their existing menu owners.
PipelineView::Stage RenderAdvancedPipeline(::Config* config, bool navigationOnly = false, bool* activated = nullptr);
void RenderWorkspaceStatus(::Config* config);
void RenderStandaloneFgSettings(::Config* config);
}

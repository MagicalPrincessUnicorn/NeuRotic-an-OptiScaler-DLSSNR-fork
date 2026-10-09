#pragma once
#include <algorithm>
namespace Neurotic::Sleek {
struct RenderingDiagnosticsDestination {
    int main=0,neural=0,diagnosticsChild=4;
    bool forceNeural=false,forceDiagnostics=false;
};
inline RenderingDiagnosticsDestination ResolveRenderingDiagnosticsDestination(int main,int neural,int diagnosticsChild,int requested,bool firstNeuralVisit=false) {
    RenderingDiagnosticsDestination destination{std::clamp(main,0,6),std::clamp(neural,0,6),diagnosticsChild,requested>=0,false};
    if(requested>=0){destination.main=2;destination.neural=std::clamp(requested,0,6);}
    else if(firstNeuralVisit&&destination.main==2){destination.neural=0;destination.forceNeural=true;}
    if(destination.main==2&&destination.neural==3){
        destination.main=6;destination.diagnosticsChild=4;destination.neural=0;
        destination.forceNeural=false;destination.forceDiagnostics=true;
    }
    return destination;
}
struct RenderingDiagnosticsSession {
    bool neuralVisited=false;
};
inline RenderingDiagnosticsDestination ResolveRenderingDiagnosticsSession(RenderingDiagnosticsSession& session,int main,int neural,int diagnosticsChild,int requested) {
    const auto destination=ResolveRenderingDiagnosticsDestination(main,neural,diagnosticsChild,requested,!session.neuralVisited);
    // Diagnostics links resolve outside NR and leave its first visit available.
    if(destination.main==2)session.neuralVisited=true;
    return destination;
}
}

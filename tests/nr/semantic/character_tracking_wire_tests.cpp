#include "nr/semantic/character/CharacterTrackingWire.h"
#include "nr/semantic/character/CharacterBoxHold.h"
#include <iostream>
#include <stdexcept>
using namespace Neurotic::Semantic::Character;
static void Check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
int main(){try{
    CpuFrame f{{{1,2,3,4,5,6,7,8},10,1'100'000'000},4,4,16,16,{}};f.detectObjects=true;
    const std::string nonce(32,'a'),hash(64,'b');SubmittedFrames history;history.Add(f,hash);
    Json r={{"schema",3},{"nonce",nonce},{"pid",42},{"models",ModelHashes()},{"sequence",f.key.sequence},
        {"epoch",EpochWire(f.key.epoch)},{"capture_ns",f.key.captureNs},{"width",4},{"height",4},
        {"image_sha256",hash},{"status","ok"},{"persons",Json::array()},
        {"tracking",{{"detector_candidates",1},{"detector_returned",1},{"detector_ms",75.},{"pose_ms",0.},
            {"tracking_ms",3.},{"detection_updates",1},{"pose_updates",0},{"pose_failures",0}}}};
    Json p={{"box",{.1,.1,.4,.8}},{"score",.9},{"geometry","detector_box"},{"class_id",0},
        {"track_id",7},{"source",{{"sequence",f.key.sequence},{"capture_ns",f.key.captureNs},{"image_sha256",hash}}},
        {"pose_source",nullptr},{"quality",.9},{"velocity",{.2,0.}},{"pose",Json::array()}};
    r["persons"].push_back(p);Snapshot out;TrackingStats stats;
    auto decode=[&](const Json& value){return DecodeTrackingReply(value,f,nonce,42,hash,history,out,stats);};
    Check(decode(r)&&out.count==1&&out.items[0].id==7,"valid tracked observation");
    BoxHold boxes;Check(boxes.Update(out,f.key.captureNs+1,500,true),"track accepted");
    DisplayContext display{f.key.epoch,f.key.sequence,f.key.captureNs+120'000'000,true,false,false};HeldSnapshot held;
    Check(!boxes.Read(display,500,held,16,true,true),"long hold must not extend motion sample past 100ms");
    unsigned checks=1;
    auto predicted=PredictTrackedGeometry(out,f.key.captureNs+80'000'000);
    Check(std::abs(predicted.items[0].body.x0-(out.items[0].body.x0+.004))<1e-6,"prediction stops at twenty milliseconds");++checks;
    out.items[0].hasPose=true;out.items[0].poseNs=f.key.captureNs-240'000'000;
    Check(!PredictTrackedGeometry(out,f.key.captureNs+20'000'000).items[0].hasPose,"pose expires independently at presentation");++checks;
    Check(decode(r),"restore valid observation");
    Snapshot empty=out;empty.count=0;empty.key.sequence++;empty.key.captureNs++;
    Check(boxes.Update(empty,empty.key.captureNs+1,500,true,true),"authoritative zero update accepted");
    display.frameSequence=empty.key.sequence;display.nowNs=empty.key.captureNs+2;
    Check(!boxes.Read(display,500,held,16,true,true),"cut zero does not resurrect static hold");++checks;
    auto stalled=r;stalled["tracking"]["detector_ms"]=10001.;stalled["tracking"]["pose_ms"]=60000.;
    Check(decode(stalled),"completed model stall is diagnostic, not invalid geometry");++checks;
    for(const char* field:{"sequence","capture_ns","image_sha256"}){
        auto bad=r;bad["persons"][0]["source"][field]=nullptr;Check(!decode(bad),"forged detection provenance");++checks;
    }
    for(const char* field:{"quality","track_id","velocity","pose_source","class_id"}){
        auto bad=r;bad["persons"][0][field]=true;Check(!decode(bad),"malformed track field");++checks;
    }
    auto bad=r;bad["persons"].push_back(p);Check(!decode(bad),"duplicate track id");++checks;
    bad=r;bad["persons"][0]["velocity"]={99.,0.};Check(!decode(bad),"unbounded prediction");++checks;
    bad=r;bad["persons"][0]["source"]["capture_ns"]=f.key.captureNs-1;Check(!decode(bad),"unsubmitted source timestamp");++checks;
    bad=r;bad["persons"][0]["source"]["extra"]=1;Check(!decode(bad),"unknown source field");++checks;
    bad=r;bad["tracking"]["tracking_ms"]=-1;Check(!decode(bad),"negative timing");++checks;
    CpuFrame old=f;old.key.sequence=1;old.key.captureNs=1;history.Add(old,hash);
    bad=r;bad["persons"][0]["source"]={{"sequence",1},{"capture_ns",1},{"image_sha256",hash}};
    Check(!decode(bad),"old observation cannot renew itself");++checks;
    history.Add(f,hash);f.key.epoch.scene++;r["epoch"]=EpochWire(f.key.epoch);
    Check(!decode(r),"prior epoch history cannot authorize current track");++checks;
    std::cout<<"PASS "<<checks<<" motion provenance checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

#include <menu/FgRouteCheckpoint.h>
#include <array>
#include <cassert>
#include <cstdio>
int main(){
 using Values=std::array<std::optional<int>,3>;Neurotic::FgRouteCheckpoint<Values> checkpoint;
 Values config{std::nullopt,2,99};checkpoint.Observe(config,0,false);assert(!checkpoint.Dirty(config));
 config[0]=3;assert(checkpoint.Dirty(config));checkpoint.Observe(config,1,false);
 assert(checkpoint.Dirty(config)&&!checkpoint.Saved()[0]&&checkpoint.Saved()[2]==99);
 config=checkpoint.Saved();assert(!checkpoint.Dirty(config));
 config[0]=3;checkpoint.Observe(config,2,true);assert(!checkpoint.Dirty(config));
 config[1]=4;assert(checkpoint.Dirty(config));config=checkpoint.Saved();assert(config[1]==2);
 std::puts("PASS FG route pending/cancel, failed save, successful/global save checkpoint, unset and unknown preferences");
}

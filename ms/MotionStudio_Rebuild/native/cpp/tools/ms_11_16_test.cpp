#include "../include/ms_project.hpp"
#include "../include/ms_cache_proxy.hpp"
#include "../include/ms_autosave.hpp"
#include "../include/ms_effect_presets.hpp"
#include "../include/ms_performance.hpp"
#include "../include/ms_audio.hpp"
#include <iostream>
#include <filesystem>
#include <cmath>
using namespace ms;
namespace fs=std::filesystem;
#define CHECK(x,m) do{if(!(x)){std::cerr<<"FAIL: "<<m<<"\n";return 1;}else std::cout<<"ok: "<<m<<"\n";}while(0)
int main(){
  const fs::path root="/tmp/ms_11_16"; fs::remove_all(root); fs::create_directories(root);
  ProjectSession p; CHECK(p.new_project("Test",640,360,30).ok(),"project create");
  auto tr=p.add_track("Extra",(int)TrackType::Video); ClipInfo c; c.name="C"; c.start=2;c.duration=3;c.gain=.7f;c.pan=.2f; auto clip=p.add_clip(tr,c); CHECK(clip!=0,"clip add");
  auto json=p.to_json(); ProjectSession q; CHECK(q.load_from_json(json).ok(),"project load"); // A project now starts with one picture track, so the saved document has
  // that lane plus the one added here: both must survive the round trip.
  CHECK(q.tracks().size()>=2,"tracks preserved"); CHECK(q.find_track(tr)!=nullptr && q.find_track(tr)->clips.size()==1,"clip track preserved");
  CHECK(q.move_clip(tr,clip,4),"edit"); CHECK(q.undo(),"undo"); CHECK(q.find_clip(tr,clip)->start==2,"undo state"); CHECK(q.redo(),"redo"); CHECK(q.find_clip(tr,clip)->start==4,"redo state");
  const auto save=(root/"project.msproj").string(); CHECK(q.save_to_file(save).ok(),"atomic save"); CHECK(fs::exists(save),"save exists"); ProjectSession r; CHECK(r.load_from_file(save).ok(),"file load"); CHECK(r.find_clip(tr,clip)!=nullptr,"file clip preserved");
  AutosaveController a(&r); a.set_path((root/"autosave.msproj").string()); a.set_interval(1); r.mark_dirty(); CHECK(a.force().ok(),"autosave"); CHECK(fs::exists(root/"autosave.msproj"),"autosave exists");
  ImageBuffer img(8,8); img.clear(1,0,0,1); DiskFrameCache dc((root/"frames").string()); CHECK(dc.put("frame:1",img).ok(),"disk cache put"); ImageBuffer out; CHECK(dc.get("frame:1",out)&&out.width()==8,"disk cache get"); CHECK(dc.bytes()>0,"disk cache bytes");
  ProxyManager pm((root/"proxy").string()); CHECK(!pm.has_proxy("/no/such/media.mp4"),"proxy miss");
  auto lib=EffectPresetLibrary::builtins(); CHECK(lib.find("soft_glow")!=nullptr,"preset library"); CHECK(lib.search("glow").size()==1,"preset search"); CHECK(lib.save((root/"presets.json").string()),"preset save");
  PerformanceMonitor perf; perf.set_backend("mobile-gpu"); perf.begin_frame(); perf.end_frame(true); CHECK(perf.stats().rendered==1 && perf.stats().cache_hits==1,"perf monitor"); RenderBudget budget(30); CHECK(budget.preview_scale(50)<=67,"adaptive preview scale");
  AudioMixer mix; auto at=mix.add_track("Audio"); AudioClip ac; ac.duration=1; mix.add_clip(at,ac); AudioBuffer buf; CHECK(mix.mix(0,0.1,buf).ok()&&buf.samples.size()>0,"audio mixer");
  std::cout<<"11-16 PASSED\n"; return 0;
}

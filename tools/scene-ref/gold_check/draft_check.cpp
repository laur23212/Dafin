// Stand-alone check of the draft1x effects (SceneDraftEffects.h) with FreeType + HarfBuzz text:
//   ./build.sh draft_check && ./draft_check <scene.json> <frame>… → out_<frame>.rgba (straight alpha)
#include "ft_text.h"
#include "engine/render2d/scene/SceneDraftEffects.h"
using namespace drift::scene;
int main(int argc,char**argv){ FT_Init_FreeType(&lib);
  QFile fi(argv[1]); fi.open(QIODevice::ReadOnly); QJsonObject sc=QJsonDocument::fromJson(fi.readAll()).object();
  FtText txt; QJsonObject fonts=sc["fonts"].toObject(); for(auto k:fonts.keys()) txt.paths[k]=findFont(fonts[k].toObject()["file"].toString(),fonts[k].toObject()["sha256"].toString());
  QHash<QString,QString> defs; for(auto p:sc["params"].toArray()) defs[p.toObject()["id"].toString()]=p.toObject()["default"].toString();
  auto fmt=[&](QString s){ for(auto k:defs.keys()) s.replace("{"+k+"}",defs[k]); return s; };
  QJsonObject a=sc["actors"].toArray()[0].toObject(); QJsonObject e=a["effect"].toObject();
  draft::DraftActor da; da.kind=e["type"].toString(); da.font=a["font"].toString(); da.text=fmt(a["text"].toString()); if(e.contains("secondary")) da.secondary=fmt(e["secondary"].toString());
  auto fx=draft::makeDraftEffect(txt,da); if(!fx){fprintf(stderr,"no effect\n");return 1;}
  for(int i=2;i<argc;i++){ int fr=atoi(argv[i]); draft::Rgba f(draft::FW,draft::FH); fx->draw(f,fx->timeFor(fr/25.0)); char p[64]; snprintf(p,64,"out_%04d.rgba",fr); std::ofstream o(p,std::ios::binary); o.write((char*)f.p.data(),f.p.size()); }
}

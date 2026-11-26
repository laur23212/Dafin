#include "ft_text.h"
int main(int argc,char**argv){ FT_Init_FreeType(&lib);
  QFile fi(argv[1]); fi.open(QIODevice::ReadOnly); QJsonObject sc=QJsonDocument::fromJson(fi.readAll()).object();
  FtText txt; QJsonObject fonts=sc["fonts"].toObject(); for(auto k:fonts.keys()){ txt.paths[k]=findFont(fonts[k].toObject()["file"].toString(),fonts[k].toObject()["sha256"].toString()); }
  QHash<QString,QString> defs; for(auto p:sc["params"].toArray()) defs[p.toObject()["id"].toString()]=p.toObject()["default"].toString();
  auto fmt=[&](QString s){ for(auto k:defs.keys()) s.replace("{"+k+"}",defs[k]); return s; };
  QJsonObject a=sc["actors"].toArray()[0].toObject(); GoldActor ga; ga.font=a["font"].toString(); ga.size=a["size"].toInt(); ga.text=fmt(a["text"].toString());
  for(int i=0;i<3;i++) ga.color[i]=a["color"].toArray()[i].toDouble(); QJsonObject e=a["effect"].toObject(); if(e.contains("to")) e["to"]=fmt(e["to"].toString()); ga.effect=e;
  auto fx=makeGoldEffect(txt,ga); if(!fx){fprintf(stderr,"no effect\n");return 1;}
  for(int i=2;i<argc;i++){ int fr=atoi(argv[i]); Frame f; fx->draw(f,fr/25.0); char p[64]; snprintf(p,64,"out_%04d.rgb",fr); std::ofstream o(p,std::ios::binary); o.write((char*)f.p.data(),f.p.size()); }
}

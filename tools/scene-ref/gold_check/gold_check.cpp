#include "engine/render2d/scene/SceneGoldEffects.h"
#include <QFile>
#include <QJsonDocument>
#include <QHash>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cstdio>
extern "C" {
typedef struct hb_font_t hb_font_t; typedef struct hb_buffer_t hb_buffer_t;
struct hb_glyph_info_t { uint32_t codepoint, mask, cluster, v1, v2; };
struct hb_glyph_position_t { int32_t x_advance, y_advance, x_offset, y_offset; uint32_t v; };
hb_font_t *hb_ft_font_create_referenced(FT_Face); void hb_ft_font_set_load_flags(hb_font_t *, int); void hb_ft_font_changed(hb_font_t *);
hb_buffer_t *hb_buffer_create(); void hb_buffer_add_utf8(hb_buffer_t *, const char *, int, unsigned, int);
void hb_buffer_guess_segment_properties(hb_buffer_t *); void hb_shape(hb_font_t *, hb_buffer_t *, const void *, unsigned);
hb_glyph_info_t *hb_buffer_get_glyph_infos(hb_buffer_t *, unsigned *); hb_glyph_position_t *hb_buffer_get_glyph_positions(hb_buffer_t *, unsigned *);
void hb_buffer_destroy(hb_buffer_t *); void hb_font_destroy(hb_font_t *);
}
#include <fstream>
using namespace drift::scene::gold;
static FT_Library lib;
struct FtText : GoldText {
  QHash<QString,QString> paths; mutable QHash<QString,FT_Face> faces; mutable QHash<QString,hb_font_t*> hbs;
  FT_Face face(const QString&k,int px) const { QString key=k+QString::number(px); auto it=faces.find(key); if(it!=faces.end())return *it;
    FT_Face f; if(FT_New_Face(lib,paths.value(k).toUtf8().constData(),0,&f)){fprintf(stderr,"no font %s\n",qPrintable(k));exit(2);} FT_Set_Pixel_Sizes(f,0,px);
    faces.insert(key,f); hb_font_t*h=hb_ft_font_create_referenced(f); hb_ft_font_set_load_flags(h,FT_LOAD_DEFAULT|FT_LOAD_NO_HINTING); hbs.insert(key,h); return f; }
  struct G{unsigned gid; double x,y;};
  std::vector<G> shape(const QString&k,int px,const QString&t,double*adv) const { face(k,px); hb_font_t*h=hbs.value(k+QString::number(px)); hb_buffer_t*b=hb_buffer_create(); QByteArray u=t.toUtf8();
    hb_buffer_add_utf8(b,u.constData(),u.size(),0,u.size()); hb_buffer_guess_segment_properties(b); hb_shape(h,b,nullptr,0); unsigned n; auto*inf=hb_buffer_get_glyph_infos(b,&n); auto*pos=hb_buffer_get_glyph_positions(b,&n);
    std::vector<G> out; double pen=0; for(unsigned i=0;i<n;i++){ out.push_back({inf[i].codepoint,(pen+pos[i].x_offset)/64.0,-pos[i].y_offset/64.0}); pen+=pos[i].x_advance; } if(adv)*adv=pen/64.0; hb_buffer_destroy(b); return out; }
  bool has(const QString&k) const override {return paths.contains(k);}
  TextBox bbox(const QString&k,int px,const QString&t) const override { FT_Face f=face(k,px); double adv; auto gs=shape(k,px,t,&adv); TextBox b; bool any=false; int l=0,tp=0,r=0,bt=0;
    for(auto&g:gs){ FT_Load_Glyph(f,g.gid,FT_LOAD_DEFAULT); FT_Glyph_Metrics&m=f->glyph->metrics; if(!m.width) continue; int x0=int(std::floor(g.x+0.5))+int(std::floor(m.horiBearingX/64.0)), x1=int(std::floor(g.x+0.5))+int(std::ceil((m.horiBearingX+m.width)/64.0));
      int y0=-int(std::ceil(m.horiBearingY/64.0)), y1=-int(std::floor((m.horiBearingY-m.height)/64.0)); if(!any){l=x0;tp=y0;r=x1;bt=y1;any=true;} else {l=std::min(l,x0);tp=std::min(tp,y0);r=std::max(r,x1);bt=std::max(bt,y1);} }
    b.left=std::min(0,l); b.right=std::max(int(std::floor(adv+0.5)),r); b.top=any?std::min(0,tp):0; b.bottom=any?std::max(0,bt):0; return b; }
  int ascender(const QString&k,int px) const override { FT_Face f=face(k,px); return int(std::ceil(f->size->metrics.ascender/64.0)); }
  double length(const QString&k,int px,const QString&t) const override { double adv; shape(k,px,t,&adv); return adv; }
  void draw(M8&m,const QString&k,int px,const QString&t,double x,double y) const override { FT_Face f=face(k,px); auto gs=shape(k,px,t,nullptr);
    for(auto&g:gs){ FT_Load_Glyph(f,g.gid,FT_LOAD_RENDER); auto*gl=f->glyph; int ox=int(std::floor(x+g.x+0.5))+gl->bitmap_left, oy=int(std::ceil(y-0.5))-gl->bitmap_top;
      for(unsigned j=0;j<gl->bitmap.rows;++j)for(unsigned i=0;i<gl->bitmap.width;++i){int X=ox+i,Y=oy+j; if(X<0||Y<0||X>=m.w||Y>=m.h)continue; int v=gl->bitmap.buffer[j*gl->bitmap.pitch+i]; int o=m.at(X,Y); m.at(X,Y)=std::uint8_t(o+(255-o)*v/255);} } }
};
int main(int argc,char**argv){ FT_Init_FreeType(&lib);
  QFile fi(argv[1]); fi.open(QIODevice::ReadOnly); QJsonObject sc=QJsonDocument::fromJson(fi.readAll()).object();
  FtText txt; QJsonObject fonts=sc["fonts"].toObject(); for(auto k:fonts.keys()){ QString file=fonts[k].toObject()["file"].toString(); QStringList dirs{"/usr/share/fonts/opentype/inter/","/usr/share/fonts/truetype/noto/"}; for(auto d:dirs) if(QFile::exists(d+file)) txt.paths[k]=d+file; }
  QHash<QString,QString> defs; for(auto p:sc["params"].toArray()) defs[p.toObject()["id"].toString()]=p.toObject()["default"].toString();
  auto fmt=[&](QString s){ for(auto k:defs.keys()) s.replace("{"+k+"}",defs[k]); return s; };
  QJsonObject a=sc["actors"].toArray()[0].toObject(); GoldActor ga; ga.font=a["font"].toString(); ga.size=a["size"].toInt(); ga.text=fmt(a["text"].toString());
  for(int i=0;i<3;i++) ga.color[i]=a["color"].toArray()[i].toDouble(); QJsonObject e=a["effect"].toObject(); if(e.contains("to")) e["to"]=fmt(e["to"].toString()); ga.effect=e;
  auto fx=makeGoldEffect(txt,ga); if(!fx){fprintf(stderr,"no effect\n");return 1;}
  for(int i=2;i<argc;i++){ int fr=atoi(argv[i]); Frame f; fx->draw(f,fr/25.0); char p[64]; snprintf(p,64,"out_%04d.rgb",fr); std::ofstream o(p,std::ios::binary); o.write((char*)f.p.data(),f.p.size()); }
}

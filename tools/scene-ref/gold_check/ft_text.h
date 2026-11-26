// FreeType + HarfBuzz text for the stand-alone gold/draft checks (PIL basic-layout rules).
#pragma once
#include "engine/render2d/scene/SceneGoldEffects.h"
#include <QFile>
#include <QJsonDocument>
#include <QHash>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H
#include FT_GLYPH_H
#include <cstdio>
extern "C" {
struct hb_feature_t { uint32_t tag, value; unsigned start, end; };
typedef struct hb_font_t hb_font_t; typedef struct hb_buffer_t hb_buffer_t;
struct hb_glyph_info_t { uint32_t codepoint, mask, cluster, v1, v2; };
struct hb_glyph_position_t { int32_t x_advance, y_advance, x_offset, y_offset; uint32_t v; };
hb_font_t *hb_ft_font_create_referenced(FT_Face); void hb_ft_font_set_load_flags(hb_font_t *, int); void hb_ft_font_changed(hb_font_t *);
hb_buffer_t *hb_buffer_create(); void hb_buffer_add_utf8(hb_buffer_t *, const char *, int, unsigned, int);
void hb_buffer_guess_segment_properties(hb_buffer_t *); void hb_shape(hb_font_t *, hb_buffer_t *, const hb_feature_t *, unsigned);
hb_glyph_info_t *hb_buffer_get_glyph_infos(hb_buffer_t *, unsigned *); hb_glyph_position_t *hb_buffer_get_glyph_positions(hb_buffer_t *, unsigned *);
void hb_buffer_destroy(hb_buffer_t *); void hb_font_destroy(hb_font_t *);
}
#include <fstream>
using namespace drift::scene::gold;
static FT_Library lib;
struct FtText : GoldText {
  QHash<QString,QString> paths; mutable QHash<QString,FT_Face> faces; mutable QHash<QString,hb_font_t*> hbs;
  FT_Face face(const QString&k0,int px) const { const QString k=k0.section('+',0,0); QString key=k+QString::number(px); auto it=faces.find(key); if(it!=faces.end())return *it;
    FT_Face f; if(FT_New_Face(lib,paths.value(k).toUtf8().constData(),0,&f)){fprintf(stderr,"no font %s\n",qPrintable(k));exit(2);} FT_Set_Pixel_Sizes(f,0,px);
    faces.insert(key,f); hb_font_t*h=hb_ft_font_create_referenced(f); hb_ft_font_set_load_flags(h,FT_LOAD_DEFAULT|FT_LOAD_NO_HINTING); hbs.insert(key,h); return f; }
  struct G{unsigned gid; double x,y;};
  std::vector<G> shape(const QString&k0,int px,const QString&t,double*adv) const { face(k0,px); const QString k=k0.section('+',0,0); hb_font_t*h=hbs.value(k+QString::number(px));
    std::vector<hb_feature_t> fs; for(const QString&f:k0.split('+').mid(1)) if(f.size()==4) fs.push_back({uint32_t(f[0].unicode())<<24|uint32_t(f[1].unicode())<<16|uint32_t(f[2].unicode())<<8|uint32_t(f[3].unicode()),1,0,~0u}); hb_buffer_t*b=hb_buffer_create(); QByteArray u=t.toUtf8();
    hb_buffer_add_utf8(b,u.constData(),u.size(),0,u.size()); hb_buffer_guess_segment_properties(b); hb_shape(h,b,fs.data(),unsigned(fs.size())); unsigned n; auto*inf=hb_buffer_get_glyph_infos(b,&n); auto*pos=hb_buffer_get_glyph_positions(b,&n);
    std::vector<G> out; double pen=0; for(unsigned i=0;i<n;i++){ out.push_back({inf[i].codepoint,(pen+pos[i].x_offset)/64.0,-pos[i].y_offset/64.0}); pen+=pos[i].x_advance; } if(adv)*adv=pen/64.0; hb_buffer_destroy(b); return out; }
  bool has(const QString&k) const override {return paths.contains(k.section('+',0,0));}
  TextBox bbox(const QString&k,int px,const QString&t) const override { FT_Face f=face(k,px); double adv; auto gs=shape(k,px,t,&adv); TextBox b; bool any=false; int l=0,tp=0,r=0,bt=0;
    for(auto&g:gs){ FT_Load_Glyph(f,g.gid,FT_LOAD_DEFAULT); FT_Glyph_Metrics&m=f->glyph->metrics; if(!m.width) continue; int x0=int(std::floor(g.x+0.5))+int(std::floor(m.horiBearingX/64.0)), x1=int(std::floor(g.x+0.5))+int(std::ceil((m.horiBearingX+m.width)/64.0));
      int y0=-int(std::ceil(m.horiBearingY/64.0)), y1=-int(std::floor((m.horiBearingY-m.height)/64.0)); if(!any){l=x0;tp=y0;r=x1;bt=y1;any=true;} else {l=std::min(l,x0);tp=std::min(tp,y0);r=std::max(r,x1);bt=std::max(bt,y1);} }
    b.left=std::min(0,l); b.right=std::max(int(std::floor(adv+0.5)),r); b.top=any?std::min(0,tp):0; b.bottom=any?std::max(0,bt):0; return b; }
  int ascender(const QString&k,int px) const override { FT_Face f=face(k,px); return int(std::ceil(f->size->metrics.ascender/64.0)); }
  double length(const QString&k,int px,const QString&t) const override { double adv; shape(k,px,t,&adv); return adv; }
  void draw(M8&m,const QString&k,int px,const QString&t,double x,double y,int stroke) const override { FT_Face f=face(k,px); auto gs=shape(k,px,t,nullptr);
    FT_Stroker st=nullptr; if(stroke>0){ FT_Stroker_New(lib,&st); FT_Stroker_Set(st,stroke*64,FT_STROKER_LINECAP_ROUND,FT_STROKER_LINEJOIN_ROUND,0); }
    for(auto&g:gs){ FT_Load_Glyph(f,g.gid,stroke>0?FT_LOAD_DEFAULT|FT_LOAD_NO_BITMAP:FT_LOAD_RENDER); FT_Bitmap*bmp; int bl,bt; FT_Glyph gg=nullptr;
      if(stroke>0){ FT_Get_Glyph(f->glyph,&gg); FT_Glyph_Stroke(&gg,st,1); FT_Glyph_To_Bitmap(&gg,FT_RENDER_MODE_NORMAL,nullptr,1); auto*b=(FT_BitmapGlyph)gg; bmp=&b->bitmap; bl=b->left; bt=b->top; }
      else { bmp=&f->glyph->bitmap; bl=f->glyph->bitmap_left; bt=f->glyph->bitmap_top; }
      int ox=int(std::floor(x+g.x+0.5))+bl, oy=int(std::ceil(y-0.5))-bt;
      for(unsigned j=0;j<bmp->rows;++j)for(unsigned i=0;i<bmp->width;++i){int X=ox+i,Y=oy+j; if(X<0||Y<0||X>=m.w||Y>=m.h)continue; int v=bmp->buffer[j*bmp->pitch+i]; int o=m.at(X,Y); m.at(X,Y)=std::uint8_t(o+(255-o)*v/255);}
      if(gg) FT_Done_Glyph(gg); }
    if(st){ FT_Stroker_Done(st); draw(m,k,px,t,x,y,0); } } // PIL draws the stroke, then the text over it
};
#include <QCryptographicHash>
#include <QDirIterator>
// The font file of that name and sha256 under the repo's scene-fonts/ or the system font folders.
static QString findFont(const QString &file, const QString &sha) {
  for (const QString &d : {QStringLiteral(SCENE_FONTS), QStringLiteral("/usr/share/fonts")}) {
    QDirIterator it(d, {file}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { const QString p = it.next(); QFile f(p); if (!f.open(QIODevice::ReadOnly)) continue;
      if (sha.isEmpty() || QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex() == sha.toLatin1()) return p; }
  }
  return {};
}

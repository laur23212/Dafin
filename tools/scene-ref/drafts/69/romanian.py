#!/usr/bin/env python3
"""Independent Romanian accent construction. Fonts read from user's archives; temporary proof font exists only in memory, never exported."""
import argparse,io,json,hashlib,pathlib,zipfile,unicodedata
from fontTools.ttLib import TTFont
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.cu2quPen import Cu2QuPen
from PIL import Image,ImageDraw,ImageFont
ROOT=pathlib.Path(__file__).resolve().parents[1]
TARGET='ĂÂÎȘȚăâîșț'
ARCHIVES={'Art30':'1_Art_Title_30.zip','Art27':'1_Art_Title_27.zip','Art33':'1_Art_Title_33.zip','FilmFrame03':'1_1_Film_Frame_Title_03.zip','Clean14':'1_Clean_Title_Design_Title_14.zip','Basic24':'1_Basic_Font_Title_24.zip','OldTexture':'1_Old_Texture_Text.zip'}
COMMA={'FilmFrame03':(158,220,58),'Clean14':(142,200,58),'OldTexture':(188,310,90)}
K_TOP={'breve':(560,265,95),'circumflex':(590,320,85),'circumflex_I':(410,285,85)}
def sha(b):return hashlib.sha256(b).hexdigest()
def source(folder,key):
    with zipfile.ZipFile(folder/ARCHIVES[key]) as z:
        member=next(n for n in z.namelist() if '/All/' not in n and n.lower().endswith(('.ttf','.otf')));return z.read(member),member

def bounds(gs,n):
    p=BoundsPen(gs);gs[n].draw(p);return p.bounds

def record(gs,n):
    p=DecomposingRecordingPen(gs);gs[n].draw(p);return p

def kmark(p,kind,x,y,w,h):
    def P(a,b):return (x+a*w,y+b*h)
    if kind=='breve':
        p.moveTo(P(0,1));p.lineTo(P(.26,1));p.curveTo(P(.28,.62),P(.34,.49),P(.5,.49));p.curveTo(P(.66,.49),P(.72,.62),P(.74,1));p.lineTo(P(1,1));p.curveTo(P(.96,.35),P(.80,0),P(.5,0));p.curveTo(P(.20,0),P(.04,.35),P(0,1));p.closePath()
    else:
        p.moveTo(P(0,0));p.lineTo(P(.34,1));p.lineTo(P(.66,1));p.lineTo(P(1,0));p.lineTo(P(.70,0));p.lineTo(P(.5,.60));p.lineTo(P(.30,0));p.closePath()

def plan(f,key,c):
    cm=f.getBestCmap();gs=f.getGlyphSet(); em=f['head'].unitsPerEm
    if ord(c) in cm:return cm[ord(c)],None,None
    if key=='Art33':
        alias='Ţ' if c=='Ț' else 'ţ';n=cm[ord(alias)];return n,None,dict(character=c,mechanism='map existing T comma accent glyph to correct Unicode',source_glyph=n,legacy=alias)
    base,typ=unicodedata.normalize('NFD',c);bn=cm[ord(base)];bb=bounds(gs,bn);width=f['hmtx'][bn][0];center=(bb[0]+bb[2])/2
    if key=='FilmFrame03' and base=='t':center=148
    if key=='Clean14' and base in 'TtSs':center=width*.36
    if typ=='\u0326':
        aw,ah,gap=COMMA[key];src=cm[ord(',')];sb=bounds(gs,src);sx=aw/(sb[2]-sb[0]);sy=ah/(sb[3]-sb[1]);left=center-aw/2;bottom=bb[1]-gap-ah;trans=(sx,0,0,sy,left-sb[0]*sx,bottom-sb[1]*sy);mark=record(gs,src)
        def accent(p):mark.replay(TransformPen(p,trans))
        desc=dict(mechanism='scaled native comma, detached below base',mark_source=src,transform=trans,mark_bbox=[left,bottom,left+aw,bottom+ah],gap=gap)
    elif key=='Clean14' and typ=='\u0306':
        src=cm[ord('˘')];sb=bounds(gs,src);gap=75 if base=='a' else 86;center+=55;dx=center-(sb[0]+sb[2])/2;dy=bb[3]+gap-sb[1];mark=record(gs,src);trans=(1,0,0,1,dx,dy)
        def accent(p):mark.replay(TransformPen(p,trans))
        desc=dict(mechanism='native breve repositioned above base',mark_source=src,transform=trans,mark_bbox=[sb[0]+dx,sb[1]+dy,sb[2]+dx,sb[3]+dy],gap=gap)
    elif key=='OldTexture':
        kind='breve' if typ=='\u0306' else ('circumflex_I' if base in 'Ii' else 'circumflex');aw,ah,gap=K_TOP[kind];left=center-aw/2;bottom=bb[3]+gap
        def accent(p):kmark(p,kind,left,bottom,aw,ah)
        desc=dict(mechanism='custom condensed accent outline',mark_kind=kind,mark_bbox=[left,bottom,left+aw,bottom+ah],gap=gap)
    else:raise ValueError((key,c))
    return bn,accent,dict(character=c,base=base,base_glyph=bn,advance=width,units_per_em=em,**desc)

def proof_font(b,key,expected,text):
    if sha(b)!=expected:raise ValueError('exact original font SHA mismatch')
    f=TTFont(io.BytesIO(b),recalcTimestamp=False);cm=f.getBestCmap();gs=f.getGlyphSet();rows=[];plans={c:plan(f,key,c) for c in sorted(set(text)) if c in TARGET or ord(c) in cm}
    for c in TARGET:
        _,_,r=plan(f,key,c)
        if r:rows.append(r)
    if not rows:return b,rows
    if 'glyf' in f:
        order=list(f.getGlyphOrder());pending=[]
        for c,(bn,accent,r) in plans.items():
            if not r:continue
            if accent:
                p=TTGlyphPen(None);q=Cu2QuPen(p,max_err=.7,reverse_direction=False);record(gs,bn).replay(q);accent(q);name=f'dafin.uni{ord(c):04X}';pending.append((name,p.glyph(),f['hmtx'][bn]));order.append(name)
            else:name=bn
            for tab in f['cmap'].tables:
                if tab.isUnicode() and tab.format in (4,12):tab.cmap[ord(c)]=name
        for n,g,m in pending:f['glyf'][n]=g;f['hmtx'][n]=m
        f.setGlyphOrder(order);f['maxp'].numGlyphs=len(order)
        if 'DSIG' in f:del f['DSIG']
        out=io.BytesIO();f.save(out);data=out.getvalue()
    else:
        # Source Han is CID-keyed CFF. Build a RAM-only outline specimen adapter.
        # The native implementation should compose marks on original CFF glyphs, not replace the font.
        cmap={};glyphs={};metrics={};order=['.notdef'];p=TTGlyphPen(None);glyphs['.notdef']=p.glyph();metrics['.notdef']=(f['head'].unitsPerEm,0)
        for c,(bn,accent,r) in sorted(plans.items()):
            n=f'u{ord(c):04X}';p=TTGlyphPen(None);q=Cu2QuPen(p,max_err=.7,reverse_direction=False);record(gs,bn).replay(q)
            if accent:accent(q)
            glyphs[n]=p.glyph();metrics[n]=f['hmtx'][bn];order.append(n);cmap[ord(c)]=n
        fb=FontBuilder(f['head'].unitsPerEm,isTTF=True);fb.setupGlyphOrder(order);fb.setupCharacterMap(cmap);fb.setupGlyf(glyphs);fb.setupHorizontalMetrics(metrics);fb.setupHorizontalHeader(ascent=f['hhea'].ascent,descent=f['hhea'].descent);fb.setupNameTable({'familyName':'Dafin RAM-only specimen','styleName':'Regular'});fb.setupOS2(sTypoAscender=f['OS/2'].sTypoAscender,sTypoDescender=f['OS/2'].sTypoDescender,usWinAscent=f['OS/2'].usWinAscent,usWinDescent=f['OS/2'].usWinDescent);fb.setupPost();out=io.BytesIO();fb.save(out);data=out.getvalue()
    check=TTFont(io.BytesIO(data));assert all(ord(c) in check.getBestCmap() for c in TARGET);return data,rows

def specimen(b,key):
    im=Image.new('RGBA',(1920,1080));d=ImageDraw.Draw(im)
    # All actual glyphs shown, with original title palette for priority art styles.
    for y,txt in [(335,'Ă Â Î Ș Ț'),(735,'ă â î ș ț')]:
        size=220;font=ImageFont.truetype(io.BytesIO(b),size);bb=font.getbbox(txt)
        while bb[2]-bb[0]>1600:
            size=int(size*1600/(bb[2]-bb[0]));font=ImageFont.truetype(io.BytesIO(b),size);bb=font.getbbox(txt)
        x=round(960-(bb[0]+bb[2])/2); yy=round(y-(bb[1]+bb[3])/2)
        if key=='Art30':d.text((x+6,yy+9),txt,font=font,fill=(70,157,243,255));d.text((x,yy),txt,font=font,fill='white')
        elif key=='Art27':d.text((x,yy),txt,font=font,fill=(170,74,55,255),stroke_width=11,stroke_fill='white')
        elif key=='Art33':d.text((x+5,yy+5),txt,font=font,fill=(183,28,28,255));d.text((x,yy),txt,font=font,fill=(255,191,23,255))
        else:d.text((x,yy),txt,font=font,fill=(246,244,236,255))
    return im

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--archive-dir',type=pathlib.Path,required=True);ap.add_argument('--out',type=pathlib.Path,default=ROOT/'output');a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True);lock=json.loads((ROOT/'SOURCE_FONTS.json').read_text());allrows={}
    for key in ARCHIVES:
        b,_=source(a.archive_dir,key);data,r=proof_font(b,key,lock[key]['sha256'],TARGET+' ');allrows[key]=r;im=specimen(data,key);im.save(a.out/f'{key}_alpha.png');Image.alpha_composite(Image.new('RGBA',im.size,(39,49,58,255)),im).convert('RGB').save(a.out/f'{key}_proof.png');print(key,len(r),flush=True)
    (a.out/'GLYPH_RECIPES.json').write_text(json.dumps(allrows,indent=2,ensure_ascii=False))
if __name__=='__main__':main()

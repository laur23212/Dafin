#!/usr/bin/env python3
import argparse, hashlib, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter
ROOT=Path(__file__).resolve().parents[1]
W,H,FPS,N=1920,1080,25,150
SEED=0 # no random operations
FONT_FILES={'bold': ('InterDisplay-Bold.otf', 'd7a7394984bbb3bc3c3e507a6618d34e006c94f27c7105d0ef4376667aeaf4ee'), 'medium': ('InterDisplay-Medium.otf', '9a17712bbd6da13b7ce9beb056d19fe15648799a11d5c35f8086a1270a0d13b2'), 'regular': ('InterDisplay-Regular.otf', '35c1f7437dd6d63f4e60a49144df4b4e4edc190e13ffc1e54b86083747fe3acf'), 'serif': ('NotoSerif-Regular.ttf', '9d7583b7dc9e812afd32a14280c5cac3160012efe50c8d08938f4fea266ff67f')}
TEST_TEXT="Ă Â Î Ș Ț"











DEFAULT_TEXT='Distance travelled'; SECONDARY='120'; INK=(245,242,233); ACC=(151,220,204); DIM=(100,121,131); ENTRY=(.12,.78); REVEAL=(.65,1.55); EXIT=(4.92,5.76); MILE_KM=1.609344

def convert(value):
 from decimal import Decimal, ROUND_HALF_UP
 try:q=Decimal(value)
 except Exception as e:raise ValueError('invalid distance') from e
 if not q.is_finite() or q<0 or q>999999:raise ValueError('distance must be 0..999999 km')
 def fmt(v):return f'{v.quantize(Decimal("0.1"),rounding=ROUND_HALF_UP):,}'.rstrip('0').rstrip('.') if v.quantize(Decimal('0.1'),rounding=ROUND_HALF_UP)%1 else f'{v.quantize(Decimal("1"),rounding=ROUND_HALF_UP):,}'
 return fmt(q),fmt(q/Decimal('1.609344'))
def prepare(fd,text,secondary):
 raw=SECONDARY if secondary is None else secondary;km,mi=convert(raw);cap,cm=texttile(fd,text or DEFAULT_TEXT,'medium',45,INK,1380);a,am=texttile(fd,km,'medium',138,INK,580);b,bm=texttile(fd,mi,'medium',138,ACC,580);u,um=texttile(fd,'km','regular',45,INK,150);w,wm=texttile(fd,'mi','regular',45,ACC,150);eq,em=texttile(fd,'≈','regular',75,DIM,180);x1=570;x2=1350;base=590
 def xy(tile,m,c):return (c-tile.width/2,base+m['ink_bbox'][1]-2)
 ax,ay=xy(a,am,x1);bx,by=xy(b,bm,x2)
 def render(t):
  im=Image.new('RGBA',(W,H));p=ease(t,*ENTRY);q=ease(t,*REVEAL);v=1-ease(t,*EXIT);paste(im,cap,(W-cap.width)/2,300,p*v);paste(im,a,ax,ay,p*v);paste(im,u,x1-u.width/2,628,p*v);paste(im,eq,(W-eq.width)/2,base-eq.height/2,p*v);paste(im,b,bx,by+22*(1-q),q*v);paste(im,w,x2-w.width/2,628,q*v);return im
 return render,dict(actors=[act('caption',cm,(W-cap.width)/2,300,INK,cue=ENTRY,from_values={'opacity':0},to_values={'opacity':1}),act('km_value',am,ax,ay,INK,cue=ENTRY,from_values={'opacity':0},to_values={'opacity':1}),act('km_unit',um,x1-u.width/2,628,INK,cue=ENTRY,from_values={'opacity':0},to_values={'opacity':1}),act('approx',em,(W-eq.width)/2,base-eq.height/2,DIM,cue=ENTRY,from_values={'opacity':0},to_values={'opacity':1}),act('mi_value',bm,bx,by,ACC,cue=REVEAL,from_values={'dy':22,'opacity':0},to_values={'dy':0,'opacity':1}),act('mi_unit',wm,x2-w.width/2,628,ACC,cue=REVEAL,from_values={'opacity':0},to_values={'opacity':1})],entry=ENTRY,exit=EXIT,hold_seconds=[REVEAL[1],EXIT[0]],mechanism='Exact decimal miles=kilometers/1.609344; displayed values round half-up to one decimal and omit trailing .0; approximate relation indicator. Values fit independently within their columns. Input bound 0..999999 km; native may expose additional unit pairs separately.',input_km=raw,display_km=km,display_mi=mi,conversion_factor='1.609344')

def rect(w,h,c,r=0):
    im=Image.new('RGBA',(w,h));ImageDraw.Draw(im).rounded_rectangle((0,0,w-1,h-1),radius=r,fill=(*c,255));return im

def clip(dst,src,x,y,box,a=1):
    x,y=round(x),round(y);l,t,r,b=box;l=max(0,round(l)-x);t=max(0,round(t)-y);r=min(src.width,round(r)-x);b=min(src.height,round(b)-y)
    if r>l and b>t:paste(dst,src.crop((l,t,r,b)),x+l,y+t,a)

def act(id,m,x,y,c,**kw):return dict(id=id,**m,x=x,y=y,color=c,**kw)
def clamp(x):return max(0.,min(1.,x))
def ease(t,a,b):
    x=clamp((t-a)/(b-a));return x*x*(3-2*x)
def mix(a,b,p):return a+(b-a)*p
def font(directory,role,size):
    name,h=FONT_FILES[role];p=directory/name
    if sha(p)!=h:raise ValueError(f"Incorrect font binary: {p}")
    return ImageFont.truetype(str(p),size,layout_engine=ImageFont.Layout.RAQM)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def texttile(directory,txt,role,size,color,max_width=1600):
    f=font(directory,role,size)
    while f.getlength(txt)>max_width and size>24:
        size-=1;f=font(directory,role,size)
    if f.getlength(txt)>max_width:raise ValueError('Text too wide')
    l,t,r,b=f.getbbox(txt,anchor='ls'); im=Image.new('RGBA',(r-l+4,b-t+4));ImageDraw.Draw(im).text((2-l,2-t),txt,anchor='ls',font=f,fill=(*color,255));return im,dict(text=txt,role=role,size=size,ink_bbox=[l,t,r,b],advance=f.getlength(txt),width=im.width,height=im.height)
def paste(dst,src,x,y,a=1):
    if a<=0:return
    if a<1:
        src=src.copy();src.putalpha(src.getchannel('A').point([round(i*a) for i in range(256)]))
    dst.alpha_composite(src,(round(x),round(y)))
def line_layer(points,width,color):
    # 3x supersampling for clean vector diagonals and endpoints
    k=3;im=Image.new('RGBA',(W*k,H*k));d=ImageDraw.Draw(im);p=[(round(x*k),round(y*k)) for x,y in points]
    if len(p)>1:d.line(p,fill=(*color,255),width=round(width*k),joint='curve')
    return im.resize((W,H),Image.Resampling.LANCZOS)
def encode_cmd(out):
    return ['ffmpeg','-hide_banner','-loglevel','error','-y','-filter_complex_threads','2','-f','rawvideo','-pixel_format','rgba','-video_size',f'{W}x{H}','-framerate',str(FPS),'-i','pipe:0','-filter_complex',f'[0:v]split=2[raw][ov];[raw]format=bgra[a];color=c=black:s={W}x{H}:r={FPS}[bg];[bg][ov]overlay=shortest=1:format=rgb:alpha=straight,format=yuv420p[v]','-map','[v]','-frames:v',str(N),'-an','-c:v','libx264','-preset','veryfast','-crf','16','-threads','2','-pix_fmt','yuv420p','-movflags','+faststart','-map_metadata','-1',str(out/'preview.mp4'),'-map','[a]','-frames:v',str(N),'-an','-c:v','ffv1','-level','3','-threads','2','-pix_fmt','bgra','-map_metadata','-1',str(out/'alpha.mkv')]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--font-dir',type=Path,default=ROOT/'fonts');ap.add_argument('--output',type=Path,default=ROOT/'output');ap.add_argument('--text',default=None);ap.add_argument('--secondary',default=None);ap.add_argument('--romanian',action='store_true');ap.add_argument('--preflight',action='store_true');args=ap.parse_args()
    out=args.output;out.mkdir(parents=True,exist_ok=True);render,g=prepare(args.font_dir,TEST_TEXT if args.romanian else args.text,args.secondary)
    (out/'geometry.json').write_text(json.dumps(g,ensure_ascii=False,indent=2)+'\n')
    if args.preflight:
        for n in (10,25,40,65,95,125,138):Image.alpha_composite(Image.new('RGBA',(W,H),(0,0,0,255)),render(n/FPS)).convert('RGB').resize((960,540),Image.Resampling.LANCZOS).save(out/f'{n:03}.jpg',quality=95)
        return
    cmd=encode_cmd(out);(out/'encode_command.json').write_text(json.dumps(cmd,indent=2)+'\n')
    with open(out/'ffmpeg.log','wb') as log:
        proc=subprocess.Popen(cmd,stdin=subprocess.PIPE,stderr=log)
        try:
            hold_a,hold_b=g["hold_seconds"]; frozen=None
            for n in range(N):
                if hold_a<=n/FPS<=hold_b:
                    if frozen is None:frozen=render(n/FPS).tobytes()
                    proc.stdin.write(frozen)
                else:proc.stdin.write(render(n/FPS).tobytes())
        finally:proc.stdin.close();rc=proc.wait()
        if rc:raise RuntimeError(f'ffmpeg failed {rc}')
    print(json.dumps(dict(preview_sha256=sha(out/'preview.mp4'),alpha_sha256=sha(out/'alpha.mkv')),ensure_ascii=False))
if __name__=='__main__':main()

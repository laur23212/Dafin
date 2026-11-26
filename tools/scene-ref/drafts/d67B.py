#!/usr/bin/env python3
import argparse, hashlib, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter
ROOT=Path(__file__).resolve().parents[1]
W,H,FPS,N=1920,1080,25,150
SEED=0 # no random operations
FONT_FILES={'bold': ('InterDisplay-Bold.otf', 'd7a7394984bbb3bc3c3e507a6618d34e006c94f27c7105d0ef4376667aeaf4ee'), 'medium': ('InterDisplay-Medium.otf', '9a17712bbd6da13b7ce9beb056d19fe15648799a11d5c35f8086a1270a0d13b2'), 'regular': ('InterDisplay-Regular.otf', '35c1f7437dd6d63f4e60a49144df4b4e4edc190e13ffc1e54b86083747fe3acf'), 'serif': ('NotoSerif-Regular.ttf', '9d7583b7dc9e812afd32a14280c5cac3160012efe50c8d08938f4fea266ff67f')}
TEST_TEXT="Ă Â Î Ș Ț"











DEFAULT_TEXT='perfection'; SECONDARY='progress'; PREFIX='Choose'; INK=(245,241,232); MUTED=(142,154,160); ACC=(252,177,126); ENTRY=(.10,.78); STRIKE=(1.55,2.05); REPLACE=(1.96,2.66); EXIT=(4.92,5.76)
def prepare(fd,text,secondary):
 a,am=texttile(fd,PREFIX,'regular',105,INK,500);b,bm=texttile(fd,text or DEFAULT_TEXT,'medium',105,INK,850);d,dm=texttile(fd,text or DEFAULT_TEXT,'medium',105,MUTED,850);c,cm=texttile(fd,secondary or SECONDARY,'serif',93,ACC,850);gap=28;xx=(W-a.width-gap-max(b.width,c.width))/2;bx=xx+a.width+gap;baseline=635;ay=baseline+am['ink_bbox'][1]-2;by=baseline+bm['ink_bbox'][1]-2;cy=by-c.height-28;line_y=round(by+b.height*.54);length=b.width+8
 def render(t):
  im=Image.new('RGBA',(W,H));p=ease(t,*ENTRY);q=ease(t,*STRIKE);r=ease(t,*REPLACE);v=1-ease(t,*EXIT);paste(im,a,xx,ay+15*(1-p),p*v);paste(im,b,bx,by+15*(1-p),p*v*(1-q));paste(im,d,bx,by,p*v*q);n=round(length*q)
  if n:paste(im,rect(n,4,ACC),bx-4,line_y,v)
  paste(im,c,bx,cy+20*(1-r),r*v);return im
 return render,dict(actors=[act('prefix',am,xx,ay,INK,cue=ENTRY,from_values={'dy':15,'opacity':0},to_values={'dy':0,'opacity':1}),act('original_word',bm,bx,by,INK,cue=STRIKE,from_values={'color':INK},to_values={'color':MUTED}),dict(id='strike',type='shape',x=bx-4,y=line_y,width=length,height=4,color=ACC,cue=STRIKE,from_values={'width':0},to_values={'width':length}),act('replacement',cm,bx,cy,ACC,cue=REPLACE,from_values={'dy':20,'opacity':0},to_values={'dy':0,'opacity':1})],entry=ENTRY,exit=EXIT,hold_seconds=[REPLACE[1],EXIT[0]],mechanism='Common baseline for prefix and original; stroke length derived from original ink width plus 8px. Original changes to subdued ink, replacement is a separate shaped actor aligned above it. No spelling/AI auto-correction; user supplies replacement.')

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

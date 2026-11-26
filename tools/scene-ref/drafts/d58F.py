#!/usr/bin/env python3
import argparse, hashlib, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter
ROOT=Path(__file__).resolve().parents[1]
W,H,FPS,N=1920,1080,25,150
SEED=0 # no random operations
FONT_FILES={'bold': ('InterDisplay-Bold.otf', 'd7a7394984bbb3bc3c3e507a6618d34e006c94f27c7105d0ef4376667aeaf4ee'), 'medium': ('InterDisplay-Medium.otf', '9a17712bbd6da13b7ce9beb056d19fe15648799a11d5c35f8086a1270a0d13b2'), 'regular': ('InterDisplay-Regular.otf', '35c1f7437dd6d63f4e60a49144df4b4e4edc190e13ffc1e54b86083747fe3acf'), 'serif': ('NotoSerif-Regular.ttf', '9d7583b7dc9e812afd32a14280c5cac3160012efe50c8d08938f4fea266ff67f')}
TEST_TEXT="Ă Â Î Ș Ț"


DEFAULT_TEXT='The next step'; SECONDARY=''; SIZE=126; PX=47; PY=34; ARM=38; STROKE=5; TRAVEL=66
INK=(246,244,237); ACCENT=(229,184,118); ENTRY=(.13,.87); TITLE_IN=(.47,1.23); EXIT=(4.84,5.76)
def prepare(fd,text,secondary):
    a,am=texttile(fd,text or DEFAULT_TEXT,'bold',SIZE,INK,1420);x=(W-a.width)//2;y=(H-a.height)//2;l=x-PX;r=x+a.width+PX;t=y-PY;b=y+a.height+PY
    def corner(p,sx,sy):
        k=3;sz=ARM+STROKE+4;im=Image.new('RGBA',(sz*k,sz*k));d=ImageDraw.Draw(im);o=STROKE+1;pts=[(o+ARM,o),(o,o),(o,o+ARM)];length=2*ARM*p;path=[pts[0]]
        if length<=ARM:path.append((o+ARM-length,o))
        else:path.extend([(o,o),(o,o+length-ARM)])
        if p>0:d.line([(round(xx*k),round(yy*k)) for xx,yy in path],fill=(*ACCENT,255),width=STROKE*k,joint='curve')
        im=im.resize((sz,sz),Image.Resampling.LANCZOS)
        if sx<0:im=im.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        if sy<0:im=im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        return im
    corners=[(l,t,1,1),(r,t,-1,1),(l,b,1,-1),(r,b,-1,-1)]
    def render(t):
        im=Image.new('RGBA',(W,H));p=ease(t,*ENTRY);q=ease(t,*TITLE_IN);v=1-ease(t,*EXIT);paste(im,a,x,y+14*(1-q),q*v)
        for xx,yy,sx,sy in corners:
            c=corner(p,sx,sy);ox=(STROKE+1) if sx>0 else c.width-STROKE-2;oy=(STROKE+1) if sy>0 else c.height-STROKE-2;paste(im,c,xx-ox-sx*TRAVEL*(1-p),yy-oy-sy*TRAVEL*.5*(1-p),v)
        return im
    return render,dict(actors=[act('headline',am,x,y,INK)]+[dict(id=f'corner_{i}',type='shape',x=xx,y=yy,width=ARM,height=ARM,color=ACCENT,orientation=[sx,sy],stroke=STROKE) for i,(xx,yy,sx,sy) in enumerate(corners)],entry=ENTRY,title_entry=TITLE_IN,exit=EXIT,hold_seconds=[TITLE_IN[1],EXIT[0]],padding=[PX,PY],arm=ARM,stroke=STROKE,travel=TRAVEL,mechanism='Corner trim along two 38px segments; mirrored orientations, 3x supersample; corners translate 66px horizontally and 33px vertically from outward positions; title rises 14px.')

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
            for n in range(N):proc.stdin.write(render(n/FPS).tobytes())
        finally:proc.stdin.close();rc=proc.wait()
        if rc:raise RuntimeError(f'ffmpeg failed {rc}')
    print(json.dumps(dict(preview_sha256=sha(out/'preview.mp4'),alpha_sha256=sha(out/'alpha.mkv')),ensure_ascii=False))
if __name__=='__main__':main()

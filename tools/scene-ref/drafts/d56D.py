#!/usr/bin/env python3
import argparse, hashlib, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
W,H,FPS,N=1920,1080,25,150
SEED=0 # no random operations
FONT_FILES={'bold': ('InterDisplay-Bold.otf', 'd7a7394984bbb3bc3c3e507a6618d34e006c94f27c7105d0ef4376667aeaf4ee'), 'medium': ('InterDisplay-Medium.otf', '9a17712bbd6da13b7ce9beb056d19fe15648799a11d5c35f8086a1270a0d13b2'), 'regular': ('InterDisplay-Regular.otf', '35c1f7437dd6d63f4e60a49144df4b4e4edc190e13ffc1e54b86083747fe3acf'), 'serif': ('NotoSerif-Regular.ttf', '9d7583b7dc9e812afd32a14280c5cac3160012efe50c8d08938f4fea266ff67f')}
TEST_TEXT="Ă Â Î Ș Ț"
# Settings: anchor point + routed callout, title and secondary independent. Example anchor is not tracking footage.
DEFAULT_TEXT="Optical stabilization";SECONDARY="Reduce unwanted camera movement";SIZE=77;SUBSIZE=39
INK=(247,246,240);SUB=(191,203,205);ACCENT=(132,217,223);PANEL=(20,29,33,239)
ANCHOR=(1330,350);ELBOW=(1120,520);LEFT=210;TOP=548;PAD=36;GAP=22
ENTRY=(.12,.55);ROUTE=(.38,1.24);PANEL_IN=(.92,1.58);TEXT_IN=(1.18,1.83);EXIT=(4.94,5.76)
LINE_WIDTH=4;RADIUS=12

def prepare(fd,text,secondary):
    a=text or DEFAULT_TEXT;b=secondary or SECONDARY;title,m=texttile(fd,a,'bold',SIZE,INK,965);sub,q=texttile(fd,b,'regular',SUBSIZE,SUB,965)
    bw=max(title.width,sub.width)+2*PAD;bh=2*PAD+title.height+GAP+sub.height;right=LEFT+bw; # Panel top attachment is fixed elbow x, clamped to panel bounds.
    attach=(min(right-32,max(LEFT+32,ELBOW[0])),TOP);points=[ANCHOR,ELBOW,attach];lengths=[math.dist(points[0],points[1]),math.dist(points[1],points[2])];total=sum(lengths)
    # small supersampled shape image; no opaque frame background.
    k=3;panel=Image.new('RGBA',(bw*k,bh*k));dr=ImageDraw.Draw(panel);dr.rounded_rectangle((0,0,bw*k-1,bh*k-1),radius=RADIUS*k,fill=PANEL);panel=panel.resize((bw,bh),Image.Resampling.LANCZOS)
    def trace(p):
        x0,y0=180,310;w,h=1190,270;im=Image.new('RGBA',(w*k,h*k));d=ImageDraw.Draw(im);distance=total*p;ps=[points[0]]
        for i,l in enumerate(lengths):
            f=clamp(distance/l) if l else 1.;ps.append(tuple(mix(points[i][j],points[i+1][j],f) for j in (0,1)));distance-=l
            if distance<=0:break
        c=[(round((x-x0)*k),round((y-y0)*k)) for x,y in ps];d.line(c,fill=(*ACCENT,255),width=LINE_WIDTH*k,joint='curve');x,y=c[0];r=9*k;d.ellipse((x-r,y-r,x+r,y+r),fill=(*ACCENT,255));return im.resize((w,h),Image.Resampling.LANCZOS),x0,y0
    full=trace(1.)
    def render(t):
        out=Image.new('RGBA',(W,H));o=ease(t,*EXIT);a=1-o;e=ease(t,*ENTRY);p=ease(t,*ROUTE);g=ease(t,*PANEL_IN);h=ease(t,*TEXT_IN)
        tr,x,y=full if p>=1 else trace(p);paste(out,tr,x,y,a*e)
        # Panel opens upward to its fixed connector, revealing contents without distorting type.
        ch=round(bh*g)
        if ch:paste(out,panel.crop((0,0,bw,ch)),LEFT,TOP,a)
        for im,yy in [(title,TOP+PAD),(sub,TOP+PAD+title.height+GAP)]:
            v=min(im.height,max(0,TOP+ch-yy));
            if v:paste(out,im.crop((0,0,im.width,v)),LEFT+PAD,yy,a*h)
        return out
    return render,dict(text=a,secondary=b,actors=[dict(id='title',**m,x=LEFT+PAD,y=TOP+PAD,color=INK),dict(id='detail',**q,x=LEFT+PAD,y=TOP+PAD+title.height+GAP,color=SUB),dict(id='panel',type='shape',x=LEFT,y=TOP,width=bw,height=bh,color=PANEL,radius=RADIUS),dict(id='anchor_route',type='shape',points=points,width=LINE_WIDTH,color=ACCENT)],entry=ENTRY,route=ROUTE,panel_in=PANEL_IN,text_in=TEXT_IN,exit=EXIT,hold_seconds=[TEXT_IN[1],EXIT[0]],pad=PAD,gap=GAP,panel_radius=RADIUS,draw_order=['leader','panel','title','detail'],anchor_tracking='manual static coordinates; native tracking not tested',colors=dict(ink=INK,secondary=SUB,accent=ACCENT,panel=PANEL))

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
    return ['ffmpeg','-hide_banner','-loglevel','error','-y','-filter_complex_threads','2','-f','rawvideo','-pixel_format','rgba','-video_size',f'{W}x{H}','-framerate',str(FPS),'-i','pipe:0','-filter_complex',f'[0:v]split=2[raw][ov];[raw]format=bgra[a];color=c=black:s={W}x{H}:r={FPS}[bg];[bg][ov]overlay=shortest=1:format=rgb:alpha=straight,format=yuv420p[v]','-map','[v]','-frames:v',str(N),'-an','-c:v','libx264','-preset','fast','-crf','16','-threads','2','-pix_fmt','yuv420p','-movflags','+faststart','-map_metadata','-1',str(out/'preview.mp4'),'-map','[a]','-frames:v',str(N),'-an','-c:v','ffv1','-level','3','-threads','2','-pix_fmt','bgra','-map_metadata','-1',str(out/'alpha.mkv')]
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

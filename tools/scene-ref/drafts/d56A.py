#!/usr/bin/env python3
import argparse, hashlib, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
W,H,FPS,N=1920,1080,25,150
SEED=0 # no random operations
FONT_FILES={'bold': ('InterDisplay-Bold.otf', 'd7a7394984bbb3bc3c3e507a6618d34e006c94f27c7105d0ef4376667aeaf4ee'), 'medium': ('InterDisplay-Medium.otf', '9a17712bbd6da13b7ce9beb056d19fe15648799a11d5c35f8086a1270a0d13b2'), 'regular': ('InterDisplay-Regular.otf', '35c1f7437dd6d63f4e60a49144df4b4e4edc190e13ffc1e54b86083747fe3acf'), 'serif': ('NotoSerif-Regular.ttf', '9d7583b7dc9e812afd32a14280c5cac3160012efe50c8d08938f4fea266ff67f')}
TEST_TEXT="Ă Â Î Ș Ț"
# Settings: px at 1080p, seconds, sRGB straight alpha
DEFAULT_TEXT="Brașov"; SECONDARY="Sibiu"; SIZE=105; SMALL=34
INK=(248,245,237); ACCENT=(244,186,111); DIM=(94,103,112)
P0=(270,515); P1=(650,515); P2=(835,382); P3=(1085,382); P4=(1280,515); P5=(1650,515)
ENTRY=(0.12,0.72); TRAVEL=(0.70,2.65); DEST=(2.03,2.70); EXIT=(4.94,5.76)
ROUTE_WIDTH=5; MARKER_RADIUS=12; END_RADIUS=8

def prepare(fd,text,secondary):
    a=text or DEFAULT_TEXT;b=secondary or SECONDARY
    left,la=texttile(fd,a,'bold',SIZE,INK,740);right,lb=texttile(fd,b,'bold',SIZE,INK,740)
    tag,lt=texttile(fd,'DEPARTURE','medium',SMALL,ACCENT);tag2,lr=texttile(fd,'ARRIVAL','medium',SMALL,ACCENT)
    # Cubic Bezier, horizontal tangents at ends; sampled arc-length table, distance-linear progression before ease.
    pts=[P0,P1];pts += [((1-u)**3*P1[0]+3*(1-u)**2*u*P2[0]+3*(1-u)*u*u*P3[0]+u**3*P4[0],(1-u)**3*P1[1]+3*(1-u)**2*u*P2[1]+3*(1-u)*u*u*P3[1]+u**3*P4[1]) for u in [i/160 for i in range(1,161)]];pts.append(P5)
    ds=[0.]
    for p,q in zip(pts,pts[1:]):ds.append(ds[-1]+math.dist(p,q))
    # 2x local vector layer, route-only ROI, static complete route cache.
    ox,oy=240,340; rw,rh=1440,205; k=3
    def route(p):
        d=p*ds[-1];j=next((i for i in range(1,len(ds)) if ds[i]>=d),len(ds)-1);f=(d-ds[j-1])/(ds[j]-ds[j-1]);x,y=[mix(pts[j-1][i],pts[j][i],f) for i in (0,1)];q=(x,y)
        im=Image.new('RGBA',(rw*k,rh*k));dr=ImageDraw.Draw(im)
        def coords(points):return [(round((a-ox)*k),round((b-oy)*k)) for a,b in points]
        dr.line(coords(pts),fill=(*DIM,255),width=ROUTE_WIDTH*k,joint='curve')
        if p>0:dr.line(coords(pts[:j]+[q]),fill=(*ACCENT,255),width=ROUTE_WIDTH*k,joint='curve')
        for (xx,yy),r,c in [(P0,END_RADIUS,ACCENT),(P5,END_RADIUS,ACCENT if p>=1 else DIM),(q,MARKER_RADIUS,ACCENT)]:
            xx=(xx-ox)*k;yy=(yy-oy)*k;r*=k;dr.ellipse((round(xx-r),round(yy-r),round(xx+r),round(yy+r)),fill=(*c,255))
        return im.resize((rw,rh),Image.Resampling.LANCZOS)
    full=route(1.)
    def render(t):
        out=Image.new('RGBA',(W,H));e=ease(t,*ENTRY);o=ease(t,*EXIT);alpha=e*(1-o);p=ease(t,*TRAVEL);d=ease(t,*DEST);dy=round(18*(1-e)-12*o)
        paste(out,full if p>=1 else route(p),ox,oy+dy,alpha)
        paste(out,tag,P0[0],570+dy,alpha);paste(out,left,P0[0],625+dy,alpha)
        paste(out,tag2,P5[0]-tag2.width,570+dy,alpha*d);paste(out,right,P5[0]-right.width,625+dy+round(12*(1-d)),alpha*d)
        return out
    return render,dict(text=a,secondary=b,actors=[dict(id='origin',**la,x=P0[0],y=625,color=INK),dict(id='destination',**lb,x=P5[0]-right.width,y=625,color=INK),dict(id='departure_label',**lt,x=P0[0],y=570,color=ACCENT),dict(id='arrival_label',**lr,x=P5[0]-tag2.width,y=570,color=ACCENT),dict(id='route_and_marker',type='shape',points=[P0,P1,P2,P3,P4,P5],width=ROUTE_WIDTH,radii=[END_RADIUS,MARKER_RADIUS],color=ACCENT)],entry=ENTRY,travel=TRAVEL,destination_reveal=DEST,exit=EXIT,hold_seconds=[DEST[1],EXIT[0]],curve_samples=160,arc_length=ds[-1],colors=dict(ink=INK,accent=ACCENT,dim=DIM),route_is_schematic=True)

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

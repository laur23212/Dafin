#!/usr/bin/env python3
import argparse, hashlib, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter
ROOT=Path(__file__).resolve().parents[1]
W,H,FPS,N=1920,1080,25,150
SEED=0 # no random operations
FONT_FILES={'bold': ('InterDisplay-Bold.otf', 'd7a7394984bbb3bc3c3e507a6618d34e006c94f27c7105d0ef4376667aeaf4ee'), 'medium': ('InterDisplay-Medium.otf', '9a17712bbd6da13b7ce9beb056d19fe15648799a11d5c35f8086a1270a0d13b2'), 'regular': ('InterDisplay-Regular.otf', '35c1f7437dd6d63f4e60a49144df4b4e4edc190e13ffc1e54b86083747fe3acf'), 'serif': ('NotoSerif-Regular.ttf', '9d7583b7dc9e812afd32a14280c5cac3160012efe50c8d08938f4fea266ff67f')}
TEST_TEXT="Ă Â Î Ș Ț"








DEFAULT_TEXT='Audience breakdown'; SECONDARY='Mobile,Desktop,68'; INK=(246,244,235); A=(136,213,188); B=(219,183,133); DIM=(100,112,117); X=300; BW=1320; BH=42; ENTRY=(.12,.76); SPLIT=(.60,1.62); EXIT=(4.92,5.76)
def prepare(fd,text,secondary):
 a,b,s=(secondary or SECONDARY).rsplit(',',2);share=int(s)
 if not 0<=share<=100:raise ValueError('Share 0..100')
 h,hm=texttile(fd,text or DEFAULT_TEXT,'serif',76,INK,1320);actors=[act('headline',hm,X,280,INK,cue=ENTRY,from_values={'opacity':0},to_values={'opacity':1})];tiles=[]
 for i,(name,val,c) in enumerate(((a,share,A),(b,100-share,B))):
  n,nm=texttile(fd,name,'regular',43,INK,580);v,vm=texttile(fd,f'{val}%','medium',112,c,580);xx=X if i==0 else X+BW-v.width;nx=X if i==0 else X+BW-n.width;tiles.append((n,v,nx,xx));actors.extend([act(f'label_{i}',nm,nx,690,INK,cue=ENTRY,from_values={'opacity':0},to_values={'opacity':1}),act(f'percent_{i}',vm,xx,435,c,cue=SPLIT,from_values={'opacity':0},to_values={'opacity':1})])
 boundary=round(BW*share/100);actors.extend([dict(id='share_a',type='shape',x=X,y=610,width=boundary,height=BH,color=A,cue=SPLIT,from_values={'width':0},to_values={'width':boundary}),dict(id='share_b',type='shape',x=X+boundary,y=610,width=BW-boundary,height=BH,color=B,cue=SPLIT,from_values={'width':0},to_values={'width':BW-boundary})])
 def render(t):
  im=Image.new('RGBA',(W,H));p=ease(t,*ENTRY);q=ease(t,*SPLIT);v=1-ease(t,*EXIT);paste(im,h,X,280+12*(1-p),p*v)
  for n,z,nx,xx in tiles:paste(im,n,nx,690,p*v);paste(im,z,xx,435+18*(1-q),q*v)
  paste(im,rect(BW,BH,DIM),X,610,.35*p*v);wa=round(boundary*q);wb=round((BW-boundary)*q)
  if wa:paste(im,rect(wa,BH,A),X,610,p*v)
  if wb:paste(im,rect(wb,BH,B),X+BW-wb,610,p*v)
  return im
 return render,dict(actors=actors,entry=ENTRY,exit=EXIT,hold_seconds=[SPLIT[1],EXIT[0]],mechanism='Share A integer 0..100; B=100-A. Segment A grows from left, segment B from right, meeting at exact rounded boundary. Zero-width segment omitted. Labels remain independent and anchored to opposite edges. No fake gap or manually synced value.',share_a=share,share_b=100-share,bar_width=BW,widths=[boundary,BW-boundary])

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

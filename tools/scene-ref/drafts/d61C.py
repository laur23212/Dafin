#!/usr/bin/env python3
import argparse, hashlib, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter
ROOT=Path(__file__).resolve().parents[1]
W,H,FPS,N=1920,1080,25,150
SEED=0 # no random operations
FONT_FILES={'bold': ('InterDisplay-Bold.otf', 'd7a7394984bbb3bc3c3e507a6618d34e006c94f27c7105d0ef4376667aeaf4ee'), 'medium': ('InterDisplay-Medium.otf', '9a17712bbd6da13b7ce9beb056d19fe15648799a11d5c35f8086a1270a0d13b2'), 'regular': ('InterDisplay-Regular.otf', '35c1f7437dd6d63f4e60a49144df4b4e4edc190e13ffc1e54b86083747fe3acf'), 'serif': ('NotoSerif-Regular.ttf', '9d7583b7dc9e812afd32a14280c5cac3160012efe50c8d08938f4fea266ff67f')}
TEST_TEXT="Ă Â Î Ș Ț"





DEFAULT_TEXT='Before'; SECONDARY='After'; LSUB='ORIGINAL'; RSUB='REFINED'; INK=(245,245,240); A1=(173,195,218); A2=(129,218,184); SIZE=88; TAG=27; CX=960; Y=454; GAP=72; ENTRY=(.24,1.02); RULE=(.08,.75); TAGCUE=(.68,1.35); EXIT=(4.92,5.76); RULE_H=178


def prepare(fd,text,secondary):
 a,am=texttile(fd,text or DEFAULT_TEXT,'medium',SIZE,INK,700);b,bm=texttile(fd,secondary or SECONDARY,'medium',SIZE,INK,700);l,lm=texttile(fd,LSUB,'medium',TAG,A1,690);r,rm=texttile(fd,RSUB,'medium',TAG,A2,690);ax=CX-GAP-a.width;bx=CX+GAP;ly=Y+max(a.height,b.height)+28;lx=CX-GAP-l.width;rx=CX+GAP;cy=(Y+ly+max(l.height,r.height))/2
 def render(t):
  im=Image.new('RGBA',(W,H));q=ease(t,*RULE);p=ease(t,*ENTRY);s=ease(t,*TAGCUE);v=1-ease(t,*EXIT)
  if q>0:paste(im,rect(2,max(1,round(RULE_H*q)),(170,180,185)),CX-1,cy-RULE_H*q/2,v)
  clip(im,a,ax+56*(1-p),Y,(0,0,CX-GAP,H),p*v);clip(im,b,bx-56*(1-p),Y,(CX+GAP,0,W,H),p*v)
  clip(im,l,lx+24*(1-s),ly,(0,0,CX-GAP,H),s*v);clip(im,r,rx-24*(1-s),ly,(CX+GAP,0,W,H),s*v);return im
 return render,dict(actors=[dict(id='divider',type='shape',x=CX-1,y=cy-RULE_H/2,width=2,height=RULE_H,color=(170,180,185),cue=RULE,from_values={'height':0},to_values={'height':RULE_H}),act('left',am,ax,Y,INK,cue=ENTRY,from_values={'opacity':0,'dx':56},to_values={'opacity':1,'dx':0}),act('right',bm,bx,Y,INK,cue=ENTRY,from_values={'opacity':0,'dx':-56},to_values={'opacity':1,'dx':0}),act('left_context',lm,lx,ly,A1,cue=TAGCUE,from_values={'opacity':0,'dx':24},to_values={'opacity':1,'dx':0}),act('right_context',rm,rx,ly,A2,cue=TAGCUE,from_values={'opacity':0,'dx':-24},to_values={'opacity':1,'dx':0})],entry=ENTRY,exit=EXIT,hold_seconds=[TAGCUE[1],EXIT[0]],mechanism='Two safe clipping regions share a divider and 72px gutter on each side. Left right-aligns to its boundary; right left-aligns; both independently measured. Divider expands symmetrically. Context aligns to the same edges. Move divider and recompute both layouts; native controls should expose each independently.',divider_x=CX,gap=GAP,line_gap=28)

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

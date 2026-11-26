#!/usr/bin/env python3
import argparse, hashlib, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter
ROOT=Path(__file__).resolve().parents[1]
W,H,FPS,N=1920,1080,25,150
SEED=0 # no random operations
FONT_FILES={'bold': ('InterDisplay-Bold.otf', 'd7a7394984bbb3bc3c3e507a6618d34e006c94f27c7105d0ef4376667aeaf4ee'), 'medium': ('InterDisplay-Medium.otf', '9a17712bbd6da13b7ce9beb056d19fe15648799a11d5c35f8086a1270a0d13b2'), 'regular': ('InterDisplay-Regular.otf', '35c1f7437dd6d63f4e60a49144df4b4e4edc190e13ffc1e54b86083747fe3acf'), 'serif': ('NotoSerif-Regular.ttf', '9d7583b7dc9e812afd32a14280c5cac3160012efe50c8d08938f4fea266ff67f')}
TEST_TEXT="Ă Â Î Ș Ț"









DEFAULT_TEXT='Total investment'; SECONDARY='Equipment,240;Materials,160'; INK=(246,241,232); DIM=(159,170,171); ACC=(144,216,190); X=355; BW=1210; ENTRY=(.10,.62); ADD=(.65,1.65); EXIT=(4.92,5.76)

def prepare(fd,text,secondary):
 pairs=[p.rsplit(',',1) for p in (secondary or SECONDARY).split(';')]
 if len(pairs)!=2:raise ValueError('two label,value pairs required')
 vals=[int(p[1]) for p in pairs]
 if not all(0<=v<=999999 for v in vals):raise ValueError('values 0..999999')
 total=sum(vals);title,tm=texttile(fd,text or DEFAULT_TEXT,'serif',74,INK,BW);items=[];actors=[act('heading',tm,X,224,INK,cue=ENTRY,from_values={'opacity':0},to_values={'opacity':1})]
 for i,((name,_),val) in enumerate(zip(pairs,vals)):
  n,nm=texttile(fd,name,'regular',46,DIM,760);z,zm=texttile(fd,f'{val:,}','medium',54,INK,350);y=374+i*100;items.append((n,z,y));actors.extend([act(f'label_{i}',nm,X,y,DIM,cue=ENTRY,from_values={'opacity':0},to_values={'opacity':1}),act(f'value_{i}',zm,X+BW-z.width,y,INK,cue=ENTRY,from_values={'opacity':0},to_values={'opacity':1})])
 caption,cm=texttile(fd,'TOTAL','medium',38,ACC,500);end,em=texttile(fd,f'{total:,}','medium',128,ACC,BW-350);cache={total:end};actors.extend([dict(id='sum_rule',type='shape',x=X,y=585,width=BW,height=2,color=DIM,cue=ADD,from_values={'width':0},to_values={'width':BW}),act('total_label',cm,X,680,ACC,cue=ADD,from_values={'opacity':0},to_values={'opacity':1}),act('total_value',em,X+BW-end.width,631,ACC,cue=ADD,from_values={'value':vals[0]},to_values={'value':total})])
 def render(t):
  im=Image.new('RGBA',(W,H));p=ease(t,*ENTRY);q=ease(t,*ADD);v=1-ease(t,*EXIT);paste(im,title,X,224+14*(1-p),p*v)
  for i,(n,z,y) in enumerate(items):
   a=p if i==0 else ease(t,.47,1.03);paste(im,n,X,y,a*v);paste(im,z,X+BW-z.width,y,a*v)
  ww=round(BW*q)
  if ww:paste(im,rect(ww,2,DIM),X,585,p*v)
  value=vals[0]+round(vals[1]*q)
  if value not in cache:cache[value]=texttile(fd,f'{value:,}','medium',128,ACC,BW-350)[0]
  z=cache[value];paste(im,caption,X,680,q*v);paste(im,z,X+BW-z.width,631,q*v);return im
 return render,dict(actors=actors,entry=ENTRY,exit=EXIT,hold_seconds=[ADD[1],EXIT[0]],mechanism='Two label,value pairs, values integer 0..999999. Total=sum(values); running total=first+round(second*smoothstep). Right-aligned numbers, exact total at hold; no manually edited derived result. Recompute after every edit.',values=vals,total=total,derived_text=f'{total:,}')

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

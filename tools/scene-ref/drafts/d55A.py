#!/usr/bin/env python3
"""Frozen self-contained renderer; Python 3.13.5, Pillow 12.3.0, numpy 2.3.5, OpenCV 4.13.0."""
import argparse, hashlib, json, math, subprocess, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
W,H,FPS,N=1920,1080,25,150
SEED=0 # no stochastic operations
FONT_FILES={"bold":("InterDisplay-Bold.otf","d7a7394984bbb3bc3c3e507a6618d34e006c94f27c7105d0ef4376667aeaf4ee"),"medium":("InterDisplay-Medium.otf","9a17712bbd6da13b7ce9beb056d19fe15648799a11d5c35f8086a1270a0d13b2"),"black":("InterDisplay-Black.otf","84ca5a51b3303c01b48cdd271637d3d20c263e09d00ec0ff2a945f122492a4d0")}
TEST_TEXT="Ă Â Î Ș Ț"
# SETTINGS: reference pixels, seconds. One editable text actor; editable cue indices.
DEFAULT_TEXT="Make every moment count"
FONT_ROLE="bold"; SIZE=114; MIN_SIZE=56; MAX_WIDTH=1680; BASELINE=592; GAP=34
INK=(241,240,234); INK_ACTIVE=(18,26,24); MARK=(168,224,186)
PAD_X=22; PAD_Y=17; RADIUS=18; ENTRY=(0.12,0.78); EXIT=(4.96,5.76)
# Focus travels by word index; last selected word is the held emphasis.
CUE_TIMES=[(1.10,1.55),(1.95,2.40),(2.78,3.23)]; MARK_IN=(0.72,1.08)

def prepare(directory,text):
    words=text.split()
    if not 2<=len(words)<=8: raise ValueError('Use 2–8 words')
    size=SIZE
    while True:
        f=font(directory,FONT_ROLE,size); widths=[f.getlength(w) for w in words]
        total=sum(widths)+GAP*(len(words)-1)
        if total<=MAX_WIDTH or size==MIN_SIZE: break
        size-=1
    if total>MAX_WIDTH: raise ValueError('Text exceeds safe area')
    x=(W-total)/2; items=[]; masks=[]
    for word,advance in zip(words,widths):
        xx=round(x); bbox=f.getbbox(word,anchor='ls'); items.append((word,xx,bbox)); x+=advance+GAP
    top=min(BASELINE+b[1] for _,_,b in items)-PAD_Y; bottom=max(BASELINE+b[3] for _,_,b in items)+PAD_Y
    # All words use one baseline and common vertical marker metrics; real ink bounds drive horizontal extent.
    bounds=[(xx+b[0]-PAD_X,xx+b[2]+PAD_X) for _,xx,b in items]
    textmask=Image.new('L',(W,H)); d=ImageDraw.Draw(textmask)
    for w,xx,b in items:d.text((xx,BASELINE),w,font=f,anchor='ls',fill=255)
    ta=np.asarray(textmask).copy(); roi=(max(0,min(b[0] for b in bounds)-30),max(0,top-35),min(W,max(b[1] for b in bounds)+30),min(H,bottom+35))
    x0,y0,x1,y1=roi; glyph=ta[y0:y1,x0:x1]; bw,bh=x1-x0,y1-y0
    # Exactly three cue moves distributed across the words, with every word selectable in native implementation.
    indices=[round(i*(len(words)-1)/3) for i in range(4)]
    def render(t):
        e=ease(t,*ENTRY); o=ease(t,*EXIT); a=e*(1-o); dy=round(20*(1-e)-16*o)
        left,right=bounds[indices[0]]
        for i,(s,end) in enumerate(CUE_TIMES):
            p=ease(t,s,end); l,r=bounds[indices[i+1]]; left=left+(l-left)*p; right=right+(r-right)*p
        mi=ease(t,*MARK_IN); center=(left+right)/2; left=center+(left-center)*mi; right=center+(right-center)*mi
        marker=Image.new('L',(bw,bh)); md=ImageDraw.Draw(marker)
        if mi>0:md.rounded_rectangle((round(left-x0),round(top-y0),round(right-x0),round(bottom-y0)),radius=min(RADIUS,round((right-left)/2)),fill=round(255*mi))
        ma=np.asarray(marker).astype(np.float32)/255.; g=glyph.astype(np.float32)/255.
        # Marker is a real separate backing actor, and active text ink is selected by its actual coverage.
        ink=np.array(INK)[None,None,:]*(1-ma[...,None])+np.array(INK_ACTIVE)[None,None,:]*ma[...,None]
        alpha=g+ma*(1-g); rgb=(ink*g[...,None]+np.array(MARK)*ma[...,None]*(1-g[...,None]))/np.maximum(alpha[...,None],1e-8)
        rgba=np.empty((bh,bw,4),np.uint8); rgba[:,:,:3]=np.rint(rgb).clip(0,255).astype(np.uint8); rgba[:,:,3]=np.rint(alpha*255*a).astype(np.uint8)
        out=np.zeros((H,W,4),np.uint8); out[y0+dy:y1+dy,x0:x1]=rgba; return out
    return render,dict(text=text,words=words,size=size,baseline=BASELINE,word_origins=[(w,x,BASELINE) for w,x,b in items],ink_bboxes=[b for w,x,b in items],marker_bounds=bounds,marker_vertical=[top,bottom],cue_indices=indices,cue_times=CUE_TIMES,entry=ENTRY,exit=EXIT,marker_in=MARK_IN,hold_seconds=[CUE_TIMES[-1][1],EXIT[0]],colors=dict(ink=INK,active=INK_ACTIVE,marker=MARK),pad=[PAD_X,PAD_Y],radius=RADIUS,tracking=0,word_gap=GAP)

def clamp(x): return max(0.,min(1.,x))
def smooth(x):
    x=clamp(x); return x*x*(3-2*x)
def ease(t,a,b): return smooth((t-a)/(b-a))
def font(directory,role,size):
    name,expected=FONT_FILES[role]; p=directory/name
    if hashlib.sha256(p.read_bytes()).hexdigest()!=expected: raise ValueError(f"Exact font hash mismatch: {p}")
    return ImageFont.truetype(str(p),size,layout_engine=ImageFont.Layout.RAQM)
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def encode_cmd(out):
    return ['ffmpeg','-hide_banner','-loglevel','error','-y','-filter_complex_threads','2','-f','rawvideo','-pixel_format','rgba','-video_size',f'{W}x{H}','-framerate',str(FPS),'-i','pipe:0','-filter_complex',f'[0:v]split=2[raw][ov];[raw]format=bgra[a];color=c=black:s={W}x{H}:r={FPS}[bg];[bg][ov]overlay=shortest=1:format=rgb:alpha=straight,format=yuv420p[v]','-map','[v]','-frames:v',str(N),'-an','-c:v','libx264','-preset','fast','-crf','16','-threads','2','-pix_fmt','yuv420p','-movflags','+faststart','-map_metadata','-1',str(out/'preview.mp4'),'-map','[a]','-frames:v',str(N),'-an','-c:v','ffv1','-level','3','-threads','2','-pix_fmt','bgra','-map_metadata','-1',str(out/'alpha.mkv')]
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--font-dir',type=Path,default=ROOT/'fonts'); ap.add_argument('--output',type=Path,default=ROOT/'output'); ap.add_argument('--text',default=DEFAULT_TEXT); ap.add_argument('--romanian',action='store_true'); ap.add_argument('--preflight',action='store_true'); args=ap.parse_args()
    out=args.output; out.mkdir(parents=True,exist_ok=True); text=TEST_TEXT if args.romanian else args.text
    renderer,geometry=prepare(args.font_dir,text)
    (out/'geometry.json').write_text(json.dumps(geometry,ensure_ascii=False,indent=2)+'\n')
    if args.preflight:
        for n in (8,18,30,60,125,137):
            img=Image.fromarray(renderer(n/FPS),'RGBA'); Image.alpha_composite(Image.new('RGBA',(W,H),(0,0,0,255)),img).convert('RGB').save(out/f'preflight_{n:03d}.png')
        return
    cmd=encode_cmd(out); (out/'encode_command.json').write_text(json.dumps(cmd,indent=2)+'\n')
    with open(out/'ffmpeg.log','wb') as log:
        proc=subprocess.Popen(cmd,stdin=subprocess.PIPE,stderr=log)
        try:
            for n in range(N): proc.stdin.write(renderer(n/FPS).tobytes())
            proc.stdin.close()
        finally:
            if proc.poll() is None and not proc.stdin.closed: proc.stdin.close()
            rc=proc.wait()
        if rc: raise RuntimeError(f'FFmpeg return {rc}; see {out}/ffmpeg.log')
    print(json.dumps({'preview_sha256':sha(out/'preview.mp4'),'alpha_container_sha256':sha(out/'alpha.mkv'),'geometry':geometry},ensure_ascii=False))
if __name__=='__main__':main()

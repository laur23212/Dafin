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
# SETTINGS: all values reference px and seconds. Shaped line groups never deform or clip.
DEFAULT_TEXT="Make every moment count"
FONT_ROLE="bold"; SIZE=116; MIN_SIZE=54; MAX_WIDTH=1680; GAP=36
CENTER_X=960; CENTER_BASELINE=594; BASELINE_OFFSET=94; TOP_INK=(244,242,234); BOTTOM_INK=(165,201,232)
ENTRY=(0.10,0.74); SEPARATE=(1.00,1.52); ALIGN=(1.30,2.13); EXIT=(4.92,5.72)

def prepare(directory,text):
    words=text.split()
    if not 2<=len(words)<=12: raise ValueError('Use 2–12 words')
    split=(len(words)+1)//2; groups=[' '.join(words[:split]),' '.join(words[split:])]
    size=SIZE
    while True:
        f=font(directory,FONT_ROLE,size); b=[f.getbbox(g,anchor='ls') for g in groups]; widths=[v[2]-v[0] for v in b]
        if sum(widths)+GAP<=MAX_WIDTH or size==MIN_SIZE:break
        size-=1
    if sum(widths)+GAP>MAX_WIDTH: raise ValueError('Text exceeds safe area')
    first=round(CENTER_X-(sum(widths)+GAP)/2); start=[first,first+widths[0]+GAP]; finish=[round(CENTER_X-w/2) for w in widths]
    actors=[]
    for g,bb,col in zip(groups,b,[TOP_INK,BOTTOM_INK]):
        # Tight anti-aliased straight-alpha mask with padding to preserve accented/descender ink.
        m=Image.new('L',(bb[2]-bb[0]+4,bb[3]-bb[1]+4)); ImageDraw.Draw(m).text((2-bb[0],2-bb[1]),g,font=f,anchor='ls',fill=255)
        mm=np.asarray(m); rgba=np.zeros((*mm.shape,4),np.uint8); rgba[:,:,:3]=col; rgba[:,:,3]=mm; actors.append(rgba)
    def render(t):
        e=ease(t,*ENTRY); v=ease(t,*SEPARATE); h=ease(t,*ALIGN); o=ease(t,*EXIT); opacity=e*(1-o)
        canvas=Image.new('RGBA',(W,H));
        for i,(arr,bb) in enumerate(zip(actors,b)):
            sign=-1 if i==0 else 1; xx=round(start[i]+(finish[i]-start[i])*h)-2
            baseline=round(CENTER_BASELINE+sign*BASELINE_OFFSET*v+20*(1-e)+sign*32*o)
            layer=arr.copy(); layer[:,:,3]=np.rint(layer[:,:,3]*opacity).astype(np.uint8)
            canvas.alpha_composite(Image.fromarray(layer),(xx,baseline+bb[1]-2))
        return np.asarray(canvas).copy()
    return render,dict(text=text,groups=groups,size=size,ink_bboxes=b,start_ink_x=start,finish_ink_x=finish,center_baseline=CENTER_BASELINE,baseline_offset=BASELINE_OFFSET,start_baselines=[CENTER_BASELINE]*2,hold_baselines=[CENTER_BASELINE-BASELINE_OFFSET,CENTER_BASELINE+BASELINE_OFFSET],entry=ENTRY,separate=SEPARATE,align=ALIGN,exit=EXIT,hold_seconds=[ALIGN[1],EXIT[0]],colors=[TOP_INK,BOTTOM_INK],gap=GAP,tracking=0)

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

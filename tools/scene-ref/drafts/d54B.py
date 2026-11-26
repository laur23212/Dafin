#!/usr/bin/env python3
"""Frozen self-contained renderer; Python 3.13.5, Pillow 12.3.0, numpy 2.3.5, OpenCV 4.13.0."""
import argparse, hashlib, json, math, subprocess, sys
from pathlib import Path
import numpy as np
import cv2
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
W,H,FPS,N=1920,1080,25,150
SEED=0 # no stochastic operations
FONT_FILES={"bold":("InterDisplay-Bold.otf","d7a7394984bbb3bc3c3e507a6618d34e006c94f27c7105d0ef4376667aeaf4ee"),"medium":("InterDisplay-Medium.otf","9a17712bbd6da13b7ce9beb056d19fe15648799a11d5c35f8086a1270a0d13b2"),"black":("InterDisplay-Black.otf","84ca5a51b3303c01b48cdd271637d3d20c263e09d00ec0ff2a945f122492a4d0")}
TEST_TEXT="Ă Â Î Ș Ț"
# SETTINGS: absolute reference px, seconds; numeric cue is independent of global title opacity.
DEFAULT_TEXT="km" # editable unit text actor; number independently editable via settings
NUMBER_FROM=236; NUMBER_TO=248; DIGITS=3
DIGIT_ROLE="bold"; DIGIT_SIZE=230; DIGIT_CELL=151; DIGIT_PITCH=204
UNIT_ROLE="medium"; UNIT_SIZE=80; UNIT_MIN=40; UNIT_MAX_WIDTH=650; UNIT_GAP=30
DIGIT_RGB=(244,241,232); UNIT_RGB=(154,208,194)
BASELINE_Y=605; CENTER_X=960; WINDOW_TOP=396; WINDOW_BOTTOM=634; FEATHER=24
IN_START=.0; IN_END=.30; ROLL_START=.30; ROLL_END=1.78
OUT_START=4.80; OUT_END=5.64; ENTRY_Y=22; EXIT_Y=-18

def prepare(directory,text):
    f=font(directory,DIGIT_ROLE,DIGIT_SIZE)
    u=UNIT_SIZE
    while True:
        uf=font(directory,UNIT_ROLE,u); ub=uf.getbbox(text,anchor='ls'); uw=ub[2]-ub[0]
        if uw<=UNIT_MAX_WIDTH or u==UNIT_MIN: break
        u-=1
    if uw>UNIT_MAX_WIDTH:raise ValueError('Unit text too long')
    total=DIGITS*DIGIT_CELL+(UNIT_GAP+uw if text else 0)
    x0=round(CENTER_X-total/2); ux=x0+DIGITS*DIGIT_CELL+UNIT_GAP-ub[0]
    # Pre-render complete shaped numerals to local masks. No horizontal stretch or clipped glyphs.
    masks={}; bbs={}
    for d in range(10):
        s=str(d); bb=f.getbbox(s,anchor='ls',features=['tnum']); bbs[d]=list(bb)
        if bb[2]-bb[0]>DIGIT_CELL:raise ValueError('Cell too narrow')
        im=Image.new('L',(DIGIT_CELL,WINDOW_BOTTOM-WINDOW_TOP),0)
        xx=round((DIGIT_CELL-bb[0]-bb[2])/2)
        ImageDraw.Draw(im).text((xx,BASELINE_Y-WINDOW_TOP),s,font=f,anchor='ls',features=['tnum'],fill=255)
        masks[d]=im
    ys=np.arange(WINDOW_TOP,WINDOW_BOTTOM); feather=np.minimum(np.minimum((ys-WINDOW_TOP)/FEATHER,(WINDOW_BOTTOM-1-ys)/FEATHER),1).clip(0,1)
    unit=Image.new('RGBA',(W,H),(0,0,0,0)); ImageDraw.Draw(unit).text((ux,BASELINE_Y),text,font=uf,anchor='ls',fill=(*UNIT_RGB,255))
    def render(t):
        e=ease(t,IN_START,IN_END); o=ease(t,OUT_START,OUT_END); progress=ease(t,ROLL_START,ROLL_END)
        v=NUMBER_FROM+(NUMBER_TO-NUMBER_FROM)*progress
        alpha=e*(1-o); dy=round(ENTRY_Y*(1-e)+EXIT_Y*o)
        a=Image.new('RGBA',(W,H),(0,0,0,0)); digit_layer=Image.new('L',(W,H),0)
        for i in range(DIGITS):
            factor=10**(DIGITS-1-i); whole=math.floor(v/factor+1e-9); d=whole%10
            phase=clamp(v-factor*whole-(factor-1)); phase=smooth(phase)
            if progress>=1: phase=0; d=(NUMBER_TO//factor)%10
            local=Image.new('L',(DIGIT_CELL,WINDOW_BOTTOM-WINDOW_TOP),0)
            # Two full glyphs on one rotating vertical track; clip only in vertical viewport.
            offset=round(-DIGIT_PITCH*phase); local.paste(masks[d],(0,offset)); local.paste(masks[(d+1)%10],(0,offset+DIGIT_PITCH)) if offset+DIGIT_PITCH<local.height else None
            # Paste with max union, not overwrite when the two glyph image canvases overlap.
            aa=np.zeros((local.height,DIGIT_CELL),np.uint8)
            for dd,off in [(d,offset),((d+1)%10,offset+DIGIT_PITCH)]:
                lo=max(0,off); hi=min(local.height,off+local.height)
                if hi>lo: aa[lo:hi]=np.maximum(aa[lo:hi],np.asarray(masks[dd])[lo-off:hi-off])
            # Only feather while moving, exact crisp coverage at rest.
            if phase>1e-6 and phase<1-1e-6: aa=np.rint(aa*feather[:,None]).astype(np.uint8)
            digit_layer.paste(Image.fromarray(aa),(x0+i*DIGIT_CELL,WINDOW_TOP+dy))
        a.paste((*DIGIT_RGB,255),(0,0,W,H),digit_layer)
        # paste onto transparent with a mask stores premultiplied RGB; replace RGB/alpha for straight representation.
        digit=np.array(digit_layer); arr=np.zeros((H,W,4),np.uint8); arr[:,:,:3]=DIGIT_RGB; arr[:,:,3]=digit
        layer=Image.fromarray(arr,'RGBA'); layer.alpha_composite(unit,dest=(0,dy)); out=np.array(layer); out[:,:,3]=np.rint(out[:,:,3]*alpha).astype(np.uint8)
        return out
    return render,{'unit':text,'number_from':NUMBER_FROM,'number_to':NUMBER_TO,'digits':DIGITS,'digit_size':DIGIT_SIZE,'digit_cell_px':DIGIT_CELL,'digit_pitch_px':DIGIT_PITCH,'digit_x0':x0,'baseline_y':BASELINE_Y,'unit_baseline_xy':[ux,BASELINE_Y],'unit_size':u,'unit_ink_bbox':list(ub),'digit_ink_bboxes':bbs,'hold_seconds':[ROLL_END,OUT_START],'window_top_bottom':[WINDOW_TOP,WINDOW_BOTTOM],'digit_rgba':[*DIGIT_RGB,255],'unit_rgba':[*UNIT_RGB,255]}

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

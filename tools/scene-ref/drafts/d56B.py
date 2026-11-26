#!/usr/bin/env python3
import argparse, hashlib, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
W,H,FPS,N=1920,1080,25,150
SEED=0 # no random operations
FONT_FILES={'bold': ('InterDisplay-Bold.otf', 'd7a7394984bbb3bc3c3e507a6618d34e006c94f27c7105d0ef4376667aeaf4ee'), 'medium': ('InterDisplay-Medium.otf', '9a17712bbd6da13b7ce9beb056d19fe15648799a11d5c35f8086a1270a0d13b2'), 'regular': ('InterDisplay-Regular.otf', '35c1f7437dd6d63f4e60a49144df4b4e4edc190e13ffc1e54b86083747fe3acf'), 'serif': ('NotoSerif-Regular.ttf', '9d7583b7dc9e812afd32a14280c5cac3160012efe50c8d08938f4fea266ff67f')}
TEST_TEXT="Ă Â Î Ș Ț"
# Settings: reference px, seconds. Editorial quotation, no stock quotation or attributed real person.
DEFAULT_TEXT="The best stories\nmake us look closer.";SECONDARY="FROM THE FIELD NOTES"
INK=(245,240,229); ACCENT=(190,209,190); SIZE=112; CREDIT_SIZE=33; X=286; TOP=330; LINE_GAP=152
ENTRY=(0.12,0.80); LINE_STAGGER=.32; LINE_DUR=.76; CREDIT=(1.24,1.94); EXIT=(4.94,5.76)
# Each intact line emerges through its own stationary aperture; mask clips only during reveal.
def prepare(fd,text,secondary):
    text=text or DEFAULT_TEXT; credit=secondary or SECONDARY;lines=text.split('\n')
    if not 1<=len(lines)<=3:raise ValueError('1 to 3 lines')
    tiles=[];meta=[]
    for i,l in enumerate(lines):
        tile,m=texttile(fd,l,'serif',SIZE,INK,1330);tiles.append(tile);meta.append(dict(id=f'line_{i+1}',**m,x=X,y=TOP+i*LINE_GAP,color=INK))
    quote,q=texttile(fd,'“','serif',140,ACCENT,180);ct,c=texttile(fd,credit,'medium',CREDIT_SIZE,ACCENT,1240)
    cy=TOP+len(lines)*LINE_GAP+46;meta += [dict(id='quote_mark',**q,x=X-105,y=TOP-26,color=ACCENT),dict(id='credit',**c,x=X+84,y=cy,color=ACCENT),dict(id='credit_rule',type='shape',x=X,y=cy+ct.height//2,width=58,height=3,color=ACCENT)]
    hold=max(CREDIT[1],ENTRY[0]+(len(lines)-1)*LINE_STAGGER+LINE_DUR)
    def render(t):
        out=Image.new('RGBA',(W,H));o=ease(t,*EXIT);a=1-o; dy=round(-14*o);e=ease(t,*ENTRY)
        paste(out,quote,X-105,TOP-26+dy,e*a)
        for i,im in enumerate(tiles):
            start=ENTRY[0]+i*LINE_STAGGER;p=ease(t,start,start+LINE_DUR);offset=round((im.height+10)*(1-p));vis=max(0,im.height-offset)
            if vis:paste(out,im.crop((0,0,im.width,vis)),X,TOP+i*LINE_GAP+offset+dy,a)
        p=ease(t,*CREDIT);rule=Image.new('RGBA',(58,3));ImageDraw.Draw(rule).rectangle((0,0,round(57*p),2),fill=(*ACCENT,round(255*p)))
        paste(out,rule,X,cy+ct.height//2+dy,a);paste(out,ct,X+84,cy+dy+round(10*(1-p)),p*a)
        return out
    return render,dict(text=text,secondary=credit,actors=meta,entry=ENTRY,line_stagger=LINE_STAGGER,line_duration=LINE_DUR,credit= CREDIT,exit=EXIT,hold_seconds=[hold,EXIT[0]],line_gap=LINE_GAP,mask='stationary lower aperture boundary, integer clipped rows',colors=dict(ink=INK,accent=ACCENT))

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

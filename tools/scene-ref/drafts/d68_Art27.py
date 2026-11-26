#!/usr/bin/env python3
"""Independent editable reference approximation. No vendor rendering code."""
import argparse, hashlib, io, json, pathlib, subprocess, zipfile
from PIL import Image, ImageDraw, ImageFont
ROOT=pathlib.Path(__file__).resolve().parents[1]
# All design numbers are expressed in 288x162 thumbnail coordinates, multiplied by W/288.
P={'size': 88.004, 'tracking': 13.601, 'cx': 145.511, 'cy': 80.744, 'offset': [0, 0], 'stroke': 7.17864, 'face': [170, 74, 55], 'back': [255, 255, 255], 'family': 'Montserrat', 'style': 'ExtraBold Italic', 'upstream': 'https://github.com/JulietaUla/Montserrat', 'member': '1_Art_Title_27/Font/6587146_Montserrat ExtraBold Italic.ttf', 'font_sha256': '0336d05365d450e7baf8e336428daeb0571d63da843312d1703c96015c0afbec'}
W,H,FPS,FRAMES=1920,1080,25,125
S=W/288
BG=(39,49,58,255)
def font_bytes(args):
    if args.font: b=pathlib.Path(args.font).read_bytes()
    else:
        with zipfile.ZipFile(args.source_zip) as z: b=z.read(P['member'])
    if hashlib.sha256(b).hexdigest()!=P['font_sha256']: raise ValueError('Exact reference font SHA256 mismatch; no fallback')
    return b

def render(b,text='ART',tracking=None,max_width=0.9,scale=1.0,center=None):
    # The thumbnail sample has optical tracking; the source XML RowSpace is 0.
    # Set tracking=0 for a normal shaped editable run. For tracked Latin use prefix advances.
    if not text or '\n' in text: raise ValueError('One nonempty line required')
    tr=P['tracking'] if tracking is None else tracking
    cx,cy=center or (P['cx']*S,P['cy']*S)
    def masks(factor):
        font=ImageFont.truetype(io.BytesIO(b),max(1,round(P['size']*S*factor)))
        track=tr*S*factor
        if abs(track)<1e-7:
            runs=[(text,0.)]
        else:
            runs=[(ch,float(font.getlength(text[:i]))+i*track) for i,ch in enumerate(text)]
        boxes=[(x+font.getbbox(t)[0],font.getbbox(t)[1],x+font.getbbox(t)[2],font.getbbox(t)[3]) for t,x in runs]
        l=min(q[0] for q in boxes); r=max(q[2] for q in boxes); t=min(q[1] for q in boxes); bot=max(q[3] for q in boxes)
        return font,runs,(l,t,r,bot)
    f,runs,box=masks(scale)
    for _ in range(5):
        span=box[2]-box[0]+2*(P['stroke']+abs(P['offset'][0]))*S*scale
        if span<=W*max_width:break
        scale*=W*max_width/span;f,runs,box=masks(scale)
    def mask(stroke=0,dx=0,dy=0):
        m=Image.new('L',(W,H));d=ImageDraw.Draw(m)
        for t,x in runs:d.text((round(cx+x-(box[0]+box[2])/2+dx),round(cy-(box[1]+box[3])/2+dy)),t,font=f,fill=255,stroke_width=round(stroke),stroke_fill=255)
        return m
    front=mask(); back=mask(P['stroke']*S*scale,P['offset'][0]*S*scale,P['offset'][1]*S*scale)
    result=Image.new('RGBA',(W,H),(0,0,0,0))
    for col,a in [(P['back'],back),(P['face'],front)]:
        layer=Image.new('RGBA',(W,H),tuple(col)+(255,));layer.putalpha(a);result=Image.alpha_composite(result,layer)
    return result,dict(font_px=f.size,tracking_px=tr*S*scale,stroke_px=round(P['stroke']*S*scale),offset_px=[v*S*scale for v in P['offset']],center_px=[cx,cy],ink_bbox=list(result.getbbox()),scale=scale)

def composite(im,bg=BG):return Image.alpha_composite(Image.new('RGBA',im.size,bg),im).convert('RGB')
def commands(out):
    return [ ['ffmpeg','-hide_banner','-loglevel','error','-y','-loop','1','-framerate','25','-i','preview.png','-frames:v',str(FRAMES),'-an','-c:v','libx264','-preset','fast','-crf','16','-threads','2','-pix_fmt','yuv420p','-movflags','+faststart','-map_metadata','-1','preview.mp4'], ['ffmpeg','-hide_banner','-loglevel','error','-y','-loop','1','-framerate','25','-i','alpha.png','-frames:v',str(FRAMES),'-an','-c:v','ffv1','-level','3','-threads','2','-pix_fmt','bgra','-map_metadata','-1','alpha.mkv'] ]
def main():
    ap=argparse.ArgumentParser();g=ap.add_mutually_exclusive_group(required=True);g.add_argument('--font');g.add_argument('--source-zip');ap.add_argument('--out',default=str(ROOT/'output'));ap.add_argument('--text',default='ART');ap.add_argument('--tracking',type=float);ap.add_argument('--scale',type=float,default=1);args=ap.parse_args(); b=font_bytes(args);out=pathlib.Path(args.out);out.mkdir(parents=True,exist_ok=True)
    im,metrics=render(b,args.text,args.tracking,scale=args.scale);im.save(out/'alpha.png');composite(im).save(out/'preview.png')
    # Alternate text proofs are separate, not baked into the primary style. No artificial motion.
    ro,rom=render(b,'Ă Â Î Ș Ț',0,scale=.65,center=(W/2,H/2));ro.save(out/'ROMANIAN_ALPHA.png');composite(ro).save(out/'ROMANIAN.png')
    tx,tm=render(b,'Text',0,scale=.5,center=(W/2,H/2));composite(tx).save(out/'EDITABLE_TEXT.png')
    for name,bg in [('BLACK',(0,0,0,255)),('WHITE',(255,255,255,255))]:composite(im,bg).save(out/(name+'.png'))
    (out/'GEOMETRY.json').write_text(json.dumps(dict(sample=metrics,romanian=rom,alternate=tm),indent=2))
    for cmd in commands(out):subprocess.run(cmd,cwd=out,check=True)
    (out/'ENCODE_COMMANDS.json').write_text(json.dumps(commands(out),indent=2))
if __name__=='__main__':main()

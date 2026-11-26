#!/usr/bin/env python3
"""Independent reference reconstruction: measured vector geometry and editable shaped text.
No vendor renderer, baked title pixels, or original font binary is shipped."""
import argparse, pathlib, hashlib, zipfile, io, json, math, subprocess, functools
import numpy as np
from PIL import Image, ImageFont, ImageDraw, ImageFilter, ImageChops
from romanian import proof_font,TARGET
ROOT=pathlib.Path(__file__).resolve().parents[1]; W,H,FPS,N=1920,1080,25,125; BG=(35,42,53,255)
P={
 '69A_FILM_FRAME_03':dict(key='FilmFrame03',text='FILM FRAME',sub='',size=130,tracking=46,center=[1004,574],maxw=1190,face=[12,8,7],graphic=[255,222,0],graphic_center=[960,574],graphic_scale=1.0,in_frames=[10,28],out_frames=[97,114],hold=[28,96]),
 '69B_CLEAN_TITLE_14':dict(key='Clean14',text='TITLE HERE',sub=' COPYWRITING',size=235,tracking=0,center=[970,529],maxw=1330,face=[255,255,255],sub_size=48,sub_tracking=23,sub_center=[922,681],sub_maxw=650,sub_face=[0,0,0],graphic=[255,237,0],graphic_center=[923,681],graphic_scale=1.4155,in_frames=[0,25],sub_in=[38,51],hold=[51,124]),
 '69C_LOWER_THIRDS_22':dict(key='Art30',text='YOUR TEXT HERE',sub='',size=56,tracking=0,center=[960,924],maxw=690,face=[26,26,26],graphic=[255,242,6],graphic_center=[965,918],graphic_scale=.4977,typing_in=[20,35],typing_out=[96,111],hold=[35,101]),
 '69D_BASIC_FONT_24':dict(key='Basic24',text='Simple layout',sub='Perfect clean Modern     serif font.',size=281,tracking=0,center=[960,482],maxw=1440,face=[255,255,255],sub_size=42,sub_tracking=10,sub_center=[885,743],sub_maxw=1276,sub_face=[255,255,255],in_frames=[0,13],sub_in=[7,20],shadow=[128,128,128],shadow_alpha=121,shadow_blur=7,shadow_offset=[1,1],hold=[20,124]),
 '69E_OLD_TEXTURE':dict(key='OldTexture',text='play Video game',sub='',size=270,tracking=0,center=[960,540],maxw=1600,face=[251,228,74],stroke=5,shadow_offset=[4,4],blur=.8,seed=6805,noise_sigma=7.0,hold=[0,124]),
}
ID=ROOT.name.removesuffix('_v1'); CFG=P[ID]
sha=lambda b:hashlib.sha256(b).hexdigest()
def clamp(x):return min(1,max(0,x))
def sm(x):x=clamp(x);return x*x*(3-2*x)
def easeout(x):return 1-(1-clamp(x))**3
def frac(n,a,b):return clamp((n-a)/(b-a))
def bez(a,b,c,d,t):return (1-t)**3*a+3*(1-t)**2*t*b+3*(1-t)*t*t*c+t**3*d
def cubic_ease(x,o,i):
 def val(a):return float(a[0] if isinstance(a,list) else a)
 x1,y1,x2,y2=val(o['x']),val(o['y']),val(i['x']),val(i['y']);lo,hi=0.,1.
 for _ in range(28):
  t=(lo+hi)/2
  if bez(0,x1,x2,1,t)<x:lo=t
  else:hi=t
 return bez(0,y1,y2,1,(lo+hi)/2)
def interp(keys,n):
 if n<=keys[0]['t']:return keys[0]['s'],keys[0]['s'],0
 for a,b in zip(keys,keys[1:]):
  if n<=b['t']:return a['s'],b['s'],cubic_ease(frac(n,a['t'],b['t']),a['o'],a['i'])
 return keys[-1]['s'],keys[-1]['s'],0

def font_data(folder,override=None):
 lock=json.loads((ROOT/'fonts/ACQUISITION.json').read_text()); b=pathlib.Path(override).read_bytes() if override else zipfile.ZipFile(folder/lock['archive']).read(lock['member'])
 if override and ID=='69C_LOWER_THIRDS_22':return b,sha(b),None
 if sha(b)!=lock['sha256']:raise ValueError('Wrong exact font binary')
 return b,lock['sha256'],lock

def make_text(b,text,size,tracking,maxw):
 if not text:return Image.new('L',(1,1)),[],dict(font_px=size,tracking_px=tracking,ink_size=[1,1])
 # Preserve shaped advances and avoid raster stretch. Scale both size and tracking uniformly for long edits.
 fct=1.;runs=[]
 for _ in range(6):
  f=ImageFont.truetype(io.BytesIO(b),max(1,round(size*fct))); tr=tracking*fct
  runs=[(ch,float(f.getlength(text[:i]))+i*tr) for i,ch in enumerate(text)]
  boxes=[(x+f.getbbox(ch)[0],f.getbbox(ch)[1],x+f.getbbox(ch)[2],f.getbbox(ch)[3]) for ch,x in runs]; l=math.floor(min(q[0] for q in boxes));r=math.ceil(max(q[2] for q in boxes));t=min(q[1] for q in boxes);bt=max(q[3] for q in boxes)
  if r-l<=maxw:break
  fct*=maxw/(r-l)
 # zero tracking static text uses whole shaped run; animated runs retain prefix advances.
 ww=max(1,r-l+24);hh=max(1,bt-t+24); masks=[]
 for ch,x in runs:
  m=Image.new('L',(ww,hh));ImageDraw.Draw(m).text((round(x-l+12),12-t),ch,font=f,fill=255);masks.append(m)
 full=Image.new('L',(ww,hh));
 if abs(tr)<1e-8:ImageDraw.Draw(full).text((12-l,12-t),text,font=f,fill=255)
 else:
  for m in masks:full=ImageChops.lighter(full,m)
 box=full.getbbox() or (0,0,ww,hh)
 # retain margin 12 for texture, shadow and stroke.
 center=((box[0]+box[2])/2,(box[1]+box[3])/2)
 return full,masks,dict(font_px=f.size,tracking_px=tr,ink_size=[box[2]-box[0],box[3]-box[1]],anchor=list(center),text=text)

def layer(canvas,m,col,cx,cy,anchor=None,opacity=1.,dy=0,clip=None):
 if opacity<=0:return
 if opacity<1:m=m.point(lambda v:round(v*opacity))
 if clip is not None:
  m=m.copy();d=ImageDraw.Draw(m);d.rectangle((0,0,m.width,clip[0]-1),fill=0) if clip[0]>0 else None;d.rectangle((0,clip[1],m.width,m.height),fill=0) if clip[1]<m.height else None
 a=anchor or (m.width/2,m.height/2);im=Image.new('RGBA',m.size,tuple(col)+(255,));im.putalpha(m);canvas.alpha_composite(im,(round(cx-a[0]),round(cy-a[1]+dy)))

def merge_chars(full,chars,weights):
 if all(x>=1 for x in weights):return full
 m=Image.new('L',full.size)
 for c,a in zip(chars,weights):
  if a>0:m=ImageChops.lighter(m,c if a>=1 else c.point(lambda v:round(v*a)))
 return m

class Scene:
 def __init__(self,b,digest,text,sub,romanian=False):
  self.p=CFG;self.text=text;self.sub=sub;self.geom=None
  if romanian and CFG['key']!='Art30':b,self.recipes=proof_font(b,CFG['key'],digest,text+sub+TARGET+' ')
  else:self.recipes=[]
  self.main,self.chars,self.mm=make_text(b,text,CFG['size'],CFG['tracking'],CFG['maxw']); self.subm,self.subchars,self.sm=make_text(b,sub,CFG.get('sub_size',30),CFG.get('sub_tracking',0),CFG.get('sub_maxw',1000))
  if (ROOT/'inputs/GRAPHIC_GEOMETRY.json').exists():self.geom=json.loads((ROOT/'inputs/GRAPHIC_GEOMETRY.json').read_text())
  if (ROOT/'inputs/LOTTIE_GEOMETRY.json').exists():self.lottie=json.loads((ROOT/'inputs/LOTTIE_GEOMETRY.json').read_text())
  if ID=='69E_OLD_TEXTURE':
   m=self.main;self.edge=m.filter(ImageFilter.MaxFilter(2*CFG['stroke']+1));rng=np.random.default_rng(CFG['seed']);g=rng.normal(0,CFG['noise_sigma'],(m.height,m.width)); a=np.asarray(m.filter(ImageFilter.GaussianBlur(CFG['blur'])));rgb=np.clip(np.array(CFG['face'])[None,None,:]+g[:,:,None],0,255).astype('uint8');self.texture=Image.fromarray(np.dstack([rgb,a]),'RGBA')
 def graphic(self,c,n):
  p=self.p;g=self.geom;f=g['frames'][min(n,len(g['frames'])-1)];w,h=g['source_size'];sc=p['graphic_scale'];cx,cy=p['graphic_center'];m=Image.new('L',(round(w*sc),round(h*sc)));d=ImageDraw.Draw(m)
  for poly in f['polygons']:d.polygon([(round(x*sc),round(y*sc)) for x,y in poly],fill=round(f['opacity']*255))
  if ID=='69A_FILM_FRAME_03' and (n<8 or n>116):m=m.filter(ImageFilter.GaussianBlur(4))
  layer(c,m,p['graphic'],cx,cy)
 def lower_graphic(self,c,n):
  g=self.lottie;a,b,t=interp(g['path_keys'],n);a,b=a[0],b[0];v=np.array(a['v'])*(1-t)+np.array(b['v'])*t;ii=np.array(a['i'])*(1-t)+np.array(b['i'])*t;oo=np.array(a['o'])*(1-t)+np.array(b['o'])*t
  a,b,t=interp(g['scale_keys'],n);s=(a[0]*(1-t)+b[0]*t)/100*.43*.5*0.988
  if s<=0:return
  # actual path cubic controls; source composition fitted to measured 910px hold width.
  pts=[]
  for j in range(len(v)):
   k=(j+1)%len(v)
   for u in np.linspace(0,1,20,endpoint=False):pts.append(bez(v[j],v[j]+oo[j],v[k]+ii[k],v[k],float(u))*s)
  pts=np.array(pts);center=np.array(self.p['graphic_center']);xy=pts+center+np.array([1.5,-.134])*s
  d=ImageDraw.Draw(c);d.polygon([tuple(q) for q in xy],fill=tuple(self.p['graphic'])+(255,))
 def frame(self,n):
  p=self.p;c=Image.new('RGBA',(W,H));m=self.main;cx,cy=p['center'];an=self.mm['anchor'];scx,scy=p.get('sub_center',[0,0]);sa=self.sm.get('anchor');col=p['face'];
  if ID=='69A_FILM_FRAME_03':
   self.graphic(c,n);a=sm(frac(n,*p['in_frames']));z=sm(frac(n,*p['out_frames']));dy=(self.mm['ink_size'][1]+8)*(1-a)-(self.mm['ink_size'][1]+8)*z
   # moving glyphs masked by their final lower ink boundary: reveal from bottom then exit up.
   if 10<=n<114:
    temp=Image.new('RGBA',(W,H));layer(temp,m,col,cx,cy,an,dy=dy);temp.paste((0,0,0,0),(0,0,W,round(cy-self.mm['ink_size'][1]/2-4)));temp.paste((0,0,0,0),(0,round(cy+self.mm['ink_size'][1]/2+4),W,H));c.alpha_composite(temp)
  elif ID=='69B_CLEAN_TITLE_14':
   self.graphic(c,n);weights=[sm(frac(n,2+1.5*i,10+1.5*i)) for i in range(len(self.chars))];layer(c,merge_chars(m,self.chars,weights),col,cx,min(cy,640-self.mm['ink_size'][1]/2),an)
   if n>=38:
    q=sm(frac(n,*p['sub_in']));weights=[sm(frac(q,abs(i-(len(self.subchars)-1)/2)/max(1,len(self.subchars))*.9,abs(i-(len(self.subchars)-1)/2)/max(1,len(self.subchars))*.9+.5)) for i in range(len(self.subchars))];layer(c,merge_chars(self.subm,self.subchars,weights),p['sub_face'],scx,scy,sa)
  elif ID=='69C_LOWER_THIRDS_22':
   self.lower_graphic(c,n);q=frac(n,*p['typing_in'])*(1-frac(n,*p['typing_out']));num=round(q*len(self.chars));weights=[1 if i<num else 0 for i in range(len(self.chars))];layer(c,merge_chars(m,self.chars,weights),col,cx,cy,an)
  elif ID=='69D_BASIC_FONT_24':
   q=easeout(frac(n,*p['in_frames']));dy=100*(1-q);op=sm(frac(n,0,5));layer(c,m.filter(ImageFilter.GaussianBlur(p['shadow_blur'])),p['shadow'],cx+1,cy+1,an,op*p['shadow_alpha']/255,dy);layer(c,m,col,cx,cy,an,op,dy)
   weights=[sm(frac(n,7+8*i/max(1,len(self.subchars)),12+8*i/max(1,len(self.subchars)))) for i in range(len(self.subchars))];s=merge_chars(self.subm,self.subchars,weights);layer(c,s.filter(ImageFilter.GaussianBlur(7)),p['shadow'],scx+1,scy+1,sa,121/255);layer(c,s,p['sub_face'],scx,scy,sa)
  else:
   dx,dy=p['shadow_offset'];layer(c,self.edge,[0,0,0],cx+dx,cy+dy,an);layer(c,self.edge,[0,0,0],cx,cy,an);c.alpha_composite(self.texture,(round(cx-an[0]),round(cy-an[1])))
  return c

def comp(im,bg=BG):return Image.alpha_composite(Image.new('RGBA',im.size,bg),im).convert('RGB')
def command(out):return ['ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgba','-s','1920x1080','-r','25','-i','-','-filter_complex','[0:v]split=2[a][b];color=c=0x232a35:s=1920x1080:r=25[bg];[bg][a]overlay=format=rgb:shortest=1,format=yuv420p[v]','-map','[v]','-frames:v',str(N),'-c:v','libx264','-threads','2','-preset','fast','-crf','16','-movflags','+faststart','-map_metadata','-1','preview.mp4','-map','[b]','-frames:v',str(N),'-c:v','ffv1','-level','3','-threads','2','-pix_fmt','bgra','-map_metadata','-1','alpha.mkv']
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--archive-dir',type=pathlib.Path,required=True);ap.add_argument('--font');ap.add_argument('--out',type=pathlib.Path,default=ROOT/'output');ap.add_argument('--text',default=CFG['text']);ap.add_argument('--sub',default=CFG['sub']);ap.add_argument('--only-still',action='store_true');a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True);b,digest,_=font_data(a.archive_dir,a.font)
 s=Scene(b,digest,a.text,a.sub,any(c in TARGET for c in a.text+a.sub));r=Scene(b,digest,'Ă Â Î Ș Ț ă â î ș ț','Ă Â Î Ș Ț' if CFG['sub'] else '',True);hold=70;im=s.frame(hold);rom=r.frame(hold);im.save(a.out/'HOLD_ALPHA.png');comp(im).save(a.out/'HOLD.png');rom.save(a.out/'ROMANIAN_ALPHA.png');comp(rom).save(a.out/'ROMANIAN.png');comp(im,(0,0,0,255)).save(a.out/'BLACK.png');comp(im,(255,255,255,255)).save(a.out/'WHITE.png');(a.out/'GEOMETRY.json').write_text(json.dumps(dict(settings=CFG,main=s.mm,subtitle=s.sm,romanian=r.mm,romanian_subtitle=r.sm,glyph_recipes=r.recipes),ensure_ascii=False,indent=2));
 if a.only_still:return
 cmd=command(a.out);(a.out/'ENCODE_COMMAND.json').write_text(json.dumps(cmd,indent=2));p=subprocess.Popen(cmd,stdin=subprocess.PIPE,cwd=a.out)
 try:
  last=None
  for n in range(N):
   if ID=='69E_OLD_TEXTURE':frame=im
   elif ID!='69C_LOWER_THIRDS_22' and CFG['hold'][0]<=n<=CFG['hold'][1] and last is not None:frame=last
   else:frame=s.frame(n)
   if n==CFG['hold'][0]:last=frame
   p.stdin.write(frame.tobytes())
 finally:p.stdin.close()
 if p.wait()!=0:raise RuntimeError('encode failed')
if __name__=='__main__':main()

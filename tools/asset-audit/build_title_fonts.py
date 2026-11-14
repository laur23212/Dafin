"""Build Dafin font packages (title-fonts/<id>/family.json + faces + OFL.txt) for the ChatGPT titles.

Static upstream files are copied byte for byte. Variable fonts (only families without a Reserved
Font Name) are cut into static weights with fontTools and named so family + style name resolve
the face, the way Fonts Essentials' static files do."""
import json, os, shutil, sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

SRC, OUT = sys.argv[1], sys.argv[2]
STYLE = {400: 'Regular', 500: 'Medium', 600: 'SemiBold', 700: 'Bold', 800: 'ExtraBold'}
# family, id, category, source dir, ("static", [(file, weight)]) or ("var", file, axes, [weights])
FAMILIES = [
    ('Amiri', 'amiri', 'editorial', 'amiri', ('static', [('Amiri-Regular.ttf', 400), ('Amiri-Bold.ttf', 700)])),
    ('Andika', 'andika', 'clean', 'andika', ('static', [('Andika-Regular.ttf', 400), ('Andika-Bold.ttf', 700)])),
    ('Cabin', 'cabin', 'clean', 'cabin', ('var', 'Cabin[wdth,wght].ttf', {'wdth': 100}, [400, 700])),
    ('Charis SIL', 'charis-sil', 'editorial', 'charissil', ('static', [('CharisSIL-Regular.ttf', 400), ('CharisSIL-Bold.ttf', 700)])),
    ('Comic Neue', 'comic-neue', 'playful', 'comicneue', ('static', [('ComicNeue-Regular.ttf', 400), ('ComicNeue-Bold.ttf', 700)])),
    ('Cousine', 'cousine', 'clean', 'cousine', ('static', [('Cousine-Regular.ttf', 400), ('Cousine-Bold.ttf', 700)])),
    ('EB Garamond', 'eb-garamond', 'editorial', 'ebgaramond', ('var', 'EBGaramond[wght].ttf', {}, [400, 500, 700])),
    ('GFS Didot', 'gfs-didot', 'editorial', 'gfsdidot', ('static', [('GFSDidot-Regular.ttf', 400)])),
    ('GFS Neohellenic', 'gfs-neohellenic', 'clean', 'gfsneohellenic', ('static', [('GFSNeohellenic.ttf', 400), ('GFSNeohellenicBold.ttf', 700)])),
    ('Gentium Book Plus', 'gentium-book-plus', 'editorial', 'gentiumbookplus', ('static', [('GentiumBookPlus-Regular.ttf', 400), ('GentiumBookPlus-Bold.ttf', 700)])),
    ('Inter Display', 'inter-display', 'impact', 'inter', ('var', 'Inter[opsz,wght].ttf', {'opsz': 32}, [400, 700, 800])),
    ('Lato', 'lato', 'clean', 'lato', ('static', [('Lato-Regular.ttf', 400), ('Lato-Bold.ttf', 700)])),
    ('Lobster Two', 'lobster-two', 'playful', 'lobstertwo', ('static', [('LobsterTwo-Regular.ttf', 400), ('LobsterTwo-Bold.ttf', 700)])),
    ('Noto Sans', 'noto-sans', 'clean', 'notosans', ('var', 'NotoSans[wdth,wght].ttf', {'wdth': 100}, [400, 500, 700])),
    ('Noto Serif Display', 'noto-serif-display', 'editorial', 'notoserifdisplay', ('var', 'NotoSerifDisplay[wdth,wght].ttf', {'wdth': 100}, [400, 500, 700])),
    ('Open Sans Condensed', 'open-sans-condensed', 'impact', 'opensans', ('var', 'OpenSans[wdth,wght].ttf', {'wdth': 75}, [400, 700])),
    ('PT Mono', 'pt-mono', 'clean', 'ptmono', ('static', [('PTM55FT.ttf', 400)])),
    ('PT Sans', 'pt-sans', 'clean', 'ptsans', ('static', [('PT_Sans-Web-Regular.ttf', 400), ('PT_Sans-Web-Bold.ttf', 700)])),
    ('PT Sans Narrow', 'pt-sans-narrow', 'impact', 'ptsansnarrow', ('static', [('PT_Sans-Narrow-Web-Regular.ttf', 400), ('PT_Sans-Narrow-Web-Bold.ttf', 700)])),
    ('PT Serif', 'pt-serif', 'editorial', 'ptserif', ('static', [('PT_Serif-Web-Regular.ttf', 400), ('PT_Serif-Web-Bold.ttf', 700)])),
    ('Roboto Condensed', 'roboto-condensed', 'impact', 'robotocondensed', ('var', 'RobotoCondensed[wght].ttf', {}, [400, 700, 800])),
    # Apache-2.0, not OFL: the licence file is LICENSE.txt (see LICENSE_FILE below)
    ('Roboto Slab', 'roboto-slab', 'editorial', 'robotoslab', ('var', 'RobotoSlab[wght].ttf', {}, [400, 700])),
    ('Noto Serif', 'noto-serif', 'editorial', 'notoserif', ('var', 'NotoSerif[wdth,wght].ttf', {'wdth': 100}, [400, 600, 700])),
    # IBM Plex declares the Reserved Font Name "Plex": the static upstream files are copied unchanged
    ('IBM Plex Mono', 'ibm-plex-mono', 'clean', 'ibmplexmono', ('static', [('IBMPlexMono-Regular.ttf', 400), ('IBMPlexMono-SemiBold.ttf', 600)])),
]
APACHE = {'roboto-slab'}
ONLY = set(os.environ.get('ONLY', '').split(',')) - {''}


def names(f):
    n = f['name']
    fam = n.getDebugName(16) or n.getDebugName(1)
    sty = n.getDebugName(17) or n.getDebugName(2)
    return fam, sty


def set_names(f, family, style, weight):
    n = f['name']
    ribbi = style in ('Regular', 'Bold')
    ps = (family + '-' + style).replace(' ', '')
    for rec in list(n.names):
        if rec.nameID in (1, 2, 3, 4, 6, 16, 17, 21, 22, 25):
            n.removeNames(nameID=rec.nameID)
    n.setName(family if ribbi else f'{family} {style}', 1, 3, 1, 0x409)
    n.setName(style if ribbi else 'Regular', 2, 3, 1, 0x409)
    n.setName(f'{ps};Dafin title-fonts', 3, 3, 1, 0x409)
    n.setName(f'{family} {style}', 4, 3, 1, 0x409)
    n.setName(ps, 6, 3, 1, 0x409)
    if not ribbi:
        n.setName(family, 16, 3, 1, 0x409)
        n.setName(style, 17, 3, 1, 0x409)
    os2 = f['OS/2']; os2.usWeightClass = weight
    sel = os2.fsSelection & ~0b1100001  # clear italic, bold, regular
    sel |= (1 << 5) if style == 'Bold' else (1 << 6) if style == 'Regular' else 0
    os2.fsSelection = sel | (1 << 7)  # USE_TYPO_METRICS kept as upstream sets it for GF fonts
    f['head'].macStyle = 1 if style == 'Bold' else 0
    if 'STAT' in f: del f['STAT']


total = 0
for family, fid, cat, srcdir, spec in FAMILIES:
    if ONLY and fid not in ONLY:
        continue
    d = os.path.join(OUT, fid); shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    faces = []
    if spec[0] == 'static':
        for fn, w in spec[1]:
            shutil.copy(os.path.join(SRC, srcdir, fn), d)
            fam, sty = names(TTFont(os.path.join(d, fn), lazy=True))
            assert fam == family, (fn, fam, family)
            faces.append({'file': fn, 'weight': w, 'styleName': sty})
    else:
        _, fn, axes, weights = spec
        for w in weights:
            f = TTFont(os.path.join(SRC, srcdir, fn))
            inst = instancer.instantiateVariableFont(f, dict(axes, wght=w), updateFontNames=False)
            style = STYLE[w]
            set_names(inst, family, style, w)
            out = f"{family.replace(' ', '')}-{style}.ttf"
            inst.save(os.path.join(d, out))
            faces.append({'file': out, 'weight': w, 'styleName': style})
    lic = 'LICENSE.txt' if fid in APACHE else 'OFL.txt'
    shutil.copy(os.path.join(SRC, srcdir, lic), d)
    json.dump({'id': fid, 'family': family, 'category': cat, 'license': 'Apache-2.0' if fid in APACHE else 'OFL-1.1', 'order': 0, 'faces': faces},
              open(os.path.join(d, 'family.json'), 'w'), indent=2)
    open(os.path.join(d, 'family.json'), 'a').write('\n')
    sz = sum(os.path.getsize(os.path.join(d, x)) for x in os.listdir(d)); total += sz
    print(f'{family:22} {len(faces)} faces {sz // 1024} KB', [(x['weight'], x['styleName']) for x in faces])
print('total', total // 1024, 'KB')

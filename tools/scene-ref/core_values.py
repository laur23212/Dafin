"""Value dump of scene_core for the native test (tests/tst_scenetitle.cpp, coreMatchesReference).

  python3 -I core_values.py <tools/scene-ref> <scene.json> [params JSON] > tests/data/scene-titles/core_*.txt

Prints the hold, every resolved actor, time_map at several clip lengths and the channels of every
actor at fixed times, in a fixed text format the native loader reproduces line for line.
"""
import sys, json
sys.path.insert(0, sys.argv[1])
import scene_core as core
sc = core.load_scene(sys.argv[2], json.loads(sys.argv[3]) if len(sys.argv) > 3 else None)
hs, he = core.scene_events(sc)
print("hold %.9f %.9f" % (hs, he))
for a in sc.actors: print("actor %s %s text=[%s] w=%.3f" % (a['id'], a['type'], a.get('text',''), a.get('width',1.0)))
for D in (6.0, 4.0, 10.0, 2.0):
    f = core.time_map(sc, D)
    for t in (0.0,0.3,0.9,1.6,3.0,4.9,5.5,5.99): print("tm %.2f %.2f %.9f" % (D, t, f(t)))
for a in sc.actors:
    for t in (0.0,0.2,0.4,0.6,0.8,1.0,1.5,2.0,4.7,4.8,5.0,5.2):
        c = core.channels(a, t); print("ch %s %.2f %.9f %.9f %.9f %.9f %.9f" % (a['id'], t, c['opacity'], c['wipe'], c['progress'], c['dx'], c['dy']))

from pathlib import Path
HERE=Path(__file__).resolve().parent
out=HERE/'ai_box'
out.mkdir(exist_ok=True)
text=(HERE/'check.py').read_text()
text=text.replace('ROOT=HERE.parents[1]','ROOT=HERE.parents[2]')
key="    (HERE/'reference_core.c').write_text(source,encoding='utf-8')"
code='''    helper=(HERE.parent/'ai_box_patch.txt').read_text()
    candidate=candidate.replace('static void __attribute__((optimize("O2"))) auto_avoid(',helper+'\\nstatic void __attribute__((optimize("O2"))) auto_avoid(',1)
    old='segment_box(ax,ay,bx,by,-16000,-16000,16000,16000)'
    # The helper's fallback must remain general; replace only auto_avoid.
    start=candidate.index('static void __attribute__((optimize("O2"))) auto_avoid(')
    candidate=candidate[:start]+candidate[start:].replace(old,'ai_bullet_segment_box(ax,ay,bx,by)',1)
'''
assert key in text
text=text.replace(key,code+key,1)
text=text.replace("'Only auto_avoid definition receives GCC optimize(\"O2\") attribute'", "'auto_avoid O2 plus exact fixed-square AI-only SAT with guarded general fallback'")
(out/'check.py').write_text(text)

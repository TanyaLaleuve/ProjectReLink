"""(lossnear) Ecrit les en-tetes generes du runtime ReLink (rt64, a64_suyu.h, a64_fpx.h) sans jeu, pour compiler host/ a
blanc sur la CI (.github/workflows/epreuves-lossnear.yml). Demande clang++ (build_emit.py construit la DLL du traducteur).

Usage : python -I lossnear/tests/en_tetes_runtime.py <depot ProjectReLink> <dossier de sortie>
Sortie : <sortie>/rt64 (a64.h ...) et <sortie>/gen (a64_suyu.h, a64_fpx.h).
"""
import os
import sys

repo, out = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.join(repo, "nx2"))
import kit  # noqa: E402  (pose les chemins d'a64-kit)

sys.path.insert(0, os.path.join(kit.KIT, "lift"))
import a64_rtgen  # noqa: E402
import suyu_rt_fixes  # noqa: E402,F401  (retouche a64_rtgen.runtime_h comme le fait lift.py)
import a64_lift  # noqa: E402
import rtgen  # noqa: E402

gen = os.path.join(out, "gen")
os.makedirs(gen, exist_ok=True)
a64_rtgen.write(gen)
with open(os.path.join(gen, "a64_fpx.h"), "w", newline="\n") as f:
    f.write(a64_lift.fpx_header(set()))
rtgen.write(os.path.join(out, "rt64"))
print("ecrit :", sorted(os.listdir(gen)), sorted(os.listdir(os.path.join(out, "rt64"))))

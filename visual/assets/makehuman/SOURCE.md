# Source of these files

From the MakeHuman project's public repository
(https://github.com/makehumancommunity/makehuman, branch master,
`makehuman/data/`), downloaded 2026-11-22:

| File | Original path |
|---|---|
| base.obj | 3dobjs/base.obj |
| default.mhskel, default_weights.mhw | rigs/ |
| asian-male-young.target, african-male-young.target, universal-male-young-maxmuscle-averageweight.target | targets/macrodetails/ |
| male-young-averagemuscle-averageweight-maxheight.target | targets/macrodetails/height/ |
| (added 2026-11-22, phase 2) caucasian-male-young, universal-male-young-averagemuscle-maxweight, universal-male-young-minmuscle-averageweight | targets/macrodetails/ |
| male-young-averagemuscle-averageweight-minheight | targets/macrodetails/height/ |
| head_head-oval / -square / -round / -fat-incr, nose_nose-scale-horiz-incr / -scale-vert-incr / -trans-up, chin_chin-width-incr / -prominent-incr, mouth_mouth-scale-horiz-incr, eyebrows_eyebrows-trans-down | targets/head, nose, chin, mouth, eyebrows (folder name kept as a prefix) |

Licence: the MakeHuman assets are released as **CC0 1.0** (see LICENSE.md,
section "C"). Only data files are used; the MakeHuman program (AGPL) is not
included or run. The `targets.npz` file of the PyPI package was NOT used
(it needs Python "pickle" loading, which can run code).

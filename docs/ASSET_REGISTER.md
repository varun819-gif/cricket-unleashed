# Asset Register

Every third-party asset (model, animation, texture, stadium, audio,
commentary voice, music, font, logo, likeness) must be listed here **before**
it is added to the project.

| Asset | Type | Source / vendor | Licence | Scope (platforms, regions) | Cost | Approved by | Date |
|---|---|---|---|---|---|---|---|
| National flag images: India, Pakistan, Australia, England, South Africa, New Zealand, Sri Lanka, Bangladesh, Afghanistan (`online/web/flags/*.svg`) | SVG images | flag-icons 7.2.3 (github.com/lipis/flag-icons), via the npm registry | MIT (licence text in `online/web/flags/LICENSE.txt`); the flags are national symbols | all | ₹0 | User decision "allow real country names and national flags" | 2026-11-20 |
| West Indies badge (`online/web/flags/WI.svg`) | SVG image | made by us (West Indies has no national flag) | ours | all | ₹0 | - | 2026-11-20 |
| MakeHuman base mesh, default skeleton + skin weights, 4 body-shape targets (`visual/assets/makehuman/`) | 3D human mesh, rig, shape data | MakeHuman project (github.com/makehumancommunity/makehuman, raw files) | **CC0 1.0** (see `visual/assets/makehuman/LICENSE.md`); no MakeHuman program code is used | all | ₹0 | User decision "start with free tools and assets" | 2026-11-22 |
| Bat, ball, stumps, pads, gloves, helmet, cap, pitch, grass, sky (`visual/blender/*.py`) | procedural 3D models / materials | made by us in Blender scripts | ours | all | ₹0 | - | 2026-11-22 |

## Current rules

- **Real country names and national flags are allowed** (user decision,
  2026-11-20): India, Pakistan, Australia, England, South Africa, New
  Zealand, Sri Lanka, Bangladesh, West Indies, Afghanistan
  (`NationalTeams()` in `core/src/TeamFactory.cpp`). Flags must come from a
  permitted source listed above.
- **Players stay fictional** (made-up names, no real faces or likenesses).
  The fictional club names ("Ganga Royals" etc.) remain for offline use.
- **No cricket board logos (BCCI, PCB, ECB, CWI ...), official kits,
  player faces or tournament branding (ICC events, IPL ...)** until a
  licence is signed and listed above. A national flag is not a board logo.
- Afghanistan uses the black-red-green flag that its cricket team uses at
  ICC events. This is a sensitive choice: see MISSING_DETAILS.md #27.
- **No copyrighted music** unless it is licensed. Stadium music will be
  commissioned or royalty-free with a commercial licence.
- Stadium entertainment / cheerleader performers: all must be adults (18+).
  Presentation stays tasteful sports entertainment with no sexualised camera
  framing, and users can switch it off.
- Asset purchases need explicit approval. No subscriptions or services have
  been bought.

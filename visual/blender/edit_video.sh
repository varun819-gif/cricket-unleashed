#!/bin/sh
# Joins the rendered shots of scene_v2.py into one video with simple captions.
#   visual/blender/edit_video.sh <frames_dir> <out.mp4>
# Needs ffmpeg (free). Captions use the DejaVu font that comes with Linux.
set -e
D="$1"
OUT="$2"
FONT=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf
TMP=$(mktemp -d)
cap() {  # shot, caption
  ffmpeg -y -loglevel error -framerate 25 -i "$D/$1_%04d.png" \
    -vf "drawtext=fontfile=$FONT:text='$2':x=28:y=h-56:fontsize=26:fontcolor=white:box=1:boxcolor=black@0.45:boxborderw=10" \
    -c:v libx264 -pix_fmt yuv420p -crf 19 "$TMP/$1.mp4"
  echo "file '$TMP/$1.mp4'" >> "$TMP/list.txt"
}
cap 1_broadcast "Pakistan to India - right-arm fast"
cap 2_bowler_slowmo "Delivery stride and release (half speed)"
cap 3_drive_slowmo "Front-foot cover drive - contact (quarter speed)"
cap 4_chase "Ball path from the C++ physics - FOUR"
cap 5_umpire "Umpire signals four"
ffmpeg -y -loglevel error -f concat -safe 0 -i "$TMP/list.txt" -c copy "$OUT"
rm -rf "$TMP"
echo "wrote $OUT"

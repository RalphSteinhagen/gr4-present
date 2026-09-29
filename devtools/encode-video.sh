#!/usr/bin/env bash
# Convert a video into the format the viewer can play: VP8 video with optional Vorbis audio, in a WebM container.
#
# The viewer decodes VP8 because it is royalty-free, because it is what browsers decode natively, and because a
# decoder-only build is about 214 kB against several MB for H.264 or AV1. Nothing else is supported, so anything else
# has to come through here first.
#
# Free-standing: needs ffmpeg and nothing from this repository.

set -euo pipefail

readonly kDefaultQuality=7  # 0 (worst) to 10 (best), kept from the old scale and mapped to a libvpx CRF below
readonly kDefaultAudioQuality=3
readonly kDefaultWidth=1280

usage() {
    cat <<'USAGE'
usage: encode-video.sh [options] <input> [output.webm]

Converts <input> to WebM. Without an output name, the input's name with .webm is used.

  -q <0-10>   video quality, default 7; higher is better and larger
  -a <0-10>   audio quality, default 3; ignored with -n
  -n          drop the audio track entirely
  -w <px>     scale to this width, preserving aspect, default 1280; 0 keeps the original
  -s <time>   start at this offset, e.g. 00:01:30
  -t <time>   encode only this duration, e.g. 10 or 00:00:10
  -h          show this

examples
  encode-video.sh talk-recording.mp4
  encode-video.sh -w 960 -q 8 -s 00:00:05 -t 20 big_buck_bunny.mp4 clip.webm
  encode-video.sh -n -w 640 screencast.mkv   # silent, small
USAGE
}

quality=$kDefaultQuality
audioQuality=$kDefaultAudioQuality
width=$kDefaultWidth
startAt=""
duration=""
withAudio=1

while getopts ":q:a:w:s:t:nh" option; do
    case "$option" in
        q) quality=$OPTARG ;;
        a) audioQuality=$OPTARG ;;
        w) width=$OPTARG ;;
        s) startAt=$OPTARG ;;
        t) duration=$OPTARG ;;
        n) withAudio=0 ;;
        h)
            usage
            exit 0
            ;;
        :)
            echo "option -$OPTARG needs a value" >&2
            exit 2
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
done
shift $((OPTIND - 1))

if [ $# -lt 1 ]; then
    usage >&2
    exit 2
fi

input=$1
output=${2:-"${input%.*}.webm"}

command -v ffmpeg >/dev/null || {
    echo "ffmpeg is not installed" >&2
    exit 1
}
[ -f "$input" ] || {
    echo "no such file: $input" >&2
    exit 1
}

arguments=(-hide_banner -loglevel warning -stats)
[ -n "$startAt" ] && arguments+=(-ss "$startAt")
arguments+=(-i "$input")
[ -n "$duration" ] && arguments+=(-t "$duration")

# libvpx counts quality the other way, as a constant rate factor where lower is better, so the 0-10 scale this
# script takes is mapped onto it. `-b:v 0` is what puts libvpx in constant-quality mode rather than bitrate mode.
readonly crf=$((63 - quality * 5))
# yuv420p because that is the only pixel format the viewer's decoder path handles, and the only one VP8 encodes
arguments+=(-c:v libvpx -b:v 0 -crf "$crf" -pix_fmt yuv420p)
[ "$width" != "0" ] && arguments+=(-vf "scale=$width:-2")

if [ "$withAudio" -eq 1 ]; then
    arguments+=(-c:a libvorbis -q:a "$audioQuality")
else
    arguments+=(-an)
fi

echo "encoding $input -> $output (vp8 q=$quality, crf=$crf, width=${width:-source})"
ffmpeg "${arguments[@]}" -y "$output"

printf 'done: %s, %s\n' "$output" "$(du -h "$output" | cut -f1)"
ffprobe -hide_banner -loglevel error -show_entries stream=codec_name,width,height,duration -of default=noprint_wrappers=1 "$output" 2>/dev/null || true

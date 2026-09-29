// Writes the hand clap the live audio slide plays when it has no microphone: made here rather than recorded, so it
// is free of anyone's rights by construction (CC0), and the same bytes every time it is made.
//
//   node devtools/make-clap.mjs <package directory>/media/clap.wav
//
// Two seconds, stereo, 48 kHz, 16-bit PCM, repeated by the slide. A clap is three or four bursts of noise a few
// milliseconds apart and a short decaying tail; the noise is high-passed so it has the hiss of a clap rather than
// the thump of a drum. On a spectrum and waterfall it shows as a broadband stripe once every two seconds, over the
// faint noise of a room. The right channel hears it 0.6 ms later and 3 dB quieter, as from a source to the left of a
// listener, so a stereo display shows the two channels apart.

import { writeFileSync } from "node:fs";

const target = process.argv[2];
if (!target) {
  console.error("usage: make-clap.mjs <output.wav>");
  process.exit(2);
}

const kSampleRate = 48000;
const kSeconds = 2.0;
const kClapAt = 0.1; // s: where the clap starts in the loop
const kBursts = [0.0, 0.009, 0.017, 0.028]; // s after the start: the hands meeting is not one event
const kBurstDecay = 0.004; // s: each burst's e-folding time
const kTailDecay = 0.045; // s: the room's
const kPeak = 0.8; // of full scale
const kRightDelay = 0.0006; // s: the extra way to the right ear, about 20 cm of air
const kRightGain = 0.7071; // -3 dB
const kRoomNoise = 0.001; // of full scale, about -60 dBFS: a room is never silent, and digital silence is -inf dB,
// which a spectrum cannot draw

// mulberry32: a small seeded generator, so the file is reproducible to the byte
let state = 20261002;
const random = () => {
  state = (state + 0x6d2b79f5) | 0;
  let t = state;
  t = Math.imul(t ^ (t >>> 15), t | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
};

const length = Math.round(kSampleRate * kSeconds);
const samples = new Float64Array(length);
let previousNoise = 0;
let highPassed = 0;
const kHighPass = 0.85; // one-pole coefficient: keeps the hiss, drops the rumble
for (let index = 0; index < length; ++index) {
  const noise = random() * 2 - 1;
  highPassed = kHighPass * (highPassed + noise - previousNoise);
  previousNoise = noise;
  const since = index / kSampleRate - kClapAt;
  if (since < 0) {
    continue;
  }
  let envelope = 0.35 * Math.exp(-since / kTailDecay);
  for (const burst of kBursts) {
    if (since >= burst) {
      envelope += Math.exp(-(since - burst) / kBurstDecay);
    }
  }
  samples[index] = highPassed * envelope;
}
const loudest = samples.reduce((peak, sample) => Math.max(peak, Math.abs(sample)), 0);

const delay = Math.round(kRightDelay * kSampleRate);
const data = Buffer.alloc(length * 4);
for (let index = 0; index < length; ++index) {
  const left = (samples[index] / loudest) * kPeak + (random() * 2 - 1) * kRoomNoise;
  const right = (index >= delay ? (samples[index - delay] / loudest) * kPeak * kRightGain : 0) + (random() * 2 - 1) * kRoomNoise;
  data.writeInt16LE(Math.round(left * 32767), index * 4);
  data.writeInt16LE(Math.round(right * 32767), index * 4 + 2);
}
const header = Buffer.alloc(44);
header.write("RIFF", 0);
header.writeUInt32LE(36 + data.length, 4);
header.write("WAVE", 8);
header.write("fmt ", 12);
header.writeUInt32LE(16, 16); // PCM format chunk size
header.writeUInt16LE(1, 20); // PCM
header.writeUInt16LE(2, 22); // stereo
header.writeUInt32LE(kSampleRate, 24);
header.writeUInt32LE(kSampleRate * 4, 28); // bytes per second
header.writeUInt16LE(4, 32); // bytes per frame
header.writeUInt16LE(16, 34); // bits per sample
header.write("data", 36);
header.writeUInt32LE(data.length, 40);
writeFileSync(target, Buffer.concat([header, data]));

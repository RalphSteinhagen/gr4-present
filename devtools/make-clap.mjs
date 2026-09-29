// Writes the hand clap the live audio slide plays without a microphone:
// synthesised, so it is CC0 and byte-identical every run.
//
//   node devtools/make-clap.mjs <package directory>/media/clap.wav
//
// Two seconds of stereo 48 kHz 16-bit PCM: a few high-passed noise bursts and a decaying tail over faint room noise;
// the right channel arrives 0.6 ms later and 3 dB quieter so a stereo display shows the channels apart.

import { writeFileSync } from "node:fs";

const target = process.argv[2];
if (!target) {
  console.error("usage: make-clap.mjs <output.wav>");
  process.exit(2);
}

const kSampleRate = 48000;
const kSeconds = 2.0;
const kClapAtSeconds = 0.1;
const kBurstOffsetsSeconds = [0.0, 0.009, 0.017, 0.028]; // the hands meeting is not one event
const kBurstDecaySeconds = 0.004; // e-folding time
const kTailDecaySeconds = 0.045;
const kPeakFullScale = 0.8;
const kRightDelaySeconds = 0.0006; // about 20 cm of air
const kRightGain = 0.7071; // -3 dB
// about -60 dBFS: digital silence is -inf dB, which a spectrum cannot draw
const kRoomNoiseFullScale = 0.001;

// mulberry32, seeded so the file is reproducible
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
  const since = index / kSampleRate - kClapAtSeconds;
  if (since < 0) {
    continue;
  }
  let envelope = 0.35 * Math.exp(-since / kTailDecaySeconds);
  for (const burst of kBurstOffsetsSeconds) {
    if (since >= burst) {
      envelope += Math.exp(-(since - burst) / kBurstDecaySeconds);
    }
  }
  samples[index] = highPassed * envelope;
}
const loudest = samples.reduce((peak, sample) => Math.max(peak, Math.abs(sample)), 0);

const delay = Math.round(kRightDelaySeconds * kSampleRate);
const data = Buffer.alloc(length * 4);
for (let index = 0; index < length; ++index) {
  const left = (samples[index] / loudest) * kPeakFullScale + (random() * 2 - 1) * kRoomNoiseFullScale;
  const right = (index >= delay ? (samples[index - delay] / loudest) * kPeakFullScale * kRightGain : 0) + (random() * 2 - 1) * kRoomNoiseFullScale;
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

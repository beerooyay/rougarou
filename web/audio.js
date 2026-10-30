// the gym's sounds, synthesized: chalky catches on holds, rubber on plywood, a padded thud on the mats, a little fanfare for a send.
// everything runs through `master`, which the clip recorder taps. the context starts on the first touch or key, as browsers require.

export class Audio {
	constructor() {
		this.ctx = null;
		this.master = null;
		this.volume = 0.8;
		this.muted = false;
		const wake = () => this.wake();
		addEventListener('pointerdown', wake, { once: true, capture: true });
		addEventListener('keydown', wake, { once: true, capture: true });
	}

	wake() {
		if (this.ctx) { this.ctx.resume?.(); return; }
		const Context = window.AudioContext || window.webkitAudioContext;
		if (!Context) return;
		this.ctx = new Context();
		this.master = this.ctx.createGain();
		this.master.gain.value = this.muted ? 0 : this.volume;
		this.master.connect(this.ctx.destination);
		// a short burst of noise, reused by every rustle and thud
		const length = this.ctx.sampleRate * 0.5;
		this.noise = this.ctx.createBuffer(1, length, this.ctx.sampleRate);
		const data = this.noise.getChannelData(0);
		for (let i = 0; i < length; i++) data[i] = Math.random() * 2 - 1;
	}

	set(volume, muted) {
		this.volume = volume;
		this.muted = muted;
		if (this.master) this.master.gain.value = muted ? 0 : volume;
	}

	// filtered noise: Frequency and Q shape it, Time is its length in seconds
	hiss(level, frequency, q, time, kind = 'bandpass') {
		if (!this.ctx) return;
		const t = this.ctx.currentTime;
		const source = this.ctx.createBufferSource();
		source.buffer = this.noise;
		const filter = this.ctx.createBiquadFilter();
		filter.type = kind;
		filter.frequency.value = frequency;
		filter.Q.value = q;
		const gain = this.ctx.createGain();
		gain.gain.setValueAtTime(level, t);
		gain.gain.exponentialRampToValueAtTime(0.0001, t + time);
		source.connect(filter).connect(gain).connect(this.master);
		source.start(t, Math.random() * 0.3, time + 0.05);
	}

	tone(frequency, level, time, delay = 0, type = 'sine', bend = 1) {
		if (!this.ctx) return;
		const t = this.ctx.currentTime + delay;
		const osc = this.ctx.createOscillator();
		osc.type = type;
		osc.frequency.setValueAtTime(frequency, t);
		if (bend !== 1) osc.frequency.exponentialRampToValueAtTime(frequency * bend, t + time);
		const gain = this.ctx.createGain();
		gain.gain.setValueAtTime(0.0001, t);
		gain.gain.exponentialRampToValueAtTime(level, t + 0.01);
		gain.gain.exponentialRampToValueAtTime(0.0001, t + time);
		osc.connect(gain).connect(this.master);
		osc.start(t);
		osc.stop(t + time + 0.05);
	}

	// a hand slapping onto a hold: a dull knock and a puff of chalk
	grab(firm = 1) { this.tone(140, 0.35 * firm, 0.09, 0, 'sine', 0.6); this.hiss(0.22 * firm, 2400, 0.8, 0.12); }
	// a shoe placed on plywood
	step() { this.tone(220, 0.12, 0.05, 0, 'triangle', 0.7); this.hiss(0.08, 1200, 1.2, 0.06); }
	// a soft footfall on the mats
	pad() { this.hiss(0.05, 500, 0.7, 0.07, 'lowpass'); }
	// fingers or rubber sliding off
	slip() { this.hiss(0.25, 3200, 3, 0.25); this.tone(900, 0.06, 0.25, 0, 'sawtooth', 0.5); }
	// a body landing on crash pads
	thud(weight = 1) { this.tone(70, 0.6 * weight, 0.35, 0, 'sine', 0.5); this.hiss(0.4 * weight, 300, 0.6, 0.4, 'lowpass'); }
	click() { this.tone(1300, 0.05, 0.03, 0, 'triangle'); }
	// a send: a quick rising arpeggio
	fanfare(best) {
		const notes = best ? [523, 659, 784, 1047, 1319] : [523, 659, 784, 1047];
		notes.forEach((n, i) => this.tone(n, 0.18, 0.35, i * 0.09, 'triangle'));
	}
}

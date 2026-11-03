//! Stereo wrapper around [`orb_engine::delay::Delay`].
//!
//! One delay instance shares one [`ProcessState`] (the mono line). Audio is processed in
//! 64-sample blocks at 44100 Hz. Output is delayed by one block so a host sees a constant
//! 64-sample latency. The C API takes buffers already at 44100 Hz; resampling is the JUCE
//! plugin's job.
//!
//! Delay length uses host BPM (`ctx.tempo = BPM * 1000`). There is no pattern clock; `tick` is
//! always 0. Dry is passed through and a panned tap is added from a mono send
//! `T[mix] * 0.5 * (L + R)`. Mix uses the mixer level curve (the same `T[]` as ReBirth's delay
//! send), not a linear amplitude blend: 0 is dry, 127 is full send.

use orb_engine::delay::{self, Delay};
use orb_engine::frame::{BLOCK, Block, BlockContext, SendEffect, SongDevice};
use orb_engine::process::ProcessState;
use orb_engine::tables::Tables;

/// Engine (and original) sample rate. Length is computed in samples at this rate.
pub const ENGINE_RATE: f64 = 44100.0;
/// Fallback tempo when the host has none, as BPM*1000.
pub const DEFAULT_TEMPO_MILLI: u32 = 120_000;

/// Panel parameters applied at the start of each 64-sample block.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(C)]
pub struct Params {
    pub steps: u8,
    /// `0` is triplets; anything else is sixteenths.
    pub mode: u8,
    pub feedback: u8,
    pub pan: u8,
    /// Delay send 0..127, mapped through ReBirth's level curve `T[]`.
    pub mix: u8,
    pub bypass: u8,
}

impl Default for Params {
    fn default() -> Self {
        let d = delay::DEFAULTS;
        Self {
            steps: d[delay::STEPS],
            mode: d[delay::MODE],
            feedback: 64,
            pan: d[delay::PAN],
            mix: 0,
            bypass: 0,
        }
    }
}

/// Host playhead: BPM only. Delay length does not read song position.
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(C)]
pub struct Playhead {
    pub bpm: f64,
}

impl Default for Playhead {
    fn default() -> Self {
        Self { bpm: 120.0 }
    }
}

fn tempo_milli(bpm: f64) -> u32 {
    if !bpm.is_finite() || bpm <= 0.0 {
        DEFAULT_TEMPO_MILLI
    } else {
        (bpm * 1000.0).round() as u32
    }
}

/// Stereo delay: one mono line, dry-plus-wet, a 64-sample output delay.
pub struct DelayStereo {
    tables: Tables,
    delay: Delay,
    in_l: Block,
    in_r: Block,
    delay_l: Block,
    delay_r: Block,
    fill: usize,
    blocks_processed: u64,
}

impl Default for DelayStereo {
    fn default() -> Self {
        Self::new()
    }
}

impl DelayStereo {
    pub fn new() -> Self {
        Self {
            tables: Tables::new(),
            delay: Delay::new(ProcessState::new()),
            in_l: [0.0; BLOCK],
            in_r: [0.0; BLOCK],
            delay_l: [0.0; BLOCK],
            delay_r: [0.0; BLOCK],
            fill: 0,
            blocks_processed: 0,
        }
    }

    /// Clears the delay line, write pointer, and the output delay. Tables stay.
    pub fn reset(&mut self) {
        self.delay = Delay::new(ProcessState::new());
        self.in_l = [0.0; BLOCK];
        self.in_r = [0.0; BLOCK];
        self.delay_l = [0.0; BLOCK];
        self.delay_r = [0.0; BLOCK];
        self.fill = 0;
        self.blocks_processed = 0;
    }

    pub fn latency() -> usize {
        BLOCK
    }

    pub fn blocks_processed(&self) -> u64 {
        self.blocks_processed
    }

    pub fn process(
        &mut self,
        left: &mut [f32],
        right: &mut [f32],
        params: Params,
        playhead: Playhead,
    ) {
        let n = left.len().min(right.len());
        let tempo = tempo_milli(playhead.bpm);
        let mut i = 0;

        while i < n {
            let take = (n - i).min(BLOCK - self.fill);
            self.in_l[self.fill..self.fill + take].copy_from_slice(&left[i..i + take]);
            self.in_r[self.fill..self.fill + take].copy_from_slice(&right[i..i + take]);
            left[i..i + take].copy_from_slice(&self.delay_l[self.fill..self.fill + take]);
            right[i..i + take].copy_from_slice(&self.delay_r[self.fill..self.fill + take]);
            self.fill += take;
            i += take;

            if self.fill == BLOCK {
                if params.bypass == 0 {
                    self.apply_params(params);
                    let ctx = BlockContext {
                        tables: &self.tables,
                        meter_index: 0,
                        tempo,
                        tick: 0,
                        stack_word: None,
                    };
                    let send_gain = self.tables.level[params.mix.min(127) as usize];
                    let mut send = [0.0f32; BLOCK];
                    for s in 0..BLOCK {
                        send[s] = send_gain * 0.5 * (self.in_l[s] + self.in_r[s]);
                    }
                    self.delay
                        .process(&mut send, &mut self.in_l, &mut self.in_r, &ctx);
                }
                std::mem::swap(&mut self.in_l, &mut self.delay_l);
                std::mem::swap(&mut self.in_r, &mut self.delay_r);
                self.fill = 0;
                self.blocks_processed += 1;
            }
        }
    }

    fn apply_params(&mut self, p: Params) {
        let steps = p.steps.clamp(1, 32);
        let feedback = p.feedback.min(127);
        let pan = p.pan.min(127);
        self.delay.set_param(delay::STEPS as u8, steps);
        self.delay.set_param(delay::MODE as u8, p.mode);
        self.delay.set_param(delay::FEEDBACK as u8, feedback);
        self.delay.set_param(delay::PAN as u8, pan);
        self.delay.set_param(delay::ENABLED as u8, 1);
    }
}

#[cfg(test)]
impl DelayStereo {
    pub fn delay(&self) -> &Delay {
        &self.delay
    }

    pub fn fill(&self) -> usize {
        self.fill
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn orb_delay_new() -> *mut DelayStereo {
    Box::into_raw(Box::new(DelayStereo::new()))
}

/// # Safety
/// `delay` is a pointer from [`orb_delay_new`], or null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn orb_delay_free(delay: *mut DelayStereo) {
    if !delay.is_null() {
        drop(unsafe { Box::from_raw(delay) });
    }
}

/// # Safety
/// `delay` is a pointer from [`orb_delay_new`], or null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn orb_delay_reset(delay: *mut DelayStereo) {
    if let Some(p) = unsafe { delay.as_mut() } {
        p.reset();
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn orb_delay_latency() -> i32 {
    DelayStereo::latency() as i32
}

/// # Safety
/// `delay` is a pointer from [`orb_delay_new`]. `left` and `right` each have `nframes` samples
/// when `nframes > 0`. `params` and `playhead` may be null, in which case defaults are used.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn orb_delay_process(
    delay: *mut DelayStereo,
    left: *mut f32,
    right: *mut f32,
    nframes: i32,
    params: *const Params,
    playhead: *const Playhead,
) {
    let Some(p) = (unsafe { delay.as_mut() }) else {
        return;
    };
    if nframes <= 0 || left.is_null() || right.is_null() {
        return;
    }
    let n = nframes as usize;
    let left = unsafe { std::slice::from_raw_parts_mut(left, n) };
    let right = unsafe { std::slice::from_raw_parts_mut(right, n) };
    let params = unsafe { params.as_ref() }.copied().unwrap_or_default();
    let playhead = unsafe { playhead.as_ref() }.copied().unwrap_or_default();
    p.process(left, right, params, playhead);
}

#[cfg(test)]
mod tests;

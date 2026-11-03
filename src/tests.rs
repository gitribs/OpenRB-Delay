use super::*;
use orb_engine::delay::{self, Delay};

fn ctx<'a>(tables: &'a Tables, tempo: u32) -> BlockContext<'a> {
    BlockContext {
        tables,
        meter_index: 0,
        tempo,
        tick: 0,
        stack_word: None,
    }
}

fn apply(d: &mut Delay, params: Params) {
    d.set_param(delay::STEPS as u8, params.steps.clamp(1, 32));
    d.set_param(delay::MODE as u8, params.mode);
    d.set_param(delay::FEEDBACK as u8, params.feedback.min(127));
    d.set_param(delay::PAN as u8, params.pan.min(127));
    d.set_param(delay::ENABLED as u8, 1);
}

fn bpm(bpm: f64) -> Playhead {
    Playhead { bpm }
}

fn run(params: Params, playhead: Playhead, left: &[f32], right: &[f32]) -> (Vec<f32>, Vec<f32>) {
    let mut wrap_l = left.to_vec();
    let mut wrap_r = right.to_vec();
    DelayStereo::new().process(&mut wrap_l, &mut wrap_r, params, playhead);
    (wrap_l, wrap_r)
}

/// A few blocks through the wrapper equal `Delay::process` on dry-plus-wet, delayed by 64
/// samples. The first block is silent.
#[test]
fn wrapper_matches_delay_process() {
    let params = Params {
        mix: 127,
        ..Params::default()
    };
    let nblocks = 4;
    let n = nblocks * BLOCK;
    let mut input_l = vec![0.0f32; n];
    let mut input_r = vec![0.0f32; n];
    for i in 0..n {
        input_l[i] = ((i % 17) as f32) * 0.02 - 0.1;
        input_r[i] = ((i % 13) as f32) * -0.015 + 0.05;
    }

    let mut wrap_l = input_l.clone();
    let mut wrap_r = input_r.clone();
    let mut wrap = DelayStereo::new();
    wrap.process(&mut wrap_l, &mut wrap_r, params, bpm(120.0));

    assert!(wrap_l[..BLOCK].iter().all(|&x| x == 0.0));
    assert!(wrap_r[..BLOCK].iter().all(|&x| x == 0.0));

    let tables = Tables::new();
    let process = ProcessState::new();
    let mut delay = Delay::new(process.clone());
    apply(&mut delay, params);

    for k in 0..nblocks - 1 {
        let mut bl: Block = input_l[k * BLOCK..(k + 1) * BLOCK].try_into().unwrap();
        let mut br: Block = input_r[k * BLOCK..(k + 1) * BLOCK].try_into().unwrap();
        let mut send = [0.0f32; BLOCK];
        for i in 0..BLOCK {
            send[i] = 0.5 * (bl[i] + br[i]);
        }
        delay.process(
            &mut send,
            &mut bl,
            &mut br,
            &ctx(&tables, DEFAULT_TEMPO_MILLI),
        );
        assert_eq!(
            wrap_l[(k + 1) * BLOCK..(k + 2) * BLOCK],
            bl,
            "left block {k}"
        );
        assert_eq!(
            wrap_r[(k + 1) * BLOCK..(k + 2) * BLOCK],
            br,
            "right block {k}"
        );
    }
}

/// Impulse at 120 BPM, 1 sixteenth returns at sample 5512 plus the 64-sample wrapper delay.
#[test]
fn impulse_at_one_sixteenth_returns_at_5512() {
    let params = Params {
        steps: 1,
        mode: 1,
        feedback: 0,
        pan: 64,
        mix: 127,
        bypass: 0,
    };
    let n = 100 * BLOCK;
    let mut input_l = vec![0.0f32; n];
    let mut input_r = vec![0.0f32; n];
    input_l[0] = 0.5;
    input_r[0] = 0.5;

    let mut wrap_l = input_l;
    let mut wrap_r = input_r;
    DelayStereo::new().process(&mut wrap_l, &mut wrap_r, params, bpm(120.0));

    let at = 5512 + BLOCK;
    assert!(wrap_l[at - 1] < 1e-5);
    assert!(wrap_l[at] > 0.4 && wrap_r[at] > 0.4);
    assert!(wrap_l[at + 1] < 1e-5);
}

/// Triplet vs sixteenth at the same steps and BPM differ.
#[test]
fn triplet_and_sixteenth_differ() {
    let n = 100 * BLOCK;
    let mut input_l = vec![0.0f32; n];
    let mut input_r = vec![0.0f32; n];
    input_l[0] = 0.5;
    input_r[0] = 0.5;

    let six = Params {
        steps: 1,
        mode: 1,
        feedback: 0,
        pan: 64,
        mix: 127,
        bypass: 0,
    };
    let trip = Params { mode: 0, ..six };

    let (six_l, _) = run(six, bpm(120.0), &input_l, &input_r);
    let (trip_l, _) = run(trip, bpm(120.0), &input_l, &input_r);
    assert_ne!(&six_l[BLOCK..], &trip_l[BLOCK..]);
}

/// Tempo 120 vs 140 changes length.
#[test]
fn tempo_changes_length() {
    let params = Params {
        steps: 1,
        mode: 1,
        feedback: 0,
        pan: 64,
        mix: 127,
        bypass: 0,
    };
    let n = 100 * BLOCK;
    let mut input_l = vec![0.0f32; n];
    let mut input_r = vec![0.0f32; n];
    input_l[0] = 0.5;
    input_r[0] = 0.5;

    let (l120, _) = run(params, bpm(120.0), &input_l, &input_r);
    let (l140, _) = run(params, bpm(140.0), &input_l, &input_r);
    assert_ne!(&l120[BLOCK..], &l140[BLOCK..]);
}

/// Bypass is delayed dry. The delay counter does not advance while skipped.
#[test]
fn bypass_is_delayed_dry() {
    let params = Params {
        steps: 3,
        mode: 1,
        feedback: 80,
        pan: 64,
        mix: 127,
        bypass: 1,
    };
    let n = 2 * BLOCK;
    let mut input_l = vec![0.0f32; n];
    let mut input_r = vec![0.0f32; n];
    for i in 0..n {
        input_l[i] = (i as f32) * 0.001;
        input_r[i] = -(i as f32) * 0.002;
    }

    let mut wrap_l = input_l.clone();
    let mut wrap_r = input_r.clone();
    let mut wrap = DelayStereo::new();
    wrap.process(&mut wrap_l, &mut wrap_r, params, bpm(120.0));

    assert!(wrap_l[..BLOCK].iter().all(|&x| x == 0.0));
    assert!(wrap_r[..BLOCK].iter().all(|&x| x == 0.0));
    assert_eq!(&wrap_l[BLOCK..], &input_l[..BLOCK]);
    assert_eq!(&wrap_r[BLOCK..], &input_r[..BLOCK]);
    assert_eq!(wrap.delay().counter, 0);
    assert_eq!(
        wrap.delay().live[delay::STEPS],
        delay::DEFAULTS[delay::STEPS]
    );
}

/// A short buffer leaves a remainder; the next call finishes the block.
#[test]
fn short_buffer_leaves_a_remainder() {
    let params = Params {
        mix: 127,
        ..Params::default()
    };
    let mut p = DelayStereo::new();
    let mut l = [0.2f32; 32];
    let mut r = [0.2f32; 32];
    p.process(&mut l, &mut r, params, bpm(120.0));
    assert_eq!(p.blocks_processed(), 0);
    assert_eq!(p.fill(), 32);

    let mut l = [0.2f32; 32];
    let mut r = [0.2f32; 32];
    p.process(&mut l, &mut r, params, bpm(120.0));
    assert_eq!(p.blocks_processed(), 1);
    assert_eq!(p.fill(), 0);
}

/// Steps 0/255 clamp to 1..32; pan 255 clamps to 127.
#[test]
fn steps_and_pan_are_clamped() {
    let mut p = DelayStereo::new();
    let mut l = [0.5f32; BLOCK];
    let mut r = [0.5f32; BLOCK];
    p.process(
        &mut l,
        &mut r,
        Params {
            steps: 0,
            mode: 1,
            feedback: 80,
            pan: 64,
            mix: 127,
            bypass: 0,
        },
        bpm(120.0),
    );
    assert_eq!(p.delay().live[delay::STEPS], 1);

    let mut p = DelayStereo::new();
    let mut l = [0.5f32; BLOCK];
    let mut r = [0.5f32; BLOCK];
    p.process(
        &mut l,
        &mut r,
        Params {
            steps: 255,
            mode: 1,
            feedback: 80,
            pan: 255,
            mix: 255,
            bypass: 0,
        },
        bpm(120.0),
    );
    assert_eq!(p.delay().live[delay::STEPS], 32);
    assert_eq!(p.delay().live[delay::PAN], 127);
}

/// Mix 0 is delayed dry, but the delay line still runs.
#[test]
fn mix_zero_is_delayed_dry() {
    let params = Params {
        mix: 0,
        ..Params::default()
    };
    let n = 2 * BLOCK;
    let mut input_l = vec![0.0f32; n];
    let mut input_r = vec![0.0f32; n];
    for i in 0..n {
        input_l[i] = (i as f32) * 0.001;
        input_r[i] = -(i as f32) * 0.002;
    }

    let mut wrap_l = input_l.clone();
    let mut wrap_r = input_r.clone();
    let mut wrap = DelayStereo::new();
    wrap.process(&mut wrap_l, &mut wrap_r, params, bpm(120.0));

    assert!(wrap_l[..BLOCK].iter().all(|&x| x == 0.0));
    assert!(wrap_r[..BLOCK].iter().all(|&x| x == 0.0));
    assert_eq!(&wrap_l[BLOCK..], &input_l[..BLOCK]);
    assert_eq!(&wrap_r[BLOCK..], &input_r[..BLOCK]);
    assert!(wrap.delay().counter > 0);
}

/// Mix follows ReBirth's send curve `T[]`, not a linear amplitude blend. Mix 255 matches 127.
#[test]
fn mix_uses_the_level_curve() {
    let n = 100 * BLOCK;
    let mut input_l = vec![0.0f32; n];
    let mut input_r = vec![0.0f32; n];
    input_l[0] = 0.5;
    input_r[0] = 0.5;

    let base = Params {
        steps: 1,
        mode: 1,
        feedback: 0,
        pan: 64,
        mix: 127,
        bypass: 0,
    };
    let at = 5512 + BLOCK;
    let peak = |mix: u8| {
        let (l, _) = run(Params { mix, ..base }, bpm(120.0), &input_l, &input_r);
        l[at]
    };

    let t = Tables::new();
    let dry = peak(0);
    let mid = peak(64);
    let wet = peak(127);
    let clamped = peak(255);
    assert!(dry.abs() < 1e-5);
    assert!(wet > 0.4);
    assert!(t.level[64] < 0.1);
    assert!((mid - wet * t.level[64]).abs() < 1e-5);
    assert_eq!(clamped, wet);
}

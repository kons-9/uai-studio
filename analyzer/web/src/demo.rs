//! Demo data generator — produces a synthetic binary trace dump.

use rand::Rng;

use crate::protocol::*;

pub fn generate_demo_dump() -> Vec<u8> {
    let mut rng = rand::thread_rng();
    let mut frames = Vec::with_capacity(20_000);
    let mut seq: u32 = 0;
    let mut tick: u32 = 0;

    let task_names: &[(u16, &str)] = &[
        (1, "ble_scan"),
        (2, "ai_infer"),
        (3, "display"),
        (4, "sensor_read"),
        (5, "idle"),
    ];
    let task_prios: &[(u16, u8)] = &[(1, 8), (2, 10), (3, 5), (4, 7), (5, 1)];
    let task_ids: &[u16] = &[1, 2, 3, 4, 5];
    let mut current_task: u16 = 1;

    // Generate ~500 trace events
    for _ in 0..500 {
        tick += rng.gen_range(500..20_000);
        seq += 1;

        let r: f64 = rng.gen();
        if r < 0.4 {
            // TASK_SWITCH
            let next = loop {
                let t = task_ids[rng.gen_range(0..task_ids.len())];
                if t != current_task {
                    break t;
                }
            };
            let mut payload = [0u8; 8];
            payload[0..2].copy_from_slice(&current_task.to_le_bytes());
            payload[2..4].copy_from_slice(&next.to_le_bytes());
            frames.extend_from_slice(&make_trace_frame(
                tick,
                next,
                TRACE_TASK_SWITCH,
                0,
                &payload,
                seq,
            ));
            current_task = next;
        } else if r < 0.55 {
            // TASK_WAIT
            frames.extend_from_slice(&make_trace_frame(
                tick,
                current_task,
                TRACE_TASK_WAIT,
                0,
                &[0; 8],
                seq,
            ));
        } else if r < 0.70 {
            // TASK_READY
            let t = task_ids[rng.gen_range(0..task_ids.len())];
            frames.extend_from_slice(&make_trace_frame(
                tick,
                t,
                TRACE_TASK_READY,
                0,
                &[0; 8],
                seq,
            ));
        } else if r < 0.80 {
            // MTX_LOCK
            let res_id: u16 = rng.gen_range(1..=3);
            let mut payload = [0u8; 8];
            payload[0..2].copy_from_slice(&res_id.to_le_bytes());
            payload[2..4].copy_from_slice(&current_task.to_le_bytes());
            frames.extend_from_slice(&make_trace_frame(
                tick,
                current_task,
                TRACE_MTX_LOCK,
                0,
                &payload,
                seq,
            ));
        } else if r < 0.90 {
            // MTX_UNLOCK
            let res_id: u16 = rng.gen_range(1..=3);
            let mut payload = [0u8; 8];
            payload[0..2].copy_from_slice(&res_id.to_le_bytes());
            payload[2..4].copy_from_slice(&current_task.to_le_bytes());
            frames.extend_from_slice(&make_trace_frame(
                tick,
                current_task,
                TRACE_MTX_UNLOCK,
                0,
                &payload,
                seq,
            ));
        } else if r < 0.95 {
            // IRQ pair
            let irq_num: u16 = rng.gen_range(1..=16);
            let mut payload = [0u8; 8];
            payload[0..2].copy_from_slice(&irq_num.to_le_bytes());
            frames.extend_from_slice(&make_trace_frame(
                tick,
                0,
                TRACE_IRQ_ENTER,
                0,
                &payload,
                seq,
            ));
            seq += 1;
            tick += rng.gen_range(10..200);
            frames.extend_from_slice(&make_trace_frame(
                tick,
                0,
                TRACE_IRQ_EXIT,
                0,
                &payload,
                seq,
            ));
        } else {
            // MEM_ALLOC trace event
            let addr: u32 = rng.gen_range(0x2000_0000..0x2000_8000) & !0xF;
            let size: u16 = *[32u16, 64, 128].choose(&mut rng).unwrap();
            let mut payload = [0u8; 8];
            payload[0..4].copy_from_slice(&addr.to_le_bytes());
            payload[4..6].copy_from_slice(&size.to_le_bytes());
            frames.extend_from_slice(&make_trace_frame(
                tick,
                current_task,
                TRACE_MEM_ALLOC_EV,
                0,
                &payload,
                seq,
            ));
        }
    }

    // Task report
    frames.extend_from_slice(&make_task_report(task_names, task_prios, &mut rng));

    // Memory corruptions (1..4)
    for _ in 0..rng.gen_range(1..=4) {
        let ctype: u8 = *[0x01u8, 0x02, 0x03, 0x04].choose(&mut rng).unwrap();
        let addr: u32 = rng.gen_range(0x2000_0000..0x2000_8000) & !0xF;
        let block_idx: u16 = rng.gen_range(0..32);
        let mut buf = Vec::with_capacity(10);
        buf.extend_from_slice(&PROTO_MAGIC.to_le_bytes());
        buf.push(MSG_MEM_CORRUPTION);
        buf.push(ctype);
        buf.extend_from_slice(&addr.to_le_bytes());
        buf.extend_from_slice(&block_idx.to_le_bytes());
        frames.extend_from_slice(&buf);
    }

    // Alloc/free events (60)
    let mut live_addrs: Vec<u32> = Vec::new();
    for _ in 0..60 {
        let tid = task_ids[rng.gen_range(0..task_ids.len())];
        let size: u16 = *[32u16, 64, 128, 256].choose(&mut rng).unwrap();
        if rng.gen::<f64>() < 0.6 || live_addrs.is_empty() {
            let addr: u32 = rng.gen_range(0x2000_1000..0x2000_7000) & !0xF;
            let mut buf = Vec::with_capacity(11);
            buf.extend_from_slice(&PROTO_MAGIC.to_le_bytes());
            buf.push(MSG_MEM_ALLOC);
            buf.extend_from_slice(&tid.to_le_bytes());
            buf.extend_from_slice(&addr.to_le_bytes());
            buf.extend_from_slice(&size.to_le_bytes());
            frames.extend_from_slice(&buf);
            live_addrs.push(addr);
        } else {
            let idx = rng.gen_range(0..live_addrs.len());
            let addr = live_addrs.swap_remove(idx);
            let mut buf = Vec::with_capacity(11);
            buf.extend_from_slice(&PROTO_MAGIC.to_le_bytes());
            buf.push(MSG_MEM_FREE);
            buf.extend_from_slice(&tid.to_le_bytes());
            buf.extend_from_slice(&addr.to_le_bytes());
            buf.extend_from_slice(&size.to_le_bytes());
            frames.extend_from_slice(&buf);
        }
    }

    frames
}

fn make_trace_frame(ts_us: u32, task_id: u16, etype: u8, flags: u8, payload: &[u8; 8], seq: u32) -> [u8; TRACE_FRAME_SIZE] {
    let mut buf = [0u8; TRACE_FRAME_SIZE];
    buf[0..2].copy_from_slice(&PROTO_MAGIC.to_le_bytes());
    buf[2] = MSG_TRACE_EVENT;
    buf[3..7].copy_from_slice(&ts_us.to_le_bytes());
    buf[7..9].copy_from_slice(&task_id.to_le_bytes());
    buf[9] = etype;
    buf[10] = flags;
    buf[11..19].copy_from_slice(payload);
    buf[19..23].copy_from_slice(&seq.to_le_bytes());
    buf
}

fn make_task_report(names: &[(u16, &str)], prios: &[(u16, u8)], rng: &mut impl Rng) -> Vec<u8> {
    let n = names.len() as u16;
    let mut buf = Vec::new();
    buf.extend_from_slice(&PROTO_MAGIC.to_le_bytes());
    buf.push(MSG_TASK_REPORT);
    buf.extend_from_slice(&n.to_le_bytes());

    for &(tid, name) in names {
        let prio = prios.iter().find(|p| p.0 == tid).map(|p| p.1).unwrap_or(0);
        let state: u8 = rng.gen_range(0..=2);
        let cpu: u32 = rng.gen_range(5_000..80_000);

        buf.extend_from_slice(&tid.to_le_bytes());
        buf.push(state);
        buf.push(prio);
        buf.extend_from_slice(&cpu.to_le_bytes());

        let mut name_bytes = [0u8; 16];
        let name_b = name.as_bytes();
        let copy_len = name_b.len().min(16);
        name_bytes[..copy_len].copy_from_slice(&name_b[..copy_len]);
        buf.extend_from_slice(&name_bytes);
    }
    buf
}

// Trait import for .choose()
use rand::seq::SliceRandom;

//! μAI-Bridge binary protocol constants and frame decoding.
//!
//! All values are little-endian on the wire.

// ---- Magic & message types ----
pub const PROTO_MAGIC: u16 = 0xAB55;
pub const MSG_TASK_REPORT: u8 = 0x10;
pub const MSG_MEM_CORRUPTION: u8 = 0x11;
pub const MSG_MEM_ALLOC: u8 = 0x12;
pub const MSG_MEM_FREE: u8 = 0x13;
pub const MSG_TRACE_EVENT: u8 = 0x20;

pub const FRAME_HEADER_SIZE: usize = 3; // magic(2) + msg_type(1)

// ---- Trace event types ----
pub const TRACE_TASK_SWITCH: u8 = 0x01;
pub const TRACE_TASK_READY: u8 = 0x02;
pub const TRACE_TASK_WAIT: u8 = 0x03;
pub const TRACE_SEM_SIGNAL: u8 = 0x10;
pub const TRACE_SEM_WAIT: u8 = 0x11;
pub const TRACE_MTX_LOCK: u8 = 0x12;
pub const TRACE_MTX_UNLOCK: u8 = 0x13;
pub const TRACE_MEM_ALLOC_EV: u8 = 0x20;
pub const TRACE_MEM_FREE_EV: u8 = 0x21;
pub const TRACE_IRQ_ENTER: u8 = 0x30;
pub const TRACE_IRQ_EXIT: u8 = 0x31;

pub const TRACE_EVENT_SIZE: usize = 20;
pub const TRACE_FRAME_SIZE: usize = FRAME_HEADER_SIZE + TRACE_EVENT_SIZE; // 23

pub fn event_name(etype: u8) -> &'static str {
    match etype {
        TRACE_TASK_SWITCH => "TASK_SWITCH",
        TRACE_TASK_READY => "TASK_READY",
        TRACE_TASK_WAIT => "TASK_WAIT",
        TRACE_SEM_SIGNAL => "SEM_SIGNAL",
        TRACE_SEM_WAIT => "SEM_WAIT",
        TRACE_MTX_LOCK => "MTX_LOCK",
        TRACE_MTX_UNLOCK => "MTX_UNLOCK",
        TRACE_MEM_ALLOC_EV => "MEM_ALLOC",
        TRACE_MEM_FREE_EV => "MEM_FREE",
        TRACE_IRQ_ENTER => "IRQ_ENTER",
        TRACE_IRQ_EXIT => "IRQ_EXIT",
        _ => "UNKNOWN",
    }
}

// ---- Corruption types ----
pub fn corruption_type_name(ctype: u8) -> &'static str {
    match ctype {
        0x01 => "OVERFLOW",
        0x02 => "UNDERFLOW",
        0x03 => "USE-AFTER-FREE",
        0x04 => "DOUBLE-FREE",
        0x05 => "STACK-OVERFLOW",
        _ => "UNKNOWN",
    }
}

pub const TASK_STATES: [&str; 4] = ["READY", "RUN", "WAIT", "DORM"];

// ---- Data structures ----

#[derive(Debug, Clone)]
pub struct TraceEvent {
    pub timestamp_us: u32,
    pub task_id: u16,
    pub event_type: u8,
    pub flags: u8,
    pub payload: [u8; 8],
    pub seq: u32,
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct TaskReportEntry {
    pub id: u16,
    pub state: u8,
    pub priority: u8,
    pub cpu_us: u32,
    pub name: String,
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct MemCorruptionEvent {
    #[serde(rename = "type")]
    pub corruption_type: u8,
    pub type_name: String,
    pub address: u32,
    pub block_index: u16,
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct MemAllocFreeEvent {
    pub op: String, // "alloc" | "free"
    pub task_id: u16,
    pub address: u32,
    pub size: u16,
}

// ---- Decoding functions ----

fn read_u16_le(buf: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes([buf[offset], buf[offset + 1]])
}

fn read_u32_le(buf: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes([buf[offset], buf[offset + 1], buf[offset + 2], buf[offset + 3]])
}

pub fn decode_trace_event(buf: &[u8]) -> Option<TraceEvent> {
    if buf.len() < TRACE_FRAME_SIZE {
        return None;
    }
    let magic = read_u16_le(buf, 0);
    if magic != PROTO_MAGIC {
        return None;
    }
    if buf[2] != MSG_TRACE_EVENT {
        return None;
    }
    let timestamp_us = read_u32_le(buf, 3);
    let task_id = read_u16_le(buf, 7);
    let event_type = buf[9];
    let flags = buf[10];
    let mut payload = [0u8; 8];
    payload.copy_from_slice(&buf[11..19]);
    let seq = read_u32_le(buf, 19);

    Some(TraceEvent {
        timestamp_us,
        task_id,
        event_type,
        flags,
        payload,
        seq,
    })
}

pub fn decode_task_switch_payload(payload: &[u8; 8]) -> (u16, u16) {
    let prev = u16::from_le_bytes([payload[0], payload[1]]);
    let next = u16::from_le_bytes([payload[2], payload[3]]);
    (prev, next)
}

pub fn decode_sync_payload(payload: &[u8; 8]) -> (u16, u16) {
    let resource_id = u16::from_le_bytes([payload[0], payload[1]]);
    let waiter_id = u16::from_le_bytes([payload[2], payload[3]]);
    (resource_id, waiter_id)
}

pub fn decode_task_report(buf: &[u8]) -> Option<Vec<TaskReportEntry>> {
    if buf.len() < 5 {
        return None;
    }
    let magic = read_u16_le(buf, 0);
    if magic != PROTO_MAGIC {
        return None;
    }
    if buf[2] != MSG_TASK_REPORT {
        return None;
    }
    let n_tasks = read_u16_le(buf, 3) as usize;
    let expected = 5 + n_tasks * 24;
    if buf.len() < expected {
        return None;
    }

    let mut entries = Vec::with_capacity(n_tasks);
    for i in 0..n_tasks {
        let base = 5 + i * 24;
        let id = read_u16_le(buf, base);
        let state = buf[base + 2];
        let priority = buf[base + 3];
        let cpu_us = read_u32_le(buf, base + 4);
        let name_bytes = &buf[base + 8..base + 24];
        let name = std::str::from_utf8(name_bytes)
            .unwrap_or("")
            .trim_end_matches('\0')
            .to_string();
        entries.push(TaskReportEntry {
            id,
            state,
            priority,
            cpu_us,
            name,
        });
    }
    Some(entries)
}

pub fn decode_mem_corruption(buf: &[u8]) -> Option<MemCorruptionEvent> {
    if buf.len() < 10 {
        return None;
    }
    let magic = read_u16_le(buf, 0);
    if magic != PROTO_MAGIC || buf[2] != MSG_MEM_CORRUPTION {
        return None;
    }
    let ctype = buf[3];
    let address = read_u32_le(buf, 4);
    let block_index = read_u16_le(buf, 8);

    Some(MemCorruptionEvent {
        corruption_type: ctype,
        type_name: corruption_type_name(ctype).to_string(),
        address,
        block_index,
    })
}

pub fn decode_mem_alloc_free(buf: &[u8]) -> Option<MemAllocFreeEvent> {
    if buf.len() < 11 {
        return None;
    }
    let magic = read_u16_le(buf, 0);
    if magic != PROTO_MAGIC {
        return None;
    }
    let msg_type = buf[2];
    if msg_type != MSG_MEM_ALLOC && msg_type != MSG_MEM_FREE {
        return None;
    }
    let task_id = read_u16_le(buf, 3);
    let address = read_u32_le(buf, 5);
    let size = read_u16_le(buf, 9);

    Some(MemAllocFreeEvent {
        op: if msg_type == MSG_MEM_ALLOC {
            "alloc".into()
        } else {
            "free".into()
        },
        task_id,
        address,
        size,
    })
}

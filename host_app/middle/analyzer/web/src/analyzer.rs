//! Trace dump parser and analysis engine — ported from web_dashboard.py

use crate::bottleneck::{Bottleneck, BottleneckAnalyzer};
use crate::memory::{CorrelationAnalyzer, HeapTracker, Insight, LeakWarning};
use crate::protocol::*;

// ---- TraceDump: binary frame parser ----

pub struct TraceDump {
    pub trace_events: Vec<TraceEvent>,
    pub task_reports: Vec<Vec<TaskReportEntry>>,
    pub mem_corruptions: Vec<MemCorruptionEvent>,
    pub mem_alloc_frees: Vec<MemAllocFreeEvent>,
    pub raw_frames: u32,
    pub parse_errors: u32,
    pub dropped_events: u32,
}

impl TraceDump {
    pub fn new() -> Self {
        Self {
            trace_events: Vec::new(),
            task_reports: Vec::new(),
            mem_corruptions: Vec::new(),
            mem_alloc_frees: Vec::new(),
            raw_frames: 0,
            parse_errors: 0,
            dropped_events: 0,
        }
    }

    /// Parse raw binary dump (concatenated protocol frames).
    pub fn parse_binary(&mut self, data: &[u8]) {
        let mut pos = 0;
        let mut prev_seq: Option<u32> = None;

        while pos + FRAME_HEADER_SIZE <= data.len() {
            // Scan for magic
            if pos + 2 > data.len() {
                break;
            }
            let magic = u16::from_le_bytes([data[pos], data[pos + 1]]);
            if magic != PROTO_MAGIC {
                pos += 1;
                self.parse_errors += 1;
                continue;
            }

            let msg_type = data[pos + 2];

            match msg_type {
                MSG_TRACE_EVENT => {
                    if pos + TRACE_FRAME_SIZE > data.len() {
                        break;
                    }
                    if let Some(ev) = decode_trace_event(&data[pos..pos + TRACE_FRAME_SIZE]) {
                        // Detect dropped events via seq gaps
                        if let Some(ps) = prev_seq {
                            if ev.seq > ps + 1 {
                                self.dropped_events += ev.seq - ps - 1;
                            }
                        }
                        prev_seq = Some(ev.seq);
                        self.trace_events.push(ev);
                    }
                    pos += TRACE_FRAME_SIZE;
                    self.raw_frames += 1;
                }
                MSG_TASK_REPORT => {
                    if pos + 5 > data.len() {
                        break;
                    }
                    let n_tasks =
                        u16::from_le_bytes([data[pos + 3], data[pos + 4]]) as usize;
                    let frame_size = 5 + n_tasks * 24;
                    if pos + frame_size > data.len() {
                        break;
                    }
                    if let Some(entries) = decode_task_report(&data[pos..pos + frame_size]) {
                        self.task_reports.push(entries);
                    }
                    pos += frame_size;
                    self.raw_frames += 1;
                }
                MSG_MEM_CORRUPTION => {
                    if pos + 10 > data.len() {
                        break;
                    }
                    if let Some(ev) = decode_mem_corruption(&data[pos..pos + 10]) {
                        self.mem_corruptions.push(ev);
                    }
                    pos += 10;
                    self.raw_frames += 1;
                }
                MSG_MEM_ALLOC | MSG_MEM_FREE => {
                    if pos + 11 > data.len() {
                        break;
                    }
                    if let Some(ev) = decode_mem_alloc_free(&data[pos..pos + 11]) {
                        self.mem_alloc_frees.push(ev);
                    }
                    pos += 11;
                    self.raw_frames += 1;
                }
                _ => {
                    pos += 1;
                    self.parse_errors += 1;
                }
            }
        }
    }

    /// Parse a hex-encoded UART log (one frame per line).
    pub fn parse_uart_log(&mut self, text: &str) {
        let mut raw = Vec::new();
        for line in text.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            match hex_decode(line) {
                Some(bytes) => raw.extend_from_slice(&bytes),
                None => self.parse_errors += 1,
            }
        }
        self.parse_binary(&raw);
    }
}

fn hex_decode(s: &str) -> Option<Vec<u8>> {
    let s = s.trim();
    if s.len() % 2 != 0 {
        return None;
    }
    let mut out = Vec::with_capacity(s.len() / 2);
    for i in (0..s.len()).step_by(2) {
        let byte = u8::from_str_radix(&s[i..i + 2], 16).ok()?;
        out.push(byte);
    }
    Some(out)
}

// ---- Analysis result (JSON-serializable) ----

#[derive(serde::Serialize)]
pub struct AnalysisResult {
    pub tasks: Vec<TaskReportEntry>,
    pub mem_events: Vec<MemCorruptionEvent>,
    pub heap: HeapState,
    pub leaks: Vec<LeakWarning>,
    pub insights: Vec<Insight>,
    pub trace_timeline: Vec<TraceTimelineEntry>,
    pub bottlenecks: Vec<Bottleneck>,
    pub stats: AnalysisStats,
}

#[derive(serde::Serialize)]
pub struct HeapState {
    pub live_blocks: usize,
    pub live_bytes: u64,
    pub peak_bytes: u64,
    pub peak_blocks: usize,
    pub total_allocs: u32,
    pub total_frees: u32,
    pub timeline: Vec<TimelinePoint>,
}

#[derive(serde::Serialize)]
pub struct TimelinePoint {
    pub t: usize, // index
    pub blocks: usize,
    pub bytes: u64,
}

#[derive(Clone, serde::Serialize)]
pub struct TraceTimelineEntry {
    pub ts: u32,
    #[serde(rename = "type")]
    pub event_type: String,
    pub task: u16,
    pub detail: String,
    pub seq: u32,
}

#[derive(serde::Serialize)]
pub struct AnalysisStats {
    pub uptime_us: u64,
    pub task_count: usize,
    pub total_events: u32,
    pub trace_events: u32,
    pub corruption_count: u32,
    pub dropped_events: u32,
    pub parse_errors: u32,
    pub source: String,
}

// ---- Run full analysis ----

pub fn analyze_dump(dump: &TraceDump, source: &str) -> AnalysisResult {
    let mut bottleneck_analyzer = BottleneckAnalyzer::new();
    let mut heap_tracker = HeapTracker::new();
    let mut correlator = CorrelationAnalyzer::new();
    let mut trace_timeline = Vec::new();

    // Task entries — use last report snapshot
    let task_entries = dump
        .task_reports
        .last()
        .cloned()
        .unwrap_or_default();

    // Process trace events
    for ev in &dump.trace_events {
        bottleneck_analyzer.process_event(ev);

        let etype_name = event_name(ev.event_type).to_string();
        let detail = match ev.event_type {
            TRACE_TASK_SWITCH => {
                let (prev, next) = decode_task_switch_payload(&ev.payload);
                format!("task {} → {}", prev, next)
            }
            TRACE_SEM_WAIT | TRACE_SEM_SIGNAL | TRACE_MTX_LOCK | TRACE_MTX_UNLOCK => {
                let (res_id, waiter) = decode_sync_payload(&ev.payload);
                format!("resource={} task={}", res_id, waiter)
            }
            _ => String::new(),
        };

        trace_timeline.push(TraceTimelineEntry {
            ts: ev.timestamp_us,
            event_type: etype_name,
            task: ev.task_id,
            detail,
            seq: ev.seq,
        });
    }

    let uptime_us = if dump.trace_events.len() >= 2 {
        let t0 = dump.trace_events.first().unwrap().timestamp_us as u64;
        let t1 = dump.trace_events.last().unwrap().timestamp_us as u64;
        t1.saturating_sub(t0)
    } else {
        0
    };

    // Memory alloc/free → heap tracking
    for ev in &dump.mem_alloc_frees {
        if ev.op == "alloc" {
            heap_tracker.on_alloc(ev);
        } else {
            heap_tracker.on_free(ev);
        }
        correlator.add_alloc_event(ev);
    }

    // Memory corruptions
    let mem_events: Vec<_> = dump.mem_corruptions.clone();
    for ev in &mem_events {
        correlator.add_corruption(ev.clone());
    }

    let bottlenecks = bottleneck_analyzer.analyze();
    let leaks = heap_tracker.detect_leak();
    let insights = correlator.analyze();

    // Build heap state
    let timeline_len = heap_tracker.timeline.len();
    let timeline_start = if timeline_len > 60 {
        timeline_len - 60
    } else {
        0
    };
    let heap = HeapState {
        live_blocks: heap_tracker.live_block_count(),
        live_bytes: heap_tracker.live_bytes(),
        peak_bytes: heap_tracker.peak_bytes,
        peak_blocks: heap_tracker.peak_blocks,
        total_allocs: heap_tracker.total_allocs,
        total_frees: heap_tracker.total_frees,
        timeline: heap_tracker.timeline[timeline_start..]
            .iter()
            .enumerate()
            .map(|(i, &(blocks, bytes))| TimelinePoint {
                t: i,
                blocks,
                bytes,
            })
            .collect(),
    };

    // Limit trace_timeline to last 200
    let tl_len = trace_timeline.len();
    let tl_start = if tl_len > 200 { tl_len - 200 } else { 0 };
    let trace_timeline = trace_timeline[tl_start..].to_vec();

    // Limit mem_events to last 50
    let me_len = mem_events.len();
    let me_start = if me_len > 50 { me_len - 50 } else { 0 };
    let mem_events = mem_events[me_start..].to_vec();

    AnalysisResult {
        tasks: task_entries.clone(),
        mem_events,
        heap,
        leaks,
        insights,
        trace_timeline,
        bottlenecks,
        stats: AnalysisStats {
            uptime_us,
            task_count: task_entries.len(),
            total_events: dump.raw_frames,
            trace_events: dump.trace_events.len() as u32,
            corruption_count: dump.mem_corruptions.len() as u32,
            dropped_events: dump.dropped_events,
            parse_errors: dump.parse_errors,
            source: source.to_string(),
        },
    }
}

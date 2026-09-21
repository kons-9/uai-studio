//! Heap tracking and correlation analysis — ported from memory_monitor.py

use std::collections::HashMap;

use crate::protocol::MemAllocFreeEvent;
use crate::protocol::MemCorruptionEvent;

// ---- Heap tracker ----

#[derive(Debug, Clone)]
struct LiveBlock {
    size: u16,
    task_id: u16,
    order: u32, // insertion order for staleness
}

pub struct HeapTracker {
    live_blocks: HashMap<u32, LiveBlock>,
    pub timeline: Vec<(usize, u64)>, // (block_count, total_bytes)  max 200
    pub total_allocs: u32,
    pub total_frees: u32,
    pub peak_bytes: u64,
    pub peak_blocks: usize,
    task_allocs: HashMap<u16, u32>,
    order_counter: u32,
}

impl HeapTracker {
    pub fn new() -> Self {
        Self {
            live_blocks: HashMap::new(),
            timeline: Vec::new(),
            total_allocs: 0,
            total_frees: 0,
            peak_bytes: 0,
            peak_blocks: 0,
            task_allocs: HashMap::new(),
            order_counter: 0,
        }
    }

    pub fn on_alloc(&mut self, ev: &MemAllocFreeEvent) {
        self.order_counter += 1;
        self.live_blocks.insert(
            ev.address,
            LiveBlock {
                size: ev.size,
                task_id: ev.task_id,
                order: self.order_counter,
            },
        );
        *self.task_allocs.entry(ev.task_id).or_insert(0) += 1;
        self.total_allocs += 1;
        self.update_peak();
        self.snapshot();
    }

    pub fn on_free(&mut self, ev: &MemAllocFreeEvent) {
        self.live_blocks.remove(&ev.address);
        if let Some(cnt) = self.task_allocs.get_mut(&ev.task_id) {
            *cnt = cnt.saturating_sub(1);
        }
        self.total_frees += 1;
        self.snapshot();
    }

    pub fn live_bytes(&self) -> u64 {
        self.live_blocks.values().map(|b| b.size as u64).sum()
    }

    pub fn live_block_count(&self) -> usize {
        self.live_blocks.len()
    }

    fn update_peak(&mut self) {
        let bytes = self.live_bytes();
        let blocks = self.live_blocks.len();
        if bytes > self.peak_bytes {
            self.peak_bytes = bytes;
        }
        if blocks > self.peak_blocks {
            self.peak_blocks = blocks;
        }
    }

    fn snapshot(&mut self) {
        let blocks = self.live_blocks.len();
        let bytes = self.live_bytes();
        self.timeline.push((blocks, bytes));
        if self.timeline.len() > 200 {
            self.timeline.remove(0);
        }
    }

    pub fn detect_leak(&self) -> Vec<LeakWarning> {
        let mut warnings = Vec::new();

        // GROWING_HEAP: last 20 entries monotonically non-decreasing block count
        if self.timeline.len() >= 20 {
            let tail = &self.timeline[self.timeline.len() - 20..];
            let growing = tail.windows(2).all(|w| w[1].0 >= w[0].0)
                && tail.last().unwrap().0 > tail.first().unwrap().0;
            if growing {
                warnings.push(LeakWarning {
                    warning_type: "GROWING_HEAP".into(),
                    description: "Heap block count monotonically increasing over last 20 samples"
                        .into(),
                    severity: "WARNING".into(),
                    task_id: None,
                });
            }
        }

        // TASK_LEAK: any task with >= 10 outstanding allocs
        for (&tid, &cnt) in &self.task_allocs {
            if cnt >= 10 {
                warnings.push(LeakWarning {
                    warning_type: "TASK_LEAK".into(),
                    description: format!(
                        "Task {} has {} outstanding allocations",
                        tid, cnt
                    ),
                    severity: "WARNING".into(),
                    task_id: Some(tid),
                });
            }
        }

        // STALE_BLOCKS: blocks from early in the trace (order < total/4) still live
        let threshold = self.order_counter / 4;
        let stale: Vec<_> = self
            .live_blocks
            .values()
            .filter(|b| b.order < threshold)
            .collect();
        if stale.len() >= 5 {
            warnings.push(LeakWarning {
                warning_type: "STALE_BLOCKS".into(),
                description: format!(
                    "{} blocks from early in the trace are still allocated",
                    stale.len()
                ),
                severity: "INFO".into(),
                task_id: None,
            });
        }

        warnings
    }
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct LeakWarning {
    pub warning_type: String,
    pub description: String,
    pub severity: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub task_id: Option<u16>,
}

// ---- Correlation analyzer ----

pub struct CorrelationAnalyzer {
    corruptions: Vec<MemCorruptionEvent>,
    allocs: Vec<MemAllocFreeEvent>,
}

impl CorrelationAnalyzer {
    pub fn new() -> Self {
        Self {
            corruptions: Vec::new(),
            allocs: Vec::new(),
        }
    }

    pub fn add_corruption(&mut self, ev: MemCorruptionEvent) {
        self.corruptions.push(ev);
    }

    pub fn add_alloc_event(&mut self, ev: &MemAllocFreeEvent) {
        self.allocs.push(ev.clone());
        if self.allocs.len() > 500 {
            self.allocs.remove(0);
        }
    }

    pub fn analyze(&self) -> Vec<Insight> {
        let mut insights = Vec::new();

        // 1. Buffer overflow hotspot
        let overflows: Vec<u32> = self
            .corruptions
            .iter()
            .filter(|c| c.corruption_type == 0x01)
            .map(|c| c.address)
            .collect();
        if overflows.len() >= 3 {
            let mut sorted = overflows.clone();
            sorted.sort();
            for w in sorted.windows(3) {
                if w[2] - w[0] < 512 {
                    let base = w[0] & !0xFF;
                    insights.push(Insight {
                        category: "ROOT_CAUSE".into(),
                        title: "Buffer overflow hotspot".into(),
                        detail: format!(
                            "3+ overflows within 512B near 0x{:08X}",
                            base
                        ),
                    });
                    break;
                }
            }
        }

        // 2. Use-after-free
        let uaf_count = self
            .corruptions
            .iter()
            .filter(|c| c.corruption_type == 0x03)
            .count();
        if uaf_count >= 2 {
            insights.push(Insight {
                category: "ROOT_CAUSE".into(),
                title: "Use-after-free pattern".into(),
                detail: format!("{} use-after-free events detected", uaf_count),
            });
        }

        // 3. Double-free
        let df: Vec<u16> = self
            .corruptions
            .iter()
            .filter(|c| c.corruption_type == 0x04)
            .map(|c| c.block_index)
            .collect();
        if !df.is_empty() {
            insights.push(Insight {
                category: "ROOT_CAUSE".into(),
                title: "Double-free detected".into(),
                detail: format!("Block indices: {:?}", df),
            });
        }

        // 4. Stack overflow
        let stack_count = self
            .corruptions
            .iter()
            .filter(|c| c.corruption_type == 0x05)
            .count();
        if stack_count > 0 {
            insights.push(Insight {
                category: "CRITICAL".into(),
                title: "Stack overflow".into(),
                detail: format!("{} stack overflow events", stack_count),
            });
        }

        // 5. Multiple corruption types
        let mut types = std::collections::HashSet::new();
        for c in &self.corruptions {
            types.insert(c.corruption_type);
        }
        if types.len() >= 3 {
            insights.push(Insight {
                category: "SYSTEMIC".into(),
                title: "Multiple corruption types".into(),
                detail: format!(
                    "{} different corruption types detected — systemic issue possible",
                    types.len()
                ),
            });
        }

        insights
    }
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct Insight {
    pub category: String,
    pub title: String,
    pub detail: String,
}

//! Bottleneck detection engine — ported from bottleneck_detector.py
//!
//! Processes a stream of TraceEvents and produces a list of detected
//! bottlenecks sorted by severity.

use std::collections::HashMap;

use crate::protocol::*;

// ---- Internal tracking structs ----

#[derive(Debug, Clone)]
struct TaskStats {
    task_id: u16,
    total_run_us: u64,
    switch_in_count: u32,
    last_switch_in_us: u32,
    wait_count: u32,
    total_wait_us: u64,
    last_wait_start_us: u32,
    max_run_us: u32,
    max_wait_us: u32,
    priority: u8,
}

impl TaskStats {
    fn new(task_id: u16) -> Self {
        Self {
            task_id,
            total_run_us: 0,
            switch_in_count: 0,
            last_switch_in_us: 0,
            wait_count: 0,
            total_wait_us: 0,
            last_wait_start_us: 0,
            max_run_us: 0,
            max_wait_us: 0,
            priority: 0,
        }
    }
}

#[derive(Debug, Clone)]
struct ResourceStats {
    lock_count: u32,
    contention_count: u32,
    total_hold_us: u64,
    max_hold_us: u32,
    last_lock_us: u32,
    owner_task: u16,
    waiters: Vec<u16>,
}

impl ResourceStats {
    fn new() -> Self {
        Self {
            lock_count: 0,
            contention_count: 0,
            total_hold_us: 0,
            max_hold_us: 0,
            last_lock_us: 0,
            owner_task: 0,
            waiters: Vec::new(),
        }
    }
}

#[derive(Debug, Clone)]
struct PriorityInversion {
    higher_task: u16,
    lower_task: u16,
    resource_id: u16,
}

// ---- Public output ----

#[derive(Debug, Clone, serde::Serialize)]
pub struct Bottleneck {
    pub severity: String,
    pub category: String,
    pub description: String,
    pub suggestion: String,
    pub metric: f64,
}

fn severity_ord(s: &str) -> u8 {
    match s {
        "CRITICAL" => 0,
        "WARNING" => 1,
        _ => 2,
    }
}

// ---- Analyzer ----

pub struct BottleneckAnalyzer {
    task_stats: HashMap<u16, TaskStats>,
    resource_stats: HashMap<u16, ResourceStats>,
    event_count: u32,
    analysis_window_us: u64,
    first_timestamp: u32,
    last_timestamp: u32,
    irq_disable_start: u32,
    irq_disable_max_us: u32,
    priority_inversions: Vec<PriorityInversion>,
}

impl BottleneckAnalyzer {
    pub fn new() -> Self {
        Self {
            task_stats: HashMap::new(),
            resource_stats: HashMap::new(),
            event_count: 0,
            analysis_window_us: 0,
            first_timestamp: 0,
            last_timestamp: 0,
            irq_disable_start: 0,
            irq_disable_max_us: 0,
            priority_inversions: Vec::new(),
        }
    }

    pub fn process_event(&mut self, ev: &TraceEvent) {
        self.event_count += 1;

        if self.first_timestamp == 0 || ev.timestamp_us < self.first_timestamp {
            self.first_timestamp = ev.timestamp_us;
        }
        if ev.timestamp_us > self.last_timestamp {
            self.last_timestamp = ev.timestamp_us;
        }
        self.analysis_window_us =
            (self.last_timestamp as u64).saturating_sub(self.first_timestamp as u64);

        // Ensure task entry exists
        self.task_stats
            .entry(ev.task_id)
            .or_insert_with(|| TaskStats::new(ev.task_id));

        match ev.event_type {
            TRACE_TASK_SWITCH => {
                let (prev_id, next_id) = decode_task_switch_payload(&ev.payload);

                // Accumulate run-time for prev task
                if let Some(prev) = self.task_stats.get_mut(&prev_id) {
                    if prev.last_switch_in_us > 0 {
                        let run = ev.timestamp_us.saturating_sub(prev.last_switch_in_us);
                        prev.total_run_us += run as u64;
                        if run > prev.max_run_us {
                            prev.max_run_us = run;
                        }
                    }
                }

                // Mark next task switch-in
                let next = self
                    .task_stats
                    .entry(next_id)
                    .or_insert_with(|| TaskStats::new(next_id));
                next.last_switch_in_us = ev.timestamp_us;
                next.switch_in_count += 1;
            }
            TRACE_TASK_WAIT => {
                if let Some(ts) = self.task_stats.get_mut(&ev.task_id) {
                    ts.wait_count += 1;
                    ts.last_wait_start_us = ev.timestamp_us;
                }
            }
            TRACE_TASK_READY => {
                if let Some(ts) = self.task_stats.get_mut(&ev.task_id) {
                    if ts.last_wait_start_us > 0 {
                        let wait = ev.timestamp_us.saturating_sub(ts.last_wait_start_us);
                        ts.total_wait_us += wait as u64;
                        if wait > ts.max_wait_us {
                            ts.max_wait_us = wait;
                        }
                        ts.last_wait_start_us = 0;
                    }
                }
            }
            TRACE_SEM_WAIT | TRACE_MTX_LOCK => {
                let (res_id, waiter_id) = decode_sync_payload(&ev.payload);
                let rs = self
                    .resource_stats
                    .entry(res_id)
                    .or_insert_with(ResourceStats::new);
                rs.lock_count += 1;

                let mut need_inversion_check = false;
                let mut owner_for_check: u16 = 0;
                if rs.owner_task != 0 && rs.owner_task != waiter_id {
                    rs.contention_count += 1;
                    if !rs.waiters.contains(&waiter_id) {
                        rs.waiters.push(waiter_id);
                    }
                    need_inversion_check = true;
                    owner_for_check = rs.owner_task;
                }
                rs.owner_task = waiter_id;
                rs.last_lock_us = ev.timestamp_us;

                // Priority inversion check (done after releasing the mutable borrow on rs)
                if need_inversion_check {
                    self.check_priority_inversion(waiter_id, owner_for_check, res_id);
                }
            }
            TRACE_SEM_SIGNAL | TRACE_MTX_UNLOCK => {
                let (res_id, _) = decode_sync_payload(&ev.payload);
                if let Some(rs) = self.resource_stats.get_mut(&res_id) {
                    if rs.last_lock_us > 0 {
                        let hold = ev.timestamp_us.saturating_sub(rs.last_lock_us);
                        rs.total_hold_us += hold as u64;
                        if hold > rs.max_hold_us {
                            rs.max_hold_us = hold;
                        }
                    }
                    rs.owner_task = 0;
                    rs.waiters.clear();
                }
            }
            TRACE_IRQ_ENTER => {
                self.irq_disable_start = ev.timestamp_us;
            }
            TRACE_IRQ_EXIT => {
                if self.irq_disable_start > 0 {
                    let dur = ev.timestamp_us.saturating_sub(self.irq_disable_start);
                    if dur > self.irq_disable_max_us {
                        self.irq_disable_max_us = dur;
                    }
                    self.irq_disable_start = 0;
                }
            }
            _ => {}
        }
    }

    fn check_priority_inversion(&mut self, waiter: u16, owner: u16, res_id: u16) {
        let waiter_prio = self
            .task_stats
            .get(&waiter)
            .map(|t| t.priority)
            .unwrap_or(0);
        let owner_prio = self
            .task_stats
            .get(&owner)
            .map(|t| t.priority)
            .unwrap_or(0);

        if waiter_prio > owner_prio {
            self.priority_inversions.push(PriorityInversion {
                higher_task: waiter,
                lower_task: owner,
                resource_id: res_id,
            });
        }
    }

    pub fn analyze(&self) -> Vec<Bottleneck> {
        let mut results = Vec::new();
        let window = self.analysis_window_us.max(1) as f64;

        // 1. CPU usage per task
        for ts in self.task_stats.values() {
            let pct = ts.total_run_us as f64 / window * 100.0;
            if pct > 80.0 {
                results.push(Bottleneck {
                    severity: "CRITICAL".into(),
                    category: "CPU_BOUND".into(),
                    description: format!(
                        "Task {} uses {:.1}% CPU",
                        ts.task_id, pct
                    ),
                    suggestion: "Consider splitting workload or lowering frequency".into(),
                    metric: pct,
                });
            } else if pct > 50.0 {
                results.push(Bottleneck {
                    severity: "WARNING".into(),
                    category: "CPU_BOUND".into(),
                    description: format!(
                        "Task {} uses {:.1}% CPU",
                        ts.task_id, pct
                    ),
                    suggestion: "Monitor for further increase".into(),
                    metric: pct,
                });
            }
        }

        // 2. Wait time
        for ts in self.task_stats.values() {
            if ts.max_wait_us > 10_000 {
                results.push(Bottleneck {
                    severity: "WARNING".into(),
                    category: "WAIT_TIME".into(),
                    description: format!(
                        "Task {} max wait {:.1}ms",
                        ts.task_id,
                        ts.max_wait_us as f64 / 1000.0
                    ),
                    suggestion: "Check resource dependencies causing long waits".into(),
                    metric: ts.max_wait_us as f64,
                });
            }
        }

        // 3. Resource contention
        for (res_id, rs) in &self.resource_stats {
            if rs.contention_count > 5 {
                results.push(Bottleneck {
                    severity: "WARNING".into(),
                    category: "CONTENTION".into(),
                    description: format!(
                        "Resource {} has {} contentions",
                        res_id, rs.contention_count
                    ),
                    suggestion: "Reduce critical section length or use lock-free design".into(),
                    metric: rs.contention_count as f64,
                });
            }
        }

        // 4. Priority inversions
        if !self.priority_inversions.is_empty() {
            results.push(Bottleneck {
                severity: "CRITICAL".into(),
                category: "PRIORITY_INVERSION".into(),
                description: format!(
                    "{} priority inversions detected",
                    self.priority_inversions.len()
                ),
                suggestion: "Use priority inheritance protocol or restructure locking".into(),
                metric: self.priority_inversions.len() as f64,
            });
        }

        // 5. IRQ latency
        if self.irq_disable_max_us > 1000 {
            results.push(Bottleneck {
                severity: "CRITICAL".into(),
                category: "IRQ_LATENCY".into(),
                description: format!(
                    "Max IRQ disable duration {:.1}ms",
                    self.irq_disable_max_us as f64 / 1000.0
                ),
                suggestion: "Reduce ISR processing time or defer work to task".into(),
                metric: self.irq_disable_max_us as f64,
            });
        }

        // 6. Jitter (scheduling jitter)
        for ts in self.task_stats.values() {
            if ts.switch_in_count >= 2 {
                let avg_run = ts.total_run_us as f64 / ts.switch_in_count as f64;
                if ts.max_run_us as f64 > avg_run * 5.0 && ts.max_run_us > 1000 {
                    results.push(Bottleneck {
                        severity: "WARNING".into(),
                        category: "JITTER".into(),
                        description: format!(
                            "Task {} max run {:.1}ms vs avg {:.1}ms",
                            ts.task_id,
                            ts.max_run_us as f64 / 1000.0,
                            avg_run / 1000.0
                        ),
                        suggestion: "Investigate variable-length processing in this task".into(),
                        metric: ts.max_run_us as f64 / avg_run,
                    });
                }
            }
        }

        // Sort: CRITICAL first, then WARNING, then INFO
        results.sort_by(|a, b| severity_ord(&a.severity).cmp(&severity_ord(&b.severity)));
        results
    }
}

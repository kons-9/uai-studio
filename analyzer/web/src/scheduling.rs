//! Scheduling dashboard — AI model inference scheduling analysis & demo data.

use rand::Rng;
use serde::Serialize;

/// Single inference request entry.
#[derive(Clone, Serialize)]
pub struct SchedulingEntry {
    pub seq_id: u32,
    pub model: String,
    pub recv_ts: f64,
    pub queue_ms: f64,
    pub infer_ms: f64,
    pub send_ms: f64,
    pub total_ms: f64,
    pub input_bytes: u32,
    pub output_bytes: u32,
    pub status: u32,
}

/// Per-model aggregated stats.
#[derive(Clone, Serialize)]
pub struct ModelStats {
    pub count: u32,
    pub total_ms: f64,
    pub infer_ms: f64,
    pub queue_ms: f64,
    pub avg_total_ms: f64,
    pub avg_infer_ms: f64,
    pub avg_queue_ms: f64,
}

/// Overall scheduling statistics.
#[derive(Clone, Serialize)]
pub struct SchedulingStats {
    pub total_requests: u32,
    pub throughput: f64,
    pub avg_total_ms: f64,
    pub avg_infer_ms: f64,
    pub avg_queue_ms: f64,
    pub max_total_ms: f64,
    pub min_total_ms: f64,
    pub utilization_pct: f64,
    pub error_count: u32,
    pub warnings: Vec<String>,
}

/// Full scheduling analysis result.
#[derive(Clone, Serialize)]
pub struct SchedulingResult {
    pub entries: Vec<SchedulingEntry>,
    pub per_model: Vec<ModelStatEntry>,
    pub stats: SchedulingStats,
}

/// Named model stats (for JSON serialization with model name as key).
#[derive(Clone, Serialize)]
pub struct ModelStatEntry {
    pub model: String,
    #[serde(flatten)]
    pub stats: ModelStats,
}

/// Analyze a list of scheduling entries and return aggregated results.
pub fn analyze_scheduling(entries: &[SchedulingEntry]) -> SchedulingResult {
    let mut per_model: std::collections::HashMap<String, ModelStats> =
        std::collections::HashMap::new();

    for e in entries {
        let s = per_model.entry(e.model.clone()).or_insert(ModelStats {
            count: 0,
            total_ms: 0.0,
            infer_ms: 0.0,
            queue_ms: 0.0,
            avg_total_ms: 0.0,
            avg_infer_ms: 0.0,
            avg_queue_ms: 0.0,
        });
        s.count += 1;
        s.total_ms += e.total_ms;
        s.infer_ms += e.infer_ms;
        s.queue_ms += e.queue_ms;
    }

    // Compute averages
    for s in per_model.values_mut() {
        let n = s.count as f64;
        s.avg_total_ms = (s.total_ms / n * 100.0).round() / 100.0;
        s.avg_infer_ms = (s.infer_ms / n * 100.0).round() / 100.0;
        s.avg_queue_ms = (s.queue_ms / n * 100.0).round() / 100.0;
    }

    // Overall stats
    let total_requests = entries.len() as u32;
    let (avg_total, avg_infer, avg_queue, max_total, min_total) = if !entries.is_empty() {
        let n = entries.len() as f64;
        let sum_total: f64 = entries.iter().map(|e| e.total_ms).sum();
        let sum_infer: f64 = entries.iter().map(|e| e.infer_ms).sum();
        let sum_queue: f64 = entries.iter().map(|e| e.queue_ms).sum();
        let max_t = entries.iter().map(|e| e.total_ms).fold(0.0_f64, f64::max);
        let min_t = entries
            .iter()
            .map(|e| e.total_ms)
            .fold(f64::MAX, f64::min);
        (sum_total / n, sum_infer / n, sum_queue / n, max_t, min_t)
    } else {
        (0.0, 0.0, 0.0, 0.0, 0.0)
    };

    // Throughput
    let throughput = if entries.len() >= 2 {
        let time_span = entries.last().unwrap().recv_ts - entries.first().unwrap().recv_ts;
        if time_span > 0.0 {
            entries.len() as f64 / time_span
        } else {
            0.0
        }
    } else {
        0.0
    };

    // Utilization
    let total_time_ms: f64 = entries.iter().map(|e| e.total_ms).sum();
    let infer_time_ms: f64 = entries.iter().map(|e| e.infer_ms).sum();
    let utilization_pct = if total_time_ms > 0.0 {
        infer_time_ms / total_time_ms * 100.0
    } else {
        0.0
    };

    let error_count = entries.iter().filter(|e| e.status != 0).count() as u32;

    // Warnings
    let mut warnings = Vec::new();
    if avg_queue > avg_infer * 0.5 {
        warnings.push(format!(
            "Queue time is significant ({:.1}ms). Consider model parallelism.",
            avg_queue
        ));
    }
    if max_total > avg_total * 3.0 {
        warnings.push(
            "High latency variance detected. Check for scheduling jitter.".to_string(),
        );
    }

    let stats = SchedulingStats {
        total_requests,
        throughput,
        avg_total_ms: avg_total,
        avg_infer_ms: avg_infer,
        avg_queue_ms: avg_queue,
        max_total_ms: max_total,
        min_total_ms: min_total,
        utilization_pct,
        error_count,
        warnings,
    };

    let per_model_vec: Vec<ModelStatEntry> = per_model
        .into_iter()
        .map(|(model, stats)| ModelStatEntry { model, stats })
        .collect();

    SchedulingResult {
        entries: entries.to_vec(),
        per_model: per_model_vec,
        stats,
    }
}

/// Generate demo scheduling data (50 requests across 3 models).
pub fn generate_demo_scheduling() -> Vec<SchedulingEntry> {
    let mut rng = rand::thread_rng();
    let mut entries = Vec::new();
    let mut recv_ts: f64 = 1000.0; // simulated base time

    let models = ["digit_classify", "threshold", "echo"];
    let model_weights: [f64; 3] = [0.5, 0.3, 0.2];

    for seq_id in 1..=50u32 {
        // Pick model by weighted random
        let r: f64 = rng.gen();
        let model_idx = if r < model_weights[0] {
            0
        } else if r < model_weights[0] + model_weights[1] {
            1
        } else {
            2
        };
        let model = models[model_idx].to_string();

        // Simulate realistic timings
        let (queue_ms, mut infer_ms) = match model_idx {
            0 => (
                rng.gen_range(0.5..3.0),
                rng.gen_range(5.0..25.0),
            ),
            1 => (
                rng.gen_range(0.1..1.0),
                rng.gen_range(0.5..3.0),
            ),
            _ => (
                rng.gen_range(0.1..0.5),
                rng.gen_range(0.01..0.1),
            ),
        };

        // Occasional spike
        if rng.gen::<f64>() < 0.05 {
            infer_ms *= rng.gen_range(3.0..8.0);
        }

        let send_ms: f64 = rng.gen_range(0.1..1.0);
        let total_ms = queue_ms + infer_ms + send_ms;

        let (input_bytes, output_bytes) = match model_idx {
            0 => (3136, 38),
            _ => (4, 4),
        };

        let status = if rng.gen::<f64>() > 0.02 { 0 } else { 2 };

        entries.push(SchedulingEntry {
            seq_id,
            model,
            recv_ts,
            queue_ms,
            infer_ms,
            send_ms,
            total_ms,
            input_bytes,
            output_bytes,
            status,
        });

        recv_ts += rng.gen_range(0.3..1.5);
    }

    entries
}

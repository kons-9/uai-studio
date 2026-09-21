//! μAI-Studio Trace Analyzer — Rust/WASM edition
//!
//! Serverless trace dump analysis that runs entirely in the browser.
//! No Python, no Flask, no backend server needed.

mod analyzer;
mod bottleneck;
pub mod bytecode;
mod demo;
mod memory;
mod protocol;
mod scheduling;

use wasm_bindgen::prelude::*;

/// Analyze a binary trace dump (.bin) and return JSON result.
#[wasm_bindgen]
pub fn analyze_binary(data: &[u8], source_name: &str) -> String {
    let mut dump = analyzer::TraceDump::new();
    dump.parse_binary(data);
    let result = analyzer::analyze_dump(&dump, source_name);
    serde_json::to_string(&result).unwrap_or_else(|e| format!("{{\"error\":\"{}\"}}", e))
}

/// Analyze a hex-encoded UART log (.log) and return JSON result.
#[wasm_bindgen]
pub fn analyze_uart_log(text: &str, source_name: &str) -> String {
    let mut dump = analyzer::TraceDump::new();
    dump.parse_uart_log(text);
    let result = analyzer::analyze_dump(&dump, source_name);
    serde_json::to_string(&result).unwrap_or_else(|e| format!("{{\"error\":\"{}\"}}", e))
}

/// Generate a demo trace dump (binary), analyze it, return JSON.
#[wasm_bindgen]
pub fn analyze_demo() -> String {
    let data = demo::generate_demo_dump();
    let mut dump = analyzer::TraceDump::new();
    dump.parse_binary(&data);
    let result = analyzer::analyze_dump(&dump, "demo (simulated)");
    serde_json::to_string(&result).unwrap_or_else(|e| format!("{{\"error\":\"{}\"}}", e))
}

/// Generate demo scheduling data and analyze it, return JSON.
#[wasm_bindgen]
pub fn analyze_scheduling_demo() -> String {
    let entries = scheduling::generate_demo_scheduling();
    let result = scheduling::analyze_scheduling(&entries);
    serde_json::to_string(&result).unwrap_or_else(|e| format!("{{\"error\":\"{}\"}}", e))
}

/// Compile a filter expression to bytecode, return JSON with hex + disassembly.
#[wasm_bindgen]
pub fn compile_filter_expr(expression: &str) -> String {
    match bytecode::compile_filter_full(expression) {
        Ok(result) => {
            serde_json::to_string(&result).unwrap_or_else(|e| format!("{{\"error\":\"{}\"}}", e))
        }
        Err(e) => format!("{{\"error\":\"{}\"}}", e),
    }
}

/// Get all available filter templates as JSON.
#[wasm_bindgen]
pub fn get_filter_templates() -> String {
    let templates = bytecode::get_filter_templates();
    serde_json::to_string(&templates).unwrap_or_else(|e| format!("{{\"error\":\"{}\"}}", e))
}


//! Bytecode compiler — filter expression → Mini-VM bytecode (matching firmware).
//!
//! Translates human-readable filter expressions like `task_id == 3 and event_type == 0x01`
//! into safe, bounded bytecode for MCU-side execution.

use serde::Serialize;

// ── Opcodes (must match firmware/mini_vm/bytecode.h) ─────────────

pub mod op {
    pub const NOP: u8 = 0x00;
    pub const PUSH_IMM8: u8 = 0x01;
    pub const PUSH_IMM16: u8 = 0x02;
    pub const PUSH_IMM32: u8 = 0x03;
    pub const DUP: u8 = 0x04;
    pub const POP: u8 = 0x05;

    pub const LOAD_TIMESTAMP: u8 = 0x10;
    pub const LOAD_TASK_ID: u8 = 0x11;
    pub const LOAD_EVENT_TYPE: u8 = 0x12;
    pub const LOAD_FLAGS: u8 = 0x13;
    pub const LOAD_SEQ: u8 = 0x14;
    pub const LOAD_PAYLOAD0: u8 = 0x15;
    pub const LOAD_PAYLOAD4: u8 = 0x16;

    pub const ADD: u8 = 0x20;
    pub const SUB: u8 = 0x21;
    pub const MUL: u8 = 0x22;
    pub const DIV: u8 = 0x23;
    pub const MOD: u8 = 0x24;

    pub const AND: u8 = 0x30;
    pub const OR: u8 = 0x31;
    pub const XOR: u8 = 0x32;
    pub const NOT: u8 = 0x33;
    pub const SHL: u8 = 0x34;
    pub const SHR: u8 = 0x35;

    pub const EQ: u8 = 0x40;
    pub const NE: u8 = 0x41;
    pub const LT: u8 = 0x42;
    pub const LE: u8 = 0x43;
    pub const GT: u8 = 0x44;
    pub const GE: u8 = 0x45;

    pub const LOGIC_AND: u8 = 0x50;
    pub const LOGIC_OR: u8 = 0x51;
    pub const LOGIC_NOT: u8 = 0x52;

    pub const RET: u8 = 0xFF;
}

// ── Opcode metadata ──────────────────────────────────────────────

fn opcode_name(op: u8) -> &'static str {
    match op {
        op::NOP => "NOP",
        op::PUSH_IMM8 => "PUSH_IMM8",
        op::PUSH_IMM16 => "PUSH_IMM16",
        op::PUSH_IMM32 => "PUSH_IMM32",
        op::DUP => "DUP",
        op::POP => "POP",
        op::LOAD_TIMESTAMP => "LOAD_TIMESTAMP",
        op::LOAD_TASK_ID => "LOAD_TASK_ID",
        op::LOAD_EVENT_TYPE => "LOAD_EVENT_TYPE",
        op::LOAD_FLAGS => "LOAD_FLAGS",
        op::LOAD_SEQ => "LOAD_SEQ",
        op::LOAD_PAYLOAD0 => "LOAD_PAYLOAD0",
        op::LOAD_PAYLOAD4 => "LOAD_PAYLOAD4",
        op::ADD => "ADD",
        op::SUB => "SUB",
        op::MUL => "MUL",
        op::DIV => "DIV",
        op::MOD => "MOD",
        op::AND => "AND",
        op::OR => "OR",
        op::XOR => "XOR",
        op::NOT => "NOT",
        op::SHL => "SHL",
        op::SHR => "SHR",
        op::EQ => "EQ",
        op::NE => "NE",
        op::LT => "LT",
        op::LE => "LE",
        op::GT => "GT",
        op::GE => "GE",
        op::LOGIC_AND => "LOGIC_AND",
        op::LOGIC_OR => "LOGIC_OR",
        op::LOGIC_NOT => "LOGIC_NOT",
        op::RET => "RET",
        _ => "???",
    }
}

fn imm_size(opcode: u8) -> usize {
    match opcode {
        op::PUSH_IMM8 => 1,
        op::PUSH_IMM16 => 2,
        op::PUSH_IMM32 => 4,
        _ => 0,
    }
}

// ── Field name → LOAD opcode ─────────────────────────────────────

fn field_load_opcode(field: &str) -> Option<u8> {
    match field {
        "timestamp" => Some(op::LOAD_TIMESTAMP),
        "task_id" => Some(op::LOAD_TASK_ID),
        "event_type" => Some(op::LOAD_EVENT_TYPE),
        "flags" => Some(op::LOAD_FLAGS),
        "seq" => Some(op::LOAD_SEQ),
        "payload0" => Some(op::LOAD_PAYLOAD0),
        "payload4" => Some(op::LOAD_PAYLOAD4),
        _ => None,
    }
}

fn is_field_name(s: &str) -> bool {
    field_load_opcode(s).is_some()
}

// ── Comparison operator → opcode ─────────────────────────────────

fn cmp_opcode(op_str: &str) -> Option<u8> {
    match op_str {
        "==" => Some(op::EQ),
        "!=" => Some(op::NE),
        "<" => Some(op::LT),
        "<=" => Some(op::LE),
        ">" => Some(op::GT),
        ">=" => Some(op::GE),
        _ => None,
    }
}

// ── Named event types ────────────────────────────────────────────

fn event_type_value(name: &str) -> Option<u32> {
    match name {
        "TASK_SWITCH" => Some(0x01),
        "TASK_READY" => Some(0x02),
        "TASK_WAIT" => Some(0x03),
        "TASK_DORMANT" => Some(0x04),
        "SEM_SIGNAL" => Some(0x10),
        "SEM_WAIT" => Some(0x11),
        "MTX_LOCK" => Some(0x12),
        "MTX_UNLOCK" => Some(0x13),
        "MEM_ALLOC" => Some(0x20),
        "MEM_FREE" => Some(0x21),
        "MEM_CORRUPTION" => Some(0x22),
        "IRQ_ENTER" => Some(0x30),
        "IRQ_EXIT" => Some(0x31),
        "USER_EVENT" => Some(0x40),
        _ => None,
    }
}

// ── VM limits ────────────────────────────────────────────────────

const VM_MAX_PROGRAM: usize = 256;
const VM_MAX_INSNS: usize = 64;

// ── Token ────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
enum TokenKind {
    Field,
    Number,
    Cmp,
    Logic,
    Not,
    LParen,
    RParen,
}

#[derive(Debug, Clone)]
struct Token {
    kind: TokenKind,
    value: String,
}

fn tokenize(expr: &str) -> Result<Vec<Token>, String> {
    let s = expr.trim().as_bytes();
    let mut tokens = Vec::new();
    let mut i = 0;

    while i < s.len() {
        // Skip whitespace
        if s[i].is_ascii_whitespace() {
            i += 1;
            continue;
        }

        // 2-char comparison operators
        if i + 1 < s.len() {
            let two = &expr[i..i + 2];
            if matches!(two, "==" | "!=" | "<=" | ">=") {
                tokens.push(Token {
                    kind: TokenKind::Cmp,
                    value: two.to_string(),
                });
                i += 2;
                continue;
            }
        }

        // 1-char comparison
        if s[i] == b'<' || s[i] == b'>' {
            tokens.push(Token {
                kind: TokenKind::Cmp,
                value: (s[i] as char).to_string(),
            });
            i += 1;
            continue;
        }

        // Parentheses
        if s[i] == b'(' {
            tokens.push(Token {
                kind: TokenKind::LParen,
                value: "(".to_string(),
            });
            i += 1;
            continue;
        }
        if s[i] == b')' {
            tokens.push(Token {
                kind: TokenKind::RParen,
                value: ")".to_string(),
            });
            i += 1;
            continue;
        }

        // Hex numbers
        if i + 1 < s.len() && s[i] == b'0' && (s[i + 1] == b'x' || s[i + 1] == b'X') {
            let start = i;
            i += 2;
            while i < s.len() && s[i].is_ascii_hexdigit() {
                i += 1;
            }
            tokens.push(Token {
                kind: TokenKind::Number,
                value: expr[start..i].to_string(),
            });
            continue;
        }

        // Decimal numbers
        if s[i].is_ascii_digit() {
            let start = i;
            while i < s.len() && s[i].is_ascii_digit() {
                i += 1;
            }
            tokens.push(Token {
                kind: TokenKind::Number,
                value: expr[start..i].to_string(),
            });
            continue;
        }

        // Keywords / field names
        if s[i].is_ascii_alphabetic() || s[i] == b'_' {
            let start = i;
            while i < s.len() && (s[i].is_ascii_alphanumeric() || s[i] == b'_') {
                i += 1;
            }
            let word = &expr[start..i];
            let upper = word.to_uppercase();

            if upper == "AND" {
                tokens.push(Token {
                    kind: TokenKind::Logic,
                    value: "and".to_string(),
                });
            } else if upper == "OR" {
                tokens.push(Token {
                    kind: TokenKind::Logic,
                    value: "or".to_string(),
                });
            } else if upper == "NOT" {
                tokens.push(Token {
                    kind: TokenKind::Not,
                    value: "not".to_string(),
                });
            } else if is_field_name(word) {
                tokens.push(Token {
                    kind: TokenKind::Field,
                    value: word.to_string(),
                });
            } else if let Some(val) = event_type_value(&upper) {
                tokens.push(Token {
                    kind: TokenKind::Number,
                    value: val.to_string(),
                });
            } else {
                return Err(format!("Unknown identifier: '{}'", word));
            }
            continue;
        }

        return Err(format!(
            "Unexpected character: '{}' at position {}",
            s[i] as char, i
        ));
    }

    Ok(tokens)
}

// ── Bytecode emitter ─────────────────────────────────────────────

struct BytecodeEmitter {
    code: Vec<u8>,
}

impl BytecodeEmitter {
    fn new() -> Self {
        Self { code: Vec::new() }
    }

    fn emit_byte(&mut self, b: u8) {
        self.code.push(b);
    }

    fn emit_push(&mut self, value: u32) {
        if value <= 0xFF {
            self.emit_byte(op::PUSH_IMM8);
            self.emit_byte(value as u8);
        } else if value <= 0xFFFF {
            self.emit_byte(op::PUSH_IMM16);
            self.emit_byte(value as u8);
            self.emit_byte((value >> 8) as u8);
        } else {
            self.emit_byte(op::PUSH_IMM32);
            self.emit_byte(value as u8);
            self.emit_byte((value >> 8) as u8);
            self.emit_byte((value >> 16) as u8);
            self.emit_byte((value >> 24) as u8);
        }
    }

    fn emit_load(&mut self, field: &str) -> Result<(), String> {
        match field_load_opcode(field) {
            Some(op) => {
                self.emit_byte(op);
                Ok(())
            }
            None => Err(format!("Unknown field: '{}'", field)),
        }
    }

    fn emit_cmp(&mut self, op_str: &str) -> Result<(), String> {
        match cmp_opcode(op_str) {
            Some(op) => {
                self.emit_byte(op);
                Ok(())
            }
            None => Err(format!("Unknown comparison: '{}'", op_str)),
        }
    }

    fn emit_logic(&mut self, op_str: &str) {
        match op_str {
            "and" => self.emit_byte(op::LOGIC_AND),
            "or" => self.emit_byte(op::LOGIC_OR),
            _ => {}
        }
    }

    fn emit_ret(&mut self) {
        self.emit_byte(op::RET);
    }

    fn validate(&self) -> Result<(), String> {
        if self.code.len() > VM_MAX_PROGRAM {
            return Err(format!(
                "Program too large: {} > {}",
                self.code.len(),
                VM_MAX_PROGRAM
            ));
        }
        let mut insn_count = 0;
        let mut i = 0;
        while i < self.code.len() {
            insn_count += 1;
            let opcode = self.code[i];
            i += 1 + imm_size(opcode);
        }
        if insn_count > VM_MAX_INSNS {
            return Err(format!(
                "Too many instructions: {} > {}",
                insn_count, VM_MAX_INSNS
            ));
        }
        Ok(())
    }

    fn get_bytes(&self) -> Vec<u8> {
        self.code.clone()
    }
}

// ── Recursive descent parser ─────────────────────────────────────
//
// Grammar:
//   expr     → or_expr
//   or_expr  → and_expr ("or" and_expr)*
//   and_expr → not_expr ("and" not_expr)*
//   not_expr → "not" not_expr | atom
//   atom     → "(" expr ")" | comparison
//   comparison → field cmp_op value

struct FilterCompiler {
    tokens: Vec<Token>,
    pos: usize,
    emitter: BytecodeEmitter,
}

impl FilterCompiler {
    fn new(tokens: Vec<Token>) -> Self {
        Self {
            tokens,
            pos: 0,
            emitter: BytecodeEmitter::new(),
        }
    }

    fn peek(&self) -> Option<&Token> {
        self.tokens.get(self.pos)
    }

    fn advance(&mut self) -> &Token {
        let tok = &self.tokens[self.pos];
        self.pos += 1;
        tok
    }

    fn expect(&mut self, expected_kind: &str) -> Result<Token, String> {
        match self.peek() {
            Some(tok) if self.kind_str(tok) == expected_kind => {
                let t = tok.clone();
                self.pos += 1;
                Ok(t)
            }
            Some(tok) => Err(format!("Expected {}, got '{}'", expected_kind, tok.value)),
            None => Err(format!("Expected {}, got EOF", expected_kind)),
        }
    }

    fn kind_str(&self, tok: &Token) -> &'static str {
        match tok.kind {
            TokenKind::Field => "field",
            TokenKind::Number => "number",
            TokenKind::Cmp => "cmp",
            TokenKind::Logic => "logic",
            TokenKind::Not => "not",
            TokenKind::LParen => "lparen",
            TokenKind::RParen => "rparen",
        }
    }

    fn compile(mut self) -> Result<Vec<u8>, String> {
        self.parse_or_expr()?;
        self.emitter.emit_ret();
        self.emitter.validate()?;

        if self.pos < self.tokens.len() {
            return Err(format!(
                "Unexpected token: '{}'",
                self.tokens[self.pos].value
            ));
        }

        Ok(self.emitter.get_bytes())
    }

    fn parse_or_expr(&mut self) -> Result<(), String> {
        self.parse_and_expr()?;
        loop {
            match self.peek() {
                Some(tok) if matches!(tok.kind, TokenKind::Logic) && tok.value == "or" => {
                    self.pos += 1;
                    self.parse_and_expr()?;
                    self.emitter.emit_logic("or");
                }
                _ => break,
            }
        }
        Ok(())
    }

    fn parse_and_expr(&mut self) -> Result<(), String> {
        self.parse_not_expr()?;
        loop {
            match self.peek() {
                Some(tok) if matches!(tok.kind, TokenKind::Logic) && tok.value == "and" => {
                    self.pos += 1;
                    self.parse_not_expr()?;
                    self.emitter.emit_logic("and");
                }
                _ => break,
            }
        }
        Ok(())
    }

    fn parse_not_expr(&mut self) -> Result<(), String> {
        if let Some(tok) = self.peek() {
            if matches!(tok.kind, TokenKind::Not) {
                self.pos += 1;
                self.parse_not_expr()?;
                self.emitter.emit_byte(op::LOGIC_NOT);
                return Ok(());
            }
        }
        self.parse_atom()
    }

    fn parse_atom(&mut self) -> Result<(), String> {
        match self.peek() {
            Some(tok) if matches!(tok.kind, TokenKind::LParen) => {
                self.pos += 1;
                self.parse_or_expr()?;
                self.expect("rparen")?;
                Ok(())
            }
            Some(tok) if matches!(tok.kind, TokenKind::Field) => self.parse_comparison(),
            Some(tok) => Err(format!("Expected field name or '(', got '{}'", tok.value)),
            None => Err("Expected field name or '(', got EOF".to_string()),
        }
    }

    fn parse_comparison(&mut self) -> Result<(), String> {
        let field_tok = self.expect("field")?;
        let cmp_tok = self.expect("cmp")?;
        let val_tok = self.expect("number")?;

        let value = parse_number(&val_tok.value)?;

        self.emitter.emit_load(&field_tok.value)?;
        self.emitter.emit_push(value);
        self.emitter.emit_cmp(&cmp_tok.value)?;
        Ok(())
    }
}

fn parse_number(s: &str) -> Result<u32, String> {
    if s.starts_with("0x") || s.starts_with("0X") {
        u32::from_str_radix(&s[2..], 16).map_err(|e| format!("Invalid hex number '{}': {}", s, e))
    } else {
        s.parse::<u32>()
            .map_err(|e| format!("Invalid number '{}': {}", s, e))
    }
}

// ── Public API ───────────────────────────────────────────────────

/// Compile a filter expression to bytecode.
pub fn compile_filter(expression: &str) -> Result<Vec<u8>, String> {
    let tokens = tokenize(expression)?;
    if tokens.is_empty() {
        return Err("Empty filter expression".to_string());
    }
    FilterCompiler::new(tokens).compile()
}

/// Disassemble bytecode to human-readable text.
pub fn disassemble(bytecode: &[u8]) -> String {
    let mut lines = Vec::new();
    let mut i = 0;

    while i < bytecode.len() {
        let addr = i;
        let opcode = bytecode[i];
        let name = opcode_name(opcode);
        i += 1;

        let imm_sz = imm_size(opcode);
        if imm_sz > 0 && i + imm_sz <= bytecode.len() {
            let imm = match imm_sz {
                1 => bytecode[i] as u32,
                2 => u16::from_le_bytes([bytecode[i], bytecode[i + 1]]) as u32,
                4 => u32::from_le_bytes([
                    bytecode[i],
                    bytecode[i + 1],
                    bytecode[i + 2],
                    bytecode[i + 3],
                ]),
                _ => 0,
            };
            i += imm_sz;
            lines.push(format!("  {:04X}: {:<16} {} (0x{:X})", addr, name, imm, imm));
        } else {
            lines.push(format!("  {:04X}: {}", addr, name));
        }
    }

    lines.join("\n")
}

/// Encode bytecode as hex string.
pub fn bytecode_to_hex(bytecode: &[u8]) -> String {
    bytecode.iter().map(|b| format!("{:02x}", b)).collect()
}

// ── Compile result (for WASM JSON return) ────────────────────────

#[derive(Serialize)]
pub struct CompileResult {
    pub expression: String,
    pub bytecode_hex: String,
    pub bytecode_len: usize,
    pub disassembly: String,
}

/// Compile a filter and return a full result struct.
pub fn compile_filter_full(expression: &str) -> Result<CompileResult, String> {
    let bytecode = compile_filter(expression)?;
    let hex = bytecode_to_hex(&bytecode);
    let disasm = disassemble(&bytecode);
    Ok(CompileResult {
        expression: expression.to_string(),
        bytecode_hex: hex,
        bytecode_len: bytecode.len(),
        disassembly: disasm,
    })
}

// ── Filter templates ─────────────────────────────────────────────

#[derive(Serialize)]
pub struct FilterTemplate {
    pub name: String,
    pub description: String,
    pub template: String,
}

pub fn get_filter_templates() -> Vec<FilterTemplate> {
    vec![
        FilterTemplate {
            name: "task_only".into(),
            description: "Filter events for a specific task ID".into(),
            template: "task_id == {task_id}".into(),
        },
        FilterTemplate {
            name: "event_type_only".into(),
            description: "Filter events by type".into(),
            template: "event_type == {event_type}".into(),
        },
        FilterTemplate {
            name: "cpu_hog".into(),
            description: "Task switch events only (for CPU analysis)".into(),
            template: "event_type == 0x01".into(),
        },
        FilterTemplate {
            name: "sync_events".into(),
            description: "Semaphore and mutex events only".into(),
            template: "event_type == 0x10 or event_type == 0x11 or event_type == 0x12 or event_type == 0x13".into(),
        },
        FilterTemplate {
            name: "memory_events".into(),
            description: "Memory allocation, free, and corruption events".into(),
            template: "event_type == 0x20 or event_type == 0x21 or event_type == 0x22".into(),
        },
        FilterTemplate {
            name: "irq_events".into(),
            description: "IRQ enter/exit events only".into(),
            template: "event_type == 0x30 or event_type == 0x31".into(),
        },
    ]
}

use std::collections::HashMap;
use std::fs::File;
use std::io::{BufRead, BufReader, Read};

enum Instructions {
    ADD = 0x01,
    SUB,
    PRINT,
    JMP,
    PUSH,
    POP,
    SET,
    CALL,
    RET,
    LSR,
}

struct VirtualMachine {
    text: Vec<u32>,
    stack: Vec<u32>,
    reg0: u32,
    ip: i32,
    sp: i32,
    sbp: i32,
}

impl VirtualMachine {
    fn new() -> VirtualMachine {
        VirtualMachine {
            text: Vec::new(),
            stack: Vec::new(),
            reg0: 0,
            ip: 0,
            sp: 0,
            sbp: 0,
        }
    }

    fn run_vm(&mut self) {
        loop {
            let op = self.text[self.ip as usize];
            match op {
                0 => break,
                _ => {
                    self.ip += 1;
                }
            }
        }
    }
}

fn assemble_file(filename: &str) {
    let file = File::open(filename).expect("file not found");
    let mut new_file = File::create("output").expect("could not create file");
    let labels: HashMap<&str, i32> = HashMap::new();

    let reader = BufReader::new(file);
    for line in reader.lines() {
        let line = line.unwrap();
        let mut parts = line.split(" ").collect::<Vec<&str>>();

        'stop: loop {
            if parts.len() == 0 {
                break 'stop;
            }

            let token = parts.pop().unwrap();
            match token {
                "ADD" => {}
                _ => {
                    println!("Unknown instruction: {}", token);
                }
            }
        }
    }
}

fn main() {
    assemble_file("test.s");
    let mut vm = VirtualMachine::new();
    //vm.run_vm();
}

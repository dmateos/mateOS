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
    ip: usize,
    sp: usize,
    sbp: usize,
}

impl VirtualMachine {
    fn new(filename: &str, offset: i32) -> VirtualMachine {
        let file = File::open(filename).expect("file not found");

        return VirtualMachine {
            text: Vec::new(),
            stack: Vec::new(),
            reg0: 0,
            ip: 0,
            sp: 0,
            sbp: 0,
        };
    }

    fn print_state(&self) {
        println!("VM State");
        println!("{}", self.reg0);
        println!("{}", self.ip);
        println!("{}", self.sp);
        println!("{}", self.sbp);
    }

    fn run_vm(&mut self) {
        println!("Running:");
        loop {
            match self.text[self.ip] {
                0x1 => {
                    self.ip += 1;
                    self.reg0 += self.text[self.ip];
                    self.ip += 1;
                }
                0x02 => {
                    self.ip += 1;
                    self.reg0 -= self.text[self.ip];
                    self.ip += 1;
                }
                0x03 => {
                    self.ip += 1;
                    println!("{}", self.reg0);
                }
                0x04 => {
                    self.ip += 1;
                    self.ip = self.text[self.ip] as usize;
                }
                0x05 => {
                    self.ip += 1;
                    self.sp += 1;
                    self.stack.push(self.reg0);
                }
                0x06 => {
                    self.ip += 1;
                    self.sp -= 1;
                    self.reg0 = self.stack.pop().unwrap();
                }
                0x07 => {
                    self.ip += 1;
                    self.reg0 = self.text[self.ip];
                    self.ip += 1;
                }
                0x08 => {
                    self.ip += 1;
                    self.sbp = self.sp;
                    self.stack[self.sp] = (self.ip + 1) as u32;
                    self.sp += 1;
                    self.ip = self.text[self.ip] as usize;
                }
                0x09 => {
                    self.sp -= 1;
                    self.ip = self.stack[self.sp] as usize;
                }
                0x0A => break,
                _ => {}
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
    //assemble_file("test.s");
    let mut vm = VirtualMachine::new("output", 100);
    vm.run_vm();
    vm.print_state();
}

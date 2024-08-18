enum Instructions {
    ADD,
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

fn assemble_file(filename: &str) {}

fn main() {
    let mut vm = VirtualMachine::new();
    vm.run_vm();
}

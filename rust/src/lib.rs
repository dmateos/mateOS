#![no_std]
#![no_main]

use core::panic::PanicInfo;

// External C functions we can call
extern "C" {
    fn printf(fmt: *const u8, ...);
}

// Panic handler required for no_std
#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    // In a real kernel, you'd log this somewhere
    let msg = b"Rust panic!\n\0";
    unsafe {
        printf(msg.as_ptr());
    }

    // Print location if available
    if let Some(location) = info.location() {
        let file_msg = b"  at %s:%d\n\0";
        unsafe {
            printf(file_msg.as_ptr(),
                   location.file().as_ptr(),
                   location.line());
        }
    }

    loop {}
}

// Simple greeting function
#[no_mangle]
pub extern "C" fn rust_hello() {
    unsafe {
        printf(b"Hello from Rust! (no_std mode)\n\0".as_ptr());
    }
}

// Math example - add two numbers
#[no_mangle]
pub extern "C" fn rust_add(a: i32, b: i32) -> i32 {
    a + b
}

// Math example - factorial (recursive)
#[no_mangle]
pub extern "C" fn rust_factorial(n: u32) -> u32 {
    match n {
        0 | 1 => 1,
        _ => n * rust_factorial(n - 1),
    }
}

// String operations without heap
#[no_mangle]
pub extern "C" fn rust_strlen(s: *const u8) -> usize {
    if s.is_null() {
        return 0;
    }

    let mut len = 0;
    unsafe {
        while *s.add(len) != 0 {
            len += 1;
        }
    }
    len
}

// Check if string is palindrome (stack only)
#[no_mangle]
pub extern "C" fn rust_is_palindrome(s: *const u8) -> bool {
    if s.is_null() {
        return false;
    }

    let len = rust_strlen(s);
    if len == 0 {
        return true;
    }

    unsafe {
        for i in 0..len / 2 {
            if *s.add(i) != *s.add(len - 1 - i) {
                return false;
            }
        }
    }

    true
}

// FizzBuzz example - shows Rust pattern matching
#[no_mangle]
pub extern "C" fn rust_fizzbuzz(n: u32) {
    for i in 1..=n {
        match (i % 3, i % 5) {
            (0, 0) => unsafe { printf(b"FizzBuzz\n\0".as_ptr()) },
            (0, _) => unsafe { printf(b"Fizz\n\0".as_ptr()) },
            (_, 0) => unsafe { printf(b"Buzz\n\0".as_ptr()) },
            (_, _) => unsafe { printf(b"%d\n\0".as_ptr(), i) },
        }
    }
}

// Struct example with #[repr(C)] for C interop
#[repr(C)]
#[derive(Copy, Clone)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

#[no_mangle]
pub extern "C" fn rust_point_distance_squared(p1: Point, p2: Point) -> i32 {
    let dx = p1.x - p2.x;
    let dy = p1.y - p2.y;
    dx * dx + dy * dy
}

#[no_mangle]
pub extern "C" fn rust_point_print(p: Point) {
    unsafe {
        printf(b"Point(%d, %d)\n\0".as_ptr(), p.x, p.y);
    }
}

// Array operations without heap
#[no_mangle]
pub extern "C" fn rust_array_sum(arr: *const i32, len: usize) -> i32 {
    if arr.is_null() {
        return 0;
    }

    let mut sum = 0;
    for i in 0..len {
        unsafe {
            sum += *arr.add(i);
        }
    }
    sum
}

#[no_mangle]
pub extern "C" fn rust_array_max(arr: *const i32, len: usize) -> i32 {
    if arr.is_null() || len == 0 {
        return 0;
    }

    unsafe {
        let mut max = *arr;
        for i in 1..len {
            let val = *arr.add(i);
            if val > max {
                max = val;
            }
        }
        max
    }
}

// Demonstrate Rust iterators (no heap needed for simple cases)
#[no_mangle]
pub extern "C" fn rust_sum_of_squares(n: u32) -> u32 {
    (1..=n).map(|x| x * x).sum()
}

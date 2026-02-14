#![no_std]

use core::panic::PanicInfo;

const W: usize = 500;
const H: usize = 350;
const BUF_LEN: usize = W * H;

static mut BUF: [u8; BUF_LEN] = [0; BUF_LEN];

unsafe extern "C" {
    fn win_create(width: i32, height: i32, title: *const u8) -> i32;
    fn win_destroy(wid: i32) -> i32;
    fn win_write(wid: i32, data: *const u8, len: u32) -> i32;
    fn win_getkey(wid: i32) -> i32;
    fn detach() -> i32;
    fn r#yield() -> ();
    fn exit(code: i32) -> !;
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    unsafe { exit(1) }
}

fn fill_rect(buf: &mut [u8], bw: usize, x: usize, y: usize, w: usize, h: usize, c: u8) {
    for yy in y..(y + h) {
        let row = yy * bw;
        for xx in x..(x + w) {
            buf[row + xx] = c;
        }
    }
}

fn draw_demo(buf: &mut [u8]) {
    // Retro-ish panel
    fill_rect(buf, W, 0, 0, W, H, 7);
    fill_rect(buf, W, 0, 0, W, 12, 1);
    fill_rect(buf, W, 0, H - 1, W, 1, 0);
    fill_rect(buf, W, 0, 0, 1, H, 15);
    fill_rect(buf, W, W - 1, 0, 1, H, 0);

    // Simple window block
    fill_rect(buf, W, 40, 50, 420, 240, 15);
    fill_rect(buf, W, 40, 50, 420, 14, 9);
    fill_rect(buf, W, 40, 289, 420, 1, 0);
    fill_rect(buf, W, 40, 50, 1, 240, 15);
    fill_rect(buf, W, 459, 50, 1, 240, 0);

    // Colored blocks so it is obvious Rust drew something.
    fill_rect(buf, W, 74, 96, 90, 70, 2);
    fill_rect(buf, W, 184, 96, 90, 70, 3);
    fill_rect(buf, W, 294, 96, 90, 70, 4);
    fill_rect(buf, W, 129, 186, 90, 70, 5);
    fill_rect(buf, W, 239, 186, 90, 70, 6);
}

#[no_mangle]
pub extern "C" fn _start(_argc: i32, _argv: *const *const u8) -> ! {
    let title = b"Hello (Rust)\0";
    let wid = unsafe { win_create(W as i32, H as i32, title.as_ptr()) };
    if wid < 0 {
        unsafe { exit(1) }
    }

    unsafe {
        let _ = detach();
    }

    unsafe {
        let p = core::ptr::addr_of_mut!(BUF) as *mut u8;
        let s = core::slice::from_raw_parts_mut(p, BUF_LEN);
        draw_demo(s);
        let _ = win_write(wid, core::ptr::addr_of!(BUF) as *const u8, BUF_LEN as u32);
    }

    loop {
        let key = unsafe { win_getkey(wid) };
        if key == 'q' as i32 || key == 27 {
            break;
        }
        unsafe { r#yield() };
    }

    unsafe {
        let _ = win_destroy(wid);
        exit(0);
    }
}

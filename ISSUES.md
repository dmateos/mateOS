# mateOS Known Issues

Tracked issues from codebase audit (2026-02-26). Work through top-to-bottom.

## Legend
- [ ] Open
- [x] Fixed

---

## CRITICAL

### ~~1. task_kill crashes caller — CR3 not restored after killing another task~~
- [x] **FIXED** — Save/restore CR3 in `task_terminate`; only keep kernel CR3 when terminating self.

---

## HIGH

### ~~2. paging_map_page silently fails on PMM OOM~~
- [x] **FIXED** — Changed return type to `int` (-1 on OOM). All callers now check and handle failure.

### ~~3. net_linkoutput doesn't flatten pbuf chains~~
- [x] **FIXED** — Use `pbuf_copy_partial` into a 1536-byte TX buffer before sending.

### ~~4. sock_recv_cb acknowledges dropped data~~
- [x] **FIXED** — Track `total_pushed` into ring buffer; only `tcp_recved` that amount.

### ~~5. ELF segment validation missing~~
- [x] **FIXED** — Validate phdr table bounds, segment offset/filesz against file size, and vaddr within user region.

---

## MEDIUM

### ~~6. fd_table kmalloc failure not handled~~
- [x] **FIXED** — Return NULL from task_create on fd_table alloc failure; free kernel stack and destroy address space.

### ~~7. PMM double-free not detected~~
- [x] **FIXED** — Check `bitmap_test(idx)` before clearing; log and skip on double-free.

### ~~8. ramfs bounce buffer not reentrant~~
- [x] **FIXED** — Disable interrupts (save/restore EFLAGS) around the bounce copy loop.

### ~~9. FAT16 fat16_free_chain can loop infinitely on corrupt FAT~~
- [x] **FIXED** — Added max iteration counter (`cluster_count + 2`) to break cycles.

### ~~10. FAT16 write partial success + metadata inconsistency~~
- [x] **FIXED** — `fat16_update_dirent` failure now logs warning instead of silently returning -1 after successful write.

### ~~11. net_ping global state not safe for concurrent pings~~
- [x] **FIXED** — Added `ping_in_progress` flag; reject concurrent pings with -1.

### ~~12. vfs_proc vgen_buf not reentrant~~
- [x] **FIXED** — Disable interrupts around `vgen_buf` generation and copy in both read and size functions.

### ~~13. calloc doesn't check multiplication overflow~~
- [x] **FIXED** — Added overflow check: `if (n && sz > (size_t)-1 / n) return NULL`.

### ~~14. httpd send_all loops forever on persistent error~~
- [x] **FIXED** — Return -1 on `sock_send` error; cap would-block retries at 500.

---

All 14 issues resolved. Build verified: `make clean && make` — zero new errors.

# Phần user của sysinfo và integration test

Branch: `feat/sysinfo-user-integration`, phát triển từ `riscv`.

Phạm vi gồm ABI đề xuất, khai báo/stub syscall, lệnh `sysinfo`, chương trình
`sysinfotest` và script chạy test. Chưa có implementation thu thập dữ liệu trong
kernel. Không có handler giả được đưa vào xv6 để làm test pass.

## Những file đã thêm hoặc sửa

| File | Vai trò |
| --- | --- |
| `kernel/sysinfo.h` | Cấu trúc và hằng số dùng chung giữa kernel/user |
| `kernel/syscall.h` | Dành số 24 cho `SYS_sysinfo` |
| `user/user.h` | Forward declaration và prototype `int sysinfo(struct sysinfo *)` |
| `user/usys.pl` | Sinh stub đặt số syscall vào `a7`, thực hiện `ecall` |
| `user/sysinfo.c` | In bảng file/process; kiểm tra kích thước các danh sách; trả lỗi khi syscall thất bại |
| `user/sysinfotest.c` | 13 ca integration, kiểm tra bằng assertion và exit status |
| `tests/test_sysinfo.py` | Build, boot QEMU, chạy suite trên 1 và 3 CPU, lưu log, phát hiện lỗi/timeout |
| `tests/test_sysinfo_cli.py` | 7 host test cho code hiển thị thực tế, sử dụng snapshot giả lập |
| `tests/fixtures/sysinfo_cli_host.c` | Dữ liệu và hàm I/O dành riêng cho host test, không build vào xv6 |
| `Makefile` | Đưa hai chương trình vào `fs.img`, thêm target `test-sysinfo` |

## Giao diện cần thống nhất với người viết kernel

Include `kernel/types.h` trước `kernel/sysinfo.h` ở phía user. Phía kernel dùng
`types.h`, sau đó `sysinfo.h`. Không include `proc.h`/`file.h` từ user chỉ để lấy
enum: header chung đã có các hằng `SYSINFO_*` tương ứng.

```c
struct sysinfo;
int sysinfo(struct sysinfo *out); // 0: thành công; -1: thất bại
```

`out` phải trỏ tới vùng writable có ít nhất `sizeof(struct sysinfo)` byte.
Với `NFILE=100`, `NPROC=64`, `NOFILE=16` của repo hiện tại, cấu trúc trên RISC-V
64 bit chiếm **37336 byte**. Chương trình user đặt nó ở vùng static/BSS, không đặt
trên stack một trang. Kernel cũng không được đặt cấu trúc này trên kernel stack
hoặc coi một trang từ `kalloc()` là đủ. Cần tự quản lý buffer nhiều trang hoặc
copy từng phần có kiểm soát. Không dùng một buffer static chung mà thiếu bảo vệ
khi nhiều process gọi syscall đồng thời.

Các quy ước dữ liệu:

- `nfiles`: số slot `ftable.file[]` có `ref > 0`. Tính cả pipe và device;
  không cộng tổng fd của các process, không đếm đường dẫn/inode duy nhất.
- `files[0..nfiles)`: danh sách đặc, không để lỗ. `id` là chỉ số slot trong
  `ftable.file[]`, **không phải chỉ số trong mảng kết quả đã nén**.
- File `type`: `SYSINFO_FD_NONE/PIPE/INODE/DEVICE`, tương ứng giá trị 0/1/2/3.
  `NONE` có thể xuất hiện ở slot đang được khởi tạo khi có hoạt động đồng thời.
- File `mode`: bit `SYSINFO_READ=1`, `SYSINFO_WRITE=2`, suy ra từ
  `readable/writable`. Đây không phải các cờ `O_*` hay quyền chmod.
- File `ref`: reference count của `struct file`; `nproc` và `pids[]` liệt kê
  các PID khác nhau tham chiếu file. Hai fd của cùng một process tăng `ref`
  nhưng PID chỉ xuất hiện một lần.
- `nprocs`: số process có `state != UNUSED`, gồm cả `USED` và `ZOMBIE`.
  `procs[0..nprocs)` cũng là danh sách đặc.
- Process `ppid=0` khi không có cha; `state` dùng giá trị 0..5 tương ứng
  `UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE`.
- `nsched`: số lần scheduler chọn process để chạy, tăng trước `swtch()`;
  process mới bắt đầu từ 0, không kế thừa bộ đếm của cha và không giữ giá trị
  từ lần sử dụng trước của slot process.
- Process `nfiles`: số fd khác rỗng; `files[]` chứa cặp `(fd, file_id)`.
  `dup()` được liệt kê thành hai fd trỏ cùng `file_id`.
- Thứ tự các bản ghi không bắt buộc. ID chỉ có ý nghĩa trong snapshot đó;
  slot được tái sử dụng sau khi đóng file. Test chỉ theo dõi ID qua nhiều lần
  gọi khi còn giữ ít nhất một fd tới đối tượng đó.
- Kernel khởi tạo toàn bộ dữ liệu output, gồm padding và phần mảng không dùng,
  để không sao chép dữ liệu kernel chưa khởi tạo ra user.

Snapshot không bắt buộc là ảnh chụp nguyên tử của toàn hệ thống. Trong lúc các
process thay đổi tài nguyên, thông tin giữa các bảng có thể lệch thời điểm.
Kernel vẫn phải bảo vệ việc truy cập/lifetime, trả count trong giới hạn và các
bản ghi đã khởi tạo. Test đồng thời chỉ kiểm tra cấu trúc, giới hạn, tính duy
nhất và sự hiện diện của caller; không đòi `ref == tổng fd` toàn hệ thống lúc
đó. Các assertion chính xác về ref/PID được thực hiện khi đối tượng kiểm tra
ổn định và child đang bị chặn bằng pipe. Bộ test cần chạy trong xv6 mới boot,
không chạy tác vụ nền, không redirect ba fd chuẩn của `sysinfotest`.

Người làm kernel còn cần:

1. Đăng ký `sys_sysinfo` trong `kernel/syscall.c`, thêm tên vào
   `syscalls_name[]` để syscall `trace` có sẵn hoạt động với số 24. Cập nhật
   `num_syscall` nếu tiếp tục giữ hằng này (hiện chưa được dùng để dispatch).
2. Viết handler lấy địa chỉ bằng `argaddr(0, &addr)`, thu thập dữ liệu theo ABI
   và dùng đúng signature `copyout(pagetable, p->sz, addr, src, len)` của repo.
   Trả `-1` khi destination/range không hợp lệ; không dereference con trỏ user
   trực tiếp. Copy lỗi có thể để lại dữ liệu từng phần; user bỏ kết quả khi lỗi.
3. Thêm/reset/tăng bộ đếm lập lịch trong `proc.h`/`proc.c`.
4. Chọn cơ chế khóa an toàn cho `ftable`, `ofile[]`, PID/trạng thái và parent.
   Trong source hiện tại, chỉ giữ `p->lock` không tự bảo vệ mọi cập nhật
   `ofile[]`; parent dùng `wait_lock`. Cần giữ đúng thứ tự khóa và không gọi
   thao tác có thể sleep khi giữ tổ hợp spinlock không phù hợp.

## Build và chạy

Thực hiện trong Linux/Ubuntu WSL, tại thư mục repo. Cần RISC-V GCC/binutils,
QEMU RISC-V >= 7.2 theo Makefile, GNU make, native GCC/binutils, Perl, Python 3,
`bc`. Không cần package Python bên ngoài.

```sh
make kernel/kernel fs.img
python3 tests/test_sysinfo_cli.py
```

Host test biên dịch chính `user/sysinfo.c`, thay các symbol I/O để chạy trên
Linux, cấp dữ liệu giả qua fixture. Nó kiểm tra output, số `nsched` lớn hơn
32 bit, các mode, giới hạn mảng, enum lạ, lỗi syscall và usage. **Host test
không xác nhận implementation syscall trong kernel.** Fixture full-table/empty
chỉ kiểm tra hiển thị và biên mảng, không mô phỏng hệ thống đang chạy.

Trước khi kernel có handler, chạy riêng các integration test đường lỗi:

```sh
python3 tests/test_sysinfo.py --expect-unimplemented
```

Lệnh này boot QEMU trên 1 và 3 CPU, kiểm tra `exec("sysinfo")`, stdout/stderr,
exit status của usage và syscall thất bại. Chỉ báo `MISSING-KERNEL PASS`,
không báo full suite thành công. Sau khi kernel được tích hợp, ngừng dùng
option này.

Chạy integration đầy đủ sau khi ghép kernel:

```sh
make test-sysinfo
# Hoặc:
python3 tests/test_sysinfo.py --cpus 1 3 --timeout 120
```

Runner yêu cầu đủ 13 dòng PASS, dòng `ALL PASS (13 cases)` và shell prompt
trở lại. Lỗi, panic hoặc timeout làm script trả exit code 1; Ctrl-C trả 130.
Log nằm ở `tests/.logs/`. Không chạy hai runner cùng lúc vì cùng sử dụng
`fs.img`. Đóng QEMU chạy tay trước khi chạy runner.

Để demo thủ công:

```sh
make qemu
```

Trong shell xv6:

```text
sysinfo
sysinfotest
```

## Các ca integration đầy đủ

| Case | Kiểm tra |
| --- | --- |
| `baseline` | Caller, PID cha, trạng thái RUNNING, bộ đếm lập lịch, console fd |
| `open-modes` | Ba mode; ba lần mở cùng đường dẫn tạo ba file object |
| `dup-close` | Chia sẻ ID, tăng/giảm ref, PID không lặp, giải phóng khi đóng cuối |
| `pipe` | Hai đầu pipe, loại và quyền đọc/ghi, thu hồi tài nguyên |
| `fork-sharing` | Con kế thừa fd, PID cha đúng, ref và danh sách PID chính xác |
| `zombie-wait` | ZOMBIE còn được tính nhưng đã đóng file; wait loại process khỏi bảng |
| `scheduling` | Bộ đếm tăng sau mỗi lần pause rồi được chạy lại |
| `fd-limit` | Đủ NOFILE, open vượt giới hạn thất bại và không rò file object |
| `invalid-pointers` | NULL, địa chỉ cao, ngoài heap, buffer có đầu hợp lệ nhưng quá ngắn |
| `repeated-snapshots` | Gọi nhiều lần không tăng số tài nguyên |
| `concurrent-snapshots` | Cha/con cùng gọi syscall khi con open/dup/close liên tục |
| `cli-output` | exec lệnh user, kiểm tra output và exit status thành công |
| `cli-usage` | Tham số sai in usage và trả exit status 1 |

Child được chặn bằng pipe để kiểm tra sharing, sau đó parent quan sát SLEEPING
và ZOMBIE bằng polling có deadline. Nếu assertion thất bại, test in tên case,
dọn child do nó tạo và trả lỗi. File tạm `si-<PID viết ngược>` được dọn khi kết
thúc; nếu tên đã tồn tại, test từ chối sử dụng thay vì ghi đè.

## Trạng thái xác thực trên branch này

- Build `kernel/kernel` và `fs.img`: thành công.
- Host test: 7/7 thành công, gồm 8 subcase kiểm tra count âm/vượt giới hạn.
- Integration đường lỗi trước khi có kernel: 2/2 case trên cả 1 và 3 CPU.
- Full integration: chưa thể pass vì repo chưa có `sys_sysinfo`. Chạy mặc định
  trả lỗi tại `baseline` với `unknown sys call 24`; đây là phụ thuộc kernel còn
  thiếu, không phải kết quả full integration thành công.

Khi bàn giao, gửi `kernel/sysinfo.h` và tài liệu này cho thành viên làm kernel.
Sau khi tích hợp, chạy full suite và bổ sung kết quả thực tế vào báo cáo nhóm.

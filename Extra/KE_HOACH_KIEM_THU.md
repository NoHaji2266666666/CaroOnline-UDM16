# Kiểm thử và bằng chứng – Caro Online UDM_16

> Trạng thái được cập nhật dựa trên các lần chạy thử thực tế trên Windows 11, ngày 17/10/2026. **Đạt** chỉ có nghĩa là bài thử được mô tả đã thành công, không khẳng định mọi trường hợp đều được kiểm chứng.

## 1. Kiểm thử chức năng

| Case | Hành động | Kết quả mong đợi | Thực tế | Bằng chứng / ghi chú |
|---|---|---|---|---|
| F01 | Hai Client đăng nhập | Cả hai thấy danh sách online | **Đạt** | `alice`, `job` đăng nhập trên cùng Server; ảnh giao diện đã chụp |
| F02 | Mời, chấp nhận/từ chối lời mời | Phản hồi đúng | **Đạt** | Đã thử mời đấu, chấp nhận và từ chối; log Client |
| F03 | Tạo hai phòng độc lập | Trạng thái hai trận tách biệt | **Đạt** | Phòng #7 (`alice`–`job`) và #8 (`chari`–`tel`) hoạt động đồng thời |
| F04 | Đánh sai lượt/ô đã có quân | Không chấp nhận nước đi sai | **Đạt với trường hợp đã thử** | Thử trên giao diện; `integration_smoke.py` báo `deny invalid move` |
| F05 | Đạt 5 quân liên tiếp | Xác định đúng người thắng | **Đạt** | Trận thắng 5 quân chéo; hai Client đồng bộ |
| F06 | Để hết 30 giây lượt | Xử thua người đến lượt | **Đạt** | Phòng #6 kết thúc, `job` thắng; lịch sử ghi `Hết 30 giây lượt` |
| F07 | Ngắt Client rồi reconnect trong thời gian cho phép | Khôi phục phòng và bàn cờ | **Đạt (ngắt bằng nút trên Client)** | 5 bước reconnect đạt; smoke test báo `TCP reconnect with session token` |
| F08 | Khán giả vào giữa trận | Nhận bàn cờ hiện tại và cập nhật | **Đạt** | `chari` xem Phòng #3; số khán giả từ 0 lên 1, thấy nước đi mới |
| F09 | Khán giả thử đánh | Không thêm quân vào bàn cờ | **Đạt qua giao diện** | `chari` nhấp bàn cờ không tạo nước đi; cần kiểm thử thêm gói tin giả lập từ khán giả |
| F10 | Khán giả rời phòng | Trận tiếp tục; số người xem giảm | **Đạt** | Đã thử rời ghế khán giả, trận của hai người chơi vẫn tiếp tục |
| F11 | Khởi động lại Server | Lịch sử SQLite còn nguyên | **Đạt** | Tắt/mở Server, nhấn `Tải lại lịch sử` vẫn thấy các trận cũ |
| F12 | Dữ liệu sai / ngắt kết nối đột ngột | Server không sập, Client khác vẫn hoạt động | **Đạt phần JSON sai; còn thiếu ngắt mạng đột ngột** | Server trả `JSON không hợp lệ.`; chạy lại smoke test được `SMOKE TEST PASSED` |
| F13 | Hai Client cùng đăng nhập một tên | Từ chối tên trùng đang online | **Đạt** | Client thứ hai nhận `Tên đã được sử dụng online.` |

## 2. Kết quả kiểm thử tự động

- Lệnh: `py Extra\tests\integration_smoke.py` (chạy ở thư mục gốc dự án).
- Kết quả: `PASS: login, invite, matching, spectator snapshot, deny invalid move, turns, victory, SQLite history`.
- Kết quả: `PASS: TCP reconnect with session token`.
- Kết luận: `SMOKE TEST PASSED` (chạy lại sau khi gửi JSON sai vẫn đạt).

## 3. Performance test – yêu cầu `list_rooms` trên localhost

Lệnh dùng công cụ đo tự động: `py -u Extra\tests\monitor_caro.py --clients N --requests 500`, trong đó N lần lượt bằng 5, 20, 50.

| Case | Client kết nối | Yêu cầu hoàn thành | Mean (ms) | P95 (ms) | Max (ms) | CPU TB (%) | CPU cao nhất (%) | RAM TB (MB) | RAM cao nhất (MB) | Kết quả |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| P01 | 5/5 | 500/500 | 1.01 | 1.63 | 4.69 | 4.29 | 7.79 | 23.39 | 23.57 | **Đạt** |
| P02 | 20/20 | 500/500 | 2.64 | 4.74 | 9.76 | 5.42 | 8.45 | 24.47 | 24.81 | **Đạt** |
| P03 | 50/50 | 500/500 | 6.78 | 11.11 | 21.74 | 6.75 | 9.11 | 26.57 | 27.31 | **Đạt** |

**Giới hạn phép đo:** Thử trên `127.0.0.1:45454` của cùng máy, chủ yếu gửi `list_rooms`, không phải 50 người đánh cờ đồng thời. CPU/RAM là số liệu lấy mẫu của tiến trình Server theo chu kỳ khoảng 0,2 giây; RAM là Working Set, không phải Private Working Set. Số mẫu lần lượt là 5, 12, 33, nên kết quả, đặc biệt mức 5 Client, chưa đủ để kết luận ngưỡng chịu tải tối đa.

### File minh chứng đã xuất trên máy

Các file được tạo trong `Extra/results/` (cần xác nhận đã upload GitHub nếu muốn dùng link trực tiếp):

- `monitor_5clients_500requests_20261017_033416.csv` và `.txt` cùng tên.
- `monitor_20clients_500requests_20261017_033424.csv` và `.txt` cùng tên.
- `monitor_50clients_500requests_20261017_033152.csv` và `.txt` cùng tên.

## 4. Môi trường kiểm thử

- Hệ điều hành: Windows 11 (build 26300, theo kết quả script).
- Visual Studio 2022 / MSVC 2022 64-bit; cấu hình build đã dùng: `x64-Debug`.
- Qt: 6.12.0, MSVC 2022 64-bit.
- Mạng: TCP localhost `127.0.0.1`, cổng `45454`.
- CPU logic: 12 (theo script).
- Cơ sở dữ liệu: SQLite; lưu lịch sử trận trên máy chạy Server.

## 5. Việc còn phải kiểm tra

- Kiểm thử ngắt mạng đột ngột/đóng tiến trình Client giữa trận (khác với bấm nút ngắt kết nối).
- Kiểm thử nhiều Client đồng thời **tạo phòng và đánh cờ** bằng script tải chuyên dụng.
- Kiểm thử qua mạng LAN giữa hai máy; đo độ trễ và xử lý Windows Firewall.
- Kiểm thử thêm gói tin từ khán giả và các đầu vào biên khác ở tầng Server.
- Bổ sung đường dẫn ảnh chụp màn hình, log và file minh chứng có thể truy cập trong repository trước khi nộp.

> Nhật ký này phản ánh quá trình thử bản mẫu. Khi nhóm 6 người phát triển lại dự án chính thức, cần ghi riêng kết quả kiểm thử và đóng góp thực tế của từng thành viên, không dùng tài liệu này làm bằng chứng rằng tất cả thành viên đã chạy các bài thử.

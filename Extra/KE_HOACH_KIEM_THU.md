# Kiểm thử và bằng chứng (điền sau khi chạy thật)

| Case | Hành động | Kết quả mong đợi | Thực tế | Ảnh/log |
|---|---|---|---|---|
| F01 | Hai Client đăng nhập | Cả hai thấy online | Chưa chạy | — |
| F02 | Mời/chấp nhận/từ chối | Đúng phản hồi | Chưa chạy | — |
| F03 | Tạo hai phòng độc lập | Trạng thái tách biệt | Chưa chạy | — |
| F04 | Gửi sai lượt/ô có quân | Server từ chối | Chưa chạy | — |
| F05 | 5 quân cùng hàng | Đúng người thắng | Chưa chạy | — |
| F06 | Hết thời gian 30s | Người đến lượt bị xử thua | Chưa chạy | — |
| F07 | Tắt TCP và reconnect trong 90s | Trạng thái phục hồi | Chưa chạy | — |
| F08 | Khán giả vào giữa trận | Nhận toàn bàn hiện tại | Chưa chạy | — |
| F09 | Khán giả gửi lệnh đánh | Server từ chối | Chưa chạy | — |
| F10 | Khán giả rời | Trận tiếp tục | Chưa chạy | — |
| F11 | Khởi động lại Server | Lịch sử SQLite còn | Chưa chạy | — |
| F12 | JSON lỗi / disconnected đột ngột | Các client khác vẫn chạy | Chưa chạy | — |
| P01 | 5 Client, 20 lần list_rooms | Đo RTT; chụp CPU/RAM | Chưa chạy | — |
| P02 | 20 Client, 20 lần list_rooms | Đo RTT; chụp CPU/RAM | Chưa chạy | — |

Ghi môi trường test: Windows version, CPU, RAM, phiên bản Qt, MSVC, cấu hình build Debug/Release, số Client và port. Không tự điền số liệu giả.

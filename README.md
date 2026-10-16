# CaroOnline – UDM_16

**Game Caro trực tuyến trên Windows** viết bằng C++17, Qt Widgets và TCP Client–Server.

> **Trạng thái:** Bản mẫu phục vụ nghiên cứu và kiểm thử. Chưa phải bộ hồ sơ nộp đồ án hoàn chỉnh; vẫn cần kiểm thử bổ sung, cập nhật báo cáo và phân công đóng góp nhóm.

## 1. Công nghệ

- C++17, CMake (>= 3.21)
- Qt 6, sử dụng các mô-đun Core, Network, Widgets và Sql
- Visual Studio 2022 với *Desktop development with C++*; bộ Qt MSVC 2022 64-bit
- TCP Socket để trao đổi giữa các Client và Server
- SQLite để lưu lịch sử và kết quả trận đấu
- Python 3 để chạy các script kiểm thử

Đây là **ứng dụng desktop**, không phải Web App.

## 2. Cấu trúc thư mục

```text
CaroOnline-UDM16/
├── Code/
│   ├── Client/       # Giao diện Qt và bàn cờ
│   ├── Server/       # Xử lý TCP, quản lý trận đấu và SQLite
│   └── Shared/       # Các thành phần dùng chung
├── DOCX/             # Báo cáo (sẽ cập nhật)
├── Extra/
│   ├── tests/        # Các script kiểm thử Python
│   └── results/      # Số liệu và minh chứng kiểm thử
├── PPTX/             # Slide thuyết trình (sẽ cập nhật)
├── CMakeLists.txt
├── .gitignore
└── README.md
```

## 3. Các chức năng hiện có

- Kết nối nhiều người chơi tới một TCP Server, hiển thị danh sách người chơi online.
- Mời đấu, chấp nhận/từ chối lời mời, tạo các phòng đấu đồng thời.
- Bàn cờ 15×15; Server kiểm tra lượt, nước đi và điều kiện thắng thua.
- Đồng hồ giới hạn 30 giây mỗi lượt.
- Khán giả có thể vào xem trận đang diễn ra, nhận trạng thái bàn cờ và không được đánh cờ.
- Tạm dừng khi người chơi mất kết nối; hỗ trợ reconnect bằng session token trong thời gian chờ.
- Lưu kết quả và lịch sử trận đấu bằng SQLite.

Các chức năng trên đã được kiểm thử bằng một số kịch bản tại máy phát triển. Chưa xác nhận tất cả tình huống vận hành thực tế.

## 4. Cách cài đặt và chạy trên Windows

1. Cài **Visual Studio 2022** với thành phần **Desktop development with C++**.
2. Cài **Qt 6** phiên bản MSVC 2022 64-bit (máy thử nghiệm sử dụng Qt 6.12.0).
3. Trong Visual Studio chọn **File > Open > Folder**, trỏ tới thư mục chứa `CMakeLists.txt`.
4. Cấu hình CMake để tìm Qt. Ví dụ nếu Qt nằm ở `C:\Qt\6.12.0\msvc2022_64`, đặt tham số:

   ```text
   -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64
   ```

5. Chọn **Project > Delete Cache and Reconfigure**, sau đó **Build > Build All**.
6. Chạy **CaroServer.exe** trước, giữ cửa sổ Server đang chạy. Server dùng cổng TCP `45454` theo mặc định.
7. Mở từ hai cửa sổ **CaroClient.exe** trở lên; trên cùng một máy dùng địa chỉ `127.0.0.1`, cổng `45454`, và đặt tên người chơi khác nhau.
8. Từ một Client, chọn người chơi khác trong danh sách online và nhấn **Mời người được chọn** để bắt đầu.

Tập tin `.exe` có thể cần các Qt DLL khi chạy. Trong môi trường phát triển, cấu hình `PATH` trỏ đến `C:\Qt\6.12.0\msvc2022_64\bin` hoặc triển khai các DLL bằng công cụ `windeployqt` trước khi chuyển sang máy khác.

**Lưu ý:** `CMakeSettings.json` chứa đường dẫn cục bộ; mỗi máy cần tự cấu hình Qt. Không đưa tệp cấu hình cá nhân này lên GitHub.

## 5. Kiểm thử

Giữ Server chạy, mở CMD tại thư mục gốc dự án rồi dùng:

```cmd
py Extra\tests\integration_smoke.py
py -u Extra\tests\performance_probe.py --clients 5 --requests 500
py -u Extra\tests\performance_probe.py --clients 20 --requests 500
py -u Extra\tests\monitor_caro.py --clients 50 --requests 500
```

- `integration_smoke.py` kiểm tra các luồng cơ bản: đăng nhập, mời đấu, nước đi, thắng, khán giả, lịch sử, reconnect.
- `performance_probe.py` đo độ trễ yêu cầu danh sách phòng `list_rooms`.
- `monitor_caro.py` kết hợp kiểm thử và lấy mẫu CPU/RAM, xuất CSV/TXT vào `Extra/results`.

Đã thử trên Windows 11 và localhost (`127.0.0.1`) với các mức 5, 20 và 50 Client. Đây là **phép đo yêu cầu danh sách phòng**, không phải bằng chứng 50 người cùng chơi hoặc stress test toàn bộ hệ thống. Kết quả đo có thể khác theo máy và môi trường mạng.

Kế hoạch và bằng chứng kiểm thử được quản lý trong `Extra/KE_HOACH_KIEM_THU.md` và `Extra/results` (cần tiếp tục cập nhật).

## 6. Dữ liệu trò chơi

Server lưu lịch sử trong SQLite tại thư mục dữ liệu ứng dụng của Windows, ví dụ:

```text
%APPDATA%\CaroServer\matches.sqlite
```

Không đưa cơ sở dữ liệu, session token, mật khẩu hay thông tin cá nhân lên repository công khai.

## 7. Làm việc nhóm

Dự án có kế hoạch phân chia cho 6 thành viên theo các mảng: Server/tích hợp, giao diện Client, luật chơi/đồng hồ, phòng/lời mời, khán giả, và reconnect/lịch sử. Danh sách thành viên, nhiệm vụ chính thức và lịch trình sẽ được bổ sung sau khi thống nhất với nhóm.

Mỗi thành viên sử dụng tài khoản GitHub riêng, thực hiện thay đổi thật, tạo commit mô tả công việc và phối hợp qua Pull Request. Bản mẫu hiện tại không đại diện cho việc cả 6 thành viên đã hoàn thành các phần mã nguồn.

## 8. Giới hạn và công việc còn lại

- Kiểm thử qua mạng LAN trên nhiều máy và đánh giá khi mạng yếu.
- Bổ sung kiểm thử tải với các thao tác chơi thực tế, log và xử lý các trường hợp lỗi.
- Hoàn thiện báo cáo trong `DOCX`, slide trong `PPTX`, video demo và hướng dẫn đóng gói để nộp bài.

**Lưu ý bảo mật:** Server dùng TCP thông thường; chỉ nên chạy bản mẫu trên mạng tin cậy, không mở cổng công khai lên Internet khi chưa có biện pháp bảo vệ phù hợp.

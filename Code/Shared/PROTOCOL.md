# Giao thức CaroOnline v1

TCP, UTF-8, mỗi gói = một JSON object + ký tự newline `\n` (NDJSON). Giới hạn tối đa 64 KiB/gói. TCP có thể chia/gộp gói tùy ý; phải đọc từ buffer tới newline.

## Client -> Server
- `{"type":"hello","name":"alice","token":""}`: tên 3–20 ký tự `a-z,A-Z,0-9,_`; token do server cấp, dùng lại khi reconnect.
- `{"type":"invite","target":"bob"}`: thách đấu người đang rảnh.
- `{"type":"invite_reply","from":"alice","accept":true}`: trả lời lời mời.
- `{"type":"move","room":1,"row":7,"col":7}`: đặt quân (chỉ người đến lượt).
- `{"type":"list_rooms"}`, `{"type":"list_users"}`, `{"type":"history"}`.
- `{"type":"spectate","room":1}` và `{"type":"leave_spectate"}`.

## Server -> Client
- `welcome` (name, token, resumed)
- `error` (message), `notice` (message)
- `users` (users: [{name,busy}])
- `invite_received` (from); `invite_result` (target,accepted)
- `rooms` (rooms: [{id,x,o,spectators,paused}])
- `snapshot` (room,x,o,board: array of 15 strings,turn,secondsLeft,paused,onlineX,onlineO,active,winner,reason, ...)
- `history_result` (matches array, gần nhất trước).
- `left_room`.

Server quyết định toàn bộ lượt đi, thời gian, kết quả và quyền khán giả. Client không được phép gửi snapshot để thay đổi trạng thái.

## Luật demo
15x15, quân X đi trước, **năm hoặc nhiều hơn** quân liên tiếp (ngang/dọc/chéo) là thắng; không xét luật chặn hai đầu. Mỗi lượt 30 giây, hết giờ thì người đối diện thắng. Nếu một người rớt mạng, ván tạm dừng tối đa 90 giây; khi reconnect cùng token sẽ chơi tiếp với thời gian còn lại. Quá 90 giây: xử thua. Không còn ô trống: hòa. Hai người chơi chỉ được ở một phòng tại cùng thời điểm.

# 315581038 黃永恩

# 執行說明（Windows）

這份程式用的是 Linux/POSIX 的 socket，所以 Windows 要透過 **WSL（Ubuntu）** 來編譯和執行。

---

## 1. 安裝編譯工具（只需做一次）

打開 cmd，輸入：

```cmd
wsl
sudo apt update
sudo apt install -y build-essential
exit
```

---

## 2. 編譯

```cmd
cd "C:\Users\User\Desktop\碩一上作業\Midterm Project - A Chat Client and Server\posix_chat"
wsl make
```

成功後資料夾裡會出現 `chat_server` 和 `chat_client`。

---

## 3. 執行聊天室

開 **3 個 cmd 視窗**，每個都先 `cd` 到上面的資料夾，然後分別執行：

| 視窗 | 指令 | 說明 |
|---|---|---|
| 1 | `wsl ./chat_server 5555 both` | 啟動 server |
| 2 | `wsl ./chat_client 127.0.0.1 Wayne 5555 auto` | 使用者 Wayne |
| 3 | `wsl ./chat_client 127.0.0.1 Alice 5555 udp` | 使用者 Alice（UDP） |

---

## 4. 聊天指令

| 輸入 | 功能 |
|---|---|
| `Hello everyone!` | 傳訊息給所有人 |
| `/list` | 查看線上使用者 |
| `/msg Alice 你好` | 私訊 Alice |
| `/help` | 顯示說明 |
| `/quit` | 離開 |

要停止 server：按 `Ctrl + C`。

---

## 5. Bonus 展示

### Bonus 1：TCP 失敗自動改用 UDP

```cmd
wsl ./chat_server 5555 udp
wsl ./chat_client 127.0.0.1 Wayne 5555 auto
```

會看到：

```
Trying TCP...
TCP failed. Falling back to UDP...
Connected via UDP.
```

### Bonus 2：TCP 和 UDP 使用者互傳私訊

```cmd
wsl ./chat_server 5555 both
wsl ./chat_client 127.0.0.1 Wayne 5555 tcp
wsl ./chat_client 127.0.0.1 Alice 5555 udp
```

Wayne 輸入 `/msg Alice Hello from TCP!`，只有 Alice 會看到：

```
[PM from Wayne] Hello from TCP!
```

---

## 常見問題

- **中文路徑找不到檔案？** 先輸入 `wsl` 進入 Ubuntu，再執行：
  ```bash
  cd "/mnt/c/Users/User/Desktop/碩一上作業/Midterm Project - A Chat Client and Server/posix_chat"
  make
  ./chat_server 5555 both
  ```
- **`make: command not found`？** 代表步驟 1 還沒做。

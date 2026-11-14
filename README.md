<div align="center">

<img width="680" alt="image" src="https://github.com/user-attachments/assets/01fb6a63-58f1-4be5-b3d2-59d5d013e417" />

# NX3DX+ Linux

**Linux v2.4.1 · 原生 x86_64 · SDL2 · OpenGL ES**

**[繁體中文](README.md) | [English](EN-README.md)**

![Linux](https://img.shields.io/badge/Linux-x86__64-1793D1?logo=linux&logoColor=white)
![OpenGL ES](https://img.shields.io/badge/Graphics-OpenGL%20ES%202.0-0b86c4)
![Steam Deck](https://img.shields.io/badge/Steam%20Deck-Tested-1b9e77)
![Version](https://img.shields.io/badge/Version-2.4.1-cc2e9a)

</div>

> **NX3DX+ Linux 是由 Re3DX+ 程式碼基礎移植與延伸開發的非官方 Linux 專案。** 本 Repository 專注 Linux／Steam Deck，不包含原版遊戲素材。

## 📖 專案簡介

本專案使用 **SDL2 + 原生 OpenGL ES（EGL）** 執行遊戲，不需要 Wine、Proton 或 Vulkan。倉庫包含 Linux 專用入口與編譯所需的共用 C 引擎，不包含其他平台的啟動程式。

**目前功能**
- Linux x86_64、視窗／全螢幕、Steam Deck 控制器
- 鍵盤、滑鼠與同時雙指以上的多點觸控
- 車輛與碰撞音效、MP3／M4A 背景音樂（FFmpeg）
- CHOOSE MP3 選擇自訂歌曲（Zenity／KDialog）
- 反射、銳利化、環境遮蔽、後照鏡及部分高畫質 GPU 設定
- 個人存檔與原有遊戲資料分開儲存

## 🖥️ 系統需求

| 項目 | 說明 |
|---|---|
| 作業系統 | Linux x86_64；建議 Ubuntu 22.04+，Steam Deck 可用 Ubuntu Distrobox |
| 顯示卡 | 可使用 OpenGL ES 2.0／EGL 的 GPU 及驅動 |
| 基本套件 | SDL2、GLES2、EGL、zlib |
| 音樂 | FFmpeg |
| 自訂 MP3 視窗 | Zenity 或 KDialog |
| 遊戲資料 | 自行準備合法取得、相容的 data 資料夾 |

> ⚠️ 本專案**不提供或散布**遊戲 ROM、BIN、WPK、原版音樂、影片、其他受保護素材或個人存檔。

## 📦 安裝方式一：下載已編譯版本

若 [GitHub Releases](https://github.com/Tira-tw/nx3dx-linux/releases) 已發佈 Linux v2.4.1，下載檔案 'NX3DX-Linux-v2.4.1-x86_64.tar.gz'。

在 Ubuntu／Steam Deck 的 Ubuntu Distrobox 安裝執行環境：

~~~bash
sudo apt update
sudo apt install -y libsdl2-2.0-0 libgles2 libegl1 zlib1g ffmpeg zenity
~~~

解壓縮後，指定自己本機的遊戲資料路徑：

~~~bash
tar -xzf NX3DX-Linux-v2.4.1-x86_64.tar.gz
cd NX3DX-Linux-v2.4.1-x86_64
./NX3DX --version
./launch.sh "$HOME/NX3DX-Linux/data" --fullscreen
~~~

如果 Releases 尚未提供檔案，請使用下面的原始碼編譯方式。

## 🛠️ 安裝方式二：從原始碼編譯

### 1. 安裝編譯工具

~~~bash
sudo apt update
sudo apt install -y git build-essential pkg-config \
  libsdl2-dev libgles2-mesa-dev libegl1-mesa-dev zlib1g-dev \
  ffmpeg zenity
~~~

### 2. 下載 Linux 專用專案

~~~bash
git clone https://github.com/Tira-tw/nx3dx-linux.git
cd nx3dx-linux
~~~

### 3. 編譯 Linux v2.4.1

~~~bash
bash linux/build_linux.sh --check
bash linux/build_linux.sh
./build/linux-main/NX3DX --version
~~~

預期顯示：**NX3DX+ Linux v2.4.1 (x86_64, OpenGL ES 2.0)**。

### 4. 啟動遊戲

~~~bash
bash linux/run_linux.sh "$HOME/NX3DX-Linux/data" --window
~~~

將路徑改成自己的合法遊戲資料資料夾（其中需有相容的 menu.bin）。若要全螢幕，改用 '--fullscreen'。

### 5. 製作 Linux 發行壓縮包

~~~bash
bash linux/package_release.sh
~~~

輸出位於 'build/release/'，包含 x86_64 執行檔、啟動器及 SHA256 驗證檔，**不會包含遊戲資料**。

## 🎮 Steam Deck

先進入 Ubuntu Distrobox，再執行以上安裝、編譯或啟動指令；已經在 Distrobox 時不用重複進入。支援觸控方向盤與油門同時操作。

遊戲存檔使用 SDL 的個人偏好目錄，不會覆蓋原本放在其他資料夾中的遊戲安裝。

## 🎨 GPU Settings／已知問題

- **有效的設定：** Reflection、Sharpen、AO、Back Mirror。
- **依 GPU 支援而定：** Bloom、SSAO、MSAA、Wet／Paint 反射、ASTC HD Textures。
- **目前僅支援 OpenGL ES**，不提供 Vulkan 渲染後端。
- 內部渲染解析度百分比尚未完全接入 Linux 入口；開場／教學影片播放仍未完成。

**NVIDIA GeForce 940MX 相容性：** 部分 NVIDIA PRIME 環境在 HQ Rendering 啟用時，可能在車模載入階段崩潰。測試者已確認使用下列暫時方法可進入遊戲：

~~~bash
WMMT_NO_HQ=1 prime-run bash linux/run_linux.sh \
  "$HOME/NX3DX-Linux/data" --window
~~~

只有已安裝 'prime-run' 的雙 GPU 電腦才需使用它；否則省略 'prime-run'。此問題尚待進一步定位，不代表所有 NVIDIA GPU 都有問題。

## 🐛 回報問題

請前往 [Issues](https://github.com/Tira-tw/nx3dx-linux/issues)，附上系統版本、GPU／驅動、執行指令與錯誤日誌。**不要公開上傳遊戲資料、個人存檔或登入憑證。**

## ❤️ 致謝、來源與授權

- **來源：** [Re3DX+](https://github.com/search?q=Re3DXPlus&type=repositories) 程式碼基礎，延伸自 [NX3DX+](https://github.com/Tira-tw/nx3dx) 專案。
- **原始程式碼署名：** Allannn-sudo（高橋涼介のFC），依原有 [MIT License](LICENSE) 保留著作權聲明。
- **Linux 維護：** NX3DX+ Project／Tira-tw 與 Linux 測試貢獻者。
- MIT 授權僅適用於本倉庫有權授權的程式碼與工具，**不授權原版遊戲、音樂、商標或其他受保護內容**。

本專案與 Bandai Namco Entertainment 及原遊戲權利人**無官方關聯**。

---

<div align="center">

**NX3DX+ PROJECT EDITION — LINUX v2.4.1**

[English README](EN-README.md) · [Linux 詳細說明](linux/README.md) · [版本說明](linux/RELEASE_NOTES_v2.4.1.md)

</div>

# smartOptSensor

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: ESP32-C3](https://img.shields.io/badge/Platform-ESP32--C3-orange.svg)](https://www.espressif.com/en/products/socs/esp32-c3)
[![Protocol: Matter](https://img.shields.io/badge/Protocol-Matter-green.svg)](https://csa-iot.org/all-solutions/matter/)
[![Language: English / 日本語](https://img.shields.io/badge/Language-English%20%2F%20日本語-blue)](README.md)

[English](README.md) | **日本語**

ESP32-C3 を用いた Matter 対応照度センサー（Ambient Light Sensor）ファームウェアです。  
CdSセル（光導電素子）と固定抵抗による分圧回路の電圧を ADC で取得し、照度（Lux）および Matter 規格の `IlluminanceMeasurement` 属性（`MeasuredValue`）に変換して Matter ネットワーク上に提供します。

---

## 1. システム仕様 (System Specification)

- **マイコン (MCU)**: ESP32-C3 (RISC-V シングルコア, 160MHz)
- **通信方式**: Matter over Wi-Fi (2.4GHz Wi-Fi 対応)
- **Matter Device Type**: Light Sensor (`0x0106`)
- **Matter Cluster**: Illuminance Measurement (`0x0400`)
- **センサ**: CdSセル (GL5528 等の標準光導電素子)
- **サンプリング・送信仕様**:
  - サンプリング周期: 1,000ms (1秒)
  - 10回分散サンプリング (各サンプル間1msディレイ) による電源フリッカー軽減
  - Matter属性更新の最適化: デッドバンド制御（$\Delta \ge 50$）＋ 60秒定期ハートビート送信
- **ファクトリーリセット**: GPIO9 ボタンを 3 秒以上長押し または `idf.py erase-flash`

---

## 2. 回路構成とピン配置 (Circuit Diagram & Pinout)

### 接続表 (Pin Mapping)
| ESP32-C3 ピン | 接続先 | 役割 |
| :--- | :--- | :--- |
| **3.3V** | CdS セルの片足 | 電源供給 |
| **GPIO1 (ADC1_CH1)** | CdSセルと10kΩ抵抗の接続点 | ADC電圧計測入力 |
| **GND** | 10kΩ 抵抗の片足 | グランド |

### 回路図 (Circuit Schematic)

```text
       +3.3V
         |
       +---+
       |   |
       |CdS| (光導電素子)
       |   |
       +---+
         |
         +--------------------> GPIO1 (ESP32-C3 ADC1_CH1)
         |
       +---+
       |   |
       |10k| (固定抵抗 10kΩ)
       |   |
       +---+
         |
        GND
```

#### 動作原理

- **明るい時**: CdS セルの抵抗値が低下（約 1kΩ 以下） $\rightarrow$ GPIO1 の電圧が 3.3V 付近まで上昇。
- **暗い時**: CdS セルの抵抗値が上昇（約 100kΩ ~ 1MΩ） $\rightarrow$ GPIO1 の電圧が 0V 付近まで低下。

---

## 3. 変換計算式 (Illuminance Calculation)

1. **電圧取得 ($V_{out}$)**:  
   ESP32-C3 の ADC1_CH1 (GPIO1) よりキャリブレーション付き電圧（mV）を取得。

2. **CdS 抵抗値の算出 ($R_{CdS}$)**:  
   $$R_{CdS} = 10000 \times \left( \frac{3300}{V_{out}} - 1 \right) \quad [\Omega]$$

3. **照度の算出 ($\text{Lux}$)**:  
   実測校正値（ルームランプ直下: RAW ≈ 2600 / $R_{CdS} \approx 7.25\text{k}\Omega \rightarrow 300\text{ Lux}$、暗所: RAW ≈ 660 / $R_{CdS} \approx 58.7\text{k}\Omega \rightarrow 1\text{ Lux}$）に基づく換算:  
   $$\text{Lux} = 300.0 \times \left( \frac{7250}{R_{CdS}} \right)^{2.727}$$

4. **Matter MeasuredValue 換算**:  
   Matter 規格（Standard Cluster 0x0400, Nullable uint16）に準拠:  
   $$\text{MeasuredValue} = 10000 \times \log_{10}(\text{Lux}) + 1$$

---

## 4. ビルド、初回初期化、書き込み手順 (Build, Flash & Reset)

### 1. 環境変数のロード

```bash
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh
```

### 2. ビルド

```bash
idf.py set-target esp32c3
idf.py build
```

### 3. 初回書き込み / Matter コミッショニング用初期化

新規にスマートホーム（Google Home）へ登録する場合や、過去のペアリング情報をクリアする場合は、必ず Flash メモリを全消去して書き込みます。

```bash
# Flash メモリの全消去（旧ファブリック/ペアリング情報のクリア）
idf.py -p /dev/ttyACM0 erase-flash

# ファームウェアの書き込みとシリアルモニター起動
idf.py -p /dev/ttyACM0 flash monitor
```

---

## 5. Matter ペアリング（オンボーディング）手順

書き込み完了後、シリアルモニター上に **オンボーディングコード** が表示されます。

1. **手動ペアリングコード**:  
   シリアルログに表示される 11桁の数値（例: `34970112332`）をスマートホームアプリに入力。

2. **QRコード**:  
   シリアルログ内の URL（例: `https://project-chip.github.io/connectedhomeip/qrcode.html?data=...`）をブラウザで開き、表示された QR コードをアプリでスキャン。

3. **登録手順**:  
   - スマホを ESP32 と同じ Wi-Fi ネットワーク（`wifi_creds.h` に設定したもの）に接続します。
   - Google Home や Apple ホーム等のアプリで「デバイスの追加」 $\rightarrow$ 「Matter 対応デバイス」を選択し、コードの入力または QR コードスキャンを行うと「照度センサー」として登録されます。

---

## 6. シリアル確認ログ (1000ms Cycle Output & Optimized Reporting)

本ファームウェア起動時、1000ms (1秒) 周期で以下のような確認ログがシリアルモニター（ボーレート: 115200）へ出力されます。Matter属性の更新は、照度の一定以上の変化時（デッドバンド >= 50）または定期ハートビート（60秒間隔）の条件を満たした場合に送信されます。

```text
I (1250) app_main: [CdS Sensor] RAW: 2609 | Volt: 1913 mV | Res:  7250.4 Ohm | Lux:   300.0 | MatterVal: 24772
I (1260) esp_matter_attribute: ********** W : Endpoint 0x0001's Cluster 0x00000400's Attribute 0x00000000 is 24772 **********
I (2250) app_main: [CdS Sensor] RAW: 2610 | Volt: 1914 mV | Res:  7245.0 Ohm | Lux:   300.5 | MatterVal: 24779
```

- **RAW**: 12bit ADC 生値 (0 ~ 4095)
- **Volt**: 計測電圧 (mV)
- **Res**: 計算された CdS 抵抗値 ($\Omega$)
- **Lux**: 校正後の推定照度 ($\text{Lux}$)
- **MatterVal**: Matter 規格の `MeasuredValue` 属性送信値

---

## 7. Wi-Fi 設定について

`main/wifi_creds.h` に Wi-Fi SSID と Password を事前記述して自動接続させることができます。

---

## 8. Google home 設定例 (Google Home Script Editor)

```yaml
metadata:
  name: 暗くなったらブロードキャスト
  description: 照度センサーが低照度（50ルクス以下）を検知したら、ブロードキャストする
automations:
  - starters:
      - type: device.state.SensorState
        device: 光量 - リビングルーム
        # センサーの種類（LightLevel 等）の指定形式はデバイスによって異なる場合があります
        state: currentSensorStateData.LightLevel.rawValue
        lessThanOrEqualTo: 50
    actions:
      - type: assistant.command.Broadcast
        message: "暗くなりました"
```

---

## ライセンス (License)

本プロジェクトは MIT ライセンスの下で公開されています。詳細は [LICENSE](LICENSE) をご覧ください。

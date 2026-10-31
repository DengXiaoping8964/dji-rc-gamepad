# Disclaimer / 免责声明

This file is part of the license notice in `LICENSE` and applies to the repository,
its documentation, its prebuilt binaries, and every derivative work.
**Read it before downloading, building or running anything here.**
本文件是 `LICENSE` 许可声明的组成部分，适用于本仓库、其文档、其预编译二进制文件以及
一切衍生作品。**在下载、编译或运行任何内容之前请先读完本文件。**

---

## 1. No affiliation / 无隶属关系

This is an independent, unofficial hobby project. It is **not** developed, released,
endorsed, sponsored, authorised or certified by **SZ DJI Technology Co., Ltd.**, by any
game publisher, by any platform owner, or by any of their affiliates.

- **DJI**, **RC-N3**, **RC-N1**, **RC-N2**, **DJI Fly**, **DJI Assistant**, **DUML** and
  **OcuSync** are trademarks or property of SZ DJI Technology Co., Ltd.
- **Xbox**, **XInput**, **XUSB** are trademarks of Microsoft Corporation.
- Names of games, series and studios mentioned in this project are trademarks of their
  respective owners.
- Mentioning a product, game or standard here is **descriptive only** and does **not**
  imply any endorsement, partnership, or that the named party supports this project.

本项目为独立的非官方个人项目，**未**经深圳市大疆创新科技有限公司、任何游戏发行商、任何平台
运营方或其关联公司开发、发布、认可、赞助、授权或认证。文中出现的产品名、游戏名与商标均归各自
权利人所有，提及仅为描述用途，不代表其认可本项目。

## 2. How this software works, and what it is not / 本软件的性质

The program **reads** input state (stick positions, button bits, dial, flight-mode switch,
battery level) from a remote controller over USB serial using a **reverse-engineered,
undocumented protocol**, and re-publishes that state to Windows as a virtual game controller.

It is an **input reader plus an input emulator**. It is **not** a flight-control tool, not a
telemetry tool, not a firmware tool, and it sends no command that could move an aircraft.

- No aircraft control, arming, take-off, landing, return-to-home, or parameter writing is
  implemented, and none is implied.
- The protocol was reconstructed from public traffic and from community documentation.
  **Nothing here is derived from confidential information, leaked source code, or
  manufacturer-internal material.**
- Because the protocol is undocumented, **any firmware update on either side can break it at
  any time**. Command identifiers, frame layout and endpoints may change without notice.

程序通过逆向得到的、无公开文档的 USB 串口协议**读取**遥控器输入状态，并将其作为虚拟游戏手柄
重新呈现给 Windows。它是"输入读取 + 输入模拟"，**不是**飞行控制工具、不是遥测工具、不是固件
工具，也不会发送任何可能使航空器运动的指令。协议无公开文档，**任一侧固件更新都可能随时使其失效**。

## 3. Express assumption of risk / 风险自担

Use is **entirely at your own risk**. You are solely responsible for what happens to your
hardware, your accounts, your aircraft, your computer and your surroundings while or because
you use this software. In particular, and without limitation:

- **Never use this software with a remote controller that is bound to, paired with, or able to
  command a powered aircraft.** Even though the program only reads input, a controller that is
  physically live can still transmit. Remove batteries / power off the aircraft, or do not
  connect at all, before playing.
- Gamepads mapped from a drone transmitter have **fewer physical controls** than a real
  gamepad. Layer and d-pad substitutions are provided, but awkward or unexpected input is
  possible; do not use it where an unintended input has consequences.
- Installing USB drivers (the DJI VCOM driver, ViGEmBus, or any other kernel driver) changes
  your operating system. Kernel drivers can destabilise or hang a machine. Back up first.
- The virtual controller is created at the operating-system level. **Some anti-cheat or
  platform software may treat virtual input devices as suspicious.** Account suspension or
  bans are possible and are **your** risk. Do not use this in online, ranked, competitive or
  wagered play, or with any title whose terms forbid third-party input remapping.
- You are responsible for complying with the laws of your jurisdiction, including rules on
  interoperability and reverse engineering, radio equipment regulations, and drone operating
  rules. Nothing here authorises you to breach any law, contract, term of service or platform
  rule.
- Only Windows 10/11 x64 and x86 were tested. Other systems, remote controllers, firmware
  versions and games may not work, or may work incorrectly.

使用者须完全自担风险。**严禁在遥控器与已通电、已对频或可被控制的航空器保持连接的状态下使用本
软件**；安装内核驱动会改变操作系统，可能引起不稳定；虚拟输入设备可能被反作弊或平台软件视为可疑，
账号封禁风险由使用者自行承担；请勿在联机、排位、竞技或任何禁止第三方输入映射的作品中使用。
遵守你所在司法辖区的法律与平台条款是你的责任。

## 4. No warranty / 无担保

To the maximum extent permitted by applicable law, the software is provided **"AS IS"**, with
**no warranty of any kind**, whether express, implied, statutory or otherwise, including but
not limited to the implied warranties of merchantability, fitness for a particular purpose,
title, accuracy, compatibility, uninterrupted or error-free operation, and non-infringement.

The entire risk as to the quality, safety and performance of the software is yours. Should the
software prove defective, you assume the cost of all necessary servicing, repair or correction.

在法律允许的最大范围内，本软件按"现状"提供，**不提供任何明示或默示的担保**，包括但不限于适销性、
特定用途适用性、权利完整、准确性、兼容性、不中断、无错误及不侵权的默示担保。质量与安全的全部风险
由使用者承担；如软件存在缺陷，所有必要的维护、修复或更正费用由使用者自行承担。

## 5. Limitation of liability / 责任限制

To the maximum extent permitted by applicable law, in no event shall any author, contributor,
copyright holder, distributor, or anyone who published, mirrored, forked or re-hosted this
software be liable for **any** claim, damages or other liability, whether in contract, tort
(including negligence), strict liability or otherwise, arising from, out of, or in connection
with the software or the use or inability to use it, **including but not limited to**:

- loss of life, personal injury, or property damage of any kind, including damage to or loss of
  aircraft, batteries, controllers, computers, peripherals or third-party property;
- loss or corruption of data, files, saves, settings or licences;
- suspension, termination or banning of accounts, subscriptions, stores or services;
- loss of profits, revenue, goodwill, business, anticipated savings, or business interruption;
- cost of procuring substitute goods or services;
- any indirect, incidental, special, exemplary, punitive, consequential or statutory damages,

even if advised of the possibility of such damages, and even if a remedy fails of its essential
purpose. Some jurisdictions do not allow certain exclusions or limitations, so parts of this
section may not apply to you; in that case liability is limited to the maximum extent permitted
in that jurisdiction. If you are dissatisfied with the software or with any part of this
disclaimer, your **exclusive** remedy is to stop using it.

在法律允许的最大范围内，任何作者、贡献者、版权人、分发者及转载者均不对因使用或无法使用本软件而
产生的**任何**索赔、损害或其他责任负责，包括但不限于：人身伤害与财产灭失（含航空器、电池、遥控器、
计算机及第三方财产）、数据或存档损失、账号或服务被封禁、利润与商誉损失、替代采购成本，以及任何
间接、附带、特殊、惩罚性或后果性损害，即使已被告知发生该等损害的可能性。若你对本软件或本声明不满，
**唯一**救济是停止使用。

## 6. No telemetry / 无遥测

The program performs no network I/O of any kind: it opens no sockets, makes no outbound
connections, contains no analytics, no update checker, no crash reporter and no identifiers.
All configuration and logs are plain files written next to the executable. It can be audited by
reading the source, and it is designed to run correctly on a machine with no network at all.

程序不进行任何网络访问：不开启套接字、不发起外连、无统计、无自动更新检查、无崩溃上报、无设备标识。
配置与日志均为可执行文件旁的明文文件，可通过阅读源码审计，并可在完全离线的机器上正常运行。

## 7. Third-party components / 第三方组件

This repository vendors **ViGEmClient** (`third_party/ViGEmClient`, MIT, see its own `LICENSE`)
and depends at runtime on the **ViGEmBus** kernel driver, which is **not** included here and is
distributed by its own authors. The bus driver was retired and archived upstream in November
2023; it still works, but it receives no further maintenance, and that is not something this
project can change.

Reference projects consulted while reconstructing the protocol are listed in `README.md` for
attribution. Their source is **not** vendored or redistributed here, and their licenses do not
apply to this repository.

本仓库内置 MIT 许可的 ViGEmClient；运行时依赖的 ViGEmBus 内核驱动由原作者单独分发，未包含于此，
且上游已于 2023 年 11 月退役归档。协议重建过程中参考的开源项目仅在 `README.md` 中署名致谢，
本仓库未包含亦未再分发其源码。

## 8. Acceptance / 接受

Downloading, building, distributing or running this software constitutes acceptance of this
disclaimer and of the MIT license in `LICENSE`. If you do not accept them, do not use the
software and delete all copies.

下载、编译、分发或运行本软件即表示你接受本免责声明与 `LICENSE` 中的 MIT 许可。若不接受，请勿使用
并删除所有副本。

---

*This disclaimer is written for a non-commercial hobby project by a non-lawyer and is not legal
advice. It grants no rights beyond those in `LICENSE` and does not override mandatory consumer
protection law in any jurisdiction.*

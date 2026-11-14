# 驾驶风格（card +0x184 cool/wild，+0x185 smooth/rough）：来源与 port 接法

## 1. 对象与数值来源（已从 main 反汇编确认）

- `[0x97ed890]` = clMain 单例；`[clMain+0x4c]` = **clWorkSpace***（finiJoin 拿它调
  `clWorkSpace::isEventMode`；为真时跳过整段风格更新，只在非 event 模式的 versus 结束后更新）。
- `clWorkSpace` 是 pImpl：`[ws]` → 比赛数据块。`clWorkSpace::updateRace`（0x88bd508）每帧把
  `yt::CSeq::GetPlayResult(impl+0x240)` 填进去，所以
  - impl+0x258 = **SPlayResult+0x18** = `yt::CWallLogger::GetPlayStyle()` → `getSmoothRough`（0x88bccc4）→ card +0x185
  - impl+0x25c = **SPlayResult+0x1c** = `yt::CHitLogger::GetPlayStyle()` → `getCoolWild`（0x88bd498）→ card +0x184
- `GetPlayResult`（0x898317c）只在 play state 为 3/6/8 时填写（掩码 0x148），其他状态返回 0、不碰结构；
  `clWorkSpace::initRace` 把 +0x258/+0x25c 置 0（=中间值 3）。
- 0x898c440 处那两条清零指令**不是**这个字段：它写的是 CSeq::Init 里每辆车 new 出来的 0x684 字节对象，无关。
- 整个程序里只有 finiJoin 调用 getCoolWild/getSmoothRough。

### 日志对象（CSeq::Init 0x898cd1e..0x898cd5a 创建，CSeq::End 0x89871ee.. 释放并清零指针）
| 对象 | 指针 | 布局 |
|---|---|---|
| yt::CWallLogger（8 字节）| `CSeq::pWallLogger` 0x98028d0 | +0 float 撞墙深度累计，+4 int 撞墙帧数 |
| yt::CHitLogger（0x14 字节）| `CSeq::pHitLogger` 0x98028d4 | +0 帧计数，+4 最后命中帧，+8 本次碰撞累计，+0xc 最后碰撞种类（初值 0x7fff），+0x10 总量 |

每次 CSeq::Exec（0x8984c87..0x8984cfa，无条件到达该段时）以玩家 CMachine `m=[0x98028e4]` 调用：
- `CWallLogger::Drive(m+0xd4)`（墙的 SHitPushBack）
- `CHitLogger::Drive(m+0x90 /*车-车 SHitPushBack*/, m+0xab0 /*速度*/, copy(m+0xa0c) /*朝向向量*/, MF(m+0xb04) /*未使用*/)`

SHitPushBack（0x44 字节）：+0 byte 是否接触，+1 byte（墙：置位则不计），+4 int 种类，+0x30..+0x38 推回向量，+0x3c 其长度。

### 规则
**CWallLogger::Drive**：`if (pb[0] && !pb[1]) { sum += pb.len(+0x3c); count++; }`

**CWallLogger::GetPlayStyle**（smooth/rough 的 X）：
`sum > 300000 || count > 30` → **1**；否则 `sum >= 20000 || count > 6` → **0**；否则 **-1**。

**CHitLogger::Drive**：
```
frame++;
if (pb.hit && pb.kind <= 3 && pb.kind <= last_kind) {
  sp = |vel|;
  if (sp > 10000) {                         // mm/s，约 36 km/h
    d    = dot(vel/sp, pb.vec);             // pb.vec 未归一化（长度 pb.len）
    into = d > 0 ? 0 : -d;                  // 与运动方向相反的推回量
    n    = pb.len > 0.01 ? pb.vec/pb.len : 0;
    w    = fabsf(dot(n, head)) * 0.8 + 0.2; // 正面/追尾 1.0，侧面 0.2
    last_frame = frame; last_kind = pb.kind; acc += into * w;
  }
}
if (frame - last_frame == 10) {             // 接触结束 10 帧后结算一次碰撞
  total += clamp(acc, 0, 2500); acc = 0; last_kind = 0x7fff;
}
```
**CHitLogger::GetPlayStyle**（cool/wild 的 X）：`total < 3500` → **-1**；`total >= 7500` → **1**；否则 **0**。

**比赛值** r = clamp(0..6, (signed char)(int)((1 - X) * 3))：X=-1 → 6，X=0 → 3，X=1 → 0
（少撞墙/少撞车 → 6，多 → 0）。卡上值按 finiJoin：
`card = clamp(0..6, (int)(((card/3 - 1) + (r/3 - 1)/3 + 1) * 3))`。

## 2. port 里能不能直接拿到？—— **不能，目前恒为中间值**

`tools/recomp/recomp.py` 第 69 行 `NOT_LOGIC` 里有 `"yt::CHitLogger", "yt::CWallLogger"`
（`tools/recomp/seq.py` 第 12 行同样），所以 phys_rc.c 里这四个函数是返回 0 的 stub
（phys_rc.c 1093–1116 行：f_89b3930 / f_89b3958 / f_89b3984 / f_89b3a20）。
日志对象本身由重编译的 CSeq::Init 正常分配（内存里有），但从不被写入；SPlayResult +0x18/+0x1c 永远是 0 → r=3。

### 方案 A（最忠实，需要重新生成 phys_rc.c）
1. 从 recomp.py 的 `NOT_LOGIC`（以及 seq.py 的对照列表）删掉 `"yt::CHitLogger", "yt::CWallLogger"`，重新生成 phys_rc.c。
   用到的导入只有 `sqrtf`、`fabsf`，recomp.py 第 140–141 行已支持。
2. 读取：car.c 里 versus 分支在 st==3/6/8 时已经 `call(f_898317c, 1, P.result)`（car.c 769 行）；
   紧接着读 `(int)M32(P.result + 0x18)`（smooth/rough 的 X）和 `(int)M32(P.result + 0x1c)`（cool/wild 的 X），
   存进 car_race（例如新字段 `style_sr`, `style_cw`），再在 finiJoin 对应处（menu/app 的 versus 结算）
   用 playstyle.inc 的 `ps_race_value` + `ps_card_mix` 更新卡。
   注意：必须在 CSeq::End 之前读（End 会删掉日志对象）；versus 的 port 流程不调用 End，没问题。

### 方案 B（不动 phys_rc.c）：`src/v23/playstyle.inc`
C 版的两个 Logger，直接读 CMachine 内存：
- 每场比赛 car_start 里 CSeq::Init 之后：`playstyle_reset();`
- car.c 的 `car_update` 中 `call(f_8983f64, 1, P.gin_ptr);`（第 1192 行）之后：`playstyle_frame();`
  （只在 car_update，不在 car_movie_update 里调）
- versus 结束、写卡时（非 event 模式）：`playstyle_card_update(&card[0x184], &card[0x185]);`
  或用 `playstyle_hit()` / `playstyle_wall()` 拿 X。
与原版的差别：原版在 Exec 中途（0x8984c87）调用，B 在 Exec 之后读同样的 CMachine 字段；
那几个字段在 Exec 末尾之前不再被改写的话结果相同（未逐帧对照验证）。
.inc 尚未编译检查（zig cc 语法检查没跑完）；它需要 car.c 里已有的 rc_runtime.h、string.h 和 `MACHINE_PTR`。

## 地址速查
| 名称 | 地址 |
|---|---|
| clRaceThread::finiJoin | 0x8907a98（getCoolWild 调用 0x8907b72，getSmoothRough 0x8907c14）|
| clWorkSpace::getCoolWild / getSmoothRough | 0x88bd498 / 0x88bccc4 |
| clWorkSpace::updateRace / initRace | 0x88bd508 / 0x88bd714 |
| yt::CSeq::GetPlayResult | 0x898317c |
| CWallLogger::Drive / GetPlayStyle | 0x89b3930 / 0x89b3984 |
| CHitLogger::Drive / GetPlayStyle | 0x89b3a20 / 0x89b3958 |
| 常数 | 300000 @0x9439058，20000 @0x9438fe8，3500 @0x9423a54，7500 @0x9439054，2500 @0x9438f9c，10000 @0x93dd140，0.8 @0x934c1cc，0.2 @0x92f8258，0.01 @0x92f8254 |

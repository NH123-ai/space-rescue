"""参考实现：与 C 程序 (data_module.c / algorithm_module.c / viz_module.c) 对齐的
Python 版仿真与 trace 生成器。

用途：
  1) 在没有编译器的工作机上先验证 Web 前端能否正常渲染；
  2) 作为“独立第二个实现”，用于校验 C 版本生成的 trace 是否一致
     （同一 seed 下 target 坐标、route 顺序、各航段时刻应当相同）。

运行：python tools/gen_trace_reference.py [--n 9] [--scenario standard] [--seed 2026] [--out web]
"""
import argparse
import json
import math
import os
import sys
import time

MAX_TARGET_NUM = 12
SPEED = 10.0
UPDATE_INTERVAL = 5.0
BACKTRACK_NODE_CAP = 3_000_000

SCENARIO_LABEL = {
    0: "标准场景",
    1: "紧急场景（生存时间紧张）",
    2: "星团场景（目标聚集在远处）",
    3: "大尺度场景（散布范围大）",
    4: "完全随机场景",
}
ALGO_NN, ALGO_URGENCY, ALGO_BACKTRACK, ALGO_2OPT = 0, 1, 2, 3
ALGO_NAME = {
    ALGO_NN: "最近邻贪心 NN",
    ALGO_URGENCY: "生存时间优先 EDF",
    ALGO_BACKTRACK: "回溯搜索 (剪枝)",
    ALGO_2OPT: "2-opt 局部搜索",
}

M32 = 0xFFFFFFFF


class Rng:
    """与 C 端一致的 32 位 LCG"""

    def __init__(self, seed):
        self.s = ((seed * 2654435761) + 12345) & M32

    def next(self):
        self.s = (self.s * 1103515245 + 12345) & M32
        return ((self.s >> 16) & 0x7FFF) / 32767.0

    def range(self, lo, hi):
        return lo + (hi - lo) * self.next()


def gen_scenario(count, scenario, seed):
    r_lo, r_hi = 40.0, 160.0
    v_lo, v_hi = -0.60, 0.60
    life_lo, life_hi = 90.0, 240.0
    if scenario == 1:
        life_lo, life_hi = 45.0, 105.0
    elif scenario == 2:
        r_lo, r_hi = 140.0, 175.0
    elif scenario == 3:
        r_lo, r_hi, life_lo, life_hi = 60.0, 320.0, 120.0, 420.0

    rng = Rng(seed)
    targets = []
    for _ in range(count):
        if scenario == 2:
            theta = rng.range(0.0, 6.283185307)
            phi = rng.range(-1.0, 1.0)
            radius = rng.range(r_lo, r_hi)
            x = radius * math.sqrt(1 - phi * phi) * math.cos(theta) + rng.range(-18.0, 18.0)
            y = radius * phi + rng.range(-18.0, 18.0)
            z = radius * math.sqrt(1 - phi * phi) * math.sin(theta) + rng.range(-18.0, 18.0)
        else:
            u1, u2, u3 = rng.next(), rng.next(), rng.next()
            radius = r_lo + (r_hi - r_lo) * (u1 ** (1.0 / 3.0))
            theta = 6.283185307 * u2
            phi = math.acos(2.0 * u3 - 1.0)
            x = radius * math.sin(phi) * math.cos(theta)
            y = radius * math.cos(phi) * 0.75
            z = radius * math.sin(phi) * math.sin(theta)

        theta = rng.range(0.0, 6.283185307)
        phi = rng.range(-1.0, 1.0)
        sp = rng.range(v_lo, v_hi)
        s = math.sqrt(1 - phi * phi)
        vx, vy, vz = sp * s * math.cos(theta), sp * phi, sp * s * math.sin(theta)
        life = rng.range(life_lo, life_hi)
        targets.append({"x": x, "y": y, "z": z, "vx": vx, "vy": vy, "vz": vz, "life": life})
    return targets


def pos_at(tg, t):
    return (tg["x"] + tg["vx"] * t, tg["y"] + tg["vy"] * t, tg["z"] + tg["vz"] * t)


def travel_time_to(tg, t_begin, fx, fy, fz):
    """解一元二次方程求拦截时间（与 C 端 travel_time_to 等价）"""
    px, py, pz = pos_at(tg, t_begin)
    dx, dy, dz = px - fx, py - fy, pz - fz
    a = tg["vx"] ** 2 + tg["vy"] ** 2 + tg["vz"] ** 2 - SPEED ** 2
    b = 2.0 * (dx * tg["vx"] + dy * tg["vy"] + dz * tg["vz"])
    c = dx * dx + dy * dy + dz * dz
    if abs(a) < 1e-12:
        return 0.0 if abs(b) < 1e-12 else -c / b
    disc = b * b - 4.0 * a * c
    if disc < 0:
        return math.sqrt(c) / SPEED
    r1 = (-b + math.sqrt(disc)) / (2 * a)
    r2 = (-b - math.sqrt(disc)) / (2 * a)
    if r1 > r2:
        r1, r2 = r2, r1
    tau = r1 if r1 >= 0 else r2
    return max(tau, 0.0)


def simulate(targets, seq):
    done = [False] * len(targets)
    t, sx, sy, sz = 0.0, 0.0, 0.0, 0.0
    plan = {"seq": [], "leg_t": [], "leg_d": [], "length": 0.0,
            "finish_time": 0.0, "saved": 0, "dead": 0, "feasible": 0}

    for idx in seq:
        if idx < 0 or idx >= len(targets) or done[idx]:
            continue
        done[idx] = True
        tau = travel_time_to(targets[idx], t, sx, sy, sz)
        arrive = t + tau
        if arrive > targets[idx]["life"]:
            plan["dead"] += 1
            continue
        px, py, pz = pos_at(targets[idx], arrive)
        plan["seq"].append(idx)
        plan["leg_d"].append(tau * SPEED)
        plan["leg_t"].append(arrive)
        plan["length"] += tau * SPEED
        plan["saved"] += 1
        sx, sy, sz, t = px, py, pz, arrive

    plan["dead"] += sum(1 for d in done if not d)
    plan["finish_time"] = t
    plan["feasible"] = 1 if plan["dead"] == 0 else 0
    return plan


def plan_nearest(targets):
    used = [False] * len(targets)
    t, sx, sy, sz = 0.0, 0.0, 0.0, 0.0
    seq, nodes = [], 0
    for _ in range(len(targets)):
        best, best_tau = -1, 1e18
        for i, tg in enumerate(targets):
            if used[i]:
                continue
            tau = travel_time_to(tg, t, sx, sy, sz)
            nodes += 1
            if tau < best_tau:
                best_tau, best = tau, i
        if best < 0:
            break
        used[best] = True
        seq.append(best)
        if t + best_tau <= targets[best]["life"]:
            sx, sy, sz = pos_at(targets[best], t + best_tau)
            t += best_tau
    plan = simulate(targets, seq)
    plan["algo"] = ALGO_NN
    plan["nodes"] = nodes
    return plan


def plan_urgency(targets):
    valid = [True] * len(targets)
    t, sx, sy, sz = 0.0, 0.0, 0.0, 0.0
    seq, nodes = [], 0
    for _ in range(len(targets)):
        best, best_score, best_tau = -1, 1e18, 0.0
        for i, tg in enumerate(targets):
            if not valid[i]:
                continue
            tau = travel_time_to(tg, t, sx, sy, sz)
            nodes += 1
            score = tg["life"] - (t + tau)
            if score < best_score:
                best_score, best, best_tau = score, i, tau
        if best < 0:
            break
        valid[best] = False
        seq.append(best)
        if t + best_tau <= targets[best]["life"]:
            sx, sy, sz = pos_at(targets[best], t + best_tau)
            t += best_tau
    plan = simulate(targets, seq)
    plan["algo"] = ALGO_URGENCY
    plan["nodes"] = nodes
    return plan


def plan_backtrack(targets):
    n = len(targets)
    state = {"visited": [False] * n, "path": [], "t": 0.0, "pos": (0.0, 0.0, 0.0),
             "length": 0.0, "best_path": [], "best_saved": 0, "best_len": 0,
             "best_length": 1e18, "nodes": 0}

    def dfs():
        state["nodes"] += 1
        if state["nodes"] > BACKTRACK_NODE_CAP:
            return True
        t, (sx, sy, sz) = state["t"], state["pos"]
        for i, tg in enumerate(targets):
            if state["visited"][i]:
                continue
            tau = travel_time_to(tg, t, sx, sy, sz)
            arrive = t + tau
            if arrive > tg["life"]:
                continue
            if state["best_saved"] == n and state["length"] + tau * SPEED >= state["best_length"] - 1e-9:
                continue

            px, py, pz = pos_at(tg, arrive)
            old = (state["t"], state["pos"], state["length"])
            state["visited"][i] = True
            state["path"].append(i)
            state["t"], state["pos"] = arrive, (px, py, pz)
            state["length"] += tau * SPEED

            if len(state["path"]) > state["best_saved"] or (
                len(state["path"]) == state["best_saved"] and state["length"] < state["best_length"]
            ):
                state["best_saved"] = len(state["path"])
                state["best_len"] = len(state["path"])
                state["best_length"] = state["length"]
                state["best_path"] = list(state["path"])

            if dfs():
                return True

            state["visited"][i] = False
            state["path"].pop()
            state["t"], state["pos"], state["length"] = old
        return False

    dfs()
    plan = simulate(targets, state["best_path"])
    plan["algo"] = ALGO_BACKTRACK
    plan["nodes"] = state["nodes"]
    return plan


def plan_2opt(targets):
    cur = plan_urgency(targets)
    nn = plan_nearest(targets)
    build_nodes = cur["nodes"] + nn["nodes"]
    if nn["saved"] > cur["saved"] or (nn["saved"] == cur["saved"] and nn["length"] < cur["length"] - 1e-9):
        cur = nn
    cur["algo"] = ALGO_2OPT
    cur["nodes"] = 0
    rounds, improved = 0, True

    while improved and rounds < 8:
        improved = False
        rounds += 1
        for i in range(len(cur["seq"]) - 1):
            for j in range(i + 1, len(cur["seq"])):
                cand = cur["seq"][:i] + list(reversed(cur["seq"][i:j + 1])) + cur["seq"][j + 1:]
                trial = simulate(targets, cand)
                cur["nodes"] += 1
                if trial["saved"] > cur["saved"] or (
                    trial["saved"] == cur["saved"] and trial["length"] < cur["length"] - 1e-9
                ):
                    trial["nodes"] = cur["nodes"]
                    cur = trial
                    cur["algo"] = ALGO_2OPT
                    improved = True
    cur["nodes"] = build_nodes + cur["nodes"]
    return cur


def choose_best(plans):
    best = 0
    for i in range(1, len(plans)):
        if plans[i]["saved"] > plans[best]["saved"]:
            best = i
        elif plans[i]["saved"] == plans[best]["saved"]:
            if plans[i]["length"] < plans[best]["length"] - 1e-9:
                best = i
            elif abs(plans[i]["length"] - plans[best]["length"]) < 1e-9 and \
                    plans[i]["elapsedMs"] < plans[best]["elapsedMs"]:
                best = i
    return best


def build_trace(count, scenario, seed):
    targets = gen_scenario(count, scenario, seed)
    plans = []
    for fn, name in ((plan_nearest, "nn"), (plan_urgency, "urgency"),
                     (plan_2opt, "2opt"), (plan_backtrack, "bt")):
        t0 = time.time()
        p = fn(targets)
        p["elapsedMs"] = round((time.time() - t0) * 1000.0, 3)
        plans.append(p)

    selected = choose_best(plans)
    sel = plans[selected]

    out_targets = []
    for i, tg in enumerate(targets):
        d = math.sqrt(tg["x"] ** 2 + tg["y"] ** 2 + tg["z"] ** 2)
        out_targets.append({
            "id": i, "name": "T%d" % (i + 1),
            "p0": [round(tg["x"], 3), round(tg["y"], 3), round(tg["z"], 3)],
            "v": [round(tg["vx"], 4), round(tg["vy"], 4), round(tg["vz"], 4)],
            "life": round(tg["life"], 3),
            "dist0": round(d, 3),
            "eta0": round(d / SPEED, 3),
        })

    out_plans = []
    for i, p in enumerate(plans):
        legs = []
        for k, to in enumerate(p["seq"]):
            t0 = 0.0 if k == 0 else p["leg_t"][k - 1]
            legs.append({"from": -1 if k == 0 else p["seq"][k - 1], "to": to,
                         "t0": round(t0, 3), "t1": round(p["leg_t"][k], 3),
                         "dist": round(p["leg_d"][k], 3)})
        outcome = []
        for k in range(len(targets)):
            if k in p["seq"]:
                outcome.append({"id": k, "state": "saved",
                                "t": round(p["leg_t"][p["seq"].index(k)], 3)})
            else:
                outcome.append({"id": k, "state": "dead", "t": round(targets[k]["life"], 3)})
        out_plans.append({
            "id": i, "algo": p["algo"], "name": ALGO_NAME[p["algo"]],
            "saved": p["saved"], "dead": p["dead"], "feasible": p["feasible"],
            "length": round(p["length"], 3), "finish": round(p["finish_time"], 3),
            "nodes": p["nodes"], "elapsedMs": p["elapsedMs"],
            "route": p["seq"], "legs": legs, "outcome": outcome,
        })

    events = [{"t": 0, "type": "launch", "target": -1, "text": "救援飞船离开发射基点"}]
    t = UPDATE_INTERVAL
    rnd = 1
    while t < sel["finish_time"] + 1e-9:
        events.append({"t": round(t, 3), "type": "refresh", "target": -1,
                       "text": "收到目标最新坐标（第 %d 次刷新）" % rnd})
        t += UPDATE_INTERVAL
        rnd += 1
    for k, to in enumerate(sel["seq"]):
        events.append({"t": round(sel["leg_t"][k], 3), "type": "saved", "target": to,
                       "text": "T%d 完成对接，生存时间锁定" % (to + 1)})
    for k in range(len(targets)):
        if k not in sel["seq"]:
            events.append({"t": round(targets[k]["life"], 3), "type": "dead", "target": k,
                           "text": "T%d 生存时间耗尽，救援失败" % (k + 1)})
    events.append({"t": round(sel["finish_time"], 3), "type": "finish", "target": -1,
                   "text": "救援任务结束"})

    return {
        "meta": {
            "title": "太空救援任务 · 最优救援序列可视化",
            "generated": time.strftime("%Y-%m-%d %H:%M:%S"),
            "scenario": scenario,
            "scenarioName": SCENARIO_LABEL.get(scenario, SCENARIO_LABEL[0]),
            "seed": seed,
            "count": count,
            "speed": SPEED,
            "updateInterval": UPDATE_INTERVAL,
            "maxTargets": MAX_TARGET_NUM,
            "selectedPlan": selected,
        },
        "targets": out_targets,
        "plans": out_plans,
        "events": events,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=9)
    ap.add_argument("--scenario", default="standard",
                    choices=["standard", "tight", "cluster", "wide", "random"])
    ap.add_argument("--seed", type=int, default=2026)
    ap.add_argument("--out", default="web")
    args = ap.parse_args()

    smap = {"standard": 0, "tight": 1, "cluster": 2, "wide": 3, "random": 4}
    trace = build_trace(max(1, min(MAX_TARGET_NUM, args.n)), smap[args.scenario], args.seed)

    os.makedirs(args.out, exist_ok=True)
    body = json.dumps(trace, ensure_ascii=False, indent=2)
    with open(os.path.join(args.out, "trace.json"), "w", encoding="utf-8") as f:
        f.write(body + "\n")
    with open(os.path.join(args.out, "trace.js"), "w", encoding="utf-8") as f:
        f.write("/* 参考实现生成（tools/gen_trace_reference.py），仅用于前端联调 */\n")
        f.write("window.RESCUE_TRACE = " + body + ";\n")

    meta = trace["meta"]
    sel = trace["plans"][meta["selectedPlan"]]
    print("已生成 %s/trace.json 与 trace.js" % args.out)
    print("  场景 %s | n=%d | seed=%d" % (meta["scenarioName"], meta["count"], meta["seed"]))
    print("  推荐方案 %s | 获救 %d/%d | 总航程 %.2f | 完成 %.2f | 节点 %d" % (
        sel["name"], sel["saved"], meta["count"], sel["length"], sel["finish"], sel["nodes"]))
    for p in trace["plans"]:
        print("    - %-22s saved=%d len=%8.2f nodes=%8d %.3fms" % (
            p["name"], p["saved"], p["length"], p["nodes"], p["elapsedMs"]))


if __name__ == "__main__":
    main()

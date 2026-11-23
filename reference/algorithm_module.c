/*============================================================================
 *  algorithm_module.c  --  组员 B：核心算法模块
 *----------------------------------------------------------------------------
 *  内容：
 *    1) 统一的“带漂移的到达时间”求解（解一元二次方程，而非近似）
 *    2) 四种救援顺序求解策略
 *         plan_nearest   : 动态最近邻贪心      O(n^2)
 *         plan_urgency   : 生存时间优先贪心    O(n^2)
 *         plan_backtrack : 回溯 + 剪枝（精确） 最坏 O(n!)
 *         plan_2opt      : 2-opt 局部搜索      O(n^2 * 轮数)
 *    3) 方案仿真与可行性判定（生存约束），并统计搜索节点数用于效率对比
 *  依赖：仅依赖数据模块（space_rescue.h 中的 Target / RescueData）
 *===========================================================================*/
#include "space_rescue.h"

/* 回溯搜索的节点上限，防止极端场景长时间卡死（超过则返回当前最优） */
#define BACKTRACK_NODE_CAP   3000000L

/* 计时辅助：优先使用高精度计数器（贪心只要几十微秒，clock() 精度不够） */
#ifdef _WIN32
#  include <windows.h>
static double now_ms(void)
{
    static LARGE_INTEGER freq;
    static int inited = 0;
    LARGE_INTEGER now;
    if (!inited) { QueryPerformanceFrequency(&freq); inited = 1; }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
}
#else
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
#endif

/*----------------------------------------------------------------------------
 *  travel_time_to：从 from 点、在 t_begin 时刻出发，追上（拦截）目标所需时间
 *----------------------------------------------------------------------------
 * 目标位置随时间漂移：P(τ) = P0 + v·t_begin + v·τ
 * 拦截条件：       |P(τ) - from| = SPEED · τ
 * 展开得一元二次方程：
 *     (|v|^2 - SPEED^2)·τ^2 + 2·(D·v)·τ + |D|^2 = 0 ,  D = P0 + v·t_begin - from
 * 因 |v| << SPEED，二次项系数为负、常数项为正，方程必有且仅有一个正根。
 *--------------------------------------------------------------------------*/
static double travel_time_to(const Target *tg, double t_begin,
                             double from_x, double from_y, double from_z)
{
    double px, py, pz, dx, dy, dz, a, b, c, disc, tau;

    target_pos_at(tg, t_begin, &px, &py, &pz);
    dx = px - from_x;
    dy = py - from_y;
    dz = pz - from_z;

    a = tg->vx * tg->vx + tg->vy * tg->vy + tg->vz * tg->vz - SPEED * SPEED;
    b = 2.0 * (dx * tg->vx + dy * tg->vy + dz * tg->vz);
    c = dx * dx + dy * dy + dz * dz;

    if (fabs(a) < 1e-12) {                    /* 退化：直线追赶 */
        return (fabs(b) < 1e-12) ? 0.0 : -c / b;
    }
    disc = b * b - 4.0 * a * c;
    if (disc < 0.0) {
        /* 数值上不该发生：退化为“按当前距离近似” */
        return sqrt(c) / SPEED;
    }
    {
        double r1 = (-b + sqrt(disc)) / (2.0 * a);
        double r2 = (-b - sqrt(disc)) / (2.0 * a);
        /* 取最小非负根 */
        if (r1 > r2) { tau = r2; r2 = r1; r1 = tau; }
        tau = (r1 >= 0.0) ? r1 : r2;
        if (tau < 0.0) tau = 0.0;
    }
    return tau;
}

/*----------------------------------------------------------------------------
 *  plan_simulate：按给定顺序完整推演一次救援过程
 *----------------------------------------------------------------------------
 *  规则：
 *    · 飞船从原点出发，速度恒定 SPEED，飞向目标当前（漂移后）位置
 *    · 若到达时刻晚于目标最长生存时间，则该目标死亡；飞船放弃该目标
 *      （继续飞只会耽误别人，因此直接跳过，不消耗时间）
 *    · 记录每段航程的起止时刻与距离，用于动画与甘特图
 *--------------------------------------------------------------------------*/
void plan_simulate(RescueData *rd, const int seq[], int len, RoutePlan *out)
{
    int    done[MAX_TARGET_NUM] = {0};
    double t = 0.0;
    double sx, sy, sz;                   /* 飞船位置 */
    int    k;

    out->len         = 0;
    out->length      = 0.0;
    out->finish_time = 0.0;
    out->saved       = 0;
    out->dead        = 0;
    out->feasible    = 0;

    sx = rd->origin_x; sy = rd->origin_y; sz = rd->origin_z;

    for (k = 0; k < len; ++k) {
        int    idx = seq[k];
        double tau, arrive, px, py, pz;

        if (idx < 0 || idx >= rd->target_count || done[idx]) continue;
        done[idx] = 1;

        tau = travel_time_to(&rd->target_list[idx], t, sx, sy, sz);
        arrive = t + tau;

        if (arrive > rd->target_list[idx].life_max_time) {
            out->dead++;                       /* 到达前已死亡，放弃 */
            continue;
        }

        /* 成功救援：记录航段 */
        target_pos_at(&rd->target_list[idx], arrive, &px, &py, &pz);
        out->seq[out->len]      = idx;
        out->leg_d[out->len]    = tau * SPEED;
        out->leg_t[out->len]    = arrive;
        out->length            += tau * SPEED;
        out->len++;
        out->saved++;

        sx = px; sy = py; sz = pz;
        t  = arrive;
    }

    /* 未出现在 seq 中的目标同样视为死亡（来不及） */
    for (k = 0; k < rd->target_count; ++k)
        if (!done[k]) out->dead++;

    out->finish_time = t;
    out->feasible    = (out->dead == 0) ? 1 : 0;
}

/*----------------------------------------------------------------------------
 *  plan_apply：把 RoutePlan 写回 RescueData（供主程序 / 可视化输出使用）
 *--------------------------------------------------------------------------*/
void plan_apply(RescueData *rd, const RoutePlan *plan)
{
    int i;
    reset_target_states(rd);
    for (i = 0; i < plan->len; ++i) {
        int idx = plan->seq[i];
        rd->best_seq[i] = idx;
        rd->target_list[idx].is_saved  = 1;
        rd->target_list[idx].save_time = plan->leg_t[i];
        rd->target_list[idx].life_left = plan->leg_t[i];   /* 救援时已存活时间 */
    }
    for (i = plan->len; i < MAX_TARGET_NUM; ++i) rd->best_seq[i] = -1;

    rd->total_time     = plan->finish_time;
    rd->total_distance = plan->length;
    rd->saved_count    = plan->saved;
    rd->dead_count     = plan->dead;
    rd->algo_used      = plan->algo;
    rd->find_success   = plan->feasible;
}

/*----------------------------------------------------------------------------
 *  plan_nearest：动态最近邻贪心
 *    每一步在“当前时刻、当前位置”重新计算追踪时间，选最短的一个。
 *    复杂度 O(n^2)，不保证可行，但速度快，是最常用的基线。
 *--------------------------------------------------------------------------*/
void plan_nearest(RescueData *rd, RoutePlan *out)
{
    int    used[MAX_TARGET_NUM] = {0};
    int    seq[MAX_TARGET_NUM];
    double t = 0.0, sx, sy, sz;
    int    step, i;

    memset(out, 0, sizeof(RoutePlan));
    out->algo = ALGO_NN;

    sx = rd->origin_x; sy = rd->origin_y; sz = rd->origin_z;

    for (step = 0; step < rd->target_count; ++step) {
        int    best = -1;
        double best_tau = 1e18;

        for (i = 0; i < rd->target_count; ++i) {
            double tau;
            if (used[i]) continue;
            tau = travel_time_to(&rd->target_list[i], t, sx, sy, sz);
            out->nodes++;
            if (tau < best_tau) { best_tau = tau; best = i; }
        }
        if (best < 0) break;
        used[best] = 1;
        seq[step]  = best;

        /* 提前判断：若到达即死亡，则该目标放弃，位置与时间都不变 */
        if (t + best_tau <= rd->target_list[best].life_max_time) {
            target_pos_at(&rd->target_list[best], t + best_tau, &sx, &sy, &sz);
            t += best_tau;
        }
    }

    plan_simulate(rd, seq, rd->target_count, out);
    out->algo = ALGO_NN;
}

/*----------------------------------------------------------------------------
 *  plan_urgency：生存时间优先（紧迫度）贪心
 *    在“还能救到”的候选里，选生存余量最小（最紧迫）的目标先救，
 *    即 优先级 = life_max_time - 预计到达时刻，越小越先救。
 *    与最近邻相比，更贴合本题“存在最长生存时间”的约束。
 *--------------------------------------------------------------------------*/
void plan_urgency(RescueData *rd, RoutePlan *out)
{
    int    seq[MAX_TARGET_NUM];
    double t = 0.0, sx, sy, sz;
    int    step, i;
    char   valid[MAX_TARGET_NUM];

    memset(out, 0, sizeof(RoutePlan));
    out->algo = ALGO_URGENCY;

    sx = rd->origin_x; sy = rd->origin_y; sz = rd->origin_z;
    for (i = 0; i < rd->target_count; ++i) valid[i] = 1;

    for (step = 0; step < rd->target_count; ++step) {
        int    best = -1;
        double best_score = 1e18, best_tau = 0.0;

        for (i = 0; i < rd->target_count; ++i) {
            double tau, score;
            if (!valid[i]) continue;
            tau   = travel_time_to(&rd->target_list[i], t, sx, sy, sz);
            score = (rd->target_list[i].life_max_time - (t + tau));  /* 余量 */
            out->nodes++;
            if (score < best_score) { best_score = score; best = i; best_tau = tau; }
        }
        if (best < 0) break;
        valid[best] = 0;
        seq[step]   = best;

        if (t + best_tau <= rd->target_list[best].life_max_time) {
            target_pos_at(&rd->target_list[best], t + best_tau, &sx, &sy, &sz);
            t += best_tau;
        }
    }

    plan_simulate(rd, seq, rd->target_count, out);
    out->algo = ALGO_URGENCY;
}

/*----------------------------------------------------------------------------
 *  回溯搜索的上下文
 *--------------------------------------------------------------------------*/
typedef struct {
    RescueData *rd;
    int    visited[MAX_TARGET_NUM];
    int    path[MAX_TARGET_NUM];
    int    path_len;
    double t, sx, sy, sz;
    double length;

    /* 当前最优解 */
    int    best_path[MAX_TARGET_NUM];
    int    best_len;
    double best_length;
    double best_finish;
    int    best_saved;
    int    best_dead;

    long   nodes;
    int    aborted;
} BtCtx;

/* 递归主体：从当前状态继续枚举可行的下一个目标 */
static void bt_dfs(BtCtx *c)
{
    RescueData *rd = c->rd;
    int      i;
    int      any_candidate = 0;

    if (++c->nodes > BACKTRACK_NODE_CAP) { c->aborted = 1; return; }

    for (i = 0; i < rd->target_count; ++i) {
        double tau, arrive, px, py, pz;

        if (c->visited[i]) continue;

        tau = travel_time_to(&rd->target_list[i], c->t, c->sx, c->sy, c->sz);
        arrive = c->t + tau;

        /* 剪枝 1：到达即超期 —— 该目标此刻之后就永远救不到（到达时刻只增不减）*/
        if (arrive > rd->target_list[i].life_max_time) continue;

        /* 剪枝 2：航程已经不小于已知最优，继续走下去不可能更短 */
        if (c->best_saved == rd->target_count &&
            c->length + tau * SPEED >= c->best_length - 1e-9)   continue;

        any_candidate = 1;
        target_pos_at(&rd->target_list[i], arrive, &px, &py, &pz);

        {
            double old_t = c->t, ox = c->sx, oy = c->sy, oz = c->sz;
            double old_len = c->length;

            c->visited[i]  = 1;
            c->path[c->path_len++] = i;
            c->t = arrive; c->sx = px; c->sy = py; c->sz = pz;
            c->length += tau * SPEED;

            if (c->path_len > c->best_saved ||
                (c->path_len == c->best_saved && c->length < c->best_length)) {
                c->best_saved  = c->path_len;
                c->best_length = c->length;
                c->best_finish = c->t;
                c->best_len    = c->path_len;
                memcpy(c->best_path, c->path, sizeof(int) * (size_t)c->path_len);
            }

            bt_dfs(c);

            c->visited[i] = 0;
            c->path_len--;
            c->t = old_t; c->sx = ox; c->sy = oy; c->sz = oz;
            c->length = old_len;
        }
        if (c->aborted) return;
    }

    /* 无后续可行目标：路径自然结束（剩下的目标都救不到） */
    (void)any_candidate;
}

/*----------------------------------------------------------------------------
 *  plan_backtrack：回溯 + 剪枝，求“获救数量最多、且总航程最短”的方案
 *    规模控制：MAX_TARGET_NUM = 12；加生存约束剪枝与长度下界剪枝，
 *    并设 300 万节点上限保证任何输入都能在秒级返回。
 *--------------------------------------------------------------------------*/
void plan_backtrack(RescueData *rd, RoutePlan *out)
{
    BtCtx c;
    double t0 = now_ms();

    memset(&c, 0, sizeof(c));
    c.rd = rd;
    c.t  = 0.0;
    c.sx = rd->origin_x; c.sy = rd->origin_y; c.sz = rd->origin_z;
    c.best_length = 1e18;
    c.best_saved  = 0;
    c.best_len    = 0;

    bt_dfs(&c);

    memset(out, 0, sizeof(RoutePlan));
    out->algo       = ALGO_BACKTRACK;
    out->nodes      = c.nodes;
    out->elapsed_ms = now_ms() - t0;

    plan_simulate(rd, c.best_path, c.best_len, out);
    out->algo       = ALGO_BACKTRACK;
    out->nodes      = c.nodes;
    out->elapsed_ms = now_ms() - t0;
}

/*----------------------------------------------------------------------------
 *  plan_2opt：从“最近邻 / 紧迫度贪心”中较优者出发，做 2-opt 段内反转改进
 *    目标：在保持“获救数不下降”的前提下缩短总航程。
 *    说明：nodes 统计包含构解阶段（NN + EDF）与全部局部搜索试探，
 *          这样与其它策略的“节点数”口径一致，便于横向比较复杂度。
 *--------------------------------------------------------------------------*/
void plan_2opt(RescueData *rd, RoutePlan *out)
{
    RoutePlan cur, nn;
    int    improved = 1, rounds = 0;
    long   build_nodes;
    double t0 = now_ms();

    /* 初始解：取“最近邻”与“生存时间优先”中较优者，避免从差的起点出发 */
    plan_urgency(rd, &cur);
    plan_nearest(rd, &nn);
    build_nodes = cur.nodes + nn.nodes;
    if (nn.saved > cur.saved ||
        (nn.saved == cur.saved && nn.length < cur.length - 1e-9)) cur = nn;
    cur.algo  = ALGO_2OPT;
    cur.nodes = 0;

    while (improved && rounds < 8) {
        int i, j;
        improved = 0;
        rounds++;

        for (i = 0; i < cur.len - 1; ++i) {
            for (j = i + 1; j < cur.len; ++j) {
                int    cand[MAX_TARGET_NUM];
                RoutePlan trial;
                int    k, cidx = 0;

                for (k = 0; k < i; ++k)       cand[cidx++] = cur.seq[k];
                for (k = j; k >= i; --k)      cand[cidx++] = cur.seq[k];   /* 反转 */
                for (k = j + 1; k < cur.len; ++k) cand[cidx++] = cur.seq[k];

                plan_simulate(rd, cand, cidx, &trial);
                cur.nodes++;
                trial.algo = ALGO_2OPT;

                /* 仅接受“不减少获救数”且“更短”的改进 */
                if (trial.saved > cur.saved ||
                    (trial.saved == cur.saved && trial.length < cur.length - 1e-9)) {
                    trial.nodes = cur.nodes;
                    cur = trial;
                    cur.algo = ALGO_2OPT;
                    improved = 1;
                }
            }
        }
    }

    *out = cur;
    out->algo       = ALGO_2OPT;
    out->elapsed_ms = now_ms() - t0;
    out->nodes      = build_nodes + cur.nodes;
}

/*----------------------------------------------------------------------------
 *  兼容 v1 接口：tsp_greedy / tsp_backtrack / check_time_constraint
 *--------------------------------------------------------------------------*/
void tsp_greedy(RescueData *rd)
{
    RoutePlan plan;
    plan_nearest(rd, &plan);
    plan_apply(rd, &plan);
}

void tsp_backtrack(RescueData *rd)
{
    RoutePlan plan;
    plan_backtrack(rd, &plan);
    plan_apply(rd, &plan);
}

int check_time_constraint(RescueData *rd, int seq[], int len)
{
    RoutePlan plan;
    plan_simulate(rd, seq, len, &plan);
    return plan.feasible;
}

void plan_name(int algo, char *buf, int bufsz)
{
    const char *name;
    switch (algo) {
        case ALGO_NN:        name = "最近邻贪心 NN";        break;
        case ALGO_URGENCY:   name = "生存时间优先 EDF";     break;
        case ALGO_BACKTRACK: name = "回溯搜索 (剪枝)";      break;
        case ALGO_2OPT:      name = "2-opt 局部搜索";       break;
        default:             name = "未知方案";             break;
    }
    snprintf(buf, (size_t)bufsz, "%s", name);
}

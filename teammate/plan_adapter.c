/*============================================================================
 *  plan_adapter.c  --  适配层：把队友 A/B 的实现接到工程其余部分
 *----------------------------------------------------------------------------
 *  为什么需要这一层：
 *    · 队友 A（data.c）只提供了 read_target_data / print_target_info /
 *      calc_3d_distance / update_target_pos；而可视化与场景演示还需要
 *      “目标在任意时刻的位置”“可复现的场景生成”“状态重置”。
 *    · 队友 B（模块b.cpp）提供 check_time_constraint / tsp_greedy /
 *      tsp_backtrack，工作方式是“往 RescueData 里写 best_seq + save_time”；
 *      而可视化模块与站点需要结构化的一条方案 RoutePlan
 *      （顺序 + 每段起止时刻与距离 + 节点数 + 耗时 + 可行性），才能画航线、
 *      做算法对比、切方案回放。
 *    · 教学大纲的扩展分要求“对比分析不同方法并评价效率”，所以除队友的
 *      最近邻贪心与回溯之外，这里再补两条对照策略：EDF（生存时间优先）
 *      与 2-opt 局部改进，凑齐四种方案做横向对比。
 *
 *  约定：所有“飞行时间”都用截击方程的解析解（r果目标是漂移的，
 *        飞行期间它也在动），因此 C 端仿真与网页回放（p0+v·t）严格一致。
 *===========================================================================*/
#include "space_rescue.h"
#include "rescue_internal.h"

#include <stddef.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>      /* QueryPerformanceCounter：测算法耗时要毫秒级精度 */
#endif

/* 队友 B 的回溯节点计数器（定义在 algorithm_module_b.c） */
extern long tsp_last_nodes(void);

/* 剪枝回溯的节点上限，保证任何输入都能在秒级返回 */
#define PRUNE_NODE_CAP 3000000L

/*============================================================================
 *  一、时间与位置工具
 *===========================================================================*/
static double now_ms(void)
{
#ifdef _WIN32
    {
        static LARGE_INTEGER freq;
        static int inited = 0;
        LARGE_INTEGER now;

        if (!inited) { QueryPerformanceFrequency(&freq); inited = 1; }
        QueryPerformanceCounter(&now);
        return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
    }
#else
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
    }
#endif
}

void rescue_pos_at(const Target *tg, double t, double *px, double *py, double *pz)
{
    *px = tg->x + tg->vx * t;
    *py = tg->y + tg->vy * t;
    *pz = tg->z + tg->vz * t;
}

double rescue_intercept_time(const Target *tg, double t_begin,
                             double from_x, double from_y, double from_z)
{
    double px, py, pz, dx, dy, dz, a, b, c, disc;

    rescue_pos_at(tg, t_begin, &px, &py, &pz);
    dx = px - from_x;
    dy = py - from_y;
    dz = pz - from_z;

    a = tg->vx * tg->vx + tg->vy * tg->vy + tg->vz * tg->vz - SPEED * SPEED;
    b = 2.0 * (dx * tg->vx + dy * tg->vy + dz * tg->vz);
    c = dx * dx + dy * dy + dz * dz;

    if (fabs(a) < 1e-12) {                     /* 退化：直线追赶 */
        return (fabs(b) < 1e-12) ? 0.0 : -c / b;
    }
    disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return sqrt(c) / SPEED;    /* 数值兜底：按当前距离近似 */

    {
        double r1 = (-b + sqrt(disc)) / (2.0 * a);
        double r2 = (-b - sqrt(disc)) / (2.0 * a);
        double lo = (r1 < r2) ? r1 : r2;
        double hi = (r1 < r2) ? r2 : r1;

        if (lo >= 0.0) return lo;
        return (hi > 0.0) ? hi : 0.0;
    }
}

/* 对外契约里声明的 A 模块接口：某目标在任意时刻的位置 */
void target_pos_at(const Target *tg, double t, double *px, double *py, double *pz)
{
    rescue_pos_at(tg, t, px, py, pz);
}

/*============================================================================
 *  二、A 模块缺失的两件事：状态重置 / 可复现场景生成
 *===========================================================================*/
void reset_target_states(RescueData *rd)
{
    int i;

    if (rd == NULL) return;

    for (i = 0; i < rd->target_count; ++i) {
        rd->target_list[i].is_saved  = 0;
        rd->target_list[i].save_time = -1.0;
        rd->target_list[i].life_left = rd->target_list[i].life_max_time;
    }
    for (i = 0; i < MAX_TARGET_NUM; ++i) rd->best_seq[i] = -1;

    rd->total_time     = 0.0;
    rd->total_distance = 0.0;
    rd->saved_count    = 0;
    rd->dead_count     = 0;
    rd->find_success   = 0;
}

/* 确定性伪随机（同一 seed → 同一场景，便于测试复现与 trace 可回放） */
static unsigned int g_rand_state = 1u;

static void rand_seed(unsigned seed)
{
    g_rand_state = (unsigned int)seed * 2654435761u + 12345u;
}

static double rand_next(void)
{
    g_rand_state = g_rand_state * 1103515245u + 12345u;
    return (double)((g_rand_state >> 16) & 0x7FFFu) / 32767.0;
}

static double rand_range(double lo, double hi)
{
    return lo + (hi - lo) * rand_next();
}

void gen_scenario(RescueData *rd, int count, int scenario, unsigned seed)
{
    int    i;
    double r_lo = 40.0, r_hi = 160.0;      /* 初始距离范围 */
    double v_lo = -0.60, v_hi = 0.60;      /* 漂移速度（低速） */
    double life_lo = 90.0, life_hi = 240.0;

    if (rd == NULL) return;
    if (count < 1) count = 1;
    if (count > MAX_TARGET_NUM) count = MAX_TARGET_NUM;
    if (seed == 0u) seed = (unsigned)time(NULL);

    switch (scenario) {
        case SCENARIO_TIGHT:   life_lo = 45.0;  life_hi = 105.0;  break;
        case SCENARIO_CLUSTER: r_lo = 140.0;    r_hi = 175.0;     break;
        case SCENARIO_WIDE:    r_lo = 60.0;     r_hi = 320.0;
                               life_lo = 120.0; life_hi = 420.0;  break;
        default:                                                  break;
    }

    rand_seed(seed);
    memset(rd, 0, sizeof(RescueData));
    rd->target_count = count;
    rd->origin_x = rd->origin_y = rd->origin_z = 0.0;
    rd->seed     = seed;
    rd->scenario = scenario;

    for (i = 0; i < count; ++i) {
        Target *tg = &rd->target_list[i];
        double theta, phi, radius;

        if (scenario == SCENARIO_CLUSTER) {
            /* 星团：先取一个团心方向，再在团心附近抖动 */
            theta  = rand_range(0.0, 6.283185307);
            phi    = rand_range(-1.0, 1.0);
            radius = rand_range(r_lo, r_hi);
            tg->x = radius * sqrt(1.0 - phi * phi) * cos(theta) + rand_range(-18.0, 18.0);
            tg->y = radius * phi                              + rand_range(-18.0, 18.0);
            tg->z = radius * sqrt(1.0 - phi * phi) * sin(theta) + rand_range(-18.0, 18.0);
        } else {
            /* 球壳内均匀采样 */
            double u1 = rand_next(), u2 = rand_next(), u3 = rand_next();
            radius = r_lo + (r_hi - r_lo) * pow(u1, 1.0 / 3.0);
            theta  = 6.283185307 * u2;
            phi    = acos(2.0 * u3 - 1.0);
            tg->x = radius * sin(phi) * cos(theta);
            tg->y = radius * cos(phi) * 0.75;      /* 稍压扁，视觉上更像星带 */
            tg->z = radius * sin(phi) * sin(theta);
        }

        /* 漂移速度：随机方向 + 低速 */
        theta = rand_range(0.0, 6.283185307);
        phi   = rand_range(-1.0, 1.0);
        {
            double sp = rand_range(v_lo, v_hi);
            double s  = sqrt(1.0 - phi * phi);
            tg->vx = sp * s * cos(theta);
            tg->vy = sp * phi;
            tg->vz = sp * s * sin(theta);
        }

        tg->life_max_time = rand_range(life_lo, life_hi);
        tg->is_saved      = 0;
        tg->save_time     = -1.0;
        tg->life_left     = tg->life_max_time;
    }
    reset_target_states(rd);
}

/*============================================================================
 *  三、方案仿真：把“一条救援顺序”推演成 RoutePlan（漂移模型）
 *===========================================================================*/
void plan_simulate(RescueData *rd, const int seq[], int len, RoutePlan *out)
{
    int    done[MAX_TARGET_NUM] = { 0 };
    double t = 0.0;
    double sx = rd->origin_x, sy = rd->origin_y, sz = rd->origin_z;
    int    k;

    memset(out, 0, sizeof(RoutePlan));

    for (k = 0; k < len && k < MAX_TARGET_NUM; ++k) {
        int    idx = seq[k];
        double tau, arrive, px, py, pz;

        if (idx < 0 || idx >= rd->target_count || done[idx]) continue;
        done[idx] = 1;

        tau    = rescue_intercept_time(&rd->target_list[idx], t, sx, sy, sz);
        arrive = t + tau;

        if (arrive > rd->target_list[idx].life_max_time) {
            out->dead++;                      /* 到达前已死亡，放弃这个目标 */
            continue;
        }

        rescue_pos_at(&rd->target_list[idx], arrive, &px, &py, &pz);
        out->seq[out->len]   = idx;
        out->leg_t[out->len] = arrive;
        out->leg_d[out->len] = tau * SPEED;
        out->length         += tau * SPEED;
        out->len++;
        out->saved++;

        sx = px; sy = py; sz = pz;
        t  = arrive;
    }

    for (k = 0; k < rd->target_count; ++k)
        if (!done[k]) out->dead++;

    out->finish_time = t;
    out->feasible    = (out->dead == 0) ? 1 : 0;
}

void plan_apply(RescueData *rd, const RoutePlan *plan)
{
    int i;

    reset_target_states(rd);
    for (i = 0; i < plan->len; ++i) {
        int idx = plan->seq[i];
        rd->best_seq[i] = idx;
        rd->target_list[idx].is_saved  = 1;
        rd->target_list[idx].save_time = plan->leg_t[i];
        rd->target_list[idx].life_left = plan->leg_t[i];
    }
    for (i = plan->len; i < MAX_TARGET_NUM; ++i) rd->best_seq[i] = -1;

    rd->total_time     = plan->finish_time;
    rd->total_distance = plan->length;
    rd->saved_count    = plan->saved;
    rd->dead_count     = plan->dead;
    rd->algo_used      = plan->algo;
    rd->find_success   = plan->feasible;
}

void plan_name(int algo, char *buf, int bufsz)
{
    const char *name;

    switch (algo) {
        case ALGO_NN:        name = "最近邻贪心 NN";    break;
        case ALGO_URGENCY:   name = "生存时间优先 EDF"; break;
        case ALGO_BACKTRACK: name = "回溯搜索 (剪枝)";  break;
        case ALGO_2OPT:      name = "2-opt 局部搜索";   break;
        default:             name = "未知方案";         break;
    }
    snprintf(buf, (size_t)bufsz, "%s", name);
}

/*============================================================================
 *  四、方案族
 *===========================================================================*/
/* 把“队友 B 写进 rd 的 best_seq + save_time”转成 RoutePlan */
static void plan_from_rd(const RescueData *src, RescueData *before,
                         int algo, RoutePlan *out)
{
    int    k, n = src->target_count;
    double prev_t = 0.0;

    memset(out, 0, sizeof(RoutePlan));
    out->algo = algo;

    for (k = 0; k < n; ++k) {
        int idx = src->best_seq[k];
        double t1;

        if (idx < 0 || idx >= n) continue;
        t1 = src->target_list[idx].save_time;
        if (t1 < 0.0) continue;                  /* 没有被救到 */
        out->seq[out->len]   = idx;
        out->leg_t[out->len] = t1;
        out->leg_d[out->len] = (t1 - prev_t) * SPEED;
        out->length         += out->leg_d[out->len];
        prev_t               = t1;
        out->len++;
    }
    out->saved = out->len;
    out->dead  = n - out->len;
    out->finish_time = prev_t;
    out->feasible    = (out->dead == 0) ? 1 : 0;

    /* 队友的实现没写“可行性”之外的字段：这里用统一仿真补齐并校验一致性 */
    {
        RoutePlan chk;
        plan_simulate(before, out->seq, out->len, &chk);
        if (chk.len != out->len) {
            /* 队友给出的时刻与统一仿真不一致时，以统一仿真为准（保证回放正确） */
            plan_simulate(before, src->best_seq, n, out);
            out->algo = algo;
        }
    }
}

/* 最近邻贪心：直接调用队友 B 的 tsp_greedy（漂移适配版） */
void plan_nearest(RescueData *rd, RoutePlan *out)
{
    RescueData tmp;
    double     t0 = now_ms();

    tmp = *rd;
    tsp_greedy(&tmp);
    plan_from_rd(&tmp, rd, ALGO_NN, out);
    out->algo       = ALGO_NN;
    out->nodes      = (long)tmp.target_count * (long)tmp.target_count;
    out->elapsed_ms = now_ms() - t0;
}

/* 生存时间优先（EDF）：每步选“生存余量最小”的目标（适配层补充的策略） */
void plan_urgency(RescueData *rd, RoutePlan *out)
{
    int    seq[MAX_TARGET_NUM];
    char   valid[MAX_TARGET_NUM];
    double t = 0.0, sx = rd->origin_x, sy = rd->origin_y, sz = rd->origin_z;
    int    step, i, n = rd->target_count;
    double t0 = now_ms();
    long   nodes = 0;

    for (i = 0; i < n; ++i) valid[i] = 1;

    for (step = 0; step < n; ++step) {
        int    best = -1;
        double best_slack = 1e18, best_tau = 0.0;

        for (i = 0; i < n; ++i) {
            double tau, slack;

            if (!valid[i]) continue;
            tau   = rescue_intercept_time(&rd->target_list[i], t, sx, sy, sz);
            slack = rd->target_list[i].life_max_time - (t + tau);
            nodes++;
            if (slack < best_slack) { best_slack = slack; best = i; best_tau = tau; }
        }
        if (best < 0) break;
        valid[best] = 0;
        seq[step]   = best;

        if (t + best_tau <= rd->target_list[best].life_max_time) {
            rescue_pos_at(&rd->target_list[best], t + best_tau, &sx, &sy, &sz);
            t += best_tau;
        }
    }

    plan_simulate(rd, seq, n, out);
    out->algo       = ALGO_URGENCY;
    out->nodes      = nodes;
    out->elapsed_ms = now_ms() - t0;
}

/*----------------------------------------------------------------------------
 *  剪枝回溯（适配层补充）：n > 9 时队友的全排列不可行，这里用剪枝 + 节点上限。
 *  目标：获救数最多；同等获救数下航程最短。
 *  剪枝依据：
 *    · 若此刻到达已超期，则该目标永远救不到（到达时刻只增不减）→ 不再尝试；
 *    · 已找到“全部获救”的解时，航程超过当前最优即可剪枝。
 *--------------------------------------------------------------------------*/
typedef struct {
    RescueData *rd;
    char   visited[MAX_TARGET_NUM];
    int    path[MAX_TARGET_NUM];
    int    path_len;
    double t, sx, sy, sz, length;

    int    best_path[MAX_TARGET_NUM];
    int    best_len, best_saved;
    double best_length;
    long   nodes;
    int    aborted;
} PruneCtx;

static void prune_dfs(PruneCtx *c)
{
    RescueData *rd = c->rd;
    int i;

    if (++c->nodes > PRUNE_NODE_CAP) { c->aborted = 1; return; }

    for (i = 0; i < rd->target_count; ++i) {
        double tau, arrive, px, py, pz;

        if (c->visited[i]) continue;

        tau    = rescue_intercept_time(&rd->target_list[i], c->t, c->sx, c->sy, c->sz);
        arrive = c->t + tau;

        /* 剪枝 1：到达即超期 → 该目标此刻之后就永远救不到 */
        if (arrive > rd->target_list[i].life_max_time) continue;

        /* 剪枝 2：已经能全救时，航程不优就不必继续 */
        if (c->best_saved == rd->target_count &&
            c->length + tau * SPEED >= c->best_length - 1e-9) continue;

        rescue_pos_at(&rd->target_list[i], arrive, &px, &py, &pz);

        {
            double old_t = c->t, ox = c->sx, oy = c->sy, oz = c->sz;
            double old_len = c->length;

            c->visited[i] = 1;
            c->path[c->path_len++] = i;
            c->t = arrive; c->sx = px; c->sy = py; c->sz = pz;
            c->length += tau * SPEED;

            if (c->path_len > c->best_saved ||
                (c->path_len == c->best_saved && c->length < c->best_length)) {
                c->best_saved  = c->path_len;
                c->best_length = c->length;
                c->best_len    = c->path_len;
                memcpy(c->best_path, c->path, sizeof(int) * (size_t)c->path_len);
            }

            prune_dfs(c);

            c->visited[i] = 0;
            c->path_len--;
            c->t = old_t; c->sx = ox; c->sy = oy; c->sz = oz;
            c->length = old_len;
        }
        if (c->aborted) return;
    }
}

/* 回溯：n ≤ 9 用队友 B 的全排列（精确）；否则用剪枝版 */
void plan_backtrack(RescueData *rd, RoutePlan *out)
{
    double t0 = now_ms();

    if (rd->target_count <= 9) {
        RescueData tmp = *rd;

        tsp_backtrack(&tmp);
        if (tmp.find_success) {
            plan_from_rd(&tmp, rd, ALGO_BACKTRACK, out);
            out->algo       = ALGO_BACKTRACK;
            out->nodes      = tsp_last_nodes();
            out->elapsed_ms = now_ms() - t0;
            return;
        }
        /* 没有任何“全救活”的排列：退回剪枝版（最大化获救数） */
    }

    {
        PruneCtx c;

        memset(&c, 0, sizeof(c));
        c.rd = rd;
        c.t  = 0.0;
        c.sx = rd->origin_x; c.sy = rd->origin_y; c.sz = rd->origin_z;
        c.best_length = 1e18;

        prune_dfs(&c);

        plan_simulate(rd, c.best_path, c.best_len, out);
        out->algo       = ALGO_BACKTRACK;
        out->nodes      = c.nodes;
        out->elapsed_ms = now_ms() - t0;
    }
}

/* 2-opt 局部搜索：从“最近邻 / EDF”中较优者出发，反转子段做改进（适配层补充） */
void plan_2opt(RescueData *rd, RoutePlan *out)
{
    RoutePlan cur, nn, edf;
    int    improved = 1, rounds = 0;
    long   build_nodes;
    double t0 = now_ms();

    plan_nearest(rd, &nn);
    plan_urgency(rd, &edf);

    cur = nn;
    if (edf.saved > cur.saved ||
        (edf.saved == cur.saved && edf.length < cur.length - 1e-9)) cur = edf;
    build_nodes = nn.nodes + edf.nodes;

    cur.algo  = ALGO_2OPT;
    cur.nodes = 0;

    while (improved && rounds < 8) {
        int i, j;

        improved = 0;
        rounds++;

        for (i = 0; i < cur.len - 1; ++i) {
            for (j = i + 1; j < cur.len; ++j) {
                int       cand[MAX_TARGET_NUM];
                RoutePlan trial;
                int       k, cidx = 0;

                for (k = 0; k < i; ++k)           cand[cidx++] = cur.seq[k];
                for (k = j; k >= i; --k)          cand[cidx++] = cur.seq[k];  /* 反转 */
                for (k = j + 1; k < cur.len; ++k) cand[cidx++] = cur.seq[k];

                plan_simulate(rd, cand, cidx, &trial);
                cur.nodes++;
                trial.algo = ALGO_2OPT;

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
    out->nodes      = build_nodes + cur.nodes;
    out->elapsed_ms = now_ms() - t0;
}

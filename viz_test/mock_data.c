/*============================================================================
 *  mock_data.c  --  视觉模块独立调试用的“假数据模块 / 假算法模块”
 *----------------------------------------------------------------------------
 *  为什么需要这个文件：
 *    可视化模块 viz_module.c 只依赖三样东西：
 *        calc_3d_distance() / update_target_pos() / target_pos_at()   （数据模块 A）
 *        plan_name()                                                  （算法模块 B）
 *    为了能**单独**调试视觉模块，不必拉扯 A、B 两个模块的真实实现，
 *    这里给出上面四个函数的最小实现，再额外提供几个“手工造场景/造方案”的
 *    工具函数，方便构造边界数据（单目标、全牺牲、空方案、同点重叠……）。
 *
 *  与真实模块的关系：
 *    · 如果你要连真实模块一起测，用 `build_test.bat full`（会定义
 *      MOCK_WITH_REAL_MODULES 宏并链接 ../data_module.c ../algorithm_module.c）；
 *    · 默认构建只链接本文件，构成“只有视觉模块 + 假数据”的独立环境。
 *===========================================================================*/
#include "space_rescue.h"
#include "mock_data.h"

#ifndef MOCK_WITH_REAL_MODULES

/*============================================================================
 *  一、数据模块 A 的最小实现
 *===========================================================================*/
double calc_3d_distance(double x1, double y1, double z1,
                        double x2, double y2, double z2)
{
    double dx = x1 - x2, dy = y1 - y2, dz = z1 - z2;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

void target_pos_at(const Target *tg, double t, double *px, double *py, double *pz)
{
    *px = tg->x + tg->vx * t;
    *py = tg->y + tg->vy * t;
    *pz = tg->z + tg->vz * t;
}

void update_target_pos(Target *tg, double delta_t)
{
    if (delta_t <= 0.0) return;
    tg->x += tg->vx * delta_t;
    tg->y += tg->vy * delta_t;
    tg->z += tg->vz * delta_t;
    tg->life_left -= delta_t;
    if (tg->life_left < 0.0) tg->life_left = 0.0;
}

/*============================================================================
 *  二、算法模块 B 中“被视觉模块引用”的那个小函数
 *    算法模块里也有同名实现；默认构建用这份，full 构建时关闭本文件。
 *===========================================================================*/
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

#endif /* !MOCK_WITH_REAL_MODULES
        *  —— 以上是“与真实 A/B 模块重名”的替身实现；
        *     下面的造数据工具在两种构建方式下都需要，故不受宏控制。 */

/*============================================================================
 *  三、手工造场景的工具（测试用，默认构建与 full 构建都需要）
 *===========================================================================*/
void scene_reset(RescueData *rd, int scenario, unsigned seed)
{
    memset(rd, 0, sizeof(RescueData));
    rd->target_count = 0;
    rd->origin_x = rd->origin_y = rd->origin_z = 0.0;
    rd->scenario = scenario;
    rd->seed     = seed;
    rd->algo_used = ALGO_NN;
}

int scene_add(RescueData *rd, double x, double y, double z, double life)
{
    int i;
    if (rd->target_count >= MAX_TARGET_NUM) return -1;
    i = rd->target_count++;
    rd->target_list[i].x = x;
    rd->target_list[i].y = y;
    rd->target_list[i].z = z;
    rd->target_list[i].vx = rd->target_list[i].vy = rd->target_list[i].vz = 0.0;
    rd->target_list[i].life_max_time = life;
    rd->target_list[i].life_left     = life;
    rd->target_list[i].is_saved      = 0;
    rd->target_list[i].save_time     = -1.0;
    return i;
}

int scene_add_vel(RescueData *rd, double x, double y, double z,
                  double vx, double vy, double vz, double life)
{
    int i = scene_add(rd, x, y, z, life);
    if (i < 0) return -1;
    rd->target_list[i].vx = vx;
    rd->target_list[i].vy = vy;
    rd->target_list[i].vz = vz;
    return i;
}

/* 散布一圈目标：dist 半径、angle 起始角、life 生存上限、life_step 逐个递减 */
void scene_ring(RescueData *rd, int n, double dist, double life, double life_step)
{
    int i;
    for (i = 0; i < n; ++i) {
        double th = 6.283185307 * ((double)i / (double)(n > 0 ? n : 1));
        double life_i = life - life_step * (double)i;
        if (life_i < 1.0) life_i = 1.0;
        scene_add_vel(rd,
                      dist * cos(th), 0.45 * dist * sin(th * 1.7), dist * sin(th),
                      0.30 * cos(th), 0.10, 0.30 * sin(th),
                      life_i);
    }
}

/*============================================================================
 *  四、手工造方案的工具（测试用）
 *----------------------------------------------------------------------------
 *  拦截时间与算法模块用同一个公式：
 *      (|v|^2- SPEED^2)τ^2 + 2(D·v)τ + |D|^2 = 0
 *  保证“导出的 legs 长度”与“前端按 p0+v·t 反推的直线距离”严格一致，
 *  这样 viz_test 生成的 trace 拿去网页里也不会出现航线错位。
 *--------------------------------------------------------------------------*/
double mock_intercept_time(const Target *tg, double t_begin,
                           double fx, double fy, double fz)
{
    double px, py, pz, dx, dy, dz, a, b, c, disc, r1, r2, tmp;

    target_pos_at(tg, t_begin, &px, &py, &pz);
    dx = px - fx; dy = py - fy; dz = pz - fz;

    a = tg->vx * tg->vx + tg->vy * tg->vy + tg->vz * tg->vz - SPEED * SPEED;
    b = 2.0 * (dx * tg->vx + dy * tg->vy + dz * tg->vz);
    c = dx * dx + dy * dy + dz * dz;

    if (fabs(a) < 1e-12) return (fabs(b) < 1e-12) ? 0.0 : -c / b;

    disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return sqrt(c) / SPEED;

    r1 = (-b + sqrt(disc)) / (2.0 * a);
    r2 = (-b - sqrt(disc)) / (2.0 * a);
    if (r1 > r2) { tmp = r1; r1 = r2; r2 = tmp; }
    tmp = (r1 >= 0.0) ? r1 : r2;
    return (tmp < 0.0) ? 0.0 : tmp;
}

/* 按给定顺序推演一次，填 RoutePlan（超期目标直接放弃） */
void mock_build_plan(RescueData *rd, const int order[], int n,
                     int algo, RoutePlan *out)
{
    int    done[MAX_TARGET_NUM];
    double t = 0.0, sx = rd->origin_x, sy = rd->origin_y, sz = rd->origin_z;
    int    k;

    memset(out, 0, sizeof(RoutePlan));
    memset(done, 0, sizeof(done));
    out->algo = algo;

    for (k = 0; k < n; ++k) {
        int    idx = order[k];
        double tau, arrive, px, py, pz;

        if (idx < 0 || idx >= rd->target_count || done[idx]) continue;
        done[idx] = 1;

        tau    = mock_intercept_time(&rd->target_list[idx], t, sx, sy, sz);
        arrive = t + tau;
        out->nodes++;

        if (arrive > rd->target_list[idx].life_max_time) {
            out->dead++;
            continue;
        }

        target_pos_at(&rd->target_list[idx], arrive, &px, &py, &pz);
        out->seq[out->len]   = idx;
        out->leg_d[out->len] = tau * SPEED;
        out->leg_t[out->len] = arrive;
        out->length += tau * SPEED;
        out->len++;
        out->saved++;

        sx = px; sy = py; sz = pz;
        t  = arrive;
    }

    for (k = 0; k < rd->target_count; ++k)
        if (!done[k]) out->dead++;

    out->finish_time = t;
    out->feasible    = (out->dead == 0) ? 1 : 0;
    out->elapsed_ms  = 0.01 * (double)out->nodes;
}

/* 生成一条“最近邻”顺序，用于 emit 演示 */
void mock_nearest_plan(RescueData *rd, RoutePlan *out)
{
    int    order[MAX_TARGET_NUM];
    int    used[MAX_TARGET_NUM];
    double t = 0.0, sx = rd->origin_x, sy = rd->origin_y, sz = rd->origin_z;
    int    step, i, n = rd->target_count;

    memset(used, 0, sizeof(used));
    for (step = 0; step < n; ++step) {
        int    best = -1;
        double best_tau = 1e18;

        for (i = 0; i < n; ++i) {
            double tau;
            if (used[i]) continue;
            tau = mock_intercept_time(&rd->target_list[i], t, sx, sy, sz);
            if (tau < best_tau) { best_tau = tau; best = i; }
        }
        if (best < 0) break;
        used[best]    = 1;
        order[step]   = best;
        if (t + best_tau <= rd->target_list[best].life_max_time) {
            target_pos_at(&rd->target_list[best], t + best_tau, &sx, &sy, &sz);
            t += best_tau;
        }
    }

    mock_build_plan(rd, order, n, ALGO_NN, out);
}

/*----------------------------------------------------------------------------
 *  mock_urgency_plan：生存时间优先的贪心（测试桩）
 *    每步在“还能救到”的目标里选生存余量最小的先救，
 *    用来在演示里给出一个“比最近邻更贴合生存约束”的对比基线。
 *    注意：这是测试台里的简化实现；正式算法在 algorithm_module.c。
 *--------------------------------------------------------------------------*/
void mock_urgency_plan(RescueData *rd, RoutePlan *out)
{
    int    order[MAX_TARGET_NUM];
    int    used[MAX_TARGET_NUM];
    double t = 0.0, sx = rd->origin_x, sy = rd->origin_y, sz = rd->origin_z;
    int    step, i, n = rd->target_count;

    memset(used, 0, sizeof(used));
    for (step = 0; step < n; ++step) {
        int    best = -1;
        double best_slack = 1e18, best_tau = 0.0;

        for (i = 0; i < n; ++i) {
            double tau, slack;
            if (used[i]) continue;
            tau   = mock_intercept_time(&rd->target_list[i], t, sx, sy, sz);
            slack = rd->target_list[i].life_max_time - (t + tau);
            if (slack < best_slack) { best_slack = slack; best = i; best_tau = tau; }
        }
        if (best < 0) break;
        used[best]  = 1;
        order[step] = best;
        if (t + best_tau <= rd->target_list[best].life_max_time) {
            target_pos_at(&rd->target_list[best], t + best_tau, &sx, &sy, &sz);
            t += best_tau;
        }
    }

    mock_build_plan(rd, order, n, ALGO_URGENCY, out);
}

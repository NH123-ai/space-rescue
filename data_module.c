/*============================================================================
 *  data_module.c  --  组员 A：数据模块
 *----------------------------------------------------------------------------
 *  职责：
 *    · 读取 / 生成 待救援目标的初始三维坐标、漂移速度与最长生存时间
 *    · 三维距离计算、目标位置随时间漂移的更新
 *    · 打印目标信息表
 *  约束：本模块不依赖任何其他模块（依赖方向最底层）。
 *===========================================================================*/
#include "space_rescue.h"

/*----------------------------------------------------------------------------
 *  内部：确定性伪随机数（LCG）
 *  用自带 RNG 而不是 rand()，保证“同一 seed -> 同一场景”，
 *  便于测试用例复现与可视化 trace 的一致性。
 *--------------------------------------------------------------------------*/
static unsigned int g_rand_state = 1u;

static void  rng_seed(unsigned seed)      { g_rand_state = (unsigned int)seed * 2654435761u + 12345u; }
static double rng_next(void)
{
    g_rand_state = g_rand_state * 1103515245u + 12345u;   /* 32 位无符号，跨平台一致 */
    return (double)((g_rand_state >> 16) & 0x7FFFu) / 32767.0;   /* [0,1] */
}
static double rng_range(double lo, double hi) { return lo + (hi - lo) * rng_next(); }

/*----------------------------------------------------------------------------
 *  calc_3d_distance：三维欧氏距离
 *--------------------------------------------------------------------------*/
double calc_3d_distance(double x1, double y1, double z1,
                        double x2, double y2, double z2)
{
    double dx = x1 - x2, dy = y1 - y2, dz = z1 - z2;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

/*----------------------------------------------------------------------------
 *  target_pos_at：漂移模型  p(t) = p0 + v * t
 *  题目描述“位置低速变化（随机或与气流有关）”，在分段线性速率下
 *  位置是时间的线性函数，因此任意时刻坐标可以解析求出，
 *  无须逐步积分，也不会产生累计误差。
 *--------------------------------------------------------------------------*/
void target_pos_at(const Target *tg, double t, double *px, double *py, double *pz)
{
    *px = tg->x + tg->vx * t;
    *py = tg->y + tg->vy * t;
    *pz = tg->z + tg->vz * t;
}

/*----------------------------------------------------------------------------
 *  update_target_pos：按 delta_t 推进目标位置（同时刷新剩余生存时间）
 *--------------------------------------------------------------------------*/
void update_target_pos(Target *tg, double delta_t)
{
    if (delta_t <= 0.0) return;
    tg->x += tg->vx * delta_t;
    tg->y += tg->vy * delta_t;
    tg->z += tg->vz * delta_t;
    tg->life_left -= delta_t;
    if (tg->life_left < 0.0) tg->life_left = 0.0;
}

/*----------------------------------------------------------------------------
 *  reset_target_states：把“已救援 / 死亡 / 剩余时间”恢复到初始状态
 *  所有算法在各自独立的仿真副本上运行，互不污染。
 *--------------------------------------------------------------------------*/
void reset_target_states(RescueData *rd)
{
    int i;
    for (i = 0; i < rd->target_count; ++i) {
        rd->target_list[i].is_saved  = 0;
        rd->target_list[i].save_time = -1.0;
        rd->target_list[i].life_left = rd->target_list[i].life_max_time;
    }
    rd->total_time     = 0.0;
    rd->total_distance = 0.0;
    rd->saved_count    = 0;
    rd->dead_count     = 0;
    rd->find_success   = 0;
}

/*----------------------------------------------------------------------------
 *  gen_scenario：生成一个场景（可复现）
 *    count    : 目标数量 1..MAX_TARGET_NUM
 *    scenario : SCENARIO_*
 *    seed     : 随机种子
 *--------------------------------------------------------------------------*/
void gen_scenario(RescueData *rd, int count, int scenario, unsigned seed)
{
    int i;
    double r_lo = 40.0, r_hi = 160.0;   /* 初始距离范围 */
    double v_lo = -0.60, v_hi = 0.60;   /* 漂移速度（低速） */
    double life_lo = 90.0, life_hi = 240.0;

    if (count < 1) count = 1;
    if (count > MAX_TARGET_NUM) count = MAX_TARGET_NUM;
    if (seed == 0) seed = (unsigned)time(NULL);

    switch (scenario) {
        case SCENARIO_TIGHT:    life_lo = 45.0;  life_hi = 105.0;  break;
        case SCENARIO_CLUSTER:  r_lo = 140.0;    r_hi = 175.0;     break;
        case SCENARIO_WIDE:     r_lo = 60.0;     r_hi = 320.0;
                                life_lo = 120.0; life_hi = 420.0;  break;
        case SCENARIO_RANDOM:
        case SCENARIO_STANDARD:
        default:                break;
    }

    rng_seed(seed);
    memset(rd, 0, sizeof(RescueData));
    rd->target_count   = count;
    rd->origin_x = rd->origin_y = rd->origin_z = 0.0;
    rd->seed     = seed;
    rd->scenario = scenario;

    for (i = 0; i < count; ++i) {
        Target *tg = &rd->target_list[i];
        double theta, phi, radius;

        if (scenario == SCENARIO_CLUSTER) {
            /* 星团：先取一个团心方向，再在团心附近抖动 */
            theta  = rng_range(0.0, 6.283185307);
            phi    = rng_range(-1.0, 1.0);
            radius = rng_range(r_lo, r_hi);
            tg->x = radius * sqrt(1.0 - phi * phi) * cos(theta) + rng_range(-18.0, 18.0);
            tg->y = radius * phi                              + rng_range(-18.0, 18.0);
            tg->z = radius * sqrt(1.0 - phi * phi) * sin(theta) + rng_range(-18.0, 18.0);
        } else {
            /* 球壳内均匀采样：cos(phi) 均匀分布保证面积均匀 */
            double u1 = rng_next(), u2 = rng_next(), u3 = rng_next();
            radius = r_lo + (r_hi - r_lo) * pow(u1, 1.0 / 3.0);
            theta  = 6.283185307 * u2;
            phi    = acos(2.0 * u3 - 1.0);
            tg->x = radius * sin(phi) * cos(theta);
            tg->y = radius * cos(phi) * 0.75;          /* 稍压扁，视觉上更像星带 */
            tg->z = radius * sin(phi) * sin(theta);
        }

        /* 漂移速度：随机方向 + 低速 */
        theta      = rng_range(0.0, 6.283185307);
        phi        = rng_range(-1.0, 1.0);
        {
            double sp = rng_range(v_lo, v_hi);
            double s  = sqrt(1.0 - phi * phi);
            tg->vx = sp * s * cos(theta);
            tg->vy = sp * phi;
            tg->vz = sp * s * sin(theta);
        }

        tg->life_max_time = rng_range(life_lo, life_hi);
        tg->is_saved      = 0;
        tg->save_time     = -1.0;
        tg->life_left     = tg->life_max_time;
    }
  reset_target_states(rd);
}

/*----------------------------------------------------------------------------
 *  read_target_data：从文本文件读取目标数据
 *  文件格式（# 开头为注释，空行忽略）：
 *      <n>
 *      x y z vx vy vz life_max_time     (共 n 行)
 *  也兼容没有速度列的旧格式（x y z life_max_time），此时速度置 0。
 *--------------------------------------------------------------------------*/
int read_target_data(RescueData *rd, const char *filename)
{
    FILE *fp = fopen(filename, "r");
    char  line[512];
    int   i = 0;
    int   n = 0;

    if (fp == NULL) {
        printf("[数据模块] 无法打开文件：%s\n", filename);
        return -1;
    }

    memset(rd, 0, sizeof(RescueData));
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        if (n == 0) {                       /* 第一行：目标数量 */
            if (sscanf(p, "%d", &n) != 1 || n <= 0 || n > MAX_TARGET_NUM) {
                printf("[数据模块] 目标数量非法（1..%d）\n", MAX_TARGET_NUM);
                fclose(fp);
                return -2;
            }
            rd->target_count = n;
            continue;
        }
        if (i >= n) break;

        {
            double x, y, z, life, vx = 0.0, vy = 0.0, vz = 0.0;
            int got = sscanf(p, "%lf %lf %lf %lf %lf %lf %lf",
                             &x, &y, &z, &vx, &vy, &vz, &life);
            if (got == 4) {                 /* 旧格式：x y z life */
                life = vx; vx = vy = vz = 0.0;
            } else if (got < 4) {
                continue;
            }
            rd->target_list[i].x = x;
            rd->target_list[i].y = y;
            rd->target_list[i].z = z;
            rd->target_list[i].vx = vx;
            rd->target_list[i].vy = vy;
            rd->target_list[i].vz = vz;
            rd->target_list[i].life_max_time = life;
            rd->target_list[i].is_saved  = 0;
            rd->target_list[i].save_time = -1.0;
            rd->target_list[i].life_left = life;
            ++i;
        }
    }
    fclose(fp);

    if (i < n) {
        printf("[数据模块] 数据不足：期望 %d 条，实际读到 %d 条\n", n, i);
        return -3;
    }
    reset_target_states(rd);
    return 0;
}

/*----------------------------------------------------------------------------
 *  print_target_info：打印目标信息表
 *--------------------------------------------------------------------------*/
void print_target_info(RescueData *rd)
{
    int i;
    printf("\n  %-5s %9s %9s %9s   %7s %7s %7s %9s  %9s\n",
           "ID", "X", "Y", "Z", "Vx", "Vy", "Vz", "已飞行", "生存上限");
    printf("  ----------------------------------------------------------------------------\n");
    for (i = 0; i < rd->target_count; ++i) {
        const Target *tg = &rd->target_list[i];
        double d = calc_3d_distance(0.0, 0.0, 0.0, tg->x, tg->y, tg->z);
        printf("  T%-4d %9.2f %9.2f %9.2f   %7.2f %7.2f %7.2f %9.2f  %9.2f\n",
               i + 1, tg->x, tg->y, tg->z, tg->vx, tg->vy, tg->vz,
               d / SPEED, tg->life_max_time);
    }
    printf("  目标总数：%d ，飞船速度：%.1f ，坐标刷新间隔 t = %.1f\n\n",
           rd->target_count, SPEED, UPDATE_INTERVAL);
}

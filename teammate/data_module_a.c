/*============================================================================
 *  data_module_a.c  --  组员 A：数据模块（集成自队友交付的 data.c）
 *----------------------------------------------------------------------------
 *  来源：队友 A 交付的 `data.c`（原件保存在 teammate/original/data.c，未改动）
 *  集成时为了与工程其余部分（算法模块 / 可视化模块 / 公共头文件）对齐，
 *  做了以下**最小必要调整**，每处都在代码里用【集成调整】标明：
 *
 *    1) update_target_pos()
 *       原实现用 rand() 做“随机抖动”，且不扣减剩余生存时间。
 *       问题：随机抖动不可复现，而可视化要按 p(t)=p0+v·t 把同一段过程回放，
 *       两边对不上（航线会与目标错位）。改为按目标自带速度 vx/vy/vz 匀速漂移，
 *       并同步扣减 life_left。原来的随机抖动版保留为 update_target_pos_random()
 *       作为“气流扰动”模型的对照实现。
 *    2) read_target_data()
 *       用逐行解析重写（原实现的 fscanf 混合读法在格式不符时会半吞输入），
 *       补充可选的 3 个速度分量（没有则该目标视为静止），并初始化
 *       life_left / save_time / 基点 / 场景标记等字段。
 *       返回值沿用队友的约定：1 = 成功，0 = 失败。
 *    3) print_target_info()
 *       输出改走 viz_printf()（统一 UTF-8/GBK 编码处理），并补上速度列。
 *===========================================================================*/
#include "space_rescue.h"
#include "rescue_internal.h"

/*----------------------------------------------------------------------------
 *  read_target_data：从文本文件读取目标数据
 *  文件格式（# 开头为注释、空行忽略）：
 *      <n>
 *      x y z life            共 n 行（旧格式，目标视为静止）
 *      x y z vx vy vz life   共 n 行（带漂移速度的新格式）
 *  返回：1 = 成功，0 = 失败（与队友 A 的约定一致）
 *--------------------------------------------------------------------------*/
int read_target_data(RescueData *rd, const char *filename)
{
    FILE *fp;
    char  line[512];
    int   n = 0, i = 0;

    if (rd == NULL || filename == NULL) return 0;

    fp = fopen(filename, "r");
    if (fp == NULL) return 0;

    memset(rd, 0, sizeof(RescueData));
    rd->origin_x = rd->origin_y = rd->origin_z = 0.0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;

        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        if (n == 0) {                        /* 第一行：目标数量 */
            if (sscanf(p, "%d", &n) != 1 || n < 0 || n > MAX_TARGET_NUM) {
                fclose(fp);
                return 0;
            }
            rd->target_count = n;
            if (n == 0) break;
            continue;
        }
        if (i >= n) break;

        {
            Target *t = &rd->target_list[i];
            double x, y, z, life, vx = 0.0, vy = 0.0, vz = 0.0;

            /* 【集成调整】优先按 7 列解析（带速度），否则按 4 列（静止目标） */
            if (sscanf(p, "%lf %lf %lf %lf %lf %lf %lf",
                       &x, &y, &z, &vx, &vy, &vz, &life) != 7) {
                if (sscanf(p, "%lf %lf %lf %lf", &x, &y, &z, &life) != 4) {
                    fclose(fp);
                    return 0;                /* 数据行格式错误 */
                }
                vx = vy = vz = 0.0;
            }

            t->x = x;  t->y = y;  t->z = z;
            t->vx = vx; t->vy = vy; t->vz = vz;
            t->life_max_time = life;
            t->life_left     = life;
            t->is_saved      = 0;
            t->save_time     = -1.0;
            ++i;
        }
    }
    fclose(fp);

    if (i < n) return 0;                     /* 行数不足 */

    rd->scenario = SCENARIO_RANDOM;           /* 读文件的场景统一标成“随机” */
    rd->seed     = 0u;
    return 1;
}

/*----------------------------------------------------------------------------
 *  print_target_info：打印目标信息表
 *--------------------------------------------------------------------------*/
void print_target_info(RescueData *rd)
{
    int i;

    if (rd == NULL) return;

    viz_printf("\n===== 目标信息 =====\n");
    viz_printf("目标数量：%d   飞船速度：%.2f   坐标刷新间隔 t：%.2f\n",
               rd->target_count, SPEED, UPDATE_INTERVAL);
    for (i = 0; i < rd->target_count; ++i) {
        const Target *t = &rd->target_list[i];
        double d = calc_3d_distance(rd->origin_x, rd->origin_y, rd->origin_z,
                                    t->x, t->y, t->z);
        viz_printf("目标[%2d] (%.2f, %.2f, %.2f)  漂移(%.3f, %.3f, %.3f)  "
                   "距基点 %.2f  最长生存 %.2f  已救援：%d\n",
                   i, t->x, t->y, t->z, t->vx, t->vy, t->vz,
                   d, t->life_max_time, t->is_saved);
    }
    viz_printf("====================\n\n");
}

/*----------------------------------------------------------------------------
 *  calc_3d_distance：三维欧氏距离（队友 A 原实现，未改动）
 *--------------------------------------------------------------------------*/
double calc_3d_distance(double x1, double y1, double z1,
                        double x2, double y2, double z2)
{
    double dx = x1 - x2;
    double dy = y1 - y2;
    double dz = z1 - z2;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

/*----------------------------------------------------------------------------
 *  update_target_pos：按目标速度匀速漂移，并扣减剩余生存时间
 *  【集成调整】原实现是 rand() 随机抖动（不可复现，无法与动画回放对齐），
 *              这里改为 p += v·dt，与 rescue_pos_at() 的语义完全一致。
 *--------------------------------------------------------------------------*/
void update_target_pos(Target *tg, double dt)
{
    if (tg == NULL || dt <= 0.0) return;

    tg->x += tg->vx * dt;
    tg->y += tg->vy * dt;
    tg->z += tg->vz * dt;

    tg->life_left -= dt;
    if (tg->life_left < 0.0) tg->life_left = 0.0;
}

/*----------------------------------------------------------------------------
 *  update_target_pos_random：队友 A 原始的“随机抖动”漂移模型（保留对照）
 *  说明：位置随机变化 → 不可复现，因此只用于演示“气流扰动”这一模型；
 *        主流程（仿真 / 动画 / trace 导出）使用上面的匀速漂移版。
 *--------------------------------------------------------------------------*/
void update_target_pos_random(Target *tg, double dt)
{
    static int first = 1;
    double offset, rx, ry, rz;

    if (tg == NULL || dt <= 0.0) return;

    if (first) {
        srand((unsigned)time(NULL));
        first = 0;
    }

    offset = 0.08 * dt;
    rx = ((double)rand() / RAND_MAX - 0.5) * offset;
    ry = ((double)rand() / RAND_MAX - 0.5) * offset;
    rz = ((double)rand() / RAND_MAX - 0.5) * offset;

    tg->x += rx;
    tg->y += ry;
    tg->z += rz;

    tg->life_left -= dt;
    if (tg->life_left < 0.0) tg->life_left = 0.0;
}

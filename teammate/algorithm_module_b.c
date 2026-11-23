/*============================================================================
 *  algorithm_module_b.c  --  组员 B：核心算法模块（集成自队友交付的 模块b.cpp）
 *----------------------------------------------------------------------------
 *  来源：队友 B 交付的 `模块b.cpp`（原件保存在 teammate/original/algorithm_module_b.cpp）
 *        其中没有任何 C++ 特有语法（类/模板/STL），因此在本工程里按 C99 编译，
 *        文件名改为 .c。原注释全部保留，改动处在代码里用【集成调整】标明。
 *
 *  集成调整（全部为了“目标低速漂移”这一题意与可视化回放一致）：
 *
 *    1) 原实现把目标当作**静止**的：`距离 / SPEED` 就是飞行时间。
 *       但题目说位置会随时间漂移，飞行期间目标也在动，正确做法是解拦截方程
 *       |p0 + v·τ − ship| = SPEED·τ（见 rescue_internal.h 的
 *       rescue_intercept_time）。三处（可行性校验 / 贪心 / 回溯）都改用
 *       拦截时间，并用 rescue_pos_at 取“到达时刻”的目标位置。
 *    2) 回溯里“总飞行距离”原来用静止坐标累加，改为按各段拦截时间×速度累加，
 *       与漂移模型一致。
 *    3) 回溯增加两个工程化护栏：
 *        · 节点计数 tsp_last_nodes()，供“算法效率对比”展示搜索规模；
 *        · n > TSP_BACKTRACK_MAX_N 时直接返回，避免 n! 把程序卡死
 *          （适配层会在这个规模下改用剪枝回溯，见 plan_adapter.c）。
 *===========================================================================*/
#include "space_rescue.h"
#include "rescue_internal.h"

/* 全排列回溯的规模上限：n=9 时 9! = 36 万，毫秒级可控；再大就交给剪枝版 */
#define TSP_BACKTRACK_MAX_N 9

/* 【集成调整】搜索节点计数（只增不减，供效率对比展示） */
static long g_last_nodes = 0;

long tsp_last_nodes(void)
{
    return g_last_nodes;
}

/*
函数：check_time_constraint
功能：模块B要求：校验一个救援下标序列seq，按顺序模拟飞船飞行，判断全部目标是否都不超过各自life_max_time
参数：
    rd:救援数据结构体
    seq[]:存放目标下标的数组，比如{0,2,1}代表先救0号，再2号，再1号
    len:序列有效长度，有多少个目标要救援
返回：1=全部时间约束满足，可以成功救援；0=任意一个目标超时，该序列不可行
*/
int check_time_constraint(RescueData *rd, int seq[], int len)
{
    /* 判空保护，健壮性，大一写代码要做指针判空 */
    if (rd == NULL || seq == NULL) {
        return 0;
    }

    double ship_x = rd->origin_x;   /* 飞船初始位置：题目规定救援队在原点 */
    double ship_y = rd->origin_y;
    double ship_z = rd->origin_z;
    double current_time = 0.0;      /* 当前任务时钟，初始时间0 */

    /* 循环遍历这个救援序列里每一个目标 */
    for (int i = 0; i < len; i++) {
        int target_idx = seq[i];    /* 拿到这一步要救的目标在target_list的下标 */

        /* 下标合法性检查，防止数组越界 */
        if (target_idx < 0 || target_idx >= rd->target_count) {
            return 0;
        }

        {
            Target *t = &rd->target_list[target_idx];
            double tau;             /* 【集成调整】拦截时间，而不是 距离/速度 */

            /* 调用A模块提供的距离/漂移接口，模块B依赖A模块接口 */
            tau = rescue_intercept_time(t, current_time, ship_x, ship_y, ship_z);
            current_time = current_time + tau;   /* 到达这个目标时刻 */

            /* 核心约束判断：到达时间 > 目标最长生存时间 → 救援失败，这个序列不行 */
            if (current_time > t->life_max_time) {
                return 0;
            }

            /* 更新飞船位置：救完这个目标，飞船就在该目标“到达时刻”的坐标 */
            rescue_pos_at(t, current_time, &ship_x, &ship_y, &ship_z);
        }
    }
    /* 全部目标走完没有超时，序列合法 */
    return 1;
}


/*
函数：tsp_greedy
模块B需求：贪心算法求解救援顺序：每次从当前飞船位置，选距离最近、还没选过的目标，生成救援序列存入rd->best_seq
结果还要写回每个target的save_time，设置rd->find_success标记
注意：贪心不一定全局最优，只是局部每次选最近（TSP 最近邻贪心）
*/
void tsp_greedy(RescueData *rd)
{
    /* 指针判空，防止传空指针程序崩溃 */
    if (rd == NULL) {
        return;
    }
    int n = rd->target_count;
    if (n <= 0) {
        rd->find_success = 0;
        return;
    }

    /* visited数组标记目标是否已经被选入救援序列，0未选，1已选 */
    int visited[MAX_TARGET_NUM] = { 0 };
    int seq_len = 0;                /* 当前best_seq里面存了多少个目标 */

    /* 飞船初始位置原点 */
    double ship_x = rd->origin_x;
    double ship_y = rd->origin_y;
    double ship_z = rd->origin_z;
    double now_time = 0.0;

    /* 循环n轮，每一轮挑选一个目标加入救援序列，一共n个目标 */
    for (int round = 0; round < n; round++) {
        int select_idx = -1;        /* 本轮选中的目标下标，-1代表本轮找不到可选目标 */
        double min_time = 1e18;     /* 【集成调整】按“拦截时间最短”选，而不是静态距离 */
        double min_dist = 0.0;

        /* 遍历全部目标，筛选：没有被选过的目标 */
        for (int i = 0; i < n; i++) {
            if (visited[i] == 1) {
                continue;           /* 这个目标已经进序列，跳过 */
            }
            /* 调用A模块的漂移模型：算飞到该目标要多久 */
            {
                double tau = rescue_intercept_time(&rd->target_list[i], now_time,
                                                  ship_x, ship_y, ship_z);
                if (tau < min_time) {
                    min_time = tau;
                    min_dist = tau * SPEED;
                    select_idx = i;
                }
            }
        }

        /* 如果select_idx=-1 说明没有可以选的目标，任务无解 */
        if (select_idx == -1) {
            break;
        }

        /* 把选中目标加入best_seq，这是C模块要读取的结果字段！接口约定 */
        rd->best_seq[seq_len] = select_idx;
        seq_len++;
        visited[select_idx] = 1;    /* 标记已经选择 */

        {
            Target *tg = &rd->target_list[select_idx];
            double px, py, pz;

            now_time = now_time + min_time;      /* 到达时刻 */

            /* 把救援到达时刻写入结构体，给D模块print_result输出、C模块模拟使用 */
            tg->save_time = now_time;

            /* 更新飞船位置到“到达时刻”该目标的坐标 */
            rescue_pos_at(tg, now_time, &px, &py, &pz);
            ship_x = px;
            ship_y = py;
            ship_z = pz;
            (void)min_dist;
        }
    }

    /* 全部n个目标都选到序列里面，并且校验时间约束是否全部满足 */
    if (seq_len == n && check_time_constraint(rd, rd->best_seq, n) == 1) {
        rd->find_success = 1;       /* 找到可行方案 */
    } else {
        rd->find_success = 0;       /* 贪心找不到可行方案 */
    }
    rd->total_time = now_time;      /* 记录总救援耗时，头文件RescueData成员 */
}


/* ====================== 回溯算法 tsp_backtrack =====================
 * 回溯用到局部辅助变量，写在函数内部，不使用全局变量；
 * 思路：生成全部排列，调用check_time_constraint判断序列合法；
 *       记录总飞行距离最小的那一组序列，存到rd->best_seq；
 * 缺点：n=12 时组合爆炸，作业测试建议 n<=9（适配层对更大的 n 改用剪枝版）。
 */

/*
回溯内部递归辅助函数
参数说明：
rd：原始救援数据
cur_seq[] 当前正在构建的排列序列
used[] 标记哪些下标已经被放进cur_seq
cur_len 当前cur_seq已经放了多少目标
best_len 固定等于总目标数量n
*best_total_dist 保存当前找到最优序列的总飞行距离
temp_best[] 保存当前最优的序列副本，递归内部更新，最后复制给rd->best_seq
*/
static void backtrack_dfs(RescueData *rd, int cur_seq[], int used[], int cur_len, int best_len,
                          double *best_total_dist, int temp_best[])
{
    int n = rd->target_count;

    /* 【集成调整】节点计数：每进入一个递归节点 +1，用于效率对比 */
    g_last_nodes++;

    /* 递归终止条件：当前序列长度等于全部目标数，得到一个完整排列 */
    if (cur_len == best_len) {
        /* 第一步：检查这个序列是否满足生存时间约束，调用模块B的check_time_constraint */
        if (check_time_constraint(rd, cur_seq, cur_len) == 1) {
            /* 【集成调整】按漂移模型累加总航程：每段 = 拦截时间 × 速度 */
            double sx = rd->origin_x, sy = rd->origin_y, sz = rd->origin_z;
            double sum_dist = 0.0;
            double t_now = 0.0;
            for (int i = 0; i < cur_len; i++) {
                int idx = cur_seq[i];
                Target *t = &rd->target_list[idx];
                double tau = rescue_intercept_time(t, t_now, sx, sy, sz);
                sum_dist += tau * SPEED;
                t_now += tau;
                rescue_pos_at(t, t_now, &sx, &sy, &sz);
            }
            /* 如果当前序列总距离比记录的最优距离更小 →更新最优解 */
            if (sum_dist < *best_total_dist) {
                *best_total_dist = sum_dist;
                /* 把当前cur_seq复制到temp_best临时数组保存最优排列 */
                for (int k = 0; k < n; k++) {
                    temp_best[k] = cur_seq[k];
                }
            }
        }
        return;                     /* 返回上一层递归 */
    }

    /* 循环所有目标，挑选还没有被使用的，加入当前序列继续递归 */
    for (int i = 0; i < n; i++) {
        if (used[i] == 1) {
            continue;
        }
        used[i] = 1;                /* 标记i号目标已经被使用 */
        cur_seq[cur_len] = i;       /* 放入当前序列 */
        backtrack_dfs(rd, cur_seq, used, cur_len + 1, best_len, best_total_dist, temp_best);
        used[i] = 0;                /* 回溯！撤销标记，尝试下一种排列 */
    }
}


/*
函数：tsp_backtrack
模块B需求：回溯枚举全部排列，寻找满足时间约束、总路程最短救援序列，结果写入rd->best_seq；
          设置find_success标记，填写每个target的save_time
*/
void tsp_backtrack(RescueData *rd)
{
    if (rd == NULL) {
        return;
    }
    int n = rd->target_count;
    if (n <= 0) {
        rd->find_success = 0;
        return;
    }

    /* 【集成调整】规模护栏：n 太大时全排列不可行，交给适配层的剪枝回溯 */
    if (n > TSP_BACKTRACK_MAX_N) {
        rd->find_success = 0;
        rd->total_time = 0.0;
        g_last_nodes = 0;
        return;
    }

    int cur_seq[MAX_TARGET_NUM] = { 0 };    /* 递归中正在构造的序列 */
    int used[MAX_TARGET_NUM] = { 0 };       /* 标记是否选用 */
    int temp_best[MAX_TARGET_NUM] = { 0 };  /* 保存搜索得到最优序列临时副本 */

    /* 初始最优距离给一个极大值，任何合法解都会比它小 */
    double best_dist = 1e18;

    g_last_nodes = 0;
    /* 调用递归回溯函数，遍历全部排列 */
    backtrack_dfs(rd, cur_seq, used, 0, n, &best_dist, temp_best);

    /* 判断是否找到可行解：best_dist被修改，不再等于极大值 */
    if (best_dist < 1e18) {
        rd->find_success = 1;
        /* 把临时最优序列复制到rd->best_seq，给C模块、D模块读取，这是跨模块关键输出！ */
        for (int i = 0; i < n; i++) {
            rd->best_seq[i] = temp_best[i];
        }

        /* 根据best_seq序列重新计算每个目标save_time，填充到target_list，供其他模块使用 */
        {
            double ship_x = rd->origin_x, ship_y = rd->origin_y, ship_z = rd->origin_z;
            double time_now = 0.0;
            for (int i = 0; i < n; i++) {
                int idx = rd->best_seq[i];
                Target *t = &rd->target_list[idx];
                /* 【集成调整】漂移模型下的到达时刻 */
                double tau = rescue_intercept_time(t, time_now, ship_x, ship_y, ship_z);
                time_now += tau;
                t->save_time = time_now;
                rescue_pos_at(t, time_now, &ship_x, &ship_y, &ship_z);
            }
            rd->total_time = time_now;
        }
    } else {
        /* 所有排列全部不满足时间约束，无解 */
        rd->find_success = 0;
        rd->total_time = 0.0;
    }
}
